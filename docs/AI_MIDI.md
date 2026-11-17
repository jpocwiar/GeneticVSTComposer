# Optional local MIDI model

The composition workspace can use **SkyTNT MIDI-Model tv2o-medium** as an additional source of melodic proposals. The ONNX model runs locally in a separate CPU process; generation and playback do not contact a network service. The ordinary genetic engine works independently of this optional installation.

## Install the runtime

From the repository root, run:

```sh
./scripts/setup-ai-midi.sh
```

The script creates `.deps/ai-midi`, installs pinned Python dependencies in its virtual environment, and downloads approximately 938 MB of ONNX weights. It verifies both weight files against their published SHA-256 hashes. Nothing is installed globally. Use Python 3.9–3.12; another interpreter can be selected explicitly:

```sh
COMPOSER_AI_BOOTSTRAP_PYTHON=/absolute/path/to/python3.12 ./scripts/setup-ai-midi.sh
```

Select `.deps/ai-midi/bin/python` in the plugin's AI interpreter chooser (`Scripts/python.exe` on Windows). Projects retain that path as a chooser hint; reopening a project never grants permission to run its stored executable path. Select the local interpreter again after restarting the plugin, or set `GENETIC_COMPOSER_AI_PYTHON` to your trusted runtime. Development executables also discover `.deps/ai-midi` relative to their executable location, never through the current project directory. Runtime model files remain next to that environment, in `models/` and `vendor/`.

For a runtime stored elsewhere, pass its directory as the script's first argument. The runtime can be copied to another location, then selected in the plugin. Python virtual environments themselves are platform dependent; recreate the runtime when changing Python or operating system.

## What a proposal contains

The worker takes up to one opening bar from the selected source and generates a continuation within the requested total length. A role-specific instrument prompt distinguishes lead, bass, riff and arpeggio. It writes a single MIDI voice, selecting the lower voice for bass and the upper voice for other roles when the model proposes simultaneous notes. It retains the requested meter, phrase length and velocity, crops notes at the end, and limits the result to 512 notes and 128 quarter-note beats.

These are 12-TET MIDI proposals. Current harmony, scale and protected notes are applied by the plugin's subsequent genetic development/adaptation. The model is not used as a judge of artistic quality and does not natively represent the independent Microtonal workspace's EDO/ratio degrees. Existing note locks and document revisions remain the plugin's authority when committing a result.

The process has a bounded timeout and is killed on cancellation. A failed request leaves the current bank intact. Normal plugin operation embeds/extracts its worker script, so installed VST3/AU bundles do not depend on the repository's working directory. The optional environment and weights are installed separately from the plugin bundle.

## Command-line check and tests

```sh
.deps/ai-midi/bin/python scripts/ai-midi-bridge.py --check \
  --output /tmp/check.mid --model-dir .deps/ai-midi/models --vendor-dir .deps/ai-midi/vendor
.deps/ai-midi/bin/python -B tests/test_ai_midi_bridge.py
```

For an inference experiment:

```sh
.deps/ai-midi/bin/python scripts/ai-midi-bridge.py \
  --input /absolute/path/to/source.mid --output /tmp/proposal.mid \
  --model-dir .deps/ai-midi/models --vendor-dir .deps/ai-midi/vendor \
  --root 0 --role 0 --bars 4 --numerator 4 --denominator 4 --seed 41
```

The worker prints one JSON result with `ok`, note count, generated-event count, seed and model revision, or `ok: false` and an error. It does not download dependencies or weights during inference. A fixed seed makes sampling repeatable within the same runtime/provider; platform/ONNX version differences can change floating-point sampling.

## Sources and licenses

- Code/tokenizer: [SkyTNT/midi-model](https://github.com/SkyTNT/midi-model), source commit `f504d5cb58f769ab0f2909c679238f6621034573`, Apache-2.0. The installed tokenizer is unmodified; the worker's cache and event sampling follow the official `app_onnx.py` design.
- Weights/configuration: [skytnt/midi-model-tv2o-medium](https://huggingface.co/skytnt/midi-model-tv2o-medium), model commit `0f8f265d4330f4e46527ac2313200254c5757f5f`, model card marked Apache-2.0.
- The setup retains the upstream license, provenance and a SHA-256 manifest in the runtime. These declarations concern the supplied code and weights; corpus information is recorded in the upstream model card.

The Python inference worker is Apache-2.0, with the upstream license retained in [scripts/licenses/SkyTNT-Apache-2.0.txt](../scripts/licenses/SkyTNT-Apache-2.0.txt). The native bridge is C++/JUCE. Python dependencies are NumPy 1.26.4, ONNX Runtime 1.19.2, Pillow 10.4.0 and mido 1.3.3. Pillow is required by the upstream tokenizer import; no audio synthesis or cloud account is needed.

## Implementation verification

The protocol/MIDI suite passes all 10 tests in the isolated runtime. Real CPU inference was exercised with the downloaded ONNX weights: a two-bar lead request with seed 41 produced 16 notes; a seeded three-bar 7/8 bass continuation with seed 17 produced 21 notes over exactly 10.5 quarter-note beats. Both files parse and contain matched note-on/off events. These are integration checks, not listening-quality scores.
