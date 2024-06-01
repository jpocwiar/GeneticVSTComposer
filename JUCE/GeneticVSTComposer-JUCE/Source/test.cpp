// clang++ test.cpp genetic.cpp mingus.cpp notes_generator.cpp  -std=c++11 && ./a.out

#include <iostream>
#include "genetic.hpp"
#include "mingus.hpp"
#include "notes_generator.hpp"

int main() {

    // Przykładowe wartości domyślne dla konstruktora
    std::string scale = "C Major";
    std::pair<int, int> noteRange = { 0, 127 }; // Zakładam przykładowy zakres MIDI dla "A-3" (57) do "A-7" (81)
    std::pair<int, int> meter = { 4, 4 };       // Metrum 4/4
    double noteDuration = 0.25;                 // Czas trwania nuty podstawowej (najkrótszej możliwej). Tu: 0.25 ćwierćnuty, czyli szesnastka
    int populationSize = 512;                   // Rozmiar populacji
    int numGenerations = 200;                   // Liczba pokoleń

    // Tworzenie obiektu generatora z przykładowymi parametrami
    int composeMode = 0;
    float diversity = 0.8;
    float dynamics = 0.8;
    float arousal = 0.8;
    float valence = 0.8;
    float jazziness = 0.5;
    float weirdness = 0.5;
    GeneticMelodyGenerator generator(composeMode, scale, noteRange, diversity, dynamics, arousal, valence,
        jazziness, weirdness, meter, noteDuration, populationSize, numGenerations);

    generator.test(1);

    return 0;
}
