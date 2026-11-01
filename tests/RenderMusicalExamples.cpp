#include "PluginProcessor.h"
#include <iostream>
#include <stdexcept>

// Chord-only listening examples use the same instrument and tempo, fixed seeds,
// and no melody. These are illustrative examples, not a listening-quality test.
void renderHarmonyExamples(const juce::File& directory) {
    if (directory.createDirectory().failed())
        throw std::runtime_error("cannot create harmony example directory");
    const char* names[]{"01-balanced", "02-pop", "03-jazz", "04-modal", "05-cinematic"};
    juce::String notes("Chord generator examples: seed 41, 8 bars, one chord per bar, Originality 65%, 110 BPM.\n"
                       "Balanced/Pop/Jazz: C major. Modal: D Dorian. Cinematic: A minor, open voicings.\n"
                       "Identical internal sound and monitor settings; no melody.\n"
                       "Transition prior: Radicioni & Esposito (2014), Bach Choral Harmony, UCI, CC BY 4.0.\n"
                       "https://doi.org/10.24432/C5B61F | https://creativecommons.org/licenses/by/4.0/\n\n");
    for (int character = 0; character < 5; ++character) {
        composer::ChordGenerationSettings settings;
        settings.character = character;
        settings.scale = character == 3 ? 2 : character == 4 ? 1 : 0;
        settings.root = character == 3 ? 2 : character == 4 ? 9 : 0;
        settings.bars = 8;
        settings.originality = 0.65f;
        settings.seed = 41;
        const auto harmony = composer::generateHarmony(settings);
        if (harmony.count != 8)
            throw std::runtime_error("harmony example did not generate eight positions");
        auto processor = std::make_unique<GeneticVSTComposerJUCEAudioProcessor>();
        auto parameter = [&](const char* id, float value) {
            auto* p = processor->parameters.getParameter(id);
            p->setValueNotifyingHost(p->convertTo0to1(value));
        };
        parameter("chordVoicing", character == 4 ? 2 : 0);
        parameter("tempo", 110);
        parameter("volume", 0.8f);
        parameter("auditionSound", 0);
        parameter("auditionReverb", 0.15f);
        const auto text = juce::String(composer::harmonyText(harmony));
        if (processor->setCustomHarmony(text).failed())
            throw std::runtime_error("cannot activate example harmony");
        processor->prepareToPlay(48000, 512);
        processor->startChordAudition();
        const int samples = int(std::lround(harmony.length * 60.0 / 110.0 * 48000.0));
        juce::AudioBuffer<float> rendered(2, samples + 24000), block(2, 512);
        for (int position = 0; position < rendered.getNumSamples();) {
            const bool tail = position >= samples;
            if (tail)
                processor->setPreview(false);
            const int count = std::min(512, (tail ? rendered.getNumSamples() : samples) - position);
            block.setSize(2, count, false, false, true);
            juce::MidiBuffer midi;
            processor->processBlock(block, midi);
            for (int channel = 0; channel < 2; ++channel)
                rendered.copyFrom(channel, position, block, channel, 0, count);
            position += count;
        }
        const float peak = rendered.getMagnitude(0, rendered.getNumSamples());
        if (peak < 0.01f || peak >= 1.0f)
            throw std::runtime_error("silent or clipped harmony example");
        const auto stem = juce::String(names[character]);
        const auto file = directory.getChildFile(stem + ".wav");
        file.deleteFile();
        std::unique_ptr<juce::OutputStream> output = file.createOutputStream();
        juce::WavAudioFormat format;
        auto writer = format.createWriterFor(output, juce::AudioFormatWriterOptions{}
                                                        .withSampleRate(48000).withNumChannels(2).withBitsPerSample(24));
        if (!writer || !writer->writeFromAudioSampleBuffer(rendered, 0, rendered.getNumSamples()) ||
            processor->exportChordMidi(directory.getChildFile(stem + ".mid")).failed())
            throw std::runtime_error("cannot save harmony listening example");
        notes += stem + ": " + text + "\n";
        std::cout << names[character] << ": " << text << ", peak " << peak << '\n';
    }
    if (!directory.getChildFile("README.txt").replaceWithText(notes))
        throw std::runtime_error("cannot save listening example settings");
}

