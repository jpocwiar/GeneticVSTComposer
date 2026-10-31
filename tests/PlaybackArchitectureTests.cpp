#include "PluginProcessor.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {
using Processor = GeneticVSTComposerJUCEAudioProcessor;
constexpr int blockSize = 128;
constexpr double samplesPerBeat = 24000; // 120 BPM at 48 kHz.

void check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

void parameter(Processor& processor, const char* id, float value) {
    auto* param = processor.parameters.getParameter(id);
    check(param != nullptr, "playback architecture parameter missing");
    param->setValueNotifyingHost(param->convertTo0to1(value));
}

bool noNotes(const Processor::VisualState& state) {
    return std::all_of(state.notes.begin(), state.notes.end(), [](float value) { return value == 0; }) &&
           std::all_of(state.sourceNotes.begin(), state.sourceNotes.end(),
                       [](int index) { return index == -1; });
}

struct Frame {
    juce::AudioBuffer<float> audio{2, blockSize};
    juce::MidiBuffer midi;
};

void process(Processor& processor, Frame& frame) {
    frame.midi.clear();
    processor.processBlock(frame.audio, frame.midi);
}

void render(Processor& processor, int blocks) {
    Frame frame;
    for (int block = 0; block < blocks; ++block)
        process(processor, frame);
}

void sameMidi(const juce::MidiBuffer& a, const juce::MidiBuffer& b) {
    auto left = a.cbegin(), right = b.cbegin();
    for (; left != a.cend() && right != b.cend(); ++left, ++right) {
        const auto x = *left, y = *right;
        check(x.samplePosition == y.samplePosition && x.numBytes == y.numBytes &&
                  std::memcmp(x.data, y.data, size_t(x.numBytes)) == 0,
              "internal accompaniment changed outgoing MIDI data or timing");
    }
    check(left == a.cend() && right == b.cend(), "internal accompaniment added outgoing MIDI events");
}

void checkChordAt(const Processor& processor, const composer::Clip& chords, double beat) {
    const auto state = processor.chordVisualState();
    const double phase = std::fmod(beat, chords.length);
    std::array<int, 128> expected;
    expected.fill(-1);
    for (int i = 0; i < chords.count; ++i)
        if (chords.notes[i].start <= phase && phase < chords.notes[i].start + chords.notes[i].duration)
            expected[size_t(chords.notes[i].pitch)] = i;
    for (int pitch = 0; pitch < 128; ++pitch) {
        const int index = expected[size_t(pitch)];
        check(state.sourceNotes[size_t(pitch)] == index,
              "chord telemetry lost a held note, retained an old chord or points to a melody note");
        const float velocity = index < 0 ? 0.0f : chords.notes[index].velocity / 127.0f;
        check(std::abs(state.notes[size_t(pitch)] - velocity) < 1e-6f,
              "chord telemetry differs from the exact voiced chord");
    }
}

std::unique_ptr<Processor> player(bool accompany, int output = 1, bool detuned = true) {
    auto processor = std::make_unique<Processor>();
    parameter(*processor, "chordVoicing", 1);
    parameter(*processor, "chordCentre", 60);
    check(processor->setCustomHarmony("C:2 G:2").wasOk(), "accompaniment fixture harmony failed");
    const auto chords = processor->chordClipSnapshot();
    composer::Clip melody;
    melody.length = 4;
    melody.count = 4;
    // Each note collides with a sounding chord's MIDI key; the two voice sets must stay independent.
    const double starts[]{0, 0.5, 1.25, 2.25};
    const int cents[]{-50, 25, -25, 50};
    for (int i = 0; i < melody.count; ++i)
        melody.notes[i] = {starts[i], 0.25, chords.notes[0].pitch, 100, false, detuned ? cents[i] : 0};
    processor->setClip(0, melody);
    processor->selectClip(0);
    parameter(*processor, "auditionReverb", 0);
    processor->prepareToPlay(48000, blockSize);
    parameter(*processor, "follow", 0);
    parameter(*processor, "tempo", 120);
    parameter(*processor, "snap", 0);
    parameter(*processor, "swing", 0);
    parameter(*processor, "gate", 1);
    parameter(*processor, "monitor", 1);
    parameter(*processor, "volume", 0.65f);
    parameter(*processor, "auditionSound", 1);
    parameter(*processor, "microEnabled", 1);
    parameter(*processor, "tuning", 2);
    parameter(*processor, "microOutput", float(output));
    parameter(*processor, "bendRange", 2);
    parameter(*processor, "auditionChords", accompany ? 1.0f : 0.0f);
    return processor;
}

