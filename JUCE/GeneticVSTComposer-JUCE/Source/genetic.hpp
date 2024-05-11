#ifndef GENETIC_MELODY_GENERATOR_HPP
#define GENETIC_MELODY_GENERATOR_HPP

#include <vector>
#include <map>
#include <string>
#include <random>

class GeneticMelodyGenerator {
public:
    GeneticMelodyGenerator(const std::string& scale, const std::pair<int, int>& noteRange,
        const std::pair<int, int>& meter = { 4, 4 }, float noteDuration = 0.5,
        int populationSize = 128, int numGenerations = 100);

    void set_coefficients(const std::map<std::string, float>& mu_values = {},
        const std::map<std::string, float>& sigma_values = {},
        const std::map<std::string, int>& weights = {});

    // Metoda do krzy¿owania dwóch osobników (rodziców)
    std::pair<std::vector<int>, std::vector<int>> crossover(const std::vector<int>& parent1, const std::vector<int>& parent2);

    // Metoda do selekcji turniejowej
    std::vector<int> tournament_selection(const std::vector<std::vector<int>>& population, int tournament_size = 4);

    // Deklaracja funkcji oceny (fitness), która bêdzie potrzebna do metody tournament_selection
    float fitness(const std::vector<int>& individual);
    void mutate(std::vector<int>& melody);
    std::vector<int> run(int measures = 1);

private:
    std::vector<int> NOTES;
    std::vector<int> scale_notes;
    std::pair<int, int> meter;
    float noteDuration;
    int populationSize;
    int numGenerations;
    float mutationRate;
    float crossoverRate;
    int expectedLength;
    int notesRange;

    std::pair<float, float> fitness_intervals(const std::vector<int>& melody);
    float fitness_directional_changes(const std::vector<int>& melody);
    float fitness_melodic_contour(const std::vector<int>& melody);
    float fitness_note_range(const std::vector<int>& melody);
    float fitness_average_pitch(const std::vector<int>& melody);
    float fitness_pause_proportion(const std::vector<int>& melody);
    std::pair<float, float> fitness_scale_and_chord(const std::vector<int>& melody);
    float fitness_pitch_variation(const std::vector<int>& melody);
    float fitness_odd_index_notes(const std::vector<int>& melody);
    float fitness_note_diversity(const std::vector<int>& melody);
    float fitness_diversity_intervals(const std::vector<int>& melody);
    float fitness_rhythm(const std::vector<int>& melody);
    std::pair<float, float> fitness_log_rhythmic_value(const std::vector<int>& melody);
    float proportion_of_long_notes(const std::vector<int>& melody);
    float fitness_average_intervals(const std::vector<int>& melody);
    float fitness_small_intervals(const std::vector<int>& melody);
    float fitness_repeated_short_notes(const std::vector<int>& melody);

    // Coefficients for the genetic algorithm
    std::map<std::string, float> muValues;
    std::map<std::string, float> sigmaValues;
    std::map<std::string, int> weights;

    std::mt19937 rng;
    std::uniform_real_distribution<float> prob_dist;
    std::vector<std::vector<int>> generate_population(int note_amount);
};

#endif // GENETIC_MELODY_GENERATOR_HPP

