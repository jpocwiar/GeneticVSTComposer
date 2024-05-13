#include "genetic.hpp"
#include "mingus.hpp"
#include "notes_generator.hpp"
#include <algorithm>
#include <iostream>
#include <numeric>
#include <cmath>
#include <set>
#include <unordered_set>

GeneticMelodyGenerator::GeneticMelodyGenerator(const std::string& scale, const std::pair<int, int>& noteRange,
    float diversity, float dynamics, float arousal,
    float valence, float jazziness, float weirdness,
    const std::pair<int, int>& meter, float noteDuration,
    int populationSize, int numGenerations)
    : diversity(diversity), dynamics(dynamics), arousal(arousal),
    valence(valence), jazziness(jazziness), weirdness(weirdness), meter(meter), noteDuration(noteDuration),
    populationSize(populationSize), numGenerations(numGenerations),
    mutationRate(0.3f), crossoverRate(0.9f), // Set rates directly
    expectedLength(static_cast<int>(1 / noteDuration)),
    notesRange(noteRange.second - noteRange.first)
     {

    std::random_device rd;  // Obtain a seed for the random number generator
    rng = std::mt19937(rd()); // Use the seed to initialize the generator


    NotesGenerator generator = NotesGenerator(scale);

    int notesCount = noteRange.second - noteRange.first + 1;

    NOTES = generator.generateChromaticNotes(noteRange);
    scale_notes = NotesGenerator(scale).generateNotes(8, 1);

    set_coefficients(diversity, dynamics, arousal, valence,
        jazziness, weirdness);
}

void GeneticMelodyGenerator::set_coefficients(float diversity, float dynamics, float arousal,
    float valence, float jazziness, float weirdness, const std::map<std::string, float>& mu_values,
    const std::map<std::string, float>& sigma_values,
    const std::map<std::string, int>& weights) {
    // Ustaw wartoœci domyœlne, jeœli nie przekazano ¿adnych map
    this->muValues = mu_values.empty() ? std::map<std::string, float>{
        {"diversity", diversity},
        { "diversity_interval", diversity },
        { "dissonance", (1 - valence) * 0.4 + jazziness * 0.2 },
        { "rhythmic_diversity", diversity * 0.2 + dynamics * 0.5 + arousal * 0.2 },
        { "rhythmic_average_value", (1 - arousal) },
        { "very_long_notes_score", 0.05 },
        { "deviation_rhythmic_value", dynamics },
        { "scale_conformance", (1 - jazziness) * 0.5 + 0.5 },
        { "root_conformance", 0.3 }, 
        { "melodic_contour", valence },
        { "pitch_range", dynamics * 0.5 + arousal * 0.5 },
        { "pause_proportion", dynamics * 0.2 - arousal * 0.1 },
        { "large_intervals", weirdness },
        { "average_pitch", arousal * 0.2 + valence * 0.3 },
        { "pitch_variation", dynamics * 0.5 },
        { "odd_index_notes", weirdness },
        { "average_interval", (1 - dynamics) * 0.5 + (1 - valence) * 0.5 },
        { "scale_playing", jazziness * 0.3 + dynamics * 0.5 },
        { "short_consecutive_notes", 0.5 * arousal + jazziness * 0.2 }
    } : mu_values;

    this->sigmaValues = sigma_values.empty() ? std::map<std::string, float>{
        {"diversity", 0.1},
        { "diversity_interval", 0.1 },
        { "dissonance", 0.1 },
        { "rhythmic_diversity", 0.1 },
        { "rhythmic_average_value", 0.1 },
        { "deviation_rhythmic_value", 0.1 },
        { "very_long_notes_score", 0.1 },
        { "scale_conformance", 0.1 },
        { "root_conformance", 0.1 },
        { "melodic_contour", 0.1 },
        { "pitch_range", 0.1 },
        { "pause_proportion", 0.1 },
        { "large_intervals", 0.1 },
        { "average_pitch", 0.1 },
        { "pitch_variation", 0.1 },
        { "odd_index_notes", 0.1 },
        { "average_interval", 0.1 },
        { "scale_playing", 0.1 },
        { "short_consecutive_notes", 0.1 }
    } : sigma_values;

    this->weights = weights.empty() ? std::map<std::string, int>{
        {"diversity", 2},
        { "diversity_interval", 2 },
        { "dissonance", 3 },
        { "rhythmic_diversity", 1 },
        { "rhythmic_average_value", 3 },
        { "deviation_rhythmic_value", 2 },
        { "very_long_notes_score", 3 },
        { "scale_conformance", 3 },
        { "root_conformance", 3 },
        { "melodic_contour", 1 },
        { "pitch_range", 1 },
        { "pause_proportion", 1 },
        { "large_intervals", 5 },
        { "average_pitch", 1 },
        { "pitch_variation", 1 },
        { "odd_index_notes", 3 },
        { "average_interval", 1 },
        { "repetition", 2 },
        { "scale_playing", 2 },
        { "short_consecutive_notes", 2 }
    } : weights;
}