void testIndependentAudioAndMidi(int output) {
    auto plain = player(false, output, false), mixedPlain = player(true, output, false);
    auto tuned = player(false, output, true), mixedTuned = player(true, output, true);
    for (auto* processor : {plain.get(), mixedPlain.get(), tuned.get(), mixedTuned.get()})
        processor->setPreview(true);
    Frame a, b, c, d;
    double chordEnergy = 0, errorEnergy = 0, audibleDifference = 0;
    int melodyOns = 0;
    for (int block = 0; block < 320; ++block) {
        process(*plain, a);
        process(*mixedPlain, b);
        process(*tuned, c);
        process(*mixedTuned, d);
        sameMidi(a.midi, b.midi);
        sameMidi(c.midi, d.midi);
        for (const auto event : d.midi)
            if (event.getMessage().isNoteOn()) {
                ++melodyOns;
                check(event.getMessage().getChannel() == (output == 0 ? 1 : melodyOns + 1),
                      "accompaniment interfered with melody MIDI channel allocation");
            }
        for (int sample = 0; sample < blockSize; ++sample) {
            // Invert the shared output soft clip to isolate the accompaniment actually rendered.
            const double chordPlain = std::atanh(double(b.audio.getSample(0, sample))) -
                                      std::atanh(double(a.audio.getSample(0, sample)));
            const double chordTuned = std::atanh(double(d.audio.getSample(0, sample))) -
                                      std::atanh(double(c.audio.getSample(0, sample)));
            chordEnergy += chordPlain * chordPlain;
            errorEnergy += (chordPlain - chordTuned) * (chordPlain - chordTuned);
            const double difference = d.audio.getSample(0, sample) - c.audio.getSample(0, sample);
            audibleDifference += difference * difference;
        }
    }
    check(melodyOns == 3 && chordEnergy > 1 && audibleDifference > 1,
          "combined preview did not add an audible chord layer to the melody");
    check(errorEnergy / chordEnergy < 1e-10,
          "melody microtonal bends detuned or retriggered an internal chord voice");
    check(!mixedTuned->audibleChords() && mixedTuned->audibleChordLayer() &&
              !mixedTuned->isAuditioningChords(),
          "combined preview was mistaken for chord-only playback");
    check(noNotes(mixedTuned->visualState()), "fixture melody should have finished its third short note");
    checkChordAt(*mixedTuned, mixedTuned->chordClipSnapshot(), mixedTuned->playPosition());
    check(mixedTuned->chordVisualState().noteOnSerial == 3,
          "same-pitch melody attacks retriggered held accompaniment notes");
    check(std::abs(mixedTuned->chordPlayPosition() - mixedTuned->playPosition()) < 1e-8,
          "accompaniment playhead differs from the audible chord phase");
}

void checkStopped(Processor& processor, const char* message) {
    Frame frame;
    process(processor, frame);
    check(!processor.isPlaying() && !processor.audibleChordLayer() &&
              noNotes(processor.visualState()) && noNotes(processor.chordVisualState()) &&
              processor.chordPlayPosition() < 0,
          message);
    for (int block = 0; block < 100; ++block) {
        process(processor, frame);
        for (const auto event : frame.midi)
            check(!event.getMessage().isNoteOn(), "stopped accompaniment restarted playback");
    }
    check(frame.audio.getMagnitude(0, blockSize) == 0, "stopped accompaniment left a hanging audio voice");
}

