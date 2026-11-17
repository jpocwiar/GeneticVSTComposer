#include "AllocationGuard.h"
#include "PluginProcessor.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <stdexcept>

namespace {
using Processor = GeneticVSTComposerJUCEAudioProcessor;
constexpr int blockSize = 64;
constexpr double rate = 48000, samplesPerBeat = 24000;
void check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
void parameter(Processor& processor, const char* id, float value) {
    auto* setting = processor.parameters.getParameter(id);
    check(setting != nullptr, "composition playback parameter missing");
    setting->setValueNotifyingHost(setting->convertTo0to1(value));
}
struct Head : juce::AudioPlayHead {
    double beat = 0;
    bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo result;
        result.setPpqPosition(beat);
        result.setBpm(120);
        result.setIsPlaying(playing);
        return result;
    }
};
struct Frame {
    juce::AudioBuffer<float> audio{2, blockSize};
    juce::MidiBuffer midi;
    Frame() { midi.ensureSize(65536); }
};
void render(Processor& processor, Frame& frame) {
    frame.midi.clear();
    processor.processBlock(frame.audio, frame.midi);
}
bool hasNote(const juce::MidiBuffer& events, bool on, int pitch, int channel = 0) {
    for (const auto metadata : events) {
        const auto message = metadata.getMessage();
        if ((on ? message.isNoteOn() : message.isNoteOff()) && message.getNoteNumber() == pitch &&
            (!channel || message.getChannel() == channel))
            return true;
    }
    return false;
}
bool anyNoteOn(const juce::MidiBuffer& events, int channel = 0) {
    for (const auto metadata : events) {
        const auto message = metadata.getMessage();
        if (message.isNoteOn() && (!channel || message.getChannel() == channel))
            return true;
    }
    return false;
}
bool rpnValue(const juce::MidiBuffer& events, int channel, int number, int value) {
    juce::MidiRPNDetector detector;
    for (const auto metadata : events) {
        const auto message = metadata.getMessage();
        if (!message.isController())
            continue;
        if (const auto parsed = detector.tryParse(message.getChannel(), message.getControllerNumber(),
                                                  message.getControllerValue())) {
            const int decoded = parsed->is14BitValue ? parsed->value >> 7 : parsed->value;
            if (!parsed->isNRPN && parsed->channel == channel && parsed->parameterNumber == number &&
                decoded == value)
                return true;
        }
    }
    return false;
}
bool pitchWheel(const juce::MidiBuffer& events, int channel, int position) {
    for (const auto metadata : events) {
        const auto message = metadata.getMessage();
        if (message.isPitchWheel() && message.getChannel() == channel &&
            message.getPitchWheelValue() == position)
            return true;
    }
    return false;
}
composer::Clip heldPhrase(int pitch = 73) {
    composer::Clip clip;
    clip.length = 4;
    clip.count = 1;
    clip.notes[0] = {0, 4, pitch, 96, false, 0};
    return clip;
}
std::unique_ptr<Processor> fixture(const composer::Clip& melody, bool chords = false) {
    auto processor = std::make_unique<Processor>();
    // The bank's pitch makes an unintended fallback distinguishable from the composition.
    processor->setClip(0, heldPhrase(47));
    processor->selectClip(0);
    if (chords)
        check(processor->setCustomHarmony("C:4").wasOk(), "composition playback chord fixture");
    check(processor->addCompositionSection().wasOk(), "composition playback section fixture");
    check(processor->setCompositionSectionClip(0, melody).wasOk(), "composition playback melody fixture");
    processor->prepareToPlay(rate, blockSize);
    parameter(*processor, "tempo", 120);
    parameter(*processor, "gate", 1);
    parameter(*processor, "swing", 0);
    parameter(*processor, "humanize", 0);
    parameter(*processor, "snap", 0);
    parameter(*processor, "microEnabled", 0);
    parameter(*processor, "auditionReverb", 0);
    parameter(*processor, "chordMidiOut", chords ? 1.0f : 0.0f);
    return processor;
}

