#include "AllocationGuard.h"
#include "PluginProcessor.h"
#include <iostream>
#include <memory>
#include <stdexcept>

namespace
{
using Processor = GeneticVSTComposerJUCEAudioProcessor;

void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

void parameter(Processor& processor, const char* id, float value)
{
    auto* param = processor.parameters.getParameter(id);
    require(param != nullptr, "realtime parameter missing");
    param->setValueNotifyingHost(param->convertTo0to1(value));
}
} // namespace

void runRealtimeTests()
{
    Processor processor;
    processor.prepareToPlay(48000, 64);
    parameter(processor, "monitor", 1);
    parameter(processor, "auditionChords", 1);
    parameter(processor, "chordMidiOut", 1);
    parameter(processor, "microEnabled", 1);
    parameter(processor, "tuning", 1);
    parameter(processor, "microOutput", 1); // MPE member channels
    require(processor.setCustomHarmony("Cm:4 Fm:2 G7:2").wasOk(), "realtime harmony");
    require(processor.generateChords().wasOk(), "realtime chord voices");
    processor.setPreview(true);

    juce::AudioBuffer<float> audio(2, 64);
    juce::MidiBuffer midi;
    midi.ensureSize(65536);
    long allocations = 0;

    for (int block = 0; block < 800; ++block) {
        midi.clear();
        // Notes 48-59 hold-trigger variations; 60-127 transpose. Channel 1 is the
        // melody. A second note on a member channel exercises the MPE allocator.
        if (block == 8)
            midi.addEvent(juce::MidiMessage::noteOn(1, 48, juce::uint8(100)), 0);
        if (block == 40)
            midi.addEvent(juce::MidiMessage::noteOff(1, 48), 0);
        if (block == 48)
            midi.addEvent(juce::MidiMessage::noteOn(1, 72, juce::uint8(90)), 4);
        if (block == 80)
            midi.addEvent(juce::MidiMessage::noteOff(1, 72), 4);
        if (block == 90)
            midi.addEvent(juce::MidiMessage::noteOn(1, 52, juce::uint8(80)), 8);
        if (block == 200)
            midi.addEvent(juce::MidiMessage::noteOff(1, 52), 8);
        if (block % 120 == 100)
            midi.addEvent(juce::MidiMessage::pitchWheel(1, 8192 + (block % 7) * 200), 2);

        audio.clear();
        composertest::AllocationGuard guard;
        processor.processBlock(audio, midi);
        allocations += guard.count();
    }

    processor.setPreview(false);
    require(allocations == 0, "processBlock allocated on the audio thread");
    std::cout << "Realtime allocation check passed.\n";
}
