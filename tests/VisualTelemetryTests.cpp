#include "PluginProcessor.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>

namespace {
using Processor = GeneticVSTComposerJUCEAudioProcessor;

void check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

void setParameter(Processor& processor, const char* id, float value) {
    auto* parameter = processor.parameters.getParameter(id);
    check(parameter != nullptr, "visual telemetry parameter missing");
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

bool noNotes(const Processor::VisualState& state) {
    return std::all_of(state.notes.begin(), state.notes.end(), [](float value) { return value == 0; }) &&
           std::all_of(state.sourceNotes.begin(), state.sourceNotes.end(),
                       [](int index) { return index == -1; });
}

void checkPeak(const Processor::VisualState& state, const juce::AudioBuffer<float>& audio) {
    check(std::isfinite(state.outputPeak) && state.outputPeak >= 0 && state.outputPeak <= 1,
          "visual output peak is not finite and bounded");
    check(std::abs(state.outputPeak - audio.getMagnitude(0, audio.getNumSamples())) < 0.000001f,
          "visual output peak differs from the rendered audio");
}

int noteOns(const juce::MidiBuffer& midi) {
    int count = 0;
    for (const auto event : midi)
        count += event.getMessage().isNoteOn() ? 1 : 0;
    return count;
}

void testSourceNoteMapping() {
    auto processor = std::make_unique<Processor>();
    processor->prepareToPlay(48000, 128);
    setParameter(*processor, "follow", 0);
    setParameter(*processor, "tempo", 120);
    setParameter(*processor, "root", 0);
    setParameter(*processor, "scale", 0);
    setParameter(*processor, "snap", 1);
    setParameter(*processor, "swing", 0.5f);
    setParameter(*processor, "gate", 0.5f);
    setParameter(*processor, "monitor", 0);
    composer::Clip clip;
    clip.length = 4;
    clip.grid = 0.25;
    clip.count = 2;
    clip.notes[0] = {0, 1, 61, 96, false};
    clip.notes[1] = {0.25, 0.5, 60, 110, false};
    processor->setClip(0, clip);
    processor->selectClip(0);
    processor->setPreview(true);

    juce::AudioBuffer<float> audio(2, 128);
    juce::MidiBuffer midi;
    int emittedOnsets = 0;
    bool retriggered = false, released = false;
    for (int block = 0; block < 125; ++block) {
        // Muting the internal sound must not detach a sounding MIDI note from its source.
        if (block == 20 || block == 30)
            setParameter(*processor, "monitor", block == 20 ? 1.0f : 0.0f);
        midi.clear();
        processor->processBlock(audio, midi);
        const auto state = processor->visualState();
        checkPeak(state, audio);
        const int endSample = (block + 1) * 128;
        if (endSample <= 9000)
            check(state.notes[60] > 0 && state.sourceNotes[60] == 0,
                  "Scale Snap must preserve the source-note index at its emitted pitch");
        else if (endSample <= 15000)
            check(state.notes[60] > 0 && state.sourceNotes[60] == 1,
                  "a swung same-pitch retrigger must replace the previous source-note index");
        else
            check(noNotes(state), "gate note-off must clear source mapping before the raw note ends");
        check(state.notes[61] == 0 && state.sourceNotes[61] == -1,
              "the unsnapped source pitch must not appear as an emitted voice");
        for (const auto event : midi) {
            const auto message = event.getMessage();
            const int sample = block * 128 + event.samplePosition;
            if (message.isNoteOn()) {
                check(message.getNoteNumber() == 60, "Scale Snap emitted an unexpected pitch");
                ++emittedOnsets;
                retriggered |= sample == 9000;
            }
            released |= message.isNoteOff() && sample == 15000;
        }
        check(state.noteOnSerial == uint64_t(emittedOnsets),
              "source tracking must not alter emitted onset counting");
    }
    check(emittedOnsets == 2 && retriggered && released,
          "source mapping fixture must exercise actual swing, retrigger and shortened gate");
    processor->releaseResources();
}
} // namespace

void runVisualTelemetryTests() {
    auto processor = std::make_unique<Processor>();
    const auto initial = processor->visualState();
    check(noNotes(initial) && initial.outputPeak == 0 && initial.noteOnSerial == 0,
          "visual state must start silent before prepare");
    processor->prepareToPlay(48000, 128);
    setParameter(*processor, "follow", 0);
    setParameter(*processor, "tempo", 120);
    setParameter(*processor, "snap", 0);
    setParameter(*processor, "swing", 0);
    setParameter(*processor, "gate", 1);
    setParameter(*processor, "monitor", 0);
    composer::Clip clip;
    clip.length = 4;
    clip.count = 1;
    clip.notes[0] = {0, 1, 60, 96, false};
    processor->setClip(0, clip);
    processor->selectClip(0);
    processor->setPreview(true);

    juce::AudioBuffer<float> audio(2, 128);
    juce::MidiBuffer midi;
    auto process = [&] {
        midi.clear();
        processor->processBlock(audio, midi);
        const auto state = processor->visualState();
        checkPeak(state, audio);
        return state;
    };
    auto state = process();
    check(noteOns(midi) == 1 && state.noteOnSerial == 1, "visual onset missing for emitted MIDI");
    check(std::abs(state.notes[60] - 96.0f / 127.0f) < 0.000001f && state.sourceNotes[60] == 0,
          "MIDI note activity must remain visible with internal monitor off");
    check(state.outputPeak == 0, "MIDI-only playback must not invent audio energy");
    state = process();
    check(noteOns(midi) == 0 && state.noteOnSerial == 1 && state.notes[60] > 0,
          "a sustained MIDI note must not generate extra visual onsets");

    setParameter(*processor, "monitor", 1);
    state = process();
    check(state.outputPeak > 0 && state.notes[60] > 0 && state.sourceNotes[60] == 0,
          "monitor audio must reach the visual meter without changing the MIDI source index");
    processor->setPreview(false);
    state = process();
    check(noNotes(state) && state.noteOnSerial == 1, "stop must clear MIDI activity without an onset");
    check(state.outputPeak > 0, "visual meter must include the actual monitor release tail");
    for (int block = 0; block < 96; ++block)
        state = process();
    check(noNotes(state) && state.outputPeak == 0, "monitor release tail did not reach visual silence");

    processor->setPreview(true);
    state = process();
    check(state.noteOnSerial == 2 && state.notes[60] > 0, "restarted phrase must register a new onset");
    midi.clear();
    processor->processBlockBypassed(audio, midi);
    state = processor->visualState();
    checkPeak(state, audio);
    check(noNotes(state) && state.outputPeak == 0 && state.noteOnSerial == 2,
          "bypass must clear visual energy while retaining the onset serial");

    // This complete note is shorter than one UI frame and finishes inside one callback.
    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOn(1, 48, juce::uint8(100)), 10);
    midi.addEvent(juce::MidiMessage::noteOff(1, 48), 90);
    processor->processBlock(audio, midi);
    state = processor->visualState();
    checkPeak(state, audio);
    check(noteOns(midi) == 1 && state.noteOnSerial == 3 && noNotes(state),
          "a note ending inside one callback must still increment the visual onset serial");

    processor->setPreview(true);
    state = process();
    check(state.noteOnSerial == 4 && state.notes[60] > 0, "preview after trigger failed");
    processor->panic();
    state = process();
    check(noNotes(state) && state.noteOnSerial == 4, "panic must clear visual MIDI activity");
    processor->releaseResources();
    state = processor->visualState();
    check(noNotes(state) && state.outputPeak == 0 && state.noteOnSerial == 4,
          "release must clear the visual state without fabricating or rewinding onsets");

    processor->prepareToPlay(48000, 128);
    state = processor->visualState();
    check(noNotes(state) && state.outputPeak == 0 && state.noteOnSerial == 0,
          "prepare must reset visual telemetry");
    processor->setPreview(true);
    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOn(1, 67, juce::uint8(100)), 0);
    processor->processBlock(audio, midi);
    state = processor->visualState();
    checkPeak(state, audio);
    check(state.notes[60] == 0 && std::abs(state.notes[67] - 96.0f / 127.0f) < 0.000001f &&
              state.sourceNotes[60] == -1 && state.sourceNotes[67] == 0 && state.noteOnSerial == 1,
          "visual notes must follow the actual transposed MIDI output");
    processor->releaseResources();
    testSourceNoteMapping();
}
