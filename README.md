# GeneticVSTComposer

Interactive VST Plugin composing tool for generating short melodic lines using genetic algorithms.

## Concept
1. The user defines basic parameters, such as scale, time signature, length of a pattern, amount of dissonant intervals, dynamics, tonal range, which serve as evaluation metrics for algorithm's fitness function. Search for innovative metrics can be considered.
2. For given number of generations, the best solutions are created.
3. Best solution melodies can be trigerred by specific MIDI keys, similarly to NI Kontakt libraries. They can as well be further crossovered and mutated according to user's preferences. MIDI can as well be dragged into a track.
4. For specific range of MIDI notes, created riff / melody can be played with different pitches, which should be able to fit to scale if desired.
5. (to be considered) apart from MIDI functionality, creation of simple synthesizer can be considered. Parameters of filters, LFO, effects etc. can be optimized by genetic algorithm as well.

## Previous works
VST (Virtual Studio Technology) plugins are software components that can be integrated into digital audio workstations (DAWs) and other compatible software for music production. These plugins extend the functionality of the host software by providing additional effects, instruments, and processing capabilities.

There exist a fair amount of VST plugins designed to help musicians in their creative process. Some of them involve simple music theory tools [1], offering a wide array of audio effects, virtual instruments, and utility tools for music production, including EQ, reverb, synthesizers, and MIDI effects.
Modern plugins use advanced AI models for generation, allowing them to introduce innovative functionalities. There are a few positions on the market that are worth mentioning. There is a set of 5 AI tools from Google called Magenta Studios, which allow to completely transform existing melodies and drum patterns [2]. A plugin called Synplant allows to create synth patches from audio recordings provided by the user [3]. Another tool called Orb Producer Suite allows for quick randomization of patterns and some very advanced customization [4].

Music generation models based on neural networks are fairly effective, but their limitations are lack of controllability and restriction to dominant musical structures, like 4/4 common time signature [5]. Alternative approach might be using genetic algorithms, which are based simply on optimization of the melody, rather than predicting consecutive notes based on learned data. The biggest challenge for building a genetic algorithm is creating a fitness function to evaluate quality of the created melodies. One approach is user evaluating melodies based on their preference, as presented in widely cited GenJam [6] work. However, using that approach is very time consuming, as it requires user evaluating all members of population for every generation. For many years research has been conducted concerning finding appropriate fitness function. Many of works use music theory rules as a source of evaluation. [5,7] measure qualities such as amount of dissonances or scale conformance in the melody, when [5,6] use statistical criteria to find the distance between qualities measured in generated melody and the same qualities in real pieces. [5] used that approach to propose a way to evaluate melodies according to desired mood measured by valence-arousal plane. There are also another approaches worth analysing, such as using sequence models like LSTM as a fitness function [10].

To our best of knowledge, there is not yet a VST plugin that uses genetic algorithms for the task of MIDI generation in a way we proposed. We also did not find an approach that concerns generating rhythm and melody seperately, which we find to have a great potential in creative process.

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
[1] Scaler 2. https://www.scalerplugin.com/. Access: 10.03.2024\
[2] Magenta Studio. https://magenta.tensorflow.org/studio/. Access: 10.03.2024\
[3] Synplant. https://soniccharge.com/synplant/. Access: 11.03.2024\
[4] Orb Producer Studio 3. https://www.orbplugins.com/orb-producer-suite/. Access: 11.03.2024\
[5] Poćwiardowski J., (2023). System for synthetizing music from users description [Unpublished bachelor’s thesis]. Gdańsk University of Technology.\
[6] Biles J. “GenJam: A genetic algorithm for generating jazz solos”. in: ICMC. T. 94. Ann Arbor, MI. 1994, p. 131–137.\
[7] Zdzisław Kowalczuk, Marek Tatara & Adam Bąk. “Evolutionary music composition system with statistically modeled criteria”. in: Trends in Advanced Intelligent Control, Optimization and Automation: Proceedings of KKA 2017—The 19th Polish Control Conference, Kraków, Poland, June 18–21, 2017. Springer. 2017, p. 722–733.\
[8] L Rocha de Azevedo Santos, Carlos Nascimento Silla Jr & MD Costa-Abreu. “A methodology for procedural piano music composition with mood templates using genetic algorithms”. in: (2021).\
[9] Michael Towsey “Towards melodic extension using genetic algorithms”. in: Educational Technology and Society (2001), p. 54–65.\
[10] Farzaneh, Majid, and Rahil Mahdian Toroghi. "GGA-MG: Generative genetic algorithm for music generation." arXiv preprint arXiv:2004.04687 (2020).
