#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Adapted inference design: SkyTNT/midi-model (https://github.com/SkyTNT/midi-model).
# Changes: local single-part protocol, bounded generation, cancellation host, MIDI projection.
"""Local SkyTNT MIDI-Model ONNX proposal worker; normal operation never downloads files.

The cache/sampling structure follows SkyTNT's Apache-2.0 app_onnx.py. This worker
adds a bounded single-part protocol, deterministic seed, role prompt, and MIDI
validation. See docs/AI_MIDI.md for provenance and the optional setup command.
"""
from __future__ import annotations

import argparse
from collections import defaultdict, deque
from dataclasses import dataclass
import importlib.util
import json
import math
from pathlib import Path
import sys
from typing import List, Optional, Sequence


MAX_NOTES = 512
MAX_BEATS = 128.0
MODEL_REVISION = "0f8f265d4330f4e46527ac2313200254c5757f5f"
SOURCE_REVISION = "f504d5cb58f769ab0f2909c679238f6621034573"


@dataclass(frozen=True)
class Note:
    start: float
    duration: float
    pitch: int
    velocity: int


def checked_request(args: argparse.Namespace) -> argparse.Namespace:
    if not (1 <= args.bars <= 16 and 1 <= args.numerator <= 32 and args.denominator in (1, 2, 4, 8, 16, 32)):
        raise ValueError("Invalid phrase length or meter")
    args.length = args.bars * args.numerator * 4.0 / args.denominator
    if args.length > MAX_BEATS:
        raise ValueError("The AI phrase cannot exceed 128 quarter-note beats")
    if args.root not in range(12) or args.role not in range(4) or not (0 <= args.seed <= 0xFFFFFFFF):
        raise ValueError("Invalid root, instrument role or seed")
    if not 16 <= args.max_events <= 512:
        raise ValueError("The event budget must be between 16 and 512")
    args.output = Path(args.output).expanduser().resolve()
    if args.output.suffix.lower() not in (".mid", ".midi"):
        raise ValueError("The output must be a .mid or .midi file")
    if args.input:
        args.input = Path(args.input).expanduser().resolve()
        if args.input == args.output:
            raise ValueError("Input and output must be different files")
        if not args.input.is_file() or args.input.stat().st_size > 2 * 1024 * 1024:
            raise ValueError("The seed MIDI is missing or exceeds 2 MB")
    args.model_dir = Path(args.model_dir).expanduser().resolve()
    args.vendor_dir = Path(args.vendor_dir).expanduser().resolve()
    return args


def clean_notes(notes: Sequence[Note], length: float, role: int) -> List[Note]:
    """Choose a deterministic single melodic voice and cap it at the phrase end."""
    valid = [n for n in notes if math.isfinite(n.start) and math.isfinite(n.duration)
             and 0 <= n.start < length and n.duration > 0 and 0 <= n.pitch <= 127 and 1 <= n.velocity <= 127]
    # Bass uses the lowest simultaneous voice; other roles use the upper voice.
    valid.sort(key=lambda n: (n.start, n.pitch if role == 1 else -n.pitch, -n.duration))
    result: List[Note] = []
    for note in valid:
        if result and abs(result[-1].start - note.start) < 1.0 / 960:
            continue
        if result and result[-1].start + result[-1].duration > note.start:
            last = result[-1]
            result[-1] = Note(last.start, max(1.0 / 960, note.start - last.start), last.pitch, last.velocity)
        result.append(Note(note.start, min(note.duration, length - note.start), note.pitch, note.velocity))
        if len(result) >= MAX_NOTES:
            break
    return result


