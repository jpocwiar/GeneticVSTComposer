# GeneticVSTComposer

Interactive VST Plugin composing tool for generating short melodic lines using genetic algorithms.

## Concept
1. The user defines basic parameters, such as scale, amount of dissonant intervals, dynamics, tonal range, which serve as evaluation metrics for algorithm's fitness function. Search for innovative metrics can be considered.
2. For given number of generations, the best solutions are created.
3. Best solution melodies can be trigerred by specific MIDI keys, similarly to NI Kontakt libraries. They can as well be further crossovered and mutated according to user's preferences. MIDI can as well be dragged into a track.
4. For specific range of MIDI notes, created riff / melody can be played with different pitches, which should be able to fit to scale if desired.
5. (to be considered) apart from MIDI functionality, creation of simple synthesizer can be considered. Parameters of filters, LFO, effects etc. can be optimized by genetic algorithm as well.

## Previous works???
- magenta
- z inżynierki różne

## Schedule (tasks to be finished by the end of given week)
- 4-10.03 - Concept proposal, literature analysis, planning
- 11-17.03 - experiementing and choosing framework for VST creation, framework functionality analysis
- 18-24.03 - Implementation of basic genetic algorithm
- 25-31.03 - Creation of basic VST prototype with basic functionality
- 1.04 - 7.04 - integration of genetic algorithm and VST
- 8.04 - 14.04, 15.04 - 21.04 - Development of UI, improving functionality of genetic algorithm, optimization
- 22.04 - 28.04 - Research and initial implementation of a synthesizer and prospects of controlling it with AI methods (i.e. Genetic Algorithm)
- 29.04 - 5.05 - fajrancik
- 6.05 - 12.05 - synth building?
- 13.05 - 19.05 - further synth building?
- 20.05 - 26.05 - testing
- 27.05 - 2.06 - further testing
- 3.06 - 9.06 - Documentation, user's instruction
- 10.06 - 16.06 - Final documentation and video

## Planned experiements
- analysis of algorithm's ability to converge to the best solutions
- testing VST in DAW
- Composing a song using the VST

## Technological stack
- JUCE / Neutone
- C++ / Python

## Bibliography

