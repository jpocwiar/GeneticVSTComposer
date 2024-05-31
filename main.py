from genetic import GeneticMelodyGenerator
import pygame
import random
from midiutil import MIDIFile
import random


#tylko do prezentacji
def create_midi(melody, fundamental_note_length, tempo, meter=(4,4), filename="output.mid"):

    midi_file = MIDIFile(2)
    melody_track = 0
    percussion_track = 1

    time = 0
    melody_channel = 0
    percussion_channel = 9

    midi_file.addTempo(melody_track, time, tempo)
    midi_file.addTempo(percussion_track, time, tempo)

    midi_file.addProgramChange(melody_track, melody_channel, time, 0)

    beats_per_measure = meter[0]
    beat_unit = meter[1]

    beat_duration = 1 / beat_unit * 4

    beat_time = 0
    while beat_time < time + (len(melody) * fundamental_note_length):
        midi_file.addNote(percussion_track, percussion_channel, 56, beat_time, beat_duration, 100)

        for sub_beat in range(1, beats_per_measure):
            midi_file.addNote(percussion_track, percussion_channel, 76, beat_time + (sub_beat * beat_duration),
                              beat_duration, 80)

        beat_time += beats_per_measure * beat_duration

    i = 0
    while i < len(melody):
        note = melody[i]
        duration = fundamental_note_length

        if note == -1:

            while i + 1 < len(melody) and melody[i + 1] == -2:
                duration += fundamental_note_length
                i += 1
            time += duration
        elif note == -2:
            continue
        else:
            while i + 1 < len(melody) and melody[i + 1] == -2:
                duration += fundamental_note_length
                i += 1
            velocity = random.randint(75, 85)
            midi_file.addNote(melody_track, melody_channel, note, time, duration, velocity)
            time += duration

        i += 1

    with open(filename, "wb") as output_file:
        midi_file.writeFile(output_file)


def play_midi(filename="output.mid"):
    pygame.init()
    pygame.mixer.init()
    pygame.mixer.music.load(filename)
    pygame.mixer.music.play()

    while pygame.mixer.music.get_busy():
        pygame.time.Clock().tick(10)


fundamental_note_length = 0.25 # wzgl ćwierćnuty
meter = (7,8)
generator = GeneticMelodyGenerator("A Minor", ('A-4', 'A-6'), meter, fundamental_note_length, 128, 100)

diversity = 0.8 # notes div, rhythmic div, interval div
dynamics = 0.7 # range, pause, pitch var, rhythmic div
arousal = 0.8 # rhythmic av, range, pause, pitch av, short cons, velocity
valence = 0.5 # dissonance, contour,
jazziness = 0.2
weirdness = 0.0 #large intervals, odd_index, all deviations
pauses = 0

coefficients_values = {
    'diversity': diversity,
    'diversity_interval': diversity,
    'dissonance': (1 - valence) * 0.4 + (jazziness) * 0.2,
    'rhythmic_diversity': diversity * 0.2 + dynamics * 0.5 + arousal * 0.2,
    'rhythmic_average_value': (1 - arousal),
    'very_long_notes_score': 0.05,
    'deviation_rhythmic_value': dynamics,
    'scale_conformance': (1 - jazziness) * 0.5 + 0.5,
    'chord_conformance': (1 - jazziness),
    #'root_conformance': 0.45, #trzeba dodać
    'melodic_contour': valence,
    'pitch_range': dynamics * 0.5 + arousal * 0.5,
    'pause_proportion': (dynamics * 0.2 - arousal * 0.1) * pauses,
    'large_intervals': weirdness,
    'average_pitch': arousal * 0.2 + valence * 0.3,
    'pitch_variation': dynamics * 0.5,
    'odd_index_notes': weirdness,
    'average_interval': (1 - dynamics) * 0.5 + (1 - valence) * 0.5,
    'scale_playing': jazziness * 0.3 + dynamics * 0.5,
    'short_consecutive_notes': 0.5 * arousal + jazziness * 0.2
}
# coefficients_values = {
#     'diversity': 0.85,
#     'diversity_interval': 0.75,
#     'dissonance': 0.2,
#     'rhythmic_diversity': 0.65,
#     'rhythmic_average_value': 0.25,
#     'very_long_notes_score': 0.05,
#     'deviation_rhythmic_value': 0.45,
#     'scale_conformance': 0.85,
#     'chord_conformance': 0.45,
#     'root_conformance': 0.45, #trzeba dodać
#     'melodic_contour': 0.15,
#     'pitch_range': 0.35,
#     'pause_proportion': 0.05,
#     'large_intervals': 0.05,
#     'average_pitch': 0.65,
#     'pitch_variation': 0.45,
#     'odd_index_notes': 0.01,
#     'average_interval': 0.35,
#     'scale_playing': 0.85,
#     'short_consecutive_notes': 0.8
# }

generator.set_coefficients(**coefficients_values)
n = 5
# melodies = generator.run(1, n)
melodies = generator.run_for_rhythm([60, -1, 60, -2, 60, -2, 60, -2, 60, -2, -2, 60, 60, -2], 1, n)
# melodies = generator.run_varying(1, n)

while True:
    print(f"Available melodies (1-{n}):")
    for index, melody in enumerate(melodies, start=1):
        print(f"Melody {index}: {melody}")

    # Get user input
    choice = int(input(f"Enter the number of the melody to play (1-{n}), or 0 to exit: "))
    if choice == 0:
        break
    elif 1 <= choice <= len(melodies):
        # selected_melody = melodies[choice - 1] * 4
        original_melody = melodies[choice - 1]
        variations = [0, 0, 0]

        while variations == [0, 0, 0]:
            variations = random.sample([-7, 0, 5, 7], 3)

        selected_melody = original_melody[:]
        for variation in variations:
            new_variation = [(note + variation if note > 0 else note) for note in original_melody]
            selected_melody += new_variation
        create_midi(selected_melody, fundamental_note_length, 111, meter)
        # create_midi([60,61,-1,63,64,-2,66,-1,-2,69,70,71,72,73,74,75] * 4, fundamental_note_length, 80, meter)
        play_midi()
    else:
        print("Invalid selection. Please try again.")