void GeneticMelodyGenerator::mutate(std::vector<int>& melody) {
    std::uniform_real_distribution<float> prob_dist(0.0, 1.0);
    std::uniform_int_distribution<int> interval_dist(-12, 12);
    float MUTATION_RATE = 0.3;
    std::vector<int> valid_indices;
    for (int i = 0; i < melody.size(); ++i) {
        if (melody[i] != -1 && melody[i] != -2) {
            valid_indices.push_back(i);
        }
    }

    // Pierwsza mutacja: zamiana dwóch nut na interwa³
    if (prob_dist(rng) < MUTATION_RATE && !melody.empty()) {
        std::uniform_int_distribution<int> index_dist(0, melody.size() - 1);
        int first_note_index = index_dist(rng);
        int second_note_index;
        do {
            second_note_index = index_dist(rng);
        } while (second_note_index == first_note_index); // Zapewniamy ró¿ne indeksy

        int interval = interval_dist(rng);
        melody[second_note_index] = melody[first_note_index] + interval;
        // Ustawienie noty w obrêbie dozwolonego zakresu
        melody[second_note_index] = std::min(std::max(melody[second_note_index], NOTES.front()), NOTES.back());
    }

    // Przyk³ad kolejnej mutacji: transpozycja fragmentu melodii
    if (prob_dist(rng) < MUTATION_RATE && !melody.empty()) {
        std::uniform_int_distribution<int> index_dist(0, melody.size() - 1);
        int start_index = index_dist(rng);
        int length = std::uniform_int_distribution<int>(1, std::min(meter.first * 8 / meter.second, static_cast<int>(melody.size())))(rng);
        int end_index = std::min(start_index + length, static_cast<int>(melody.size()));

        int transpose_value = interval_dist(rng);

        // Transpozycja nut w obrêbie fragmentu
        for (int i = start_index; i < end_index; ++i) {
            if (melody[i] > 0) { // Sprawdzamy, czy nuta nie jest pauz¹
                melody[i] += transpose_value;
                melody[i] = std::min(std::max(melody[i], NOTES.front()), NOTES.back());
            }
        }
    }

    // Extension mutation
    if (std::uniform_real_distribution<float>(0.0, 1.0)(rng) < MUTATION_RATE && valid_indices.size() > 1 && !melody.empty()) {
    // Adjust the range for uniform_int_distribution to exclude the first index
        int extend_index = valid_indices[std::uniform_int_distribution<int>(1, valid_indices.size() - 1)(rng)];

        //melody[extend_index - 1] = melody[extend_index];
        melody[extend_index] = -2;
    }

    // Replacement mutation
    if (std::uniform_real_distribution<float>(0.0, 1.0)(rng) < MUTATION_RATE && !melody.empty()) {
        int replace_index = std::uniform_int_distribution<int>(0, melody.size() - 1)(rng);
        if (melody[replace_index] == -1) {
            // Replace a pause with a random note
            melody[replace_index] = NOTES[std::uniform_int_distribution<int>(0, NOTES.size() - 1)(rng)];
        }
        else {
            // Replace a note with a pause
            melody[replace_index] = -1;
        }
    }

    // Extension mutation
    if (std::uniform_real_distribution<float>(0.0, 1.0)(rng) < MUTATION_RATE && valid_indices.size() > 1) {
        // Adjust the range to exclude index 0 from being chosen for the start of extension
        int start_index = valid_indices[std::uniform_int_distribution<int>(1, valid_indices.size() - 1)(rng)];

        int num_notes_to_extend = std::uniform_int_distribution<int>(1, std::min(meter.first * 8 / meter.second, static_cast<int>(melody.size() - start_index)))(rng);

        int end_index = std::min(start_index + num_notes_to_extend, static_cast<int>(melody.size()));
    
        std::fill(melody.begin() + start_index, melody.begin() + end_index, -2);
    }

    // Recalculate valid_indices after extension mutation
    valid_indices.clear();
    for (int i = 0; i < melody.size(); ++i) {
        if (melody[i] != -2) {
            valid_indices.push_back(i);
        }
    }

    // Note replacement mutation within extensions
    if (std::uniform_real_distribution<float>(0.0, 1.0)(rng) < MUTATION_RATE && valid_indices.size() > 1) {
        int chosen_index = valid_indices[std::uniform_int_distribution<int>(0, valid_indices.size() - 1)(rng)];
        int chosen_note = melody[chosen_index];
        int extension_count = 0;
        int next_index = chosen_index + 1;

        // Check extensions following the chosen index
        while (next_index < melody.size() && melody[next_index] == -2) {
            extension_count++;
            next_index++;
        }

        // Ensure we have a valid range for the random number generator
        if (chosen_index + 1 < melody.size()) {
            if (extension_count + 1 > expectedLength) {
                // The range for replace_index is chosen_index + 1 to chosen_index + 1 + extension_count
                // This should not go beyond the size of the melody vector
                int replace_index = std::uniform_int_distribution<int>(chosen_index + 1, std::min(chosen_index + 1 + extension_count, static_cast<int>(melody.size()) - 1))(rng);
                melody[replace_index] = chosen_note;
            }
            else if (extension_count + 1 < expectedLength && chosen_index + 1 + extension_count < melody.size()) {
                // If we're going to extend, ensure it doesn't go beyond the size of the melody
                int additional_extensions = std::uniform_int_distribution<int>(1, std::min(expectedLength - extension_count, static_cast<int>(melody.size()) - chosen_index - extension_count - 1))(rng);
                int end_index = std::min(chosen_index + 1 + additional_extensions, static_cast<int>(melody.size()));
                std::fill(melody.begin() + chosen_index + 1, melody.begin() + end_index, -2);
            }
        }
    }

    // Sort mutation
    if (std::uniform_real_distribution<float>(0.0, 1.0)(rng) < MUTATION_RATE && !melody.empty()) {
        int start_index = std::uniform_int_distribution<int>(0, static_cast<int>(melody.size()) - 1)(rng);
        float max_length = meter.first * 8 / meter.second;
        if (static_cast<int>(max_length) > 1) {
            int length = std::uniform_int_distribution<int>(1, std::min(meter.first * 8 / meter.second, static_cast<int>(melody.size())))(rng);
            int end_index = std::min(start_index + length, static_cast<int>(melody.size()) - 1);
            std::vector<int> fragment(melody.begin() + start_index, melody.begin() + end_index);
            fragment.erase(std::remove_if(fragment.begin(), fragment.end(), [](int note) { return note == -2 || note == -1; }), fragment.end());
            if (fragment.size() > 1) {
                bool ascending = std::uniform_int_distribution<int>(0, 1)(rng);
                if (ascending) {
                    std::sort(fragment.begin(), fragment.end());
                }
                else {
                    std::sort(fragment.rbegin(), fragment.rend());
                }
                std::copy(fragment.begin(), fragment.end(), melody.begin() + start_index);
            }
        }
    }

   
}