void testHostSeekStopAndSourceRetention() {
    Head head;
    auto processor = fixture(heldPhrase(), true);
    processor->setPlayHead(&head);
    processor->auditionComposition(true);
    parameter(*processor, "follow", 1);
    Frame frame;
    head.beat = 1;
    render(*processor, frame);
    check(hasNote(frame.midi, true, 73, 1) && anyNoteOn(frame.midi, 16),
          "composition seek did not chase the held melody and chord voices");
    check(processor->audibleCompositionSection() == 0, "composition seek chose the wrong section");
    head.beat += blockSize / samplesPerBeat;
    head.playing = false;
    render(*processor, frame);
    check(hasNote(frame.midi, false, 73, 1) && !anyNoteOn(frame.midi),
          "host stop left the chased melody active or started another source");
    head.beat = 2;
    head.playing = true;
    render(*processor, frame);
    check(hasNote(frame.midi, true, 73, 1) && anyNoteOn(frame.midi, 16),
          "composition did not resume held voices after host stop/seek");

    head.beat = 8;
    render(*processor, frame);
    check(hasNote(frame.midi, false, 73, 1), "seeking beyond the composition left a held melody");
    for (int block = 0; block < 4; ++block) {
        head.beat += blockSize / samplesPerBeat;
        render(*processor, frame);
        check(!anyNoteOn(frame.midi) && !processor->isCompositionPlaying(),
              "finished composition fell back to the selected bank while Follow DAW was running");
    }
    head.beat = 0.25;
    render(*processor, frame);
    check(hasNote(frame.midi, true, 73, 1), "finished composition could not restart after a host seek");
    processor->stopComposition();
    render(*processor, frame);
    check(hasNote(frame.midi, false, 73, 1), "explicit composition stop left a note active");
    for (int block = 0; block < 3; ++block) {
        head.beat += blockSize / samplesPerBeat;
        render(*processor, frame);
        check(!anyNoteOn(frame.midi), "stopped composition unexpectedly resumed bank playback");
    }

    processor->auditionComposition(false);
    parameter(*processor, "follow", 1);
    head.beat = 5.5;
    render(*processor, frame);
    check(hasNote(frame.midi, true, 73, 1) &&
              std::abs(processor->playPosition() - (1.5 + blockSize / samplesPerBeat)) < 1e-7,
          "a looping section did not wrap the host seek into its own musical length");
    processor->setPlayHead(nullptr);
}

void testLiveOutputModeCleanup() {
    Head head;
    auto melody = heldPhrase(64);
    melody.count = 4;
    for (int index = 0; index < 4; ++index)
        melody.notes[size_t(index)] = {double(index), 0.75, 64, 96, false, -50};
    auto processor = fixture(melody);
    processor->setPlayHead(&head);
    parameter(*processor, "microEnabled", 1);
    parameter(*processor, "tuning", 1);
    parameter(*processor, "microOutput", 1);
    processor->auditionComposition(true);
    parameter(*processor, "follow", 1);
    Frame frame;
    render(*processor, frame);
    check(hasNote(frame.midi, true, 64, 2) && rpnValue(frame.midi, 1, 6, 15),
          "composition did not configure MPE before the first note");
    parameter(*processor, "microEnabled", 0);
    head.beat = 0.25;
    render(*processor, frame);
    check(hasNote(frame.midi, false, 64, 2) && pitchWheel(frame.midi, 2, 8192) &&
              rpnValue(frame.midi, 1, 6, 0),
          "switching composition MPE off failed to release the member note, bend or zone");
    check(hasNote(frame.midi, true, 64, 1), "plain composition mode did not chase on channel one");

    parameter(*processor, "microEnabled", 1);
    parameter(*processor, "microOutput", 0);
    parameter(*processor, "bendRange", 2);
    head.beat = 1;
    render(*processor, frame);
    check(hasNote(frame.midi, true, 64, 1) && rpnValue(frame.midi, 1, 0, 2) &&
              pitchWheel(frame.midi, 1, 6144),
          "composition mono bends did not use the configured two-semitone range");
    parameter(*processor, "bendRange", 12);
    head.beat = 1.25;
    render(*processor, frame);
    check(hasNote(frame.midi, false, 64, 1) && rpnValue(frame.midi, 1, 0, 12) &&
              pitchWheel(frame.midi, 1, 7851),
          "live composition bend-range change did not release and reconfigure the note");
    processor->setPlayHead(nullptr);
}