def read_seed(path: Optional[Path], length: float) -> List[Note]:
    if path is None:
        return []
    import mido
    midi = mido.MidiFile(str(path), clip=True)
    if midi.ticks_per_beat <= 0:
        raise ValueError("The seed must use musical ticks, not SMPTE time")
    notes: List[Note] = []
    for track in midi.tracks:
        time = 0
        active = defaultdict(deque)
        for message in track:
            time += message.time
            if message.type == "note_on" and message.velocity > 0 and message.channel != 9:
                active[(message.channel, message.note)].append((time, message.velocity))
            elif message.type == "note_off" or (message.type == "note_on" and message.velocity == 0):
                queue = active[(message.channel, message.note)]
                if queue:
                    start, velocity = queue.popleft()
                    if start < length * midi.ticks_per_beat:
                        notes.append(Note(start / midi.ticks_per_beat,
                                          max(1, time - start) / midi.ticks_per_beat,
                                          message.note, velocity))
        for (_, pitch), queue in active.items():
            for start, velocity in queue:
                if start < length * midi.ticks_per_beat:
                    notes.append(Note(start / midi.ticks_per_beat,
                                      length - start / midi.ticks_per_beat, pitch, velocity))
    return notes


def write_midi(path: Path, notes: Sequence[Note], length: float, numerator: int, denominator: int) -> None:
    import mido
    ticks = 960
    midi = mido.MidiFile(type=0, ticks_per_beat=ticks)
    track = mido.MidiTrack()
    midi.tracks.append(track)
    track.append(mido.MetaMessage("time_signature", numerator=numerator, denominator=denominator, time=0))
    track.append(mido.MetaMessage("set_tempo", tempo=500000, time=0))
    events = []
    for note in notes:
        start = max(0, int(round(note.start * ticks)))
        end = min(int(round(length * ticks)), max(start + 1, int(round((note.start + note.duration) * ticks))))
        if end > start:
            events.append((start, 1, mido.Message("note_on", channel=0, note=note.pitch, velocity=note.velocity)))
            events.append((end, 0, mido.Message("note_off", channel=0, note=note.pitch, velocity=0)))
    previous = 0
    for time, _, message in sorted(events, key=lambda event: (event[0], event[1], event[2].note)):
        message.time = time - previous
        previous = time
        track.append(message)
    track.append(mido.MetaMessage("end_of_track", time=max(0, int(round(length * ticks)) - previous)))
    path.parent.mkdir(parents=True, exist_ok=True)
    partial = path.with_name(path.name + ".partial")
    try:
        midi.save(str(partial))
        partial.replace(path)
    finally:
        partial.unlink(missing_ok=True)


def dependencies(model_dir: Path, vendor_dir: Path):
    import numpy as np
    import onnxruntime as ort
    import mido  # noqa: F401 -- fail before expensive inference when missing
    import PIL.Image  # noqa: F401 -- upstream tokenizer's optional visualization dependency
    for name in ("config.json", "model_base.onnx", "model_token.onnx"):
        if not (model_dir / name).is_file():
            raise ValueError("Missing local model file: " + name + ". Run setup-ai-midi.sh")
    tokenizer_path = vendor_dir / "midi_tokenizer.py"
    if not tokenizer_path.is_file():
        raise ValueError("Missing local tokenizer. Run setup-ai-midi.sh")
    with (model_dir / "config.json").open(encoding="utf8") as source:
        config = json.load(source)
    if config.get("tokenizer", {}).get("version") != "v2":
        raise ValueError("This bridge requires the SkyTNT v2 tokenizer")
    spec = importlib.util.spec_from_file_location("composer_skytnt_tokenizer", tokenizer_path)
    if spec is None or spec.loader is None:
        raise ValueError("Could not load the local tokenizer")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    tokenizer = module.MIDITokenizer("v2")
    tokenizer.set_optimise_midi(bool(config["tokenizer"].get("optimise_midi", True)))
    if tokenizer.vocab_size != config["tokenizer"]["vocab_size"]:
        raise ValueError("Tokenizer and model vocabulary do not match")
    return np, ort, tokenizer