std::vector<std::vector<int>> GeneticMelodyGenerator::generate_population(int note_amount) {
    std::vector<std::vector<int>> population;
    std::uniform_int_distribution<int> note_dist(0, NOTES.size() - 1); // Dystrybucja dla indeksów NOTES
    std::uniform_int_distribution<int> change_dist(-12, 12); // Dystrybucja dla zmiany wysokoœci dŸwiêku

    for (int i = 0; i < populationSize; ++i) {
        std::vector<int> individual;
        individual.push_back(NOTES[note_dist(rng)]); // Losowy pierwszy dŸwiêk

        for (int j = 1; j < note_amount; ++j) {
            int change = change_dist(rng);
            int next_note = individual.back() + change;

            // Upewnij siê, ¿e next_note jest w zakresie NOTES
            next_note = std::max(NOTES.front(), std::min(next_note, NOTES.back()));

            if (next_note < NOTES.front() || next_note > NOTES.back()) {
                next_note = individual.back() - 2 * change;
            }
            individual.push_back(next_note);
        }

        population.push_back(individual);
    }

    return population;
}

std::pair<std::vector<int>, std::vector<int>> GeneticMelodyGenerator::crossover(const std::vector<int>& parent1, const std::vector<int>& parent2) {
    std::uniform_int_distribution<int> dist(1, parent1.size() - 2); // Zapobiegamy tworzeniu skrajnych przypadków
    int index = dist(rng);

    std::vector<int> child1(parent1.begin(), parent1.begin() + index);
    child1.insert(child1.end(), parent2.begin() + index, parent2.end());

    std::vector<int> child2(parent2.begin(), parent2.begin() + index);
    child2.insert(child2.end(), parent1.begin() + index, parent1.end());

    return { child1, child2 };
}

