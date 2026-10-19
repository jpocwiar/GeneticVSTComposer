#include "PluginProcessor.h"
#include <cmath>
#include <stdexcept>
#include <vector>

namespace {
using WorkflowProcessor = GeneticVSTComposerJUCEAudioProcessor;

void check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

void setParameter(WorkflowProcessor& processor, const char* id, float value) {
    auto* parameter = processor.parameters.getParameter(id);
    check(parameter != nullptr, "workflow parameter missing");
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

bool sameNote(const composer::Note& a, const composer::Note& b) {
    return a.start == b.start && a.duration == b.duration && a.pitch == b.pitch && a.velocity == b.velocity &&
           a.locked == b.locked;
}

bool sameClip(const composer::Clip& a, const composer::Clip& b) {
    if (a.count != b.count || a.length != b.length || a.grid != b.grid || a.numerator != b.numerator ||
        a.denominator != b.denominator || a.favourite != b.favourite || a.seed != b.seed)
        return false;
    for (int i = 0; i < a.count; ++i)
        if (!sameNote(a.notes[i], b.notes[i]))
            return false;
    return true;
}

bool sameBank(const composer::Bank& a, const composer::Bank& b) {
    for (int i = 0; i < composer::slotCount; ++i)
        if (!sameClip(a[i], b[i]))
            return false;
    return true;
}

bool contains(const composer::Clip& clip, const composer::Note& note) {
    for (int i = 0; i < clip.count; ++i)
        if (sameNote(clip.notes[i], note))
            return true;
    return false;
}

composer::Clip phrase(int base = 60) {
    composer::Clip clip;
    clip.length = 8;
    clip.count = 16;
    clip.seed = 123;
    for (int i = 0; i < clip.count; ++i)
        clip.notes[i] = {i * 0.5, 0.375, base + (i % 4) * 2, 60 + i * 3, false};
    return clip;
}

struct TemporaryMidi {
    juce::File file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getNonexistentChildFile("genetic-workflow-test", ".mid", false);
    explicit TemporaryMidi(const composer::Clip& clip) {
        juce::MidiMessageSequence sequence;
        for (int i = 0; i < clip.count; ++i) {
            const auto& n = clip.notes[i];
            auto on = juce::MidiMessage::noteOn(1, n.pitch, juce::uint8(n.velocity));
            auto off = juce::MidiMessage::noteOff(1, n.pitch);
            on.setTimeStamp(n.start * 960);
            off.setTimeStamp((n.start + n.duration) * 960);
            sequence.addEvent(on);
            sequence.addEvent(off);
        }
        auto end = juce::MidiMessage::endOfTrack();
        end.setTimeStamp(clip.length * 960);
        sequence.addEvent(end);
        sequence.updateMatchedPairs();
        juce::MidiFile midi;
        midi.setTicksPerQuarterNote(960);
        midi.addTrack(sequence);
        juce::FileOutputStream stream(file);
        check(stream.openedOk() && midi.writeTo(stream), "workflow MIDI fixture write");
    }
    ~TemporaryMidi() { file.deleteFile(); }
};

void testHarmonyAndReferences() {
    WorkflowProcessor processor;
    const auto original = processor.bankSnapshot();
    check(processor.setCustomHarmony("Cm:4 Fm:2 G7:2").wasOk(), "custom harmony parse");
    const auto text = processor.customHarmonyText();
    check(text.isNotEmpty() && processor.harmonySnapshot().count == 3, "custom harmony stored");
    check(processor.setCustomHarmony("Cwrong:4").failed(), "invalid chord accepted");
    check(processor.customHarmonyText() == text, "invalid chord replaced harmony");
    check(sameBank(original, processor.bankSnapshot()), "harmony edit replaced melody bank");
    processor.undo();
    check(processor.customHarmonyText().isEmpty(), "harmony missing from undo history");
    processor.redo();
    check(processor.customHarmonyText() == text, "harmony redo failed");
    processor.clearCustomHarmony();
    check(processor.customHarmonyText().isEmpty(), "clear custom harmony");
    processor.undo();
    check(processor.customHarmonyText() == text, "undo clear harmony");

    composer::Clip chords;
    chords.length = 8;
    chords.count = 6;
    chords.notes[0] = {0, 4, 60, 90, false};
    chords.notes[1] = {0, 4, 63, 90, false};
    chords.notes[2] = {0, 4, 67, 90, false};
    chords.notes[3] = {4, 4, 65, 90, false};
    chords.notes[4] = {4, 4, 68, 90, false};
    chords.notes[5] = {4, 4, 72, 90, false};
    TemporaryMidi harmonyFile(chords);
    check(processor.importHarmonyMidi(harmonyFile.file).wasOk(), "harmony MIDI import");
    check(processor.harmonySnapshot().count > 0, "harmony import produced no chords");
    check(sameBank(original, processor.bankSnapshot()), "harmony MIDI replaced melody");

    auto reference = phrase();
    for (int i = 0; i < reference.count; ++i) {
        reference.notes[i].start += i % 2 ? 0.08 : 0;
        reference.notes[i].velocity = i % 2 ? 45 : 115;
    }
    TemporaryMidi grooveFile(reference);
    check(!processor.hasGrooveReference(), "new document already has groove reference");
    check(processor.importGrooveMidi(grooveFile.file).wasOk(), "groove MIDI import");
    check(processor.hasGrooveReference(), "groove reference not retained");
    check(sameBank(original, processor.bankSnapshot()), "groove MIDI replaced melody");
    processor.undo();
    check(!processor.hasGrooveReference(), "groove reference missing from undo history");
    processor.redo();
    check(processor.hasGrooveReference(), "groove reference redo failed");

    juce::MemoryBlock state;
    processor.getStateInformation(state);
    WorkflowProcessor restored;
    restored.setStateInformation(state.getData(), int(state.getSize()));
    check(restored.customHarmonyText() == processor.customHarmonyText(), "harmony state round trip");
    check(restored.hasGrooveReference(), "groove reference state round trip");
    const auto before = processor.selectedClip();
    check(processor.applyGroove(1, 1).wasOk(), "apply groove");
    check(restored.applyGroove(1, 1).wasOk(), "apply restored groove");
    check(sameClip(processor.selectedClip(), restored.selectedClip()), "restored groove changed result");
    check(!sameClip(before, processor.selectedClip()), "groove has no timing or velocity effect");
    processor.undo();
    check(sameClip(before, processor.selectedClip()) && processor.hasGrooveReference(),
          "undo groove application must retain reference");
}

void testPartialEdits() {
    for (auto action : {composer::RegionAction::Simplify, composer::RegionAction::MoreMovement,
                        composer::RegionAction::Answer, composer::RegionAction::Resolve}) {
        WorkflowProcessor processor;
        auto original = phrase();
        original.notes[6].locked = true;
        processor.setClip(0, original);
        processor.selectClip(0);
        check(processor.applyRegion(2, 4, action).wasOk(), "region transform failed");
        const auto transformed = processor.selectedClip();
        for (int i = 0; i < original.count; ++i) {
            const auto& n = original.notes[i];
            if (n.start < 2 || n.start >= 4 || n.locked)
                check(contains(transformed, n), "region edit changed outside or locked event");
        }
        for (int i = 0; i < transformed.count; ++i) {
            const auto& n = transformed.notes[i];
            if (n.start < 2 || n.start >= 4)
                check(contains(original, n), "region edit inserted note outside selection");
        }
        processor.undo();
        check(sameClip(processor.selectedClip(), original), "undo region transform");
    }
}

void testArrangementHarmonyPosition() {
    WorkflowProcessor processor;
    composer::Clip motif;
    motif.length = 4;
    motif.count = 4;
    for (int i = 0; i < 4; ++i)
        motif.notes[i] = {double(i), 0.75, 64, 90, false};
    processor.setClip(0, motif);
    check(processor.setCustomHarmony("C:4 F#:4").wasOk(), "arrangement harmony setup");
    check(processor.previewArrangement().wasOk(), "arrangement with a longer chord loop");
    const auto result = processor.displayedClip();
    check(result.notes[result.count - 1].pitch % 12 == 6,
          "arrangement ending must resolve against its absolute chord position");
}

void testDraftsAndArrangement() {
    WorkflowProcessor processor;
    auto rhythm = phrase(48), pitches = phrase(72), accents = phrase(60);
    for (int i = 0; i < accents.count; ++i)
        accents.notes[i].velocity = i % 2 ? 50 : 110;
    processor.setClip(0, rhythm);
    processor.setClip(1, pitches);
    processor.setClip(2, accents);
    processor.selectClip(0);
    const auto original = processor.bankSnapshot();
    check(processor.previewCombination(0, 1, 2).wasOk(), "combination preview failed");
    check(processor.hasDraft() && processor.draftDescription().isNotEmpty(), "draft missing");
    const auto draft = processor.displayedClip();
    check(sameBank(original, processor.bankSnapshot()) && sameClip(processor.selectedClip(), rhythm),
          "preview changed committed bank before acceptance");
    check(draft.count == rhythm.count, "combination lost rhythm count");
    for (int i = 0; i < draft.count; ++i)
        check(draft.notes[i].start == rhythm.notes[i].start &&
                  draft.notes[i].duration == rhythm.notes[i].duration &&
                  draft.notes[i].pitch == pitches.notes[i].pitch &&
                  draft.notes[i].velocity == accents.notes[i].velocity,
              "combination failed to use chosen source traits");

    processor.captureComparison();
    juce::MemoryBlock state;
    processor.getStateInformation(state);
    WorkflowProcessor restored;
    restored.setStateInformation(state.getData(), int(state.getSize()));
    check(!restored.hasDraft() && sameBank(original, restored.bankSnapshot()),
          "unaccepted preview leaked into saved bank");
    check(restored.hasComparison() && sameClip(restored.comparisonClip(), draft) && !restored.isComparing(),
          "captured comparison must persist independently of transient preview");
    restored.clearComparison();
    check(!restored.hasComparison(), "clear comparison failed");
    TemporaryMidi exported(phrase());
    check(processor.exportMidi(exported.file).wasOk(), "draft export failed");
    setParameter(restored, "snap", 0);
    check(restored.importMidi(exported.file).wasOk(), "draft export reimport failed");
    check(restored.selectedClip().notes[0].pitch == draft.notes[0].pitch, "export ignored displayed draft");

    processor.discardDraft();
    check(!processor.hasDraft() && sameBank(original, processor.bankSnapshot()),
          "discard altered committed bank");
    check(processor.previewCombination(0, 1, 2).wasOk(), "second combination preview failed");
    processor.applyDraft();
    check(!processor.hasDraft() && sameClip(processor.selectedClip(), draft), "apply draft failed");
    processor.undo();
    check(sameBank(original, processor.bankSnapshot()), "undo draft acceptance failed");

    check(processor.previewArrangement().wasOk(), "arrangement preview failed");
    const auto arranged = processor.displayedClip();
    check(arranged.length == rhythm.length * 4 && arranged.count <= composer::maxNotes,
          "arrangement extent invalid");
    check(sameBank(original, processor.bankSnapshot()), "arrangement preview committed early");
    for (int i = 0; i < rhythm.count; ++i)
        check(contains(arranged, rhythm.notes[i]), "arrangement changed initial A segment");
    for (int segment = 0; segment < 4; ++segment) {
        bool populated = false;
        for (int i = 0; i < arranged.count; ++i)
            populated |= arranged.notes[i].start >= segment * rhythm.length &&
                         arranged.notes[i].start < (segment + 1) * rhythm.length;
        check(populated, "arrangement has an empty segment");
    }
    processor.discardDraft();
    auto tooLong = rhythm;
    tooLong.length = 33;
    processor.setClip(0, tooLong);
    check(processor.previewArrangement().failed() && !processor.hasDraft(),
          "arrangement exceeding 128 beats was not rejected");
    auto tooDense = rhythm;
    tooDense.count = 129;
    for (int i = 0; i < tooDense.count; ++i)
        tooDense.notes[i] = {i * 0.05, 0.025, 60, 96, false};
    processor.setClip(0, tooDense);
    check(processor.previewArrangement().failed() && !processor.hasDraft(),
          "arrangement exceeding note capacity was not rejected");
}

void testRangeExport() {
    WorkflowProcessor processor;
    setParameter(processor, "gate", 1);
    setParameter(processor, "snap", 0);
    setParameter(processor, "swing", 0);
    composer::Clip clip;
    clip.length = 4;
    clip.count = 3;
    clip.notes[0] = {0.5, 2, 60, 91, false};
    clip.notes[1] = {2, 0.75, 64, 92, false};
    clip.notes[2] = {3, 0.75, 67, 93, false};
    processor.setClip(0, clip);
    processor.selectClip(0);
    TemporaryMidi output(clip);
    check(processor.exportMidiRange(output.file, 1, 3).wasOk(), "range MIDI export failed");
    juce::FileInputStream stream(output.file);
    juce::MidiFile midi;
    check(midi.readFrom(stream) && midi.getTimeFormat() > 0, "range export is not musical MIDI");
    int ons = 0, offs = 0;
    bool clippedStart = false, clippedEnd = false, shiftedSecond = false, correctLength = false;
    for (int track = 0; track < midi.getNumTracks(); ++track) {
        const auto* sequence = midi.getTrack(track);
        for (int i = 0; i < sequence->getNumEvents(); ++i) {
            const auto& message = sequence->getEventPointer(i)->message;
            const auto beat = message.getTimeStamp() / midi.getTimeFormat();
            if (message.isNoteOn()) {
                ++ons;
                clippedStart |= message.getNoteNumber() == 60 && std::abs(beat) < 1e-8;
                shiftedSecond |= message.getNoteNumber() == 64 && std::abs(beat - 1) < 1e-8;
            }
            if (message.isNoteOff()) {
                ++offs;
                clippedEnd |= message.getNoteNumber() == 60 && std::abs(beat - 1.5) < 1e-8;
            }
            if (message.isEndOfTrackMetaEvent())
                correctLength |= std::abs(beat - 2) < 1e-8;
        }
    }
    check(ons == 2 && offs == 2 && clippedStart && clippedEnd && shiftedSecond && correctLength,
          "range export must clip crossing notes, rebase starts and preserve selected duration");
    check(sameClip(clip, processor.selectedClip()), "range export altered source phrase");
    check(processor.exportMidiRange(output.file, 2, 2).failed(), "empty export range accepted");
}

struct WorkflowPlayHead : juce::AudioPlayHead {
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

struct MidiEvent {
    int sample = 0, pitch = 0;
    bool on = false;
};

void testLoopSwitching(bool followHost, bool comparison, bool draft = false) {
    WorkflowProcessor processor;
    WorkflowPlayHead head;
    processor.prepareToPlay(48000, 128);
    setParameter(processor, "snap", 0);
    setParameter(processor, "swing", 0);
    setParameter(processor, "gate", 1);
    setParameter(processor, "tempo", 120);
    setParameter(processor, "monitor", 0);
    setParameter(processor, "follow", followHost ? 1 : 0);
    setParameter(processor, "quantiseSwitch", 1);
    composer::Clip a;
    a.length = 1;
    a.count = 1;
    a.notes[0] = {0, 1, 60, 100, false};
    auto b = a;
    b.notes[0].pitch = 67;
    processor.setClip(0, a);
    processor.setClip(1, b);
    processor.selectClip(0);
    if (draft) {
        auto source = a;
        source.notes[0].pitch = 72;
        processor.setClip(2, source);
        check(processor.previewCombination(0, 2, 0).wasOk(), "loop test draft creation");
    }
    if (comparison) {
        processor.captureComparison();
        check(processor.hasComparison() && sameClip(processor.comparisonClip(), a),
              "comparison capture failed");
        processor.setClip(0, b);
    }
    if (followHost)
        processor.setPlayHead(&head);
    else
        processor.setPreview(true);

    std::vector<MidiEvent> events;
    int elapsed = 0;
    auto render = [&] {
        head.beat = double(elapsed) / 24000.0;
        juce::AudioBuffer<float> audio(2, 128);
        juce::MidiBuffer midi;
        processor.processBlock(audio, midi);
        for (const auto metadata : midi) {
            const auto message = metadata.getMessage();
            if (message.isNoteOnOrOff())
                events.push_back(
                    {elapsed + metadata.samplePosition, message.getNoteNumber(), message.isNoteOn()});
        }
        elapsed += 128;
    };
    render();
    check(processor.isPlaying() && processor.audibleIndex() == 0, "workflow playback start");
    if (comparison) {
        processor.toggleComparison();
        check(processor.isComparing() && !processor.audibleComparison(),
              "A/B request should await loop boundary");
    } else {
        processor.selectClip(1);
        if (draft)
            check(!processor.hasDraft(), "changing variation should discard unaccepted preview");
        check(processor.selectedIndex() == 1 && processor.audibleIndex() == 0 && processor.queuedIndex() == 1,
              "selection must edit new slot while old slot plays to loop boundary");
    }
    while (elapsed < 23936)
        render();
    check(processor.audibleIndex() == 0 && !processor.audibleComparison(),
          "queued change happened before loop boundary");
    render();
    const int oldPitch = comparison ? 67 : draft ? 72 : 60, newPitch = comparison ? 60 : 67;
    bool oldOff = false, newOn = false;
    int oldOns = 0, newOns = 0;
    for (const auto& event : events) {
        if (event.on && event.pitch == oldPitch)
            ++oldOns;
        if (event.on && event.pitch == newPitch) {
            ++newOns;
            newOn |= std::abs(event.sample - 24000) <= 1;
        }
        if (!event.on && event.pitch == oldPitch)
            oldOff |= std::abs(event.sample - 24000) <= 1;
    }
    check(oldOns == 1 && newOns == 1 && oldOff && newOn,
          "loop switch must release old note and start new phrase at actual next loop");
    check(comparison ? processor.audibleComparison()
                     : processor.audibleIndex() == 1 && processor.queuedIndex() == -1,
          "audible selection did not acknowledge completed switch");
    if (followHost) {
        head.playing = false;
        render();
        check(!processor.isPlaying(), "host stop after queued switch");
        bool released = false;
        for (const auto& event : events)
            released |= !event.on && event.pitch == newPitch && event.sample >= 24064;
        check(released, "host stop left switched note active");
    }
    processor.setPlayHead(nullptr);
}

void testMidiTriggerLeavesComparison() {
    WorkflowProcessor processor;
    processor.prepareToPlay(48000, 128);
    setParameter(processor, "snap", 0);
    setParameter(processor, "gate", 1);
    setParameter(processor, "monitor", 0);
    setParameter(processor, "quantiseSwitch", 1);
    auto a = phrase(60), b = phrase(72);
    processor.setClip(0, a);
    processor.setClip(1, b);
    processor.selectClip(0);
    processor.captureComparison();
    processor.toggleComparison();
    processor.setPreview(true);
    juce::AudioBuffer<float> audio(2, 128);
    juce::MidiBuffer midi;
    processor.processBlock(audio, midi);
    check(processor.audibleComparison(), "trigger comparison fixture did not start A");
    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOn(1, 49, juce::uint8(100)), 10);
    processor.processBlock(audio, midi);
    bool oldOff = false, selectedOn = false;
    for (const auto event : midi) {
        const auto message = event.getMessage();
        oldOff |=
            message.isNoteOff() && message.getNoteNumber() == a.notes[0].pitch && event.samplePosition == 10;
        selectedOn |=
            message.isNoteOn() && message.getNoteNumber() == b.notes[0].pitch && event.samplePosition == 10;
    }
    check(oldOff && selectedOn && !processor.isComparing() && !processor.audibleComparison() &&
              processor.audibleIndex() == 1,
          "MIDI trigger must leave A/B and play requested bank slot immediately");
}
} // namespace

void runWorkflowTests() {
    testHarmonyAndReferences();
    testPartialEdits();
    testDraftsAndArrangement();
    testArrangementHarmonyPosition();
    testRangeExport();
    testLoopSwitching(false, false);
    testLoopSwitching(true, false);
    testLoopSwitching(false, true);
    testLoopSwitching(true, true);
    testLoopSwitching(false, false, true);
    testLoopSwitching(true, false, true);
    testMidiTriggerLeavesComparison();
}