def prompt_tokens(tokenizer, seed: Sequence[Note], args):
    """Use only one opening bar so a proposal always has space for generated continuation."""
    ticks = 480
    opening = min(args.numerator * 4.0 / args.denominator, args.length * 0.5)
    selected = clean_notes([n for n in seed if n.start < opening], opening, args.role)
    # The model vocabulary represents numerators 1..16 and denominators 2/4/8/16.
    # Unsupported signatures still get a bounded proposal; the output uses the requested meter.
    metadata = [["set_tempo", 0, 500000]]
    if args.numerator <= 16 and args.denominator in (2, 4, 8, 16):
        metadata.append(["time_signature", 0, args.numerator, int(math.log2(args.denominator)), 24, 8])
    sf = (args.root * 7) % 12
    if sf > 6:
        sf -= 12
    metadata.append(["key_signature", 0, sf, 0])
    programs = (80, 33, 26, 0)
    performance = [["patch_change", 0, 0, programs[args.role]]]
    performance.extend(["note", round(n.start * ticks), max(1, round(n.duration * ticks)),
                        0, n.pitch, n.velocity] for n in selected)
    tokens = tokenizer.tokenize([ticks, metadata, performance], add_bos_eos=True,
                                remap_track_channel=False, add_default_instr=False,
                                remove_empty_channels=False)
    tokens = [row for row in tokens if row[0] != tokenizer.eos_id]
    return tokens[-1024:]


def run_cached(session, feed, cache, length, np):
    """Execute one autoregressive step, using published input/output names and KV caches."""
    values = dict(feed)
    for parameter in session.get_inputs():
        if parameter.name.startswith("past_key_values"):
            present = parameter.name.replace("past_key_values", "present", 1)
            if present in cache:
                values[parameter.name] = cache[present]
            else:
                shape = parameter.shape
                values[parameter.name] = np.zeros((1, int(shape[1]), length, int(shape[3])), dtype=np.float32)
    outputs = session.run(None, values)
    result = dict(zip((output.name for output in session.get_outputs()), outputs))
    return result, {name: value for name, value in result.items() if name.startswith("present")}


def sample(logits, allowed: Sequence[int], random, np, temperature=0.95):
    if not allowed:
        raise ValueError("No valid event tokens for the local model")
    ids = np.asarray(allowed, dtype=np.int64)
    scores = np.asarray(logits, dtype=np.float64)[ids] / temperature
    scores -= np.max(scores)
    probabilities = np.exp(scores)
    order = np.argsort(-probabilities)
    order = order[:min(20, len(order))]
    probabilities = probabilities[order]
    probabilities /= probabilities.sum()
    cumulative = np.cumsum(probabilities)
    probabilities[cumulative - probabilities > 0.98] = 0
    probabilities /= probabilities.sum()
    return int(random.choice(ids[order], p=probabilities))