std::vector<int> GeneticMelodyGenerator::tournament_selection(const std::vector<std::vector<int>>& population, int tournament_size) {
    std::uniform_int_distribution<int> dist(0, population.size() - 1);
    float best_fitness = -std::numeric_limits<float>::infinity();
    std::vector<int> best;

    for (int i = 0; i < tournament_size; ++i) {
        const std::vector<int>& candidate = population[dist(rng)];
        float candidate_fitness = fitness(candidate, population);
        if (candidate_fitness > best_fitness) {
            best_fitness = candidate_fitness;
            best = candidate;
        }
    }

    return best;
}

float GeneticMelodyGenerator::fitness_repeated_short_notes(const std::vector<int>& melody) {
    int total_consecutive_short_notes = 0;
    int total_short_notes = 0;
    for (size_t i = 0; i < melody.size(); ++i) {
        if (melody[i] >= 0) {
            total_short_notes++;
            if (i > 0 && melody[i - 1] >= 0 && melody[i] - melody[i - 1] <= 2) {
                total_consecutive_short_notes++;
            }
        }
    }

    if (total_short_notes == 0) return 0.0;

    return static_cast<float>(total_consecutive_short_notes) / total_short_notes;
}

float GeneticMelodyGenerator::fitness_note_diversity(const std::vector<int>& melody) {
    int beat_length = static_cast<int>(meter.first / noteDuration * 4.0 / meter.second);
    int num_beats = melody.size() / beat_length;
    if (num_beats == 0) return 0.0;

    std::vector<float> diversity_scores;

    for (int i = 0; i < num_beats; ++i) {
        std::set<int> unique_notes;
        for (int j = 0; j < beat_length; ++j) {
            int index = i * beat_length + j;
            if (melody[index] != -1 && melody[index] != -2) {
                unique_notes.insert(melody[index]);
            }
        }
        float diversity_score = unique_notes.size() > 1 ? static_cast<float>(unique_notes.size()) / beat_length : 0.0;
        diversity_scores.push_back(diversity_score);
    }

    float average_diversity = std::accumulate(diversity_scores.begin(), diversity_scores.end(), 0.0) / diversity_scores.size();

    return average_diversity;
}

float GeneticMelodyGenerator::fitness_diversity_intervals(const std::vector<int>& melody) {
    int beat_length = static_cast<int>(meter.first / noteDuration * 4.0 / meter.second);
    int num_beats = melody.size() / beat_length;
    if (num_beats == 0) return 0.0;

    std::vector<float> diversity_scores;

    for (int i = 0; i < num_beats; ++i) {
        std::vector<int> intervals;
        std::set<int> unique_intervals;
        for (int j = 1; j < beat_length; ++j) {
            int index = i * beat_length + j;
            if (melody[index] != -1 && melody[index] != -2 && melody[index - 1] != -1 && melody[index - 1] != -2) {
                int interval = melody[index] - melody[index - 1];
                if (std::abs(interval) <= 12) {
                    intervals.push_back(interval);
                }
            }
        }
        for (int interval : intervals) {
            unique_intervals.insert(std::abs(interval));
        }
        float diversity_score = unique_intervals.size() > 1 ? static_cast<float>(unique_intervals.size()) / intervals.size() : 0.0;
        diversity_scores.push_back(diversity_score);
    }

    float average_diversity = std::accumulate(diversity_scores.begin(), diversity_scores.end(), 0.0) / diversity_scores.size();

    return average_diversity;
}

float GeneticMelodyGenerator::fitness_pitch_variation(const std::vector<int>& melody) {
    std::vector<int> valid_notes;
    std::copy_if(melody.begin(), melody.end(), std::back_inserter(valid_notes), [](int note) { return note != -1 && note != -2; });

    if (valid_notes.size() < 2) return 0.0;

    float mean = std::accumulate(valid_notes.begin(), valid_notes.end(), 0.0) / valid_notes.size();
    float sq_sum = std::inner_product(valid_notes.begin(), valid_notes.end(), valid_notes.begin(), 0.0);
    float stdev = std::sqrt(sq_sum / valid_notes.size() - mean * mean);
    float max_possible_std = notesRange / std::sqrt(12.0);
    return stdev / max_possible_std;
}

