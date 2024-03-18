from midiutil import MIDIFile
import random
from mingus.containers import Note
import mingus.core.notes as NOTES
import mingus.core.chords as Chords
import numpy as np
from notes_generator import NotesGenerator

class GeneticMelodyGenerator:
    def __init__(self, scale, note_range, meter=(4, 4),
                 note_duration=0.5, population_size=64, num_generations=50, mutation_rate=0.3, crossover_rate=0.9):

        notes_gen = NotesGenerator(scale)
        notes = notes_gen.generate_chromatic_notes(note_range)
        self.scale_notes = notes_gen.generate_notes(8,1)
        self.NOTES = notes  # tu chyba w pluginie będziemy kontrolować zakres
        self.meter = meter
        self.note_duration = note_duration

        self.POPULATION_SIZE = population_size
        self.NUM_GENERATIONS = num_generations
        self.MUTATION_RATE = mutation_rate
        self.CROSSOVER_RATE = crossover_rate

        self.expected_length = int(1 / note_duration)

        self.notes_range = max(self.NOTES) - min(self.NOTES)

        self.set_coefficients()

    def set_coefficients(self, diversity=0.8, diversity_interval=0.8, dissonance=0.25, rhythmic_diversity=0.7,
                         rhythmic_average_value=0.5, very_long_notes_score=0, deviation_rhythmic_value=0.5,
                         scale_conformance=0.9, chord_conformance=0.5, melodic_contour=0.1, pitch_range=0.3,
                         pause_proportion=0.3, large_intervals=0.0, average_pitch=0.6, pitch_variation=0.4,
                         odd_index_notes=0.1, average_interval=0.3, scale_playing=0.8, short_consecutive_notes=0.75):
        self.mu_values = {
            'diversity': diversity,
            'diversity_interval': diversity_interval,
            'dissonance': dissonance,
            'rhythmic_diversity': rhythmic_diversity,
            'rhythmic_average_value': rhythmic_average_value,
            'very_long_notes_score': very_long_notes_score,
            'deviation_rhythmic_value': deviation_rhythmic_value,
            'scale_conformance': scale_conformance,
            'chord_conformance': chord_conformance,
            'melodic_contour': melodic_contour,
            'pitch_range': pitch_range,
            'pause_proportion': pause_proportion,
            'large_intervals': large_intervals,
            'average_pitch': average_pitch,
            'pitch_variation': pitch_variation,
            'odd_index_notes': odd_index_notes,
            'average_interval': average_interval,
            'scale_playing': scale_playing,
            'short_consecutive_notes': short_consecutive_notes,
        }
        self.sigma_values = {
            'diversity': 0.1,
            'diversity_interval': 0.1,
            'dissonance': 0.1,
            'rhythmic_diversity': 0.1,
            'rhythmic_average_value': 0.1,
            'deviation_rhythmic_value': 0.1,
            'very_long_notes_score': 0.1,
            'scale_conformance': 0.1,
            'chord_conformance': 0.1,
            'melodic_contour': 0.1,
            'pitch_range': 0.1,
            'pause_proportion': 0.1,
            'large_intervals': 0.1,
            'average_pitch': 0.1,
            'pitch_variation': 0.1,
            'odd_index_notes': 0.1,
            'average_interval': 0.1,
            'scale_playing': 0.1,
            'short_consecutive_notes': 0.1,
        }
        self.weights = {
            'diversity': 2,
            'diversity_interval': 2,
            'dissonance': 3,
            'rhythmic_diversity': 1,
            'rhythmic_average_value': 3,
            'deviation_rhythmic_value': 2,
            'very_long_notes_score': 3,
            'scale_conformance': 3,
            'chord_conformance': 3,
            'melodic_contour': 1,
            'pitch_range': 1,
            'pause_proportion': 1,
            'large_intervals': 5,
            'average_pitch': 1,
            'pitch_variation': 1,
            'odd_index_notes': 3,
            'average_interval': 1,
            'repetition': 2,
            'scale_playing': 2,
            'short_consecutive_notes': 2,
        }

    def fitness_directional_changes(self, melody_np):

        notes = melody_np[(melody_np != -1) & (melody_np != -2)]

        if len(notes) < 2:
            return 0

        intervals = np.diff(notes)

        intervals = intervals[intervals != 0]

        if len(intervals) < 2:
            return 0

        direction_changes = np.sum(np.diff(np.sign(intervals)) != 0)

        total_intervals_count = len(intervals) - 1

        if total_intervals_count == 0:
            return 0

        directional_change_score = direction_changes / total_intervals_count

        return directional_change_score

    def fitness_melodic_contour(self, melody_np):

        notes = melody_np[(melody_np != -1) & (melody_np != -2)]

        if len(notes) < 2:
            return 0

        intervals = np.diff(notes)
        positive_intervals_count = np.sum(intervals > 0)
        total_intervals_count = np.sum(intervals != 0)

        if total_intervals_count == 0:
            return 0.5

        melodic_contour_score = positive_intervals_count / total_intervals_count

        return melodic_contour_score

    def fitness_note_range(self, melody_np):
        notes = melody_np[(melody_np != -1) & (melody_np != -2)]
        if len(notes) < 1:
            return 0
        pitch_range = np.max(notes) - np.min(notes)
        pitch_range_score = pitch_range / self.notes_range
        return pitch_range_score

    def fitness_average_pitch(self, melody_np):

        valid_notes = melody_np[(melody_np != -1) & (melody_np != -2)]
        if len(valid_notes) == 0:
            return 0
        average_pitch = np.mean(valid_notes)
        average_pitch_score = average_pitch / self.notes_range

        return average_pitch_score

    def fitness_pause_proportion(self, melody_np):
        total_length = len(melody_np)

        if total_length == 0:
            return 0

        pause_length_counter = 0
        in_pause = False

        for note in melody_np:
            if note == -1 or (in_pause and note == -2):
                pause_length_counter += 1
                in_pause = True
            else:
                in_pause = False

        pause_proportion_score = pause_length_counter / total_length

        return pause_proportion_score

    def fitness_intervals(self, melody_np): # dissonances

        notes_without_pauses_and_extensions = melody_np[(melody_np != -1) & (melody_np != -2)]

        if len(notes_without_pauses_and_extensions) < 2:
            dissonance_score = 0
            large_intervals_score = 0
        else:
            intervals = np.abs(np.diff(notes_without_pauses_and_extensions))
            intervals = np.mod(intervals, 12)
            dissonance_values = np.where(
                np.isin(intervals, [10]), 0.5,
                np.where(np.isin(intervals, [0, 1, 2, 3, 4, 5, 7, 8, 9, 12]), 0, 1)
            )
            dissonance_score = np.mean(dissonance_values)

            large_intervals_count = np.sum(intervals > 12)
            total_intervals_count = len(intervals)
            large_intervals_score = large_intervals_count / total_intervals_count

        return dissonance_score, large_intervals_score

    def fitness_scale_and_chord(self, melody_np): #how many notes belong to scale / chord
        scale_notes_np = np.array(self.scale_notes)

        scale_length_counter = 0
        chord_length_counter = 0
        total_length_counter = 0
        in_scale = False
        in_chord = False

        for i, note in enumerate(melody_np):
            if note != -2:
                in_scale = note in scale_notes_np
                # chord_notes = Chords.from_shorthand(self.find_chord_at_time(i * self.note_duration))
                # in_chord = NOTES.int_to_note(note % 12) in chord_notes
            total_length_counter += 1
            if in_scale:
                scale_length_counter += 1
            if in_chord:
                chord_length_counter += 1

        if total_length_counter != 0:
            scale_conformance_score = scale_length_counter / total_length_counter
            chord_conformance_score = chord_length_counter / total_length_counter
        else:
            return 0

        return scale_conformance_score, chord_conformance_score

    def fitness_pitch_variation(self, melody_np):
        valid_notes = melody_np[(melody_np != -1) & (melody_np != -2)]

        if len(valid_notes) < 2:
            return 0

        standard_deviation = np.std(valid_notes)
        max_possible_std = self.notes_range / np.sqrt(12)
        pitch_variation_score = standard_deviation / max_possible_std

        return pitch_variation_score


    def fitness_odd_index_notes(self, melody_np):
        beat_length = int(self.meter[0] / self.note_duration * 4 / self.meter[1])
        num_beats = len(melody_np) // beat_length
        odd_index_scores = []
        for i in range(num_beats):

            beat = melody_np[i * beat_length:(i + 1) * beat_length]
            odd_note_indices = np.where((beat[1::2] > 0))[0] * 2 + 1
            note_and_extension_count = 0
            for idx in odd_note_indices:
                extension_idx = idx + 1
                while extension_idx < len(beat) and beat[extension_idx] == -2:
                    note_and_extension_count += 1
                    extension_idx += 1
            beat_length_adjusted = len(beat) - 2

            if beat_length_adjusted > 0:

                odd_index_note_and_extension_ratio = note_and_extension_count / beat_length_adjusted
                odd_index_scores.append(odd_index_note_and_extension_ratio)
            else:
                odd_index_scores.append(0)

        if odd_index_scores:
            average_odd_index_ratio = np.mean(odd_index_scores)
        else:
            average_odd_index_ratio = 0

        return average_odd_index_ratio

    def fitness_note_diversity(self, melody_np): # percentage of unique notes

        beat_length = int(self.meter[0] / self.note_duration * 4 / self.meter[1])
        num_beats = len(melody_np) // beat_length
        if num_beats == 0:
            return 0

        diversity_scores = []

        for i in range(num_beats):

            beat = melody_np[i * beat_length:(i + 1) * beat_length]

            beat = beat[(beat != -2) & (beat != -1)]

            if len(beat) > 1:
                unique_notes = np.unique(beat)
                diversity_score = len(unique_notes) / len(beat)
                diversity_scores.append(diversity_score)
            else:
                diversity_scores.append(0)

        if diversity_scores:
            average_diversity = np.mean(diversity_scores)
        else:
            average_diversity = 0

        return average_diversity

    def fitness_diversity_intervals(self, melody_np): #percentage of unique intervals

        beat_length = int(self.meter[0] / self.note_duration * 4 / self.meter[1])
        num_beats = len(melody_np) // beat_length
        if num_beats == 0:
            return 0

        diversity_scores = []

        for i in range(num_beats):

            beat = melody_np[i * beat_length:(i + 1) * beat_length]

            beat = beat[(beat != -2) & (beat != -1)]

            if len(beat) > 1:
                intervals = np.diff(beat)
                intervals = intervals[intervals <= 12]
                unique_intervals = len(np.unique(np.abs(intervals)))
                diversity_score = unique_intervals / len(beat)
                diversity_scores.append(diversity_score)
            else:
                diversity_scores.append(0)

        if diversity_scores:
            average_diversity = np.mean(diversity_scores)
        else:
            average_diversity = 0

        return average_diversity

    def fitness_rhythm(self, melody_np): # percentage of unique rhythm values
        beat_length = int(self.meter[0] / self.note_duration * 4 / self.meter[1])

        num_beats = len(melody_np) // beat_length

        rhythmic_diversity_scores = []

        for i in range(num_beats):

            beat = melody_np[i * beat_length:(i + 1) * beat_length]

            extension_lengths = []
            current_length = 0
            for note in beat:
                if note == -2:
                    current_length += 1
                elif current_length > 0:
                    extension_lengths.append(current_length)
                    current_length = 0
            if current_length > 0:
                extension_lengths.append(current_length)

            if len(extension_lengths) > 1:
                unique_lengths_count = len(set(extension_lengths))
                total_extensions_count = len(extension_lengths)
                rhythmic_diversity_score = unique_lengths_count / total_extensions_count
                rhythmic_diversity_scores.append(rhythmic_diversity_score)
            else:
                rhythmic_diversity_scores.append(0)

        if rhythmic_diversity_scores:
            average_rhythmic_diversity = np.mean(rhythmic_diversity_scores)
        else:
            average_rhythmic_diversity = 0

        return average_rhythmic_diversity

    def fitness_log_rhythmic_value(self, melody_np):
        extension_indices = np.where(melody_np == -2)[0]
        rhythmic_values = []

        if len(extension_indices) > 0:
            start_index = extension_indices[0]
            for i in range(1, len(extension_indices)):
                if extension_indices[i] != extension_indices[i - 1] + 1:
                    rhythmic_values.append(
                        extension_indices[i - 1] - start_index + 2)
                    start_index = extension_indices[i]
            rhythmic_values.append(extension_indices[-1] - start_index + 2)

        single_notes = len(melody_np) - len(extension_indices) - len(rhythmic_values)
        rhythmic_values.extend([1] * single_notes)
        log_rhythmic_values = np.log2(rhythmic_values) if rhythmic_values else []

        if rhythmic_values:
            average_rhythmic_value = np.mean(rhythmic_values)
            log_average_rhythmic_value = np.log2(average_rhythmic_value)

            normalized_log_rhythmic_value = (log_average_rhythmic_value - np.log2(1)) / (
                        np.log2(4 * self.expected_length) - np.log2(1))
            std_log_rhythmic_value = np.std(log_rhythmic_values)
            normalized_std_log_rhythmic_value = std_log_rhythmic_value / np.std([0, np.log2(4 * self.expected_length)])
        else:
            normalized_log_rhythmic_value = 0
            normalized_std_log_rhythmic_value = 0

        return normalized_log_rhythmic_value, normalized_std_log_rhythmic_value

    def proportion_of_long_notes(self, melody_np):
        extension_indices = np.where(melody_np == -2)[0]
        rhythmic_values = []

        if len(extension_indices) > 0:
            start_index = extension_indices[0]
            for i in range(1, len(extension_indices)):
                if extension_indices[i] != extension_indices[i - 1] + 1:
                    rhythmic_values.append(
                        extension_indices[i - 1] - start_index + 2)
                    start_index = extension_indices[i]
            rhythmic_values.append(extension_indices[-1] - start_index + 2)

        single_notes = len(melody_np) - len(extension_indices) - len(rhythmic_values)
        rhythmic_values.extend([1] * single_notes)

        long_notes_count = len([value for value in rhythmic_values if value > 4])
        total_notes_count = len(rhythmic_values)
        proportion_long_notes = long_notes_count / total_notes_count if total_notes_count > 0 else 0

        return proportion_long_notes

    def fitness_average_intervals(self, melody_np):

        valid_notes = melody_np[(melody_np != -1) & (melody_np != -2)]

        if len(valid_notes) < 2:
            return -1

        intervals = np.diff(valid_notes)

        valid_intervals = intervals[intervals <= 12]
        if len(valid_intervals) < 1:
            return -1

        average_interval = np.mean(np.abs(valid_intervals))

        average_interval_ratio = average_interval / 12

        return average_interval_ratio


    def fitness_small_intervals(self, melody_np):

        valid_notes = melody_np[(melody_np != -2)]

        intervals = np.diff(valid_notes)

        small_intervals_mask = (intervals >= -3) & (intervals <= 3) & (intervals != 0)

        small_consecutive_intervals = np.sum(small_intervals_mask[:-1] & small_intervals_mask[1:])

        total_intervals = len(intervals)

        if total_intervals == 0:
            return 0
        small_interval_ratio = small_consecutive_intervals / total_intervals

        return small_interval_ratio

    def fitness_repeated_short_notes(self, melody_np):

        shortest_notes_mask = (melody_np >= 0)
        shortest_notes_indices = np.nonzero(shortest_notes_mask)[0]

        consecutive_notes_mask = np.diff(shortest_notes_indices) <= 2

        total_consecutive_short_notes = np.sum(consecutive_notes_mask)

        total_short_notes = np.count_nonzero(shortest_notes_mask)

        if total_short_notes == 0:
            return 0

        short_notes_ratio = total_consecutive_short_notes / total_short_notes

        return short_notes_ratio

    def count_repeated_note_series(self, melody_np, series_length):
        valid_notes = melody_np[(melody_np != -1) & (melody_np != -2)]

        if len(valid_notes) - series_length - 1 < series_length:
            return 0

        repeated_series_count = 0
        seen_series = set()

        for i in range(len(valid_notes) - series_length + 1):
            series = tuple(valid_notes[i:i + series_length])

            if series in seen_series:
                if len(set(series)) == 1:

                    repeated_series_count += 0.5
                else:

                    repeated_series_count += 1
            else:
                seen_series.add(series)

        return repeated_series_count * series_length / (len(valid_notes) - series_length - 1)

    def fitness_repeated_note_series(self, melody_np, max_sequence_length=4):
        total_repeats_score = 0

        for series_length in range(2, max_sequence_length + 1):
            repeats = self.count_repeated_note_series(melody_np, series_length)
            total_repeats_score += repeats

        normalized_score = total_repeats_score / (max_sequence_length - 1)

        return normalized_score

    def fitness(self, melody):


        melody_np = np.array(melody)

        dissonance_score, large_intervals_score = self.fitness_intervals(melody_np)

        scale_conformance_score, chord_conformance_score = self.fitness_scale_and_chord(melody_np)
        pause_proportion_score = self.fitness_pause_proportion(melody_np)

        diversity_score = self.fitness_note_diversity(melody_np)
        diversity_interval_score = self.fitness_diversity_intervals(melody_np)
        rhythmic_diversity_score = self.fitness_rhythm(melody_np)
        melodic_contour_score = self.fitness_melodic_contour(melody_np)

        pitch_range_score = self.fitness_note_range(melody_np)
        average_pitch_score = self.fitness_average_pitch(melody_np)
        pitch_variation_score = self.fitness_pitch_variation(melody_np)
        odd_index_notes_score = self.fitness_odd_index_notes(melody_np)
        average_interval_score = self.fitness_average_intervals(melody_np)

        scale_playing_score = self.fitness_small_intervals(melody_np)
        short_consecutive_score = self.fitness_repeated_short_notes(melody_np)

        rhythmic_average_value, deviation_rhythmic_value = self.fitness_log_rhythmic_value(melody_np)

        scores = {
            'diversity': diversity_score,
            'diversity_interval': diversity_interval_score,
            'dissonance': dissonance_score,
            'rhythmic_diversity': rhythmic_diversity_score,
            'rhythmic_average_value': rhythmic_average_value,
            'deviation_rhythmic_value': deviation_rhythmic_value,
            'scale_conformance': scale_conformance_score,
            # 'chord_conformance': chord_conformance_score,
            'melodic_contour': melodic_contour_score,
            'pitch_range': pitch_range_score,
            'pause_proportion': pause_proportion_score,
            'large_intervals': large_intervals_score,
            'average_pitch': average_pitch_score,
            'pitch_variation': pitch_variation_score,
            'odd_index_notes': odd_index_notes_score,
            'average_interval': average_interval_score,
            'scale_playing': scale_playing_score,
            'short_consecutive_notes': short_consecutive_score,
        }

        fitness_value = sum(
            self.weights[feature] * np.exp(-0.5 * ((score - self.mu_values[feature]) / self.sigma_values[feature]) ** 2)
            for feature, score in scores.items()
        )
        return fitness_value

    def mutate(self, melody):
        melody_np = np.array(melody)
        indices = np.arange(len(melody))
        valid_indices = np.where((melody_np != -1) & (melody_np != -2))[0]

        if random.random() < self.MUTATION_RATE and len(valid_indices) > 1:
            first_note_index, second_note_index = np.random.choice(valid_indices, 2, replace=False)
            interval = random.randint(-12, 12)
            melody_np[second_note_index] = melody_np[first_note_index] + interval

        if random.random() < self.MUTATION_RATE:
            start_index = np.random.randint(0, len(melody))
            length = np.random.randint(1, min(self.meter[0] * 8 / self.meter[1], len(melody)))
            end_index = min(start_index + length, len(melody_np) - 1)

            transpose_value = random.randint(-12, 12)

            notes_mask = (melody_np[start_index:end_index] > 0)

            melody_np[start_index:end_index][notes_mask] += transpose_value
            melody_np[start_index:end_index][notes_mask] = np.clip(melody_np[start_index:end_index][notes_mask],
                                                                   self.NOTES[0], self.NOTES[-1])
        valid_indices = np.where(melody_np != -2)[0]

        if random.random() < self.MUTATION_RATE and len(valid_indices) > 0:
            extend_index = np.random.choice(valid_indices)

            if extend_index == 0:
                melody_np = np.insert(melody_np, 1, -2)
                melody_np = np.delete(melody_np, -1)
            else:

                melody_np[extend_index - 1], melody_np[extend_index] = melody_np[extend_index], -2

        if random.random() < self.MUTATION_RATE:
            replace_index = np.random.choice(indices)
            if melody_np[replace_index] == -1:

                melody_np[replace_index] = np.random.choice(self.NOTES)
            else:

                melody_np[replace_index] = -1

        if random.random() < self.MUTATION_RATE:
            start_index = np.random.choice(indices)
            num_notes_to_extend = np.random.randint(1, min(self.meter[0] * 8 / self.meter[1], len(melody)))
            end_index = min(start_index + num_notes_to_extend, len(melody) - 1)

            melody_np[start_index:end_index] = -2

        valid_indices = np.where(melody_np != -2)[0]

        if len(valid_indices) > 0 and random.random() < self.MUTATION_RATE:
            chosen_index = np.random.choice(valid_indices)
            chosen_note = melody_np[chosen_index]

            extension_count = 0
            next_index = chosen_index + 1
            while next_index < len(melody_np) and melody_np[next_index] == -2:
                extension_count += 1
                next_index += 1

            if extension_count + 1 > self.expected_length:

                replace_index = np.random.randint(chosen_index + 1, chosen_index + 1 + extension_count)
                melody_np[replace_index] = chosen_note
            elif extension_count + 1 < self.expected_length:

                additional_extensions = np.random.randint(1, self.expected_length - extension_count + 1)
                end_index = min(chosen_index + 1 + additional_extensions, len(melody_np))
                melody_np[chosen_index + 1:end_index] = -2

        if random.random() < self.MUTATION_RATE:

            start_index = np.random.randint(0, len(melody) - 1)
            length = np.random.randint(1, min(self.meter[0] * 8 / self.meter[1], len(melody)))
            end_index = min(start_index + length, len(melody_np) - 1)

            fragment = melody_np[start_index:end_index]
            valid_notes_fragment = fragment[(fragment != -2) & (fragment != -1)]

            if len(valid_notes_fragment) > 1:
                ascending = random.choice([True, False])
                sorted_fragment = np.sort(valid_notes_fragment) if ascending else np.sort(valid_notes_fragment)[
                                                                                  ::-1]

                valid_notes_indices = indices[start_index:end_index][(fragment != -2) & (fragment != -1)]
                melody_np[valid_notes_indices] = sorted_fragment

        if random.random() < self.MUTATION_RATE:

            beat_length = int(self.meter[0] / self.note_duration * 4 / self.meter[1])

            num_beats = len(melody) // beat_length

            if num_beats > 1:
                beat1, beat2 = np.random.choice(num_beats, 2, replace=False)
                start1 = beat1 * beat_length
                start2 = beat2 * beat_length

                num_notes_to_copy = np.random.randint(0, beat_length)

                end1 = min(start1 + num_notes_to_copy, len(melody))
                end2 = min(start2 + num_notes_to_copy, len(melody))

                melody_np[start2:end2] = melody_np[start1:end1]

                if random.random() < self.MUTATION_RATE:
                    transpose_values = [0, 12, 5, 7, -12, -5, -7]
                    transpose_value = random.choice(transpose_values)

                    notes_mask = (melody_np[start2:end2] > 0)

                    melody_np[start2:end2][notes_mask] += transpose_value

                    melody_np[start2:end2][notes_mask] = np.clip(melody_np[start2:end2][notes_mask], self.NOTES[0],
                                                                 self.NOTES[len(self.NOTES) - 1])

        if random.random() < self.MUTATION_RATE:

            beat_length = int(self.meter[0] / self.note_duration * 4 / self.meter[1])
            num_beats = len(melody) // beat_length

            if num_beats > 1:
                selected_beat = np.random.choice(num_beats, 1)[0]
                start = selected_beat * beat_length

                num_notes_to_copy = np.random.randint(1, beat_length // 2 + 1)

                end = min(start + num_notes_to_copy, len(melody))
                replace_start = start + num_notes_to_copy
                replace_end = min(replace_start + num_notes_to_copy, len(melody))

                melody_np[replace_start:replace_end] = melody_np[start:end]

        return melody_np.tolist()

    def crossover(self, parent1, parent2):
        index = random.randint(1, len(parent1) - 1)
        child1 = parent1[:index] + parent2[index:]
        child2 = parent2[:index] + parent1[index:]
        return child1, child2

    def tournament_selection(self, population, tournament_size=4):
        tournament = random.sample(population, tournament_size)
        best = max(tournament, key=self.fitness)
        return best

    def get_range(self, start_note=45, end_note=105):
        if isinstance(start_note, int) and isinstance(end_note, int):
            return start_note, end_note
        elif isinstance(start_note, str) and isinstance(end_note, str):
            return int(Note(start_note)), int(Note(end_note))
        else:
            raise ValueError("Wrong argument for notes range")

    def generate_population(self, note_amount):
        population = []
        for _ in range(self.POPULATION_SIZE):
            individual = [random.choice(self.NOTES)]
            for _ in range(note_amount - 1):
                change = random.randint(-12, 12)
                next_note = individual[-1] + change
                if next_note < self.NOTES[0] or next_note > self.NOTES[len(self.NOTES) - 1]:
                    next_note = individual[-1] - 2 * change
                individual.append(next_note)
            population.append(individual)
        return population

    def run(self, measures=1):

        note_amount = int(self.meter[0] / self.note_duration * 4 / self.meter[1] * measures)
        population = self.generate_population(note_amount)
        for generation in range(self.NUM_GENERATIONS):
            print(f'Generation {generation + 1}/{self.NUM_GENERATIONS}')
            new_population = []
            while len(new_population) < self.POPULATION_SIZE:
                parent1 = self.tournament_selection(population)
                parent2 = self.tournament_selection(population)
                if random.random() < self.CROSSOVER_RATE:
                    child1, child2 = self.crossover(parent1, parent2)
                else:
                    child1, child2 = parent1, parent2

                new_population.extend([self.mutate(child1), self.mutate(child2)])
            population = new_population

        best_melody = max(population, key=self.fitness)

        return best_melody
