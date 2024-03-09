# GeneticVSTComposer

Interactive VST Plugin composing tool for generating short melodic lines using genetic algorithms.

## Concept
1. The user defines basic parameters, such as scale, time signature, length of a pattern, amount of dissonant intervals, dynamics, tonal range, which serve as evaluation metrics for algorithm's fitness function. Search for innovative metrics can be considered.
2. For given number of generations, the best solutions are created.
3. Best solution melodies can be trigerred by specific MIDI keys, similarly to NI Kontakt libraries. They can as well be further crossovered and mutated according to user's preferences. MIDI can as well be dragged into a track.
4. For specific range of MIDI notes, created riff / melody can be played with different pitches, which should be able to fit to scale if desired.
5. (to be considered) apart from MIDI functionality, creation of simple synthesizer can be considered. Parameters of filters, LFO, effects etc. can be optimized by genetic algorithm as well.

## Previous works???
- magenta
- z inżynierki różne

## Schedule (tasks to be finished by the end of given week)
- 04.03 - 10.03 - Concept proposal, literature analysis, planning
- 11.03 - 17.03 - creating issues for accepted schedule, experiementing and choosing framework for VST creation, framework functionality analysis
- 18.03 - 24.03 - Basic VST plugin
- 25.03 - 31.03 - Implementation of basic genetic algorithm
- 01.04 - 07.04 - Integration of the basic genetic algorithm and VST -> Basic VST prototype with basic functionality (1st milestone)
- 08.04 - 14.04,\
  15.04 - 21.04 - Development of UI, improving functionality of genetic algorithm, optimization
- 22.04 - 28.04 - Research about potential sonification of MIDI (i.e. synthesizer controlled by GA or ability to control different midi signals on other vst instruments)
- 29.04 - 05.05 - fajrancik (majóweczka, chill)
- 06.05 - 12.05,\
  13.05 - 19.05 - working on sonification (and its control)
- 20.05 - 26.05,\
  27.05 - 02.06 - Testing
- 03.06 - 09.06 - Documentation, user's instruction
- 10.06 - 16.06 - Final documentation and video

## Planned experiements
- analysis of algorithm's ability to converge to the best solutions
- testing VST in DAW
- Composing a song using the VST

## Technological stack
- JUCE / Neutone
- C++ / Python

## Bibliography

