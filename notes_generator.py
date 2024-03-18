import numpy as np
from mingus.containers import Note
import mingus.core.scales as scales
import re


class NotesGenerator:
    def __init__(self, key='C Major'):
        self.key = key

    def parse_scale_name(self):
        match = re.match(r"([A-G]#?|Bb)(.*)", self.key)
        if match:
            note = match.group(1)
            scale_type = match.group(2).strip()
            return note, scale_type
        else:
            raise ValueError("Wrong scale name")

    def choose_scale(self):
        note, scale_type = self.parse_scale_name()
        scale_methods = {
            "Harmonic Minor": scales.HarmonicMinor,
            "Natural Minor": scales.NaturalMinor,
            "Melodic Minor": scales.MelodicMinor,
            "Major": scales.Major,
            "Ionian": scales.Ionian,
            "Dorian": scales.Dorian,
            "Phrygian": scales.Phrygian,
            "Lydian": scales.Lydian,
            "Mixolydian": scales.Mixolydian,
            "Aeolian": scales.Aeolian,
            "Locrian": scales.Locrian,
            "Chromatic": scales.Chromatic,
            "Whole Tone": scales.WholeTone,
            "Octatonic": scales.Octatonic,
        }

        if scale_type in scale_methods:
            return np.array(scale_methods[scale_type](note).ascending()[:-1])
        else:
            for word in scale_type.split():
                for key in scale_methods:
                    if word in key:
                        #print(f"Wybieranie skali na podstawie słowa: {word}")
                        return np.array(scale_methods[key](note).ascending()[:-1])

            print("Unknown scale. Choosing chromatic...")
            return np.array(scales.Chromatic(note).ascending()[:-1])

    def generate_notes(self, number_of_octaves=1, start_octave=4):
        scale = self.choose_scale()
        notes = []

        for octave in range(start_octave, start_octave + number_of_octaves):
            for note in scale:
                note_value = int(Note(note, octave))
                if notes and note_value <= notes[-1]:
                    note_value += 12
                notes.append(note_value)

        return notes

    def generate_chromatic_notes(self, note_range):
        start_note, end_note = note_range
        if isinstance(start_note, int) and isinstance(end_note, int):
            return list(range(start_note, end_note + 1))
        elif isinstance(start_note, str) and isinstance(end_note, str):
            return list(range(int(Note(start_note)), int(Note(end_note)) + 1))
        else:
            raise ValueError("Wrong argument for notes range")