float GeneticMelodyGenerator::fitness_odd_index_notes(const std::vector<int>& melody) {
    int beat_length = static_cast<int>(meter.first / noteDuration * 4.0 / meter.second);
    int num_beats = melody.size() / beat_length;
    std::vector<float> odd_index_scores;

    for (int i = 0; i < num_beats; ++i) {
        int note_and_extension_count = 0;
        int beat_length_adjusted = 0;
        for (int j = 1; j < beat_length; j += 2) {
            if (melody[i * beat_length + j] > 0) {
                note_and_extension_count++;
                // Check for extensions of this note
                int extension_idx = i * beat_length + j + 1;
                while (extension_idx < (i + 1) * beat_length && melody[extension_idx] == -2) {
                    note_and_extension_count++;
                    extension_idx++;
                }
            }
            beat_length_adjusted = beat_length - 2;
        }

        if (beat_length_adjusted > 0) {
            odd_index_scores.push_back(static_cast<float>(note_and_extension_count) / beat_length_adjusted);
        }
        else {
            odd_index_scores.push_back(0);
        }
    }

    float average_odd_index_ratio = odd_index_scores.empty() ? 0.0 :
        std::accumulate(odd_index_scores.begin(), odd_index_scores.end(), 0.0) / odd_index_scores.size();

    return average_odd_index_ratio;
}

float GeneticMelodyGenerator::fitness_rhythm(const std::vector<int>& melody) {
    int beat_length = static_cast<int>(meter.first / noteDuration * 4.0 / meter.second);
    int num_beats = melody.size() / beat_length;
    std::vector<float> rhythmic_diversity_scores;

    for (int i = 0; i < num_beats; ++i) {
        std::set<int> unique_lengths;
        int current_length = 0;
        for (int j = 0; j < beat_length; ++j) {
            if (melody[i * beat_length + j] == -2) {
                current_length += 1;
            }
            else if (current_length > 0) {
                unique_lengths.insert(current_length);
                current_length = 0;
            }
        }
        if (current_length > 0) unique_lengths.insert(current_length);

        float rhythmic_diversity_score = unique_lengths.size() > 1
            ? static_cast<float>(unique_lengths.size()) / unique_lengths.size()
            : 0.0;
        rhythmic_diversity_scores.push_back(rhythmic_diversity_score);
    }

    float average_rhythmic_diversity = std::accumulate(rhythmic_diversity_scores.begin(), rhythmic_diversity_scores.end(), 0.0) / rhythmic_diversity_scores.size();

    return average_rhythmic_diversity;
}

std::pair<float, float> GeneticMelodyGenerator::fitness_log_rhythmic_value(const std::vector<int>& melody) {
    std::vector<int> rhythmic_values;
    int start_index = -1;
    for (int i = 0; i < melody.size(); ++i) {
        if (melody[i] == -2) {
            if (start_index == -1) start_index = i; // Start of a new extension
        }
        else {
            if (start_index != -1) {
                rhythmic_values.push_back(i - start_index + 1);
                start_index = -1; // End of the extension
            }
        }
    }
    // Add the last extension if the melody ends with it
    if (start_index != -1) {
        rhythmic_values.push_back(static_cast<int>(melody.size()) - start_index + 1);
    }

    int single_notes = static_cast<int>(melody.size()) - std::count(melody.begin(), melody.end(), -2);
    std::vector<int> single_note_durations(single_notes, 1);
    rhythmic_values.insert(rhythmic_values.end(), single_note_durations.begin(), single_note_durations.end());

    std::vector<float> log_rhythmic_values;
    for (int value : rhythmic_values) {
        log_rhythmic_values.push_back(std::log2(value));
    }

    float average_rhythmic_value = std::accumulate(rhythmic_values.begin(), rhythmic_values.end(), 0.0) / rhythmic_values.size();
    float log_average_rhythmic_value = std::log2(average_rhythmic_value);

    float normalized_log_rhythmic_value = (log_average_rhythmic_value - std::log2(1)) / (std::log2(4 * expectedLength) - std::log2(1));

    float sq_sum_log = std::inner_product(log_rhythmic_values.begin(), log_rhythmic_values.end(), log_rhythmic_values.begin(), 0.0);
    float stdev_log = std::sqrt(sq_sum_log / log_rhythmic_values.size() - log_average_rhythmic_value * log_average_rhythmic_value);

    float normalized_std_log_rhythmic_value = stdev_log / std::log2(4 * expectedLength);

    return { normalized_log_rhythmic_value, normalized_std_log_rhythmic_value };
}

float GeneticMelodyGenerator::proportion_of_long_notes(const std::vector<int>& melody) {
    std::vector<int> rhythmic_values;
    int start_index = -1;
    for (int i = 0; i < melody.size(); ++i) {
        if (melody[i] == -2) {
            if (start_index == -1) start_index = i;
        }
        else {
            if (start_index != -1) {
                rhythmic_values.push_back(i - start_index + 1);
                start_index = -1;
            }
        }
    }
    if (start_index != -1) rhythmic_values.push_back(static_cast<int>(melody.size()) - start_index + 1);
    int single_notes = static_cast<int>(melody.size()) - std::count(melody.begin(), melody.end(), -2);
    rhythmic_values.insert(rhythmic_values.end(), single_notes, 1);
    int long_notes_count = std::count_if(rhythmic_values.begin(), rhythmic_values.end(), [](int value) { return value > 4; });
    float proportion_long_notes = long_notes_count / static_cast<float>(rhythmic_values.size());
    return proportion_long_notes;
}

