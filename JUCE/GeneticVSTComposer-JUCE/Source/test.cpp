// clang++ test.cpp genetic.cpp mingus.cpp notes_generator.cpp  -std=c++11 && ./a.out

#include "genetic.hpp"
#include "mingus.hpp"
#include "notes_generator.hpp"
#include <iostream>

int main() {

  // Przykładowe wartości domyślne dla konstruktora
  std::string scale = "C Major";
  std::pair<int, int> noteRange = {
      0, 127}; // Zakładamy przykładowy zakres MIDI dla "A-3" (57) do "A-7" (81)
  std::pair<int, int> meter = {4, 4}; // Metrum 4/4
  double noteDuration =
      0.25; // Czas trwania nuty podstawowej (najkrótszej możliwej). Tu: 0.25
            // ćwierćnuty, czyli szesnastka
  int populationSize = 128; // Rozmiar populacji
  int numGenerations = 256; // Liczba pokoleń

  // Tworzenie obiektu generatora z przykładowymi parametrami
  int composeMode = 0;
  float pauseAmount = 0;
  float fundNoteDuration = 0.25;
  // float diversity = 0.8;
  // float dynamics = 0.8;
  // float arousal = 0.8;
  // float valence = 0.8;
  // float jazziness = 0.5;
  // float weirdness = 0.5;
  float diversity = 0.1;
  float dynamics = 1;
  float arousal = 1;
  float valence = 1;
  float jazziness = 0;
  float weirdness = 0;
  GeneticMelodyGenerator generator(
      composeMode, scale, noteRange, diversity, dynamics, arousal, pauseAmount,
      valence, jazziness, weirdness, meter, fundNoteDuration, populationSize,
      numGenerations);

  generator.test(1, "fitness_low_diversity.txt");

  return 0;
}