void testChaseToggleAndLifecycle() {
    auto processor = player(false);
    processor->setPreview(true);
    render(*processor, 188); // Just after beat 1: no melody note is held, the C chord should sustain.
    check(!processor->audibleChordLayer() && noNotes(processor->chordVisualState()),
          "disabled accompaniment published active chord notes");
    const double before = processor->playPosition();
    parameter(*processor, "auditionChords", 1);
    Frame frame;
    process(*processor, frame);
    check(processor->audibleChordLayer() && !processor->audibleChords() &&
              processor->playPosition() > before && processor->playPosition() < before + 0.01,
          "enabling accompaniment restarted the melody instead of joining its current position");
    check(frame.midi.isEmpty(), "chasing a held internal chord emitted MIDI notes");
    checkChordAt(*processor, processor->chordClipSnapshot(), processor->playPosition());
    check(processor->chordVisualState().noteOnSerial == 3,
          "enabling accompaniment mid-bar failed to chase all three held chord notes");

    parameter(*processor, "auditionChords", 0);
    process(*processor, frame);
    check(!processor->audibleChordLayer() && noNotes(processor->chordVisualState()),
          "disabling accompaniment left held chord notes");
    parameter(*processor, "auditionChords", 1);
    process(*processor, frame);
    checkChordAt(*processor, processor->chordClipSnapshot(), processor->playPosition());
    parameter(*processor, "monitor", 0);
    process(*processor, frame);
    check(!processor->audibleChordLayer() && noNotes(processor->chordVisualState()),
          "muting internal sound left a supposedly audible accompaniment layer");
    parameter(*processor, "monitor", 1);
    process(*processor, frame);
    checkChordAt(*processor, processor->chordClipSnapshot(), processor->playPosition());

    processor->panic();
    checkStopped(*processor, "panic left combined preview active");
    processor->setPreview(true);
    process(*processor, frame);
    check(processor->audibleChordLayer(), "preview did not recover after panic");
    processor->setPreview(false);
    checkStopped(*processor, "Stop left combined preview active");

    processor->setPreview(true);
    process(*processor, frame);
    processor->clearCustomHarmony();
    process(*processor, frame);
    check(processor->isPlaying() && !processor->audibleChordLayer() &&
              noNotes(processor->chordVisualState()),
          "clearing chords stopped the melody or retained an orphaned accompaniment");
    processor->releaseResources();
    check(noNotes(processor->chordVisualState()) && !processor->audibleChordLayer(),
          "releaseResources retained chord activity");
}

struct PlayHead : juce::AudioPlayHead {
    double beat = 0;
    bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo position;
        position.setPpqPosition(beat);
        position.setBpm(120);
        position.setIsPlaying(playing);
        return position;
    }
};

void testHostSeekLoopAndFollowReturn() {
    auto processor = player(true);
    PlayHead head;
    processor->setPlayHead(&head);
    parameter(*processor, "follow", 1);
    Frame frame;
    auto processHost = [&] {
        process(*processor, frame);
        head.beat += blockSize / samplesPerBeat;
    };
    processHost();
    checkChordAt(*processor, processor->chordClipSnapshot(), processor->playPosition());
    head.beat = 2.5;
    processHost();
    checkChordAt(*processor, processor->chordClipSnapshot(), processor->playPosition());
    head.beat = 3.99;
    for (int i = 0; i < 4; ++i)
        processHost();
    check(processor->playPosition() < 0.02 && processor->audibleChordLayer(),
          "accompaniment failed to cross the host-driven loop boundary");
    checkChordAt(*processor, processor->chordClipSnapshot(), processor->playPosition());
    head.playing = false;
    processHost();
    check(!processor->isPlaying() && !processor->audibleChordLayer() &&
              noNotes(processor->chordVisualState()),
          "host Stop left accompaniment notes active");
    for (int i = 0; i < 100; ++i)
        processHost();
    check(frame.audio.getMagnitude(0, blockSize) == 0, "host Stop left an audible chord tail hanging");

    // Solo chord preview is local. Re-enabling Follow returns to melody plus the internal layer.
    processor->startChordAudition();
    processHost();
    check(processor->audibleChords() && processor->isAuditioningChords(), "solo chord fixture failed");
    head.beat = 1;
    head.playing = true;
    parameter(*processor, "follow", 1);
    processHost();
    check(!processor->audibleChords() && !processor->isAuditioningChords() &&
              processor->audibleChordLayer(),
          "Follow DAW failed to leave chord-only mode and restore combined melody playback");
    checkChordAt(*processor, processor->chordClipSnapshot(), processor->playPosition());
    bool mpeNote = false;
    for (int block = 0; block < 50; ++block) {
        processHost();
        for (const auto event : frame.midi)
            if (event.getMessage().isNoteOn()) {
                check(event.getMessage().getChannel() > 1,
                      "returning from solo chords left melody notes on the plain MIDI channel");
                mpeNote = true;
            }
    }
    check(mpeNote, "Follow DAW return did not resume melody MIDI after solo chords");
    processor->setPlayHead(nullptr);
}