float GeneticMelodyGenerator::fitness_average_intervals(const std::vector<int>& melody) {
    std::vector<int> valid_notes;
    std::copy_if(melody.begin(), melody.end(), std::back_inserter(valid_notes), [](int note) { return note != -1 && note != -2; });
    if (valid_notes.size() < 2) return -1.0;
    std::vector<int> intervals;
    for (size_t i = 1; i < valid_notes.size(); ++i) {
        int interval = valid_notes[i] - valid_notes[i - 1];
        if (std::abs(interval) <= 12) intervals.push_back(interval);
    }
    if (intervals.empty()) return -1.0;
    float sum = std::accumulate(intervals.begin(), intervals.end(), 0.0, [](float acc, int interval) { return acc + std::abs(interval); });
    float average_interval = sum / intervals.size();
    return average_interval / 12.0;
}

float GeneticMelodyGenerator::fitness_small_intervals(const std::vector<int>& melody) {
    std::vector<int> valid_notes;
    std::copy_if(melody.begin(), melody.end(), std::back_inserter(valid_notes), [](int note) { return note != -2; });
    if (valid_notes.size() < 2) return 0.0;
    std::vector<int> intervals;
    for (size_t i = 1; i < valid_notes.size(); ++i) {
        intervals.push_back(valid_notes[i] - valid_notes[i - 1]);
    }
    int small_consecutive_intervals = 0;
    for (size_t i = 1; i < intervals.size(); ++i) {
        if ((std::abs(intervals[i - 1]) <= 3) && (std::abs(intervals[i]) <= 3) && intervals[i - 1] * intervals[i] != 0) {
            small_consecutive_intervals++;
        }
    }
    float small_interval_ratio = small_consecutive_intervals / static_cast<float>(intervals.size());
    return small_interval_ratio;
}

std::pair<float, float> GeneticMelodyGenerator::fitness_scale_and_chord(const std::vector<int>& melody) {
    int scale_length_counter = 0;
    int root_length_counter = 0; // We'll set this aside for now since it's not implemented
    int total_length_counter = 0;

    for (int note : melody) {
        if (note != -2) {
            // Check if the note is in the scale notes
            if (std::find(scale_notes.begin(), scale_notes.end(), note % 12) != scale_notes.end()) {
                scale_length_counter++;
            }
            if (note % 12 == scale_notes[0]) {
                root_length_counter++;
            }
            // For chords, you will need to implement a way to check if a note is in the current chord
            // ...

            total_length_counter++;
        }
    }

    float scale_conformance_score = 0.0;
    float root_conformance_score = 0.0; // We'll leave this as 0 until chord checking is implemented

    if (total_length_counter != 0) {
        scale_conformance_score = static_cast<float>(scale_length_counter) / total_length_counter;
        root_conformance_score = static_cast<float>(root_length_counter) / total_length_counter;
    }
    else {
        return { 0.0, 0.0 };
    }

    return { scale_conformance_score, root_conformance_score };
}

float GeneticMelodyGenerator::fitness_pause_proportion(const std::vector<int>& melody) {
    int total_length = static_cast<int>(melody.size());

    if (total_length == 0) {
        return 0.0;
    }

    int pause_length_counter = 0;
    bool in_pause = false;

    for (int note : melody) {
        if (note == -1 || (in_pause && note == -2)) {
            pause_length_counter += 1;
            in_pause = true;
        }
        else {
            in_pause = false;
        }
    }

    float pause_proportion_score = static_cast<float>(pause_length_counter) / total_length;

    return pause_proportion_score;
}

