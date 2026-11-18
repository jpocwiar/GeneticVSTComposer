#include "AllocationGuard.h"
#include "PluginProcessor.h"
#include <iostream>
#include <array>
#include <cstdlib>
#include <cstring>
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
    // Validate the observer itself: JUCE uses malloc directly for long MIDI.
    // ASan replaces malloc, so its allocations do not reach Darwin's logger.
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define COMPOSER_ASAN 1
#endif
#endif
#if !defined(COMPOSER_ASAN) && !defined(__SANITIZE_ADDRESS__)
    if (composertest::AllocationGuard::observesCAllocations()) {
        void* (*volatile allocate)(std::size_t) = std::malloc;
        void* (*volatile resize)(void*, std::size_t) = std::realloc;
        long direct = 0;
        {
            composertest::AllocationGuard guard;
            void* memory = allocate(64);
            memory = resize(memory, 128);
            std::free(memory);
            direct = guard.cAllocationCount();
        }
        require(direct >= 2, "allocation guard did not observe direct malloc/realloc");
    }
#endif
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
        if (block == 12 || block == 250)
            processor.setPlaybackRegion(1, 1.5, true);
        if (block == 300)
            processor.setPlaybackRegion(1, 1.5, false);
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
    std::array<juce::uint8, 4096> sysex;
    sysex.fill(0x7d);
    sysex.front() = 0xf0;
    sysex.back() = 0xf7;
    midi.clear();
    midi.addEvent(sysex.data(), int(sysex.size()), 17);
    {
        composertest::AllocationGuard guard;
        processor.processBlock(audio, midi);
        allocations += guard.count();
    }
    bool forwarded = false;
    for (const auto event : midi)
        forwarded |= event.samplePosition == 17 && event.numBytes == int(sysex.size()) &&
                     std::memcmp(event.data, sysex.data(), sysex.size()) == 0;
    require(forwarded, "long SysEx was dropped or moved instead of forwarding its raw bytes");
    {
        Processor fresh;
        fresh.prepareToPlay(48000, 64);
        juce::MidiBuffer tiny;
        fresh.processBlock(audio, tiny);
        juce::MidiBuffer incoming;
        incoming.addEvent(sysex.data(), int(sysex.size()), 17);
        long allocated = 0;
        {
            composertest::AllocationGuard guard;
            fresh.processBlock(audio, incoming);
            allocated = guard.count();
        }
        require(allocated == 0, "an empty host block discarded the preallocated MIDI work buffer");
        require(incoming.getNumEvents() == 1 && (*incoming.cbegin()).numBytes == int(sysex.size()),
                "raw forwarding failed after an empty host buffer");
    }
    processor.setMicrotonalMode(true);
    midi.clear();
    midi.addEvent(sysex.data(), int(sysex.size()), 17);
    {
        composertest::AllocationGuard guard;
        processor.processBlock(audio, midi);
        allocations += guard.count();
    }
    require(allocations == 0, "processBlock allocated on the audio thread");
    std::cout << "Realtime allocation check passed.\n";
}
