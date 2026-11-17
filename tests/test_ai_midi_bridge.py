"""Protocol/phrase tests require no weights; MIDI round-trips run when mido is installed."""
import argparse
import importlib.util
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True
SCRIPT = Path(__file__).resolve().parents[1] / "scripts/ai-midi-bridge.py"
spec = importlib.util.spec_from_file_location("ai_midi_bridge", SCRIPT)
bridge = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = bridge
spec.loader.exec_module(bridge)


class RequestTests(unittest.TestCase):
    def request(self, directory, **changes):
        values = dict(input=None, output=str(Path(directory) / "phrase.mid"), model_dir=directory,
                      vendor_dir=directory, root=0, role=0, bars=4, numerator=4,
                      denominator=4, seed=1, max_events=64)
        values.update(changes)
        return argparse.Namespace(**values)

    def test_valid_odd_meter_length(self):
        with tempfile.TemporaryDirectory() as directory:
            result = bridge.checked_request(self.request(directory, numerator=7, denominator=8, bars=3))
            self.assertEqual(result.length, 10.5)
            self.assertTrue(result.output.is_absolute())

    def test_reject_invalid_budgets_and_meter(self):
        with tempfile.TemporaryDirectory() as directory:
            for changed in (dict(bars=0), dict(numerator=0), dict(denominator=3), dict(bars=16, numerator=32),
                            dict(seed=-1), dict(role=4), dict(root=12), dict(max_events=0)):
                with self.subTest(changed=changed), self.assertRaises(ValueError):
                    bridge.checked_request(self.request(directory, **changed))

    def test_reject_overwriting_seed(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "seed.mid"
            path.write_bytes(b"test")
            with self.assertRaises(ValueError):
                bridge.checked_request(self.request(directory, input=str(path), output=str(path)))
            self.assertEqual(path.read_bytes(), b"test")

    def test_cli_failure_has_machine_readable_error(self):
        with tempfile.TemporaryDirectory() as directory:
            result = subprocess.run([sys.executable, str(SCRIPT), "--output", str(Path(directory) / "x.mid"),
                                     "--model-dir", directory, "--vendor-dir", directory, "--denominator", "3"],
                                    capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 1)
            response = json.loads(result.stdout)
            self.assertFalse(response["ok"])
            self.assertIn("meter", response["error"])
            self.assertFalse((Path(directory) / "x.mid").exists())


class NoteTests(unittest.TestCase):
    def test_selects_upper_voice_for_lead_and_lower_for_bass(self):
        notes = [bridge.Note(0, 1, 60, 90), bridge.Note(0, 1, 72, 90), bridge.Note(1, 1, 62, 90)]
        self.assertEqual(bridge.clean_notes(notes, 4, 0)[0].pitch, 72)
        self.assertEqual(bridge.clean_notes(notes, 4, 1)[0].pitch, 60)

    def test_crops_phrase_and_avoids_single_voice_overlap(self):
        notes = [bridge.Note(0, 3, 60, 90), bridge.Note(1, 4, 62, 90), bridge.Note(4, 1, 64, 90)]
        result = bridge.clean_notes(notes, 4, 0)
        self.assertEqual(len(result), 2)
        self.assertEqual(result[0].duration, 1)
        self.assertEqual(result[1].duration, 3)

    def test_discards_invalid_notes(self):
        invalid = [bridge.Note(math.nan, 1, 60, 90), bridge.Note(0, math.inf, 60, 90),
                   bridge.Note(-1, 1, 60, 90), bridge.Note(0, 0, 60, 90),
                   bridge.Note(0, 1, 999, 90), bridge.Note(0, 1, 60, 0)]
        self.assertEqual(bridge.clean_notes(invalid, 4, 0), [])

    def test_caps_output_at_engine_capacity(self):
        notes = [bridge.Note(i / 16, 1 / 16, 60, 90) for i in range(1024)]
        self.assertEqual(len(bridge.clean_notes(notes, 128, 0)), 512)

    def test_notes_are_deterministically_sorted(self):
        notes = [bridge.Note(2, 1, 62, 90), bridge.Note(0, 1, 60, 90), bridge.Note(1, 1, 64, 90)]
        self.assertEqual([n.start for n in bridge.clean_notes(notes, 4, 0)], [0, 1, 2])

    def test_midi_round_trip_preserves_meter_and_phrase_end(self):
        try:
            import mido
        except ImportError:
            self.skipTest("mido is installed by the optional AI runtime")
        notes = [bridge.Note(0, 0.5, 60, 90), bridge.Note(1.5, 0.25, 67, 100)]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "roundtrip.mid"
            bridge.write_midi(path, notes, 3.5, 7, 8)
            self.assertEqual(bridge.read_seed(path, 3.5), notes)
            midi = mido.MidiFile(str(path))
            self.assertEqual(sum(message.time for message in midi.tracks[0]), 3360)
            signatures = [m for m in midi.tracks[0] if m.type == "time_signature"]
            self.assertEqual((signatures[0].numerator, signatures[0].denominator), (7, 8))
            self.assertFalse(path.with_name(path.name + ".partial").exists())


if __name__ == "__main__":
    unittest.main()