float GeneticMelodyGenerator::fitness_directional_changes(const std::vector<int>& melody) {
    std::vector<int> notes;
    for (int note : melody) {
        if (note != -1 && note != -2) {
            notes.push_back(note);
        }
    }

    if (notes.size() < 2) {
        return 0.0;
    }

    std::vector<int> intervals;
    for (size_t i = 1; i < notes.size(); ++i) {
        int interval = notes[i] - notes[i - 1];
        if (interval != 0) {
            intervals.push_back(interval);
        }
    }

    if (intervals.size() < 2) {
        return 0.0;
    }

    int direction_changes = 0;
    int current_sign = (intervals[0] > 0) ? 1 : -1;

    for (size_t i = 1; i < intervals.size(); ++i) {
        int next_sign = (intervals[i] > 0) ? 1 : -1;
        if (next_sign != current_sign) {
            direction_changes++;
            current_sign = next_sign;
        }
    }

    int total_intervals_count = static_cast<int>(intervals.size()) - 1;
    if (total_intervals_count == 0) {
        return 0.0;
    }

    float directional_change_score = static_cast<float>(direction_changes) / total_intervals_count;

    return directional_change_score;
}

float GeneticMelodyGenerator::fitness_melodic_contour(const std::vector<int>& melody) {
    std::vector<int> notes;
    std::copy_if(melody.begin(), melody.end(), std::back_inserter(notes), [](int note) { return note != -1 && note != -2; });

    if (notes.size() < 2) return 0.0;

    int positive_intervals_count = 0;
    int total_intervals_count = 0;

    for (size_t i = 1; i < notes.size(); ++i) {
        int interval = notes[i] - notes[i - 1];
        if (interval > 0) positive_intervals_count++;
        if (interval != 0) total_intervals_count++;
    }

    return total_intervals_count == 0 ? 0.5 : static_cast<float>(positive_intervals_count) / total_intervals_count;
}

float GeneticMelodyGenerator::fitness_note_range(const std::vector<int>& melody) {
    std::vector<int> notes;
    std::copy_if(melody.begin(), melody.end(), std::back_inserter(notes), [](int note) { return note != -1 && note != -2; });

    if (notes.empty()) return 0.0;

    int pitch_range = *std::max_element(notes.begin(), notes.end()) - *std::min_element(notes.begin(), notes.end());
    return static_cast<float>(pitch_range) / notesRange;
}

float GeneticMelodyGenerator::fitness_average_pitch(const std::vector<int>& melody) {
    std::vector<int> valid_notes;
    std::copy_if(melody.begin(), melody.end(), std::back_inserter(valid_notes), [](int note) { return note != -1 && note != -2; });

    if (valid_notes.empty()) return 0.0;

    float sum = std::accumulate(valid_notes.begin(), valid_notes.end(), 0.0);
    float average_pitch = sum / valid_notes.size();
    return average_pitch / notesRange;
}

std::pair<float, float> GeneticMelodyGenerator::fitness_intervals(const std::vector<int>& melody) {
    float dissonance_score = 0.0;
    float large_intervals_score = 0.0;
    std::vector<int> intervals;
    int dissonant_intervals = 0;
    int large_intervals = 0;
    int previous_note = -1;

    // Calculate intervals
    for (int note : melody) {
        if (note != -1 && note != -2 && previous_note != -1) { // Exclude pauses and extensions
            int interval = std::abs(note - previous_note);
            // Large intervals
            if (interval > 12) {
                large_intervals += 1;
            }

            interval = interval % 12; // Wrap around octave

            // Dissonance values
            if (interval == 10) {
                dissonant_intervals += 1;
            }
            else if (interval == 0 || interval == 1 || interval == 2 || interval == 3 ||
                interval == 4 || interval == 5 || interval == 7 || interval == 8 ||
                interval == 9 || interval == 12) {
                // not dissonant, do nothing
            }
            else {
                dissonant_intervals += 2;
            }

            

            intervals.push_back(interval);
            previous_note = note;
        }
        else if (note != -1 && note != -2) {
            previous_note = note; // Set the first note for the next interval calculation
        }
    }

    // Calculate scores
    if (!intervals.empty()) {
        dissonance_score = static_cast<float>(dissonant_intervals) / intervals.size();
        large_intervals_score = static_cast<float>(large_intervals) / intervals.size();
    }

    return { dissonance_score, large_intervals_score };
}

float GeneticMelodyGenerator::calculate_similarity_penalty(const std::vector<int>& melody, const std::vector<std::vector<int>>& population) {
    int total_similarity = 0;
    int total_notes = (melody.size() - 1) * (population.size() - 1);

    for (const auto& other_melody : population) {
        if (&melody != &other_melody) {  // Unikamy porównania z samą sobą
            for (size_t i = 1; i < melody.size() && i < other_melody.size(); ++i) {
                if (melody[i] == other_melody[i]) {
                    total_similarity++;
                }
            }
        }
    }
    if (total_notes > 0) {
        return static_cast<float>(total_similarity) / total_notes;
    }
    return 0.0f;
}

