#include <iostream>
#include "genetic.hpp"

int main() {
    // Przykładowe wartości domyślne dla konstruktora
    std::string scale = "A Minor";
    std::pair<int, int> noteRange = { 57, 81 }; // Zakładam przykładowy zakres MIDI dla "A-3" (57) do "A-7" (81)
    std::pair<int, int> meter = { 4, 4 };      // Metrum 4/4
    double noteDuration = 0.25;              // Czas trwania nuty (przykładowo ćwierćnuta)
    int populationSize = 100;                // Rozmiar populacji
    int numGenerations = 200;                // Liczba pokoleń
    double mutationRate = 0.05;              // Współczynnik mutacji
    double crossoverRate = 0.7;              // Współczynnik krzyżowania

    // Tworzenie obiektu generatora z przykładowymi parametrami
    GeneticMelodyGenerator generator(scale, noteRange, meter, noteDuration,
        populationSize, numGenerations, mutationRate, crossoverRate);

    // Wywołanie metody run bez argumentów (zakładając, że run nie przyjmuje parametrów)
    std::vector<int> melody = generator.run(1);

    // Wypisanie wygenerowanej melodii
    std::cout << "Generated Melody: ";
    for (int note : melody) {
        std::cout << note << " ";
    }
    std::cout << std::endl;

    return 0;
}