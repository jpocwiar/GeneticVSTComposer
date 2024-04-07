#ifndef GENETIC_MELODY_GENERATOR_HPP
#define GENETIC_MELODY_GENERATOR_HPP

#include <vector>
#include <map>
#include <string>
#include <random>

class GeneticMelodyGenerator {
public:
    GeneticMelodyGenerator(const std::string& scale, const std::pair<int, int>& noteRange,
        const std::pair<int, int>& meter = { 4, 4 }, double noteDuration = 0.5,
        int populationSize = 64, int numGenerations = 50,
        double mutationRate = 0.3, double crossoverRate = 0.9);

    void set_coefficients(const std::map<std::string, double>& mu_values = {},
        const std::map<std::string, double>& sigma_values = {},
        const std::map<std::string, int>& weights = {});

    // Metoda do krzy¿owania dwóch osobników (rodziców)
    std::pair<std::vector<int>, std::vector<int>> crossover(const std::vector<int>& parent1, const std::vector<int>& parent2);

    // Metoda do selekcji turniejowej
    std::vector<int> tournament_selection(const std::vector<std::vector<int>>& population, int tournament_size = 4);

    // Deklaracja funkcji oceny (fitness), która bêdzie potrzebna do metody tournament_selection
    double fitness(const std::vector<int>& individual);
    void mutate(std::vector<int>& melody);
    std::vector<int> run(int measures = 1);

private:
    std::vector<int> NOTES;
    std::vector<int> scale_notes;
    std::pair<int, int> meter;
    double noteDuration;
    int populationSize;
    int numGenerations;
    double mutationRate;
    double crossoverRate;
    int expectedLength;
    int notesRange;

    std::pair<double, double> fitness_intervals(const std::vector<int>& melody);

    // Coefficients for the genetic algorithm
    std::map<std::string, double> muValues;
    std::map<std::string, double> sigmaValues;
    std::map<std::string, int> weights;

    std::mt19937 rng;
    std::uniform_real_distribution<double> prob_dist;
    std::vector<std::vector<int>> generate_population(int note_amount);
};

#endif // GENETIC_MELODY_GENERATOR_HPP