def generate(args) -> dict:
    np, ort, tokenizer = dependencies(args.model_dir, args.vendor_dir)
    seed = read_seed(args.input, args.length) if args.input else []
    tokens = prompt_tokens(tokenizer, seed, args)
    prompt_count = len(tokens)
    options = ort.SessionOptions()
    options.log_severity_level = 3
    options.intra_op_num_threads = 2
    options.inter_op_num_threads = 1
    base = ort.InferenceSession(str(args.model_dir / "model_base.onnx"), options,
                                providers=["CPUExecutionProvider"])
    event_model = ort.InferenceSession(str(args.model_dir / "model_token.onnx"), options,
                                       providers=["CPUExecutionProvider"])
    random = np.random.RandomState(args.seed)
    base_cache = {}
    base_past = 0
    next_input = np.asarray([tokens], dtype=np.int64)
    generated_notes = 0
    usable_generated_notes = 0
    # Both tokenizers encode time1 as the relative quarter-note count; time2 is sixteenths.
    clock = sum(event[1] for row in tokens if (event := tokenizer.tokens2event(row)))
    for _ in range(args.max_events):
        base_result, base_cache = run_cached(base, {"x": next_input}, base_cache, base_past, np)
        hidden = base_result["hidden"][:, -1:]
        embedding = hidden.shape[-1]
        row: List[int] = []
        event_name = ""
        event_cache = {}
        for step in range(tokenizer.max_token_seq):
            if step == 0:
                allowed = [tokenizer.event_ids["note"]]
                if generated_notes >= 4:
                    allowed.append(tokenizer.eos_id)
            else:
                name = tokenizer.events[event_name][step - 1]
                if name in ("track", "channel"):
                    allowed = [tokenizer.parameter_ids[name][0]]
                elif name == "pitch":
                    low, high = ((28, 60) if args.role == 1 else (48, 88))
                    allowed = tokenizer.parameter_ids[name][low:high + 1]
                elif name == "duration":
                    allowed = tokenizer.parameter_ids[name][1:65]
                elif name == "velocity":
                    allowed = tokenizer.parameter_ids[name][48:121]
                elif name == "time1":
                    remaining = max(0, int(math.ceil(args.length - clock)))
                    allowed = tokenizer.parameter_ids[name][:min(8, remaining + 1)]
                else:
                    allowed = tokenizer.parameter_ids[name]
            inputs = {"hidden": hidden if step == 0 else np.zeros((1, 0, embedding), dtype=np.float32),
                      "x": np.asarray([row[-1:] if step else []], dtype=np.int64)}
            result, event_cache = run_cached(event_model, inputs, event_cache, step, np)
            token = sample(result["y"][0, -1], allowed, random, np)
            row.append(token)
            if step == 0:
                if token == tokenizer.eos_id:
                    break
                event_name = tokenizer.id_events[token]
            if step == len(tokenizer.events[event_name]):
                break
        if row[0] == tokenizer.eos_id:
            break
        row += [tokenizer.pad_id] * (tokenizer.max_token_seq - len(row))
        tokens.append(row)
        event = tokenizer.tokens2event(row)
        if event:
            clock += event[1]
            if event[0] == "note":
                generated_notes += 1
                if clock + event[2] / 16.0 >= args.length:
                    break
                usable_generated_notes += 1
        base_past = len(tokens) - 1
        next_input = np.asarray([[row]], dtype=np.int64)
    if not usable_generated_notes:
        raise ValueError("The model did not produce a continuation; try another seed")
    score = tokenizer.detokenize(tokens)
    notes = []
    for track in score[1:]:
        for event in track:
            if event[0] == "note" and event[3] != 9:
                notes.append(Note(event[1] / score[0], event[2] / score[0], event[4], event[5]))
    notes = clean_notes(notes, args.length, args.role)
    # Do not publish an unchanged prompt as a successful AI proposal.
    generated_score = tokenizer.detokenize(tokens[prompt_count:])
    if not notes or not any(event[0] == "note" for track in generated_score[1:] for event in track):
        raise ValueError("The model did not produce usable notes; try another seed")
    write_midi(args.output, notes, args.length, args.numerator, args.denominator)
    return {"ok": True, "provider": "SkyTNT MIDI-Model tv2o-medium", "notes": len(notes),
            "generated_events": generated_notes, "length_beats": args.length, "seed": args.seed,
            "model_revision": MODEL_REVISION}


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input")
    parser.add_argument("--output", required=True)
    parser.add_argument("--model-dir", required=True)
    parser.add_argument("--vendor-dir", required=True)
    parser.add_argument("--root", type=int, default=0)
    parser.add_argument("--role", type=int, default=0)
    parser.add_argument("--bars", type=int, default=4)
    parser.add_argument("--numerator", type=int, default=4)
    parser.add_argument("--denominator", type=int, default=4)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--max-events", type=int, default=192)
    parser.add_argument("--check", action="store_true", help="Check installed local dependencies without inference")
    try:
        args = checked_request(parser.parse_args(argv))
        if args.check:
            dependencies(args.model_dir, args.vendor_dir)
            result = {"ok": True, "ready": True, "model_revision": MODEL_REVISION}
        else:
            result = generate(args)
        print(json.dumps(result, ensure_ascii=True), flush=True)
        return 0
    except Exception as error:
        print(json.dumps({"ok": False, "error": str(error)[:500]}, ensure_ascii=True), flush=True)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