// Optional, reproducible listening material. Invoked only by the snapshot CLI's fourth argument.
void renderMusicalExamples(const juce::File& directory) {
    if (directory.createDirectory().failed())
        throw std::runtime_error("cannot create musical example directory");
    const char* names[]{"riff", "motif", "melody"};
    for (int example = 0; example < 6; ++example) {
        const int type = example % 3;
        const bool micro = example >= 3;
        auto processor = std::make_unique<GeneticVSTComposerJUCEAudioProcessor>();
        auto parameter = [&](const char* id, float value) {
            auto* p = processor->parameters.getParameter(id);
            p->setValueNotifyingHost(p->convertTo0to1(value));
        };
        processor->setCustomHarmony("Cm:4 Fm:4 Ab:4 G7:4");
        composer::Settings settings;
        settings.harmony = processor->harmonySnapshot();
        settings.phraseType = type;
        settings.scale = 6;
        settings.low = 60;
        settings.high = 81;
        settings.microEnabled = micro;
        settings.tuning = 6;
        settings.diversity = 0.4f;
        settings.arousal = 0.6f;
        settings.seed = 41;
        std::atomic<bool> cancelled{false};
        const auto clips = composer::generate(settings, nullptr, cancelled);
        if (clips.empty())
            throw std::runtime_error("musical example generation failed");
        processor->setClip(0, clips.front());
        processor->selectClip(0);
        parameter("microEnabled", micro ? 1 : 0);
        parameter("tuning", 6);
        parameter("microOutput", 1);
        parameter("snap", 0);
        parameter("tempo", 110);
        parameter("auditionChords", 1);
        parameter("volume", 0.85f);
        parameter("gate", 0.94f);
        processor->prepareToPlay(48000, 512);
        processor->startAudition();
        const int samples = int(std::ceil(clips.front().length * 60.0 / 110.0 * 48000.0));
        juce::AudioBuffer<float> rendered(2, samples + 4800), block(2, 512);
        for (int position = 0; position < rendered.getNumSamples();) {
            const bool tail = position >= samples;
            if (tail)
                processor->setPreview(false);
            const int count = std::min(512, (tail ? rendered.getNumSamples() : samples) - position);
            block.setSize(2, count, false, false, true);
            juce::MidiBuffer midi;
            processor->processBlock(block, midi);
            for (int channel = 0; channel < 2; ++channel)
                rendered.copyFrom(channel, position, block, channel, 0, count);
            position += count;
        }
        const auto stem = juce::String(names[type]);
        const auto file =
            directory.getChildFile(stem + (micro ? "-micro-with-chords.wav" : "-12tet-with-chords.wav"));
        file.deleteFile();
        std::unique_ptr<juce::OutputStream> output = file.createOutputStream();
        juce::WavAudioFormat format;
        auto writer = format.createWriterFor(
            output,
            juce::AudioFormatWriterOptions{}.withSampleRate(48000).withNumChannels(2).withBitsPerSample(24));
        if (!writer)
            throw std::runtime_error("cannot create example WAV writer");
        if (!writer->writeFromAudioSampleBuffer(rendered, 0, rendered.getNumSamples()))
            throw std::runtime_error("cannot write example WAV");
        if (processor->exportMidi(directory.getChildFile(stem + (micro ? "-melody-MPE.mid" : "-melody.mid")))
                .failed() ||
            processor->exportChordMidi(directory.getChildFile(stem + "-chords.mid")).failed())
            throw std::runtime_error("cannot export example MIDI");
        const auto diagnostic = composer::analysePhrase(clips.front(), settings);
        std::cout << names[type] << ": " << clips.front().count << " notes, " << diagnostic.microtonalNotes
                  << " microtonal passing notes, repetition " << diagnostic.repetition << ", small steps "
                  << diagnostic.smallStepRatio << '\n';
    }
}
