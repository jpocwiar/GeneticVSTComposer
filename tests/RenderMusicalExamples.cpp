#include "PluginProcessor.h"
#include <iostream>
#include <stdexcept>

// Optional, reproducible listening material. Invoked only by the snapshot CLI's fourth argument.
void renderMusicalExamples(const juce::File& directory) {
    if (directory.createDirectory().failed())
        throw std::runtime_error("cannot create musical example directory");
    const char* names[]{"riff", "motif", "melody"};
    for (int type = 0; type < 3; ++type) {
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
        settings.microEnabled = true;
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
        parameter("microEnabled", 1);
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
        const auto file = directory.getChildFile(stem + "-micro-with-chords.wav");
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
        if (processor->exportMidi(directory.getChildFile(stem + "-melody-MPE.mid")).failed() ||
            processor->exportChordMidi(directory.getChildFile(stem + "-chords.mid")).failed())
            throw std::runtime_error("cannot export example MIDI");
        const auto diagnostic = composer::analysePhrase(clips.front(), settings);
        std::cout << names[type] << ": " << clips.front().count << " notes, " << diagnostic.microtonalNotes
                  << " microtonal passing notes, repetition " << diagnostic.repetition << ", small steps "
                  << diagnostic.smallStepRatio << '\n';
    }
}
