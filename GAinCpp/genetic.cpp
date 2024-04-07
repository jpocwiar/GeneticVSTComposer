#include "genetic.hpp"
#include <algorithm>
#include <iostream>

// Konstruktor
GeneticMelodyGenerator::GeneticMelodyGenerator(const std::string& scale, const std::pair<int, int>& noteRange,
    const std::pair<int, int>& meter, double noteDuration,
    int populationSize, int numGenerations,
    double mutationRate, double crossoverRate)
    : meter(meter), noteDuration(noteDuration), populationSize(populationSize),
    numGenerations(numGenerations), mutationRate(mutationRate), crossoverRate(crossoverRate),
    expectedLength(static_cast<int>(1 / noteDuration)), notesRange(noteRange.second - noteRange.first) {

    std::random_device rd;  // Uzyskaj ziarno dla generatora liczb losowych
    rng = std::mt19937(rd()); // U¿yj ziarna do inicjalizacji generatora
    std::uniform_real_distribution<double> prob_dist{ 0.0, 1.0 };
    // Przyk³adowe wype³nienie NOTES zakresem od 0 do 127
    NOTES.resize(128);
    for (int i = 0; i < 128; ++i) {
        NOTES[i] = i;
    }

    // Przyk³adowe wype³nienie scale_notes - to bêdzie wymaga³o prawdziwej implementacji
    scale_notes = { 0, 2, 4, 5, 7, 9, 11, 12 }; // C-dur dla przyk³adu

    set_coefficients();
}

// set_coefficients - implementacja jak powy¿ej...
void GeneticMelodyGenerator::set_coefficients(const std::map<std::string, double>& mu_values,
    const std::map<std::string, double>& sigma_values,
    const std::map<std::string, int>& weights) {
    // Ustaw wartoœci domyœlne, jeœli nie przekazano ¿adnych map
    this->muValues = mu_values.empty() ? std::map<std::string, double>{
        {"diversity", 0.8},
        { "diversity_interval", 0.8 },
        { "dissonance", 0.25 },
        { "rhythmic_diversity", 0.7 },
        { "rhythmic_average_value", 0.5 },
        { "very_long_notes_score", 0 },
        { "deviation_rhythmic_value", 0.5 },
        { "scale_conformance", 0.9 },
        { "chord_conformance", 0.5 },
        { "melodic_contour", 0.1 },
        { "pitch_range", 0.3 },
        { "pause_proportion", 0.3 },
        { "large_intervals", 0.0 },
        { "average_pitch", 0.6 },
        { "pitch_variation", 0.4 },
        { "odd_index_notes", 0.1 },
        { "average_interval", 0.3 },
        { "scale_playing", 0.8 },
        { "short_consecutive_notes", 0.75 }
    } : mu_values;

    this->sigmaValues = sigma_values.empty() ? std::map<std::string, double>{
        {"diversity", 0.1},
        { "diversity_interval", 0.1 },
        { "dissonance", 0.1 },
        { "rhythmic_diversity", 0.1 },
        { "rhythmic_average_value", 0.1 },
        { "deviation_rhythmic_value", 0.1 },
        { "very_long_notes_score", 0.1 },
        { "scale_conformance", 0.1 },
        { "chord_conformance", 0.1 },
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
        { "chord_conformance", 3 },
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
    std::uniform_real_distribution<double> prob_dist(0.0, 1.0);
    std::uniform_int_distribution<int> index_dist(0, melody.size() - 1);
    std::uniform_int_distribution<int> interval_dist(-12, 12);
    float MUTATION_RATE = 0.3;

    // Poni¿ej pokazujê jedn¹ z mutacji jako przyk³ad. Pozosta³e mutacje powinieneœ zaimplementowaæ analogicznie.

    // Pierwsza mutacja: zamiana dwóch nut na interwa³
    if (prob_dist(rng) < MUTATION_RATE) {
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

    // ... (reszta mutacji)

    // Przyk³ad kolejnej mutacji: transpozycja fragmentu melodii
    if (prob_dist(rng) < MUTATION_RATE) {
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

    // ... (reszta kodu powinna obs³ugiwaæ inne przypadki mutacji)
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
    double best_fitness = -std::numeric_limits<double>::infinity();
    std::vector<int> best;

    for (int i = 0; i < tournament_size; ++i) {
        const std::vector<int>& candidate = population[dist(rng)];
        double candidate_fitness = fitness(candidate);
        if (candidate_fitness > best_fitness) {
            best_fitness = candidate_fitness;
            best = candidate;
        }
    }

    return best;
}

std::pair<double, double> GeneticMelodyGenerator::fitness_intervals(const std::vector<int>& melody) {
    double dissonance_score = 0.0;
    double large_intervals_score = 0.0;
    std::vector<int> intervals;
    int dissonant_intervals = 0;
    int large_intervals = 0;
    int previous_note = -1;

    // Calculate intervals
    for (int note : melody) {
        if (note != -1 && note != -2 && previous_note != -1) { // Exclude pauses and extensions
            int interval = std::abs(note - previous_note);
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

            // Large intervals
            if (interval > 12) {
                large_intervals += 1;
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
        dissonance_score = static_cast<double>(dissonant_intervals) / intervals.size();
        large_intervals_score = static_cast<double>(large_intervals) / intervals.size();
    }

    return { dissonance_score, large_intervals_score };
}

double GeneticMelodyGenerator::fitness(const std::vector<int>& melody) {
    std::pair<double, double> interval_scores = fitness_intervals(melody);
    double dissonance_score = interval_scores.first;
    double large_intervals_score = interval_scores.second;

    // Here, we would calculate other scores...
    // For now, we only consider the interval scores for the fitness value
    double fitness_value = weights["dissonance"] * std::exp(-0.5 * std::pow((dissonance_score - muValues["dissonance"]) / sigmaValues["dissonance"], 2))
        + weights["large_intervals"] * std::exp(-0.5 * std::pow((large_intervals_score - muValues["large_intervals"]) / sigmaValues["large_intervals"], 2));
    // ... (tutaj bêdziemy dodawaæ kolejne sk³adowe wartoœci fitness)

    return fitness_value;
}

// Metoda run
std::vector<int> GeneticMelodyGenerator::run(int measures) {
    int note_amount = static_cast<int>(meter.first / noteDuration * 4.0 / meter.second * measures);
    std::vector<std::vector<int>> population = generate_population(note_amount);
    std::vector<std::vector<int>> new_population;
    int POPULATION_SIZE = 64;
    int NUM_GENERATIONS = 1000;
    int CROSSOVER_RATE = 0.9;
    new_population.reserve(POPULATION_SIZE);

    for (int generation = 0; generation < NUM_GENERATIONS; ++generation) {
        std::cout << "Generation " << generation + 1 << "/" << NUM_GENERATIONS << '\n';
        new_population.clear();

        while (new_population.size() < POPULATION_SIZE) {
            std::vector<int> parent1 = tournament_selection(population);
            std::vector<int> parent2 = tournament_selection(population);
            std::vector<int> child1, child2;

            if (prob_dist(rng) < CROSSOVER_RATE) {
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

    auto best_it = std::max_element(population.begin(), population.end(),
        [this](const std::vector<int>& a, const std::vector<int>& b) {
            return fitness(a) < fitness(b);
        });

    std::vector<int> best_melody = (best_it != population.end()) ? *best_it : std::vector<int>();
    return best_melody;
}
// Inne metody...