int noteOffAfter(Processor& processor, Head& head, Frame& frame, double begin, int pitch, int blocks,
                 int expectedOnPitch) {
    int offAt = -1;
    for (int block = 0; block < blocks; ++block) {
        head.beat = begin + block * blockSize / samplesPerBeat;
        render(processor, frame);
        if (block == 0)
            check(hasNote(frame.midi, true, expectedOnPitch, 1),
                  "composition used a stale live pitch projection");
        for (const auto metadata : frame.midi) {
            const auto message = metadata.getMessage();
            if (message.isNoteOff() && message.getNoteNumber() == pitch)
                offAt = block * blockSize + metadata.samplePosition;
        }
    }
    return offAt;
}
void testLivePerformanceProjection() {
    Head head;
    auto melody = heldPhrase();
    melody.count = 2;
    melody.notes[0] = {0.25, 0.25, 61, 100, false, 0};
    melody.notes[1] = {1.25, 0.25, 63, 100, false, 0};
    auto processor = fixture(melody);
    processor->setPlayHead(&head);
    parameter(*processor, "root", 0);
    parameter(*processor, "scale", 0);
    processor->auditionComposition(true);
    parameter(*processor, "follow", 1);
    // Change these after the schedule was published; the stored notes remain original.
    parameter(*processor, "swing", 0.5f);
    parameter(*processor, "gate", 0.5f);
    parameter(*processor, "snap", 1);
    Frame frame;
    head.beat = 0.25;
    render(*processor, frame);
    check(!anyNoteOn(frame.midi), "live Swing did not delay the composition's offbeat");
    const int shortened = noteOffAfter(*processor, head, frame, 0.375, 60, 28, 60);
    check(std::abs(shortened - 1500) <= 1, "live Swing/Gate did not shorten the swung note end consistently");
    parameter(*processor, "swing", 0);
    parameter(*processor, "gate", 1);
    parameter(*processor, "snap", 0);
    const int straight = noteOffAfter(*processor, head, frame, 1.25, 63, 100, 63);
    check(std::abs(straight - 6000) <= 1,
          "live Gate/Fit changes were not applied to the next composition note");
    const auto original = processor->compositionSnapshot().sections[0].melody;
    check(original.notes[0].start == 0.25 && original.notes[0].duration == 0.25 &&
              original.notes[0].pitch == 61,
          "live composition projection rewrote the editable notes");
    processor->setPlayHead(nullptr);
}

void testSysExHasNoRealtimeAllocations() {
    auto processor = fixture(heldPhrase());
    processor->auditionComposition(false);
    Frame frame;
    render(*processor, frame); // Warm the normal source transition before the allocation fixture.
    std::array<juce::uint8, 2048> payload{};
    payload.fill(0x01);
    payload[0] = 0x7d;
    const auto sysex = juce::MidiMessage::createSysExMessage(payload.data(), int(payload.size()));
    long allocations = 0;
    for (int block = 0; block < 12; ++block) {
        frame.midi.clear();
        frame.midi.addEvent(sysex, 11);
        {
            composertest::AllocationGuard guard;
            processor->processBlock(frame.audio, frame.midi);
            allocations += guard.count();
        }
        bool forwarded = false;
        for (const auto metadata : frame.midi)
            forwarded |= metadata.samplePosition == 11 && metadata.numBytes == sysex.getRawDataSize() &&
                         std::memcmp(metadata.data, sysex.getRawData(), size_t(metadata.numBytes)) == 0;
        check(forwarded, "composition altered or dropped an ordinary bounded SysEx event");
    }
    check(allocations == 0, "composition SysEx forwarding allocated on the audio thread");
}

void testTransitionToIndependentMicrotonal() {
    auto melody = heldPhrase(84);
    melody.notes[0].cents = 25;
    auto processor = fixture(melody);
    auto settings = processor->microtonalSnapshot().settings;
    settings.mpe = true;
    settings.followHost = false;
    settings.beats = 4;
    settings.bars = 0;
    check(processor->setMicrotonalSettings(settings).wasOk(), "microtonal transition settings fixture");
    microtonal::Clip independent;
    independent.length = 4;
    independent.count = 1;
    independent.notes[0] = {0, 1, 1, 96, false};
    processor->setMicrotonalClip(independent);
    parameter(*processor, "microEnabled", 1);
    parameter(*processor, "tuning", 1);
    parameter(*processor, "microOutput", 1);
    processor->auditionComposition(false);
    Frame frame;
    render(*processor, frame);
    check(hasNote(frame.midi, true, 84, 2),
          "composition-to-micro transition did not begin with a held MPE note");
    processor->startMicrotonalAudition();
    bool released = false, independentOn = false, zoneCleared = false;
    for (int block = 0; block < 5; ++block) {
        render(*processor, frame);
        released |= hasNote(frame.midi, false, 84, 2);
        zoneCleared |= rpnValue(frame.midi, 1, 6, 0);
        for (const auto metadata : frame.midi) {
            const auto message = metadata.getMessage();
            if (message.isNoteOn()) {
                check(message.getNoteNumber() != 84 && message.getNoteNumber() != 47,
                      "source transition restarted a stale composition or ordinary bank note");
                independentOn = true;
            }
        }
    }
    check(released && zoneCleared && independentOn && processor->isMicrotonalMode() &&
              !processor->isCompositionPlaying(),
          "composition-to-independent-Microtonal handoff lost note cleanup or the new source");
}
} // namespace

void runCompositionPlaybackTests() {
    testHostSeekStopAndSourceRetention();
    testLiveOutputModeCleanup();
    testLivePerformanceProjection();
    testSysExHasNoRealtimeAllocations();
    testTransitionToIndependentMicrotonal();
}