float GeneticMelodyGenerator::fitness(const std::vector<int>& melody, const std::vector<std::vector<int>>& population) {
    // Scores from individual fitness functions
    std::pair<float, float> intervals_score = fitness_intervals(melody);
    std::pair<float, float> scale_chord_score = fitness_scale_and_chord(melody);
    float pause_proportion_score = fitness_pause_proportion(melody);
    float diversity_score = fitness_note_diversity(melody);
    float diversity_interval_score = fitness_diversity_intervals(melody);
    float rhythmic_diversity_score = fitness_rhythm(melody);
    float melodic_contour_score = fitness_melodic_contour(melody);
    float pitch_range_score = fitness_note_range(melody);
    float average_pitch_score = fitness_average_pitch(melody);
    float pitch_variation_score = fitness_pitch_variation(melody);
    float odd_index_notes_score = fitness_odd_index_notes(melody);
    float average_interval_score = fitness_average_intervals(melody);
    float scale_playing_score = fitness_small_intervals(melody);
    float short_consecutive_score = fitness_repeated_short_notes(melody);
    std::pair<float, float> log_rhythmic_values = fitness_log_rhythmic_value(melody);

    // Calculate overall fitness
    float fitness_value = 0.0;
    std::map<std::string, float> scores = {
        {"diversity", diversity_score},
        {"diversity_interval", diversity_interval_score},
        {"dissonance", intervals_score.first},
        {"rhythmic_diversity", rhythmic_diversity_score},
        {"rhythmic_average_value", log_rhythmic_values.first},
        {"deviation_rhythmic_value", log_rhythmic_values.second},
        {"scale_conformance", scale_chord_score.first},
        {"root_conformance", scale_chord_score.second},
        {"melodic_contour", melodic_contour_score},
        {"pitch_range", pitch_range_score},
        {"pause_proportion", pause_proportion_score},
        {"large_intervals", intervals_score.second},
        {"average_pitch", average_pitch_score},
        {"pitch_variation", pitch_variation_score},
        {"odd_index_notes", odd_index_notes_score},
        {"average_interval", average_interval_score},
        {"scale_playing", scale_playing_score},
        {"short_consecutive_notes", short_consecutive_score},
    };

    for (const std::pair<const std::string, float>& score_pair : scores) {
        const std::string& feature = score_pair.first;
        float score = score_pair.second;
        if (muValues.find(feature) != muValues.end() && sigmaValues.find(feature) != sigmaValues.end() && weights.find(feature) != weights.end()) {
            fitness_value += weights[feature] * std::exp(-0.5 * std::pow((score - muValues[feature]) / sigmaValues[feature], 2));
        }
    }
    int similarity_weight = 10;
    float similarity_penalty = calculate_similarity_penalty(melody, population);
    fitness_value -= similarity_penalty * similarity_weight;
    return fitness_value;
}

// Metoda run
std::vector<std::vector<int>> GeneticMelodyGenerator::run(int measures) {
    int note_amount = static_cast<int>(meter.first / noteDuration * 4.0 / meter.second * measures);
    std::vector<std::vector<int>> population = generate_population(note_amount);
    std::vector<std::vector<int>> new_population;
    int POPULATION_SIZE = 128;
    int NUM_GENERATIONS = 100;
    float CROSSOVER_RATE = 0.9;
    new_population.reserve(POPULATION_SIZE);

    for (int generation = 0; generation < NUM_GENERATIONS; ++generation) {
        std::cout << "Generation " << generation + 1 << "/" << NUM_GENERATIONS << '\n';
        new_population.clear();

        while (new_population.size() < POPULATION_SIZE) {
            std::vector<int> parent1 = tournament_selection(population);
            std::vector<int> parent2 = tournament_selection(population);
            std::vector<int> child1, child2;

            if (prob_dist(rng) < CROSSOVER_RATE && !parent1.empty() && !parent2.empty()) {
                std::tie(child1, child2) = crossover(parent1, parent2);
            }
            else {
                child1 = parent1;
                child2 = parent2;
            }

            mutate(child1);
            mutate(child2);
            new_population.push_back(std::move(child1));
            new_population.push_back(std::move(child2));
        }

        population = std::move(new_population);
    }

    // Sort the population by fitness in descending order
    std::sort(population.begin(), population.end(), [this, &population](const std::vector<int>& a, const std::vector<int>& b) {
        return fitness(a, population) > fitness(b, population);
    });

    // Collect the top 12 best melodies
    std::vector<std::vector<int>> best_melodies;
    int count = std::min(12, static_cast<int>(population.size()));  // Ensure there are enough melodies
    for (int i = 0; i < count; ++i) {
        best_melodies.push_back(population[i]);
    }

    return best_melodies;
}