void testHarmonyLongerThanRiff() {
    auto processor = player(false);
    auto riff = processor->selectedClip();
    riff.length = 1;
    riff.count = 1;
    processor->setClip(0, riff);
    const auto chords = processor->chordClipSnapshot();
    processor->setPreview(true);
    render(*processor, 470); // 2.5067 beats: the one-beat riff has already looped twice.
    parameter(*processor, "auditionChords", 1);
    Frame frame;
    process(*processor, frame);
    check(processor->playPosition() > 0.5 && processor->playPosition() < 0.52 &&
              processor->chordPlayPosition() > 2.5 && processor->chordPlayPosition() < 2.52,
          "joining a short riff restarted harmony instead of chasing its full progression");
    checkChordAt(*processor, chords, processor->chordPlayPosition());
    const auto attacks = processor->chordVisualState().noteOnSerial;
    check(attacks == 3, "joining the second chord failed to chase its three held notes");
    render(*processor, 186); // Past the next melody loop, still within the same held G chord.
    check(processor->chordPlayPosition() > 3.5 && processor->chordPlayPosition() < 3.52 &&
              processor->chordVisualState().noteOnSerial == attacks,
          "a short melody loop reset or retriggered the longer chord progression");
    checkChordAt(*processor, chords, processor->chordPlayPosition());
    render(*processor, 118); // The harmony itself has now crossed its four-beat loop boundary.
    check(processor->chordPlayPosition() > 0.1 && processor->chordPlayPosition() < 0.15 &&
              processor->chordVisualState().noteOnSerial == attacks + 3,
          "the full progression did not loop independently of the shorter melody");
    checkChordAt(*processor, chords, processor->chordPlayPosition());

    auto hosted = player(true);
    hosted->setClip(0, riff);
    PlayHead head;
    head.beat = 3;
    hosted->setPlayHead(&head);
    parameter(*hosted, "follow", 1);
    process(*hosted, frame);
    check(hosted->playPosition() < 0.01 && hosted->chordPlayPosition() > 3 &&
              hosted->chordPlayPosition() < 3.01,
          "Follow DAW derived chord phase from the shorter melody's wrapped position");
    checkChordAt(*hosted, hosted->chordClipSnapshot(), hosted->chordPlayPosition());
    head.beat = 4.5;
    process(*hosted, frame);
    check(hosted->chordPlayPosition() > 0.5 && hosted->chordPlayPosition() < 0.51,
          "host seek did not place the independent chord clock at absolute PPQ modulo harmony length");
    checkChordAt(*hosted, hosted->chordClipSnapshot(), hosted->chordPlayPosition());
    hosted->setPlayHead(nullptr);
}

void testLiveFitToScaleWithHarmony() {
    auto processor = player(false, 0);
    check(processor->setCustomHarmony("C:4").wasOk(), "Fit to scale harmony fixture failed");
    parameter(*processor, "root", 0);
    parameter(*processor, "scale", 0);
    composer::Clip melody;
    melody.length = 4;
    melody.count = 3;
    for (int i = 0; i < melody.count; ++i)
        melody.notes[i] = {double(i), 0.5, 61, 100, false, -37};
    processor->setClip(0, melody);
    const auto revision = processor->documentRevision();
    processor->setPreview(true);
    Frame frame;
    std::vector<int> pitches, bends;
    auto collect = [&] {
        int lastBend = -1;
        for (const auto event : frame.midi) {
            const auto message = event.getMessage();
            if (message.isPitchWheel())
                lastBend = message.getPitchWheelValue();
            if (message.isNoteOn()) {
                pitches.push_back(message.getNoteNumber());
                bends.push_back(lastBend);
            }
        }
    };
    for (int block = 0; block < 94; ++block) {
        process(*processor, frame);
        collect();
    }
    parameter(*processor, "snap", 1);
    for (int block = 0; block < 94; ++block) {
        process(*processor, frame);
        collect();
    }
    frame.midi.clear();
    frame.midi.addEvent(juce::MidiMessage::noteOn(1, 61, juce::uint8(100)), 0); // Transpose +1.
    processor->processBlock(frame.audio, frame.midi);
    collect();
    for (int block = 0; block < 187; ++block) {
        process(*processor, frame);
        collect();
    }
    check(pitches == std::vector<int>{61, 60, 62},
          "Fit to scale did not apply live after transposition while custom harmony was active");
    const int expectedBend = 8192 + int(std::lround(-37.0 * 8192 / 200));
    check(bends == std::vector<int>(3, expectedBend),
          "Fit to scale replaced the melody's optimized microtonal offsets");
    const auto saved = processor->selectedClip();
    for (int i = 0; i < saved.count; ++i)
        check(saved.notes[i].pitch == 61 && saved.notes[i].cents == -37 &&
                  saved.notes[i].start == double(i),
              "live Fit to scale destructively rewrote the melody bank");
    check(processor->documentRevision() == revision, "live Fit to scale created a document edit");
}
} // namespace

void testChordMidiOutputAndCombinedExport() {
    auto processor = player(true, 0, false);
    parameter(*processor, "chordMidiOut", 1);
    processor->setPreview(true);
    Frame frame;
    int chordOns = 0, melodyOns = 0;
    for (int block = 0; block < 40; ++block) {
        process(*processor, frame);
        for (const auto event : frame.midi)
            if (event.getMessage().isNoteOn()) {
                if (event.getMessage().getChannel() == 16)
                    ++chordOns;
                else
                    ++melodyOns;
            }
    }
    check(chordOns > 0 && melodyOns > 0, "chord MIDI did not join the melody on channel 16");

    auto mpe = player(true, 1, false);
    parameter(*mpe, "chordMidiOut", 1);
    mpe->setPreview(true);
    int channel16 = 0;
    for (int block = 0; block < 40; ++block) {
        process(*mpe, frame);
        for (const auto event : frame.midi)
            if (event.getMessage().getChannel() == 16 && event.getMessage().isNoteOnOrOff())
                ++channel16;
    }
    check(channel16 == 0, "MPE allowed chord MIDI on channel 16");

    const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getNonexistentChildFile("composer-both", ".mid", false);
    check(processor->exportMelodyAndChords(file).wasOk(), "combined export failed");
    juce::MemoryBlock bytes;
    check(file.loadFileAsData(bytes) && bytes.getSize() > 10 && static_cast<const char*>(bytes.getData())[9] == 1,
          "combined export is not MIDI type 1");
    juce::FileInputStream stream(file);
    juce::MidiFile midi;
    check(stream.openedOk() && midi.readFrom(stream) && midi.getNumTracks() == 2,
          "combined export did not write melody and chord tracks");
    bool melodyBend = false;
    for (int i = 0; i < midi.getTrack(0)->getNumEvents(); ++i)
        melodyBend |= midi.getTrack(0)->getEventPointer(i)->message.isPitchWheel();
    check(melodyBend, "melody track dropped its pitch-bend performance");
    int chordNotes = 0;
    const auto* chordTrack = midi.getTrack(1);
    for (int i = 0; i < chordTrack->getNumEvents(); ++i) {
        const auto& message = chordTrack->getEventPointer(i)->message;
        check(!message.isPitchWheel() && !message.isController(), "chord track includes melody transforms");
        if (message.isNoteOn()) {
            check(message.getChannel() == 1, "chord track left channel 1");
            ++chordNotes;
        }
    }
    check(chordNotes == processor->chordClipSnapshot().count, "chord track dropped a voiced note");
    file.deleteFile();
}

void testRegionLoop(int output) {
    auto processor = std::make_unique<Processor>();
    composer::Clip clip;
    clip.length = 4;
    clip.count = 5;
    clip.notes[0] = {0, 2, 59, 100, false, 0}; // Sustains across the left boundary; must not start.
    clip.notes[1] = {1, 0.25, 60, 100, false, 25};
    clip.notes[2] = {1.5, 1.5, 64, 100, false, -25}; // Must stop at the right boundary.
    clip.notes[3] = {2, 0.25, 67, 100, false, 0}; // Right boundary is excluded.
    clip.notes[4] = {3, 0.25, 72, 100, false, 0};
    processor->setClip(0, clip);
    parameter(*processor, "microEnabled", output > 0 ? 1 : 0);
    parameter(*processor, "microOutput", float(std::max(0, output - 1)));
    parameter(*processor, "tuning", 1);
    parameter(*processor, "gate", 1.5f);
    parameter(*processor, "quantiseSwitch", 0);
    processor->prepareToPlay(48000, blockSize);
    processor->setPlaybackRegion(1, 2, true);
    processor->setPreview(true);
    Frame frame;
    int first = 0, second = 0;
    std::array<int, 128> held{};
    for (int block = 0; block < 600; ++block) {
        process(*processor, frame);
        check(processor->playPosition() >= 1 && processor->playPosition() < 2,
              "region loop playhead escaped the selection");
        for (const auto metadata : frame.midi) {
            const auto m = metadata.getMessage();
            if (m.isNoteOn()) {
                check(m.getNoteNumber() == 60 || m.getNoteNumber() == 64,
                      "region loop emitted an onset outside the selection");
                if (m.getNoteNumber() == 60) {
                    check(held[64] == 0, "region loop did not release the preceding loop's last note");
                    ++first;
                } else
                    ++second;
                ++held[size_t(m.getNoteNumber())];
            } else if (m.isNoteOff())
                --held[size_t(m.getNoteNumber())];
        }
    }
    check(first >= 3 && second >= 3, "region loop did not repeat the selected section");
    processor->setPlaybackRegion(3, 4, true);
    process(*processor, frame);
    check(processor->playPosition() >= 3 && processor->playPosition() < 4,
          "moving the selection did not update the active loop");
    bool heardNewRange = false;
    for (const auto metadata : frame.midi)
        if (metadata.getMessage().isNoteOn()) {
            check(metadata.getMessage().getNoteNumber() == 72, "old range started notes after a range edit");
            heardNewRange = true;
        }
    check(heardNewRange, "range edit did not start the newly selected note");
    processor->setPlaybackRegion(3, 4, false);
    process(*processor, frame);
    check(processor->playPosition() < 1, "disabling Loop range did not restore full-phrase playback");
    processor->setPlaybackRegion(1, 2, true);
    processor->selectClip(1);
    process(*processor, frame);
    check(processor->playPosition() < 1, "a different variation inherited the previous range loop");

    // The editor publishes a new selection range while a variation switch is queued.
    processor->selectClip(0);
    processor->setPlaybackRegion(1, 2, true);
    process(*processor, frame);
    parameter(*processor, "quantiseSwitch", 1);
    processor->selectClip(1);
    processor->setPlaybackRegion(0, processor->selectedClip().length, false);
    process(*processor, frame);
    check(processor->audibleIndex() == 0 && processor->playPosition() >= 1 && processor->playPosition() < 2,
          "publishing a new selection bypassed the queued range-loop switch");
    for (int block = 0; block < 200 && processor->audibleIndex() == 0; ++block)
        process(*processor, frame);
    check(processor->audibleIndex() == 1, "queued variation did not activate at the range boundary");
    parameter(*processor, "quantiseSwitch", 0);

    processor->selectClip(0);
    processor->setPlaybackRegion(1, 2, true);
    PlayHead head;
    head.beat = 7.25;
    processor->setPlayHead(&head);
    parameter(*processor, "follow", 1);
    process(*processor, frame);
    check(processor->playPosition() >= 1.25 && processor->playPosition() < 1.27,
          "Follow DAW seek did not map into the selected loop");
    processor->setPlayHead(nullptr);
    processor->panic();
    process(*processor, frame);
    check(!processor->isPlaying() && noNotes(processor->visualState()), "panic left range notes held");
}

void runPlaybackArchitectureTests() {
    for (int output = 0; output < 3; ++output)
        testRegionLoop(output);
    Processor defaults;
    check(defaults.parameters.getRawParameterValue("snap")->load() == 0,
          "Fit to scale must default to the original performance switch's OFF state");
    testIndependentAudioAndMidi(0);
    testIndependentAudioAndMidi(1);
    testChaseToggleAndLifecycle();
    testHostSeekLoopAndFollowReturn();
    testHarmonyLongerThanRiff();
    testLiveFitToScaleWithHarmony();
    testChordMidiOutputAndCombinedExport();
}
