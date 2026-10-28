#include "PluginProcessor.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {
using Processor = GeneticVSTComposerJUCEAudioProcessor;

void check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

void parameter(Processor& p, const char* id, float value) {
    auto* param = p.parameters.getParameter(id);
    check(param != nullptr, "chord parameter missing");
    param->setValueNotifyingHost(param->convertTo0to1(value));
}

bool sameClip(const composer::Clip& a, const composer::Clip& b) {
    if (a.count != b.count || a.length != b.length || a.grid != b.grid || a.numerator != b.numerator ||
        a.denominator != b.denominator || a.favourite != b.favourite || a.seed != b.seed)
        return false;
    for (int i = 0; i < a.count; ++i) {
        const auto& x = a.notes[i];
        const auto& y = b.notes[i];
        if (x.start != y.start || x.duration != y.duration || x.pitch != y.pitch ||
            x.velocity != y.velocity || x.locked != y.locked || x.cents != y.cents)
            return false;
    }
    return true;
}

bool sameBank(const composer::Bank& a, const composer::Bank& b) {
    for (int i = 0; i < composer::slotCount; ++i)
        if (!sameClip(a[i], b[i]))
            return false;
    return true;
}

void validateVoicing(const composer::Clip& clip, const composer::Harmony& harmony) {
    check(harmony.count > 0 && clip.count >= harmony.count * 3,
          "chord progression has no polyphonic voicing");
    check(clip.length == harmony.length, "voicing length does not match its harmony");
    for (int i = 0; i < clip.count; ++i) {
        const auto& note = clip.notes[i];
        check(note.pitch >= 0 && note.pitch <= 127 && note.cents == 0 && note.velocity > 0 &&
                  note.velocity <= 127 && note.start >= 0 && note.duration > 0 &&
                  note.start + note.duration <= clip.length + 1e-9,
              "invalid or microtonal chord voicing");
        check(i == 0 || clip.notes[i - 1].start <= note.start, "chord voicing is not sorted");
        bool chordStart = false;
        for (int j = 0; j < harmony.count; ++j)
            chordStart |= std::abs(note.start - harmony.chords[j].start) < 1e-9;
        check(chordStart, "voiced note does not start at a chord boundary");
        for (int j = 0; j < i; ++j)
            check(clip.notes[j].pitch != note.pitch ||
                      clip.notes[j].start + clip.notes[j].duration <= note.start + 1e-9,
                  "voicing contains overlapping copies of the same MIDI key");
    }
}

struct TemporaryMidi {
    juce::File file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getNonexistentChildFile("genetic-chord-test", ".mid", false);
    ~TemporaryMidi() { file.deleteFile(); }
};

void testDocumentAndHistory() {
    auto p = std::make_unique<Processor>();
    const auto bank = p->bankSnapshot();
    check(p->setTimeSignature("7/8").wasOk(), "chord meter setup failed");
    parameter(*p, "bars", 1);        // Two bars, seven quarter-note beats.
    parameter(*p, "chordLength", 0); // Explicitly follow the melody length.
    parameter(*p, "chordRhythm", 1);
    parameter(*p, "chordCentre", 36);
    parameter(*p, "chordOriginality", 0.7f);
    check(p->generateChords().wasOk(), "chord generation failed");
    const auto harmony = p->harmonySnapshot();
    const auto first = p->chordClipSnapshot();
    const auto text = p->customHarmonyText();
    validateVoicing(first, harmony);
    check(harmony.count == 2 && first.length == 7 && first.numerator == 7 && first.denominator == 8,
          "chord generation ignored bar count, harmonic rhythm or meter");
    check(sameBank(bank, p->bankSnapshot()), "generating chords replaced the melody bank");

    p->undo();
    check(p->harmonySnapshot().count == 0 && p->chordClipSnapshot().count == 0 &&
              sameBank(bank, p->bankSnapshot()),
          "undo chord generation did not restore the previous document");
    p->redo();
    check(p->customHarmonyText() == text && sameClip(first, p->chordClipSnapshot()),
          "redo regenerated instead of restoring the exact voicing");

    parameter(*p, "chordCentre", 72);
    parameter(*p, "chordVoicing", 2);
    check(p->revoiceChords().wasOk(), "revoicing failed");
    const auto revoiced = p->chordClipSnapshot();
    validateVoicing(revoiced, p->harmonySnapshot());
    check(p->customHarmonyText() == text && !sameClip(first, revoiced),
          "revoicing changed chord identities or ignored its register controls");
    check(sameBank(bank, p->bankSnapshot()), "revoicing altered the melody bank");
    p->undo();
    check(sameClip(first, p->chordClipSnapshot()), "undo revoice lost the exact previous voicing");
    p->redo();
    check(sameClip(revoiced, p->chordClipSnapshot()), "redo revoice lost the exact new voicing");

    juce::MemoryBlock state;
    p->getStateInformation(state);
    const auto xml = juce::AudioProcessor::getXmlFromBinary(state.getData(), int(state.getSize()));
    check(xml && xml->getIntAttribute("version") == 5, "current document must use state schema version 5");
    auto restored = std::make_unique<Processor>();
    check(restored->setCustomHarmony("F#:1").wasOk(), "restore sentinel harmony failed");
    restored->setStateInformation(state.getData(), int(state.getSize()));
    check(restored->customHarmonyText() == text && sameClip(revoiced, restored->chordClipSnapshot()) &&
              sameBank(bank, restored->bankSnapshot()),
          "saved document did not restore exact chord voicings and melody bank");

    p->clearCustomHarmony();
    check(p->harmonySnapshot().count == 0 && p->chordClipSnapshot().count == 0,
          "clearing harmony left an orphaned chord voicing");
    p->undo();
    check(p->customHarmonyText() == text && sameClip(revoiced, p->chordClipSnapshot()),
          "undo clearing harmony lost the voiced chords");
    p->redo();
    check(p->chordClipSnapshot().count == 0, "redo clearing harmony left voiced chords");

    check(p->setCustomHarmony("C:0.75 F#m7:0.5 G7:0.75").wasOk(), "manual chord editing failed");
    const auto edited = p->chordClipSnapshot();
    validateVoicing(edited, p->harmonySnapshot());
    check(edited.length == 2 && !sameClip(edited, revoiced), "manual harmony did not rebuild voicings");
    check(p->setCustomHarmony("Cwrong:4").failed() && sameClip(edited, p->chordClipSnapshot()),
          "invalid chord edit changed voiced notes");
    check(p->setCustomHarmony("   ").wasOk() && p->chordClipSnapshot().count == 0,
          "empty chord text did not clear the voicing");
    check(sameBank(bank, p->bankSnapshot()), "chord document operations changed melody notes");

    // Version 2 had no custom harmony; version 3 stored harmony text but no voiced clip.
    for (int version : {2, 3}) {
        auto legacy = juce::ValueTree::fromXml(*xml);
        legacy.setProperty("version", version, nullptr);
        for (int i = legacy.getNumChildren() - 1; i >= 0; --i) {
            const auto child = legacy.getChild(i);
            if (!child.hasType("Parameters") && !child.hasType("Clip") && !child.hasType("Groove") &&
                !child.hasType("Comparison"))
                legacy.removeChild(i, nullptr);
        }
        legacy.setProperty("harmony", version == 3 ? "D:2 G7:2" : "", nullptr);
        auto params = legacy.getChildWithName("Parameters");
        params.getChildWithProperty("id", "snap").setProperty("value", 1.0f, nullptr);
        for (const char* id :
             {"chordOriginality", "chordRhythm", "chordVoicing", "chordCentre", "chordResolve"})
            params.removeChild(params.getChildWithProperty("id", id), nullptr);
        juce::MemoryBlock oldState;
        const auto oldXml = legacy.createXml();
        juce::AudioProcessor::copyXmlToBinary(*oldXml, oldState);
        restored->setStateInformation(state.getData(), int(state.getSize()));
        restored->setStateInformation(oldState.getData(), int(oldState.getSize()));
        check(sameBank(bank, restored->bankSnapshot()), "legacy state restore damaged melody notes");
        check(restored->parameters.getRawParameterValue("snap")->load() == (version == 3 ? 0 : 1),
              "legacy Fit to scale migration did not preserve its effective harmony-dependent state");
        const auto oldChords = restored->chordClipSnapshot();
        if (version == 2)
            check(restored->harmonySnapshot().count == 0 && oldChords.count == 0,
                  "version 2 restore retained chords from the previously open document");
        else {
            check(restored->harmonySnapshot().count == 2 && restored->harmonySnapshot().length == 4,
                  "version 3 harmony was not restored");
            check(oldChords.count == 0 || oldChords.length == 4,
                  "version 3 restore retained stale voicings from a different progression");
        }
    }
    // Schema 4 stored exact voicings but silently bypassed Fit to scale whenever harmony was present.
    auto voicedLegacy = juce::ValueTree::fromXml(*xml);
    voicedLegacy.setProperty("version", 4, nullptr);
    voicedLegacy.getChildWithName("Parameters")
        .getChildWithProperty("id", "snap")
        .setProperty("value", 1.0f, nullptr);
    const auto voicedLegacyXml = voicedLegacy.createXml();
    juce::MemoryBlock voicedLegacyState;
    juce::AudioProcessor::copyXmlToBinary(*voicedLegacyXml, voicedLegacyState);
    restored->setStateInformation(voicedLegacyState.getData(), int(voicedLegacyState.getSize()));
    check(restored->parameters.getRawParameterValue("snap")->load() == 0 &&
              sameClip(revoiced, restored->chordClipSnapshot()) && sameBank(bank, restored->bankSnapshot()),
          "schema 4 migration changed voicings or enabled previously bypassed Fit to scale");
}

void testPlainChordMidiExport() {
    auto p = std::make_unique<Processor>();
    check(p->setTimeSignature("11/16").wasOk(), "chord export meter setup failed");
    check(p->setCustomHarmony("C:0.75 F#m7:0.5 G7:0.75").wasOk(), "chord export fixture failed");
    const auto expected = p->chordClipSnapshot();
    parameter(*p, "tuning", 2);
    parameter(*p, "microEnabled", 1);
    parameter(*p, "microOutput", 1);
    parameter(*p, "snap", 1);
    parameter(*p, "swing", 0.75f);
    parameter(*p, "gate", 0.1f);
    TemporaryMidi file;
    check(p->exportChordMidi(file.file).wasOk(), "chord MIDI export failed");
    auto stream = file.file.createInputStream();
    juce::MidiFile midi;
    check(stream && midi.readFrom(*stream), "chord MIDI export cannot be read");
    check(midi.getTimeFormat() > 0, "chord MIDI export lost musical time");
    const double ppq = midi.getTimeFormat();
    std::vector<composer::Note> notes;
    bool foundMeter = false, foundEnd = false;
    for (int track = 0; track < midi.getNumTracks(); ++track) {
        const auto* sequence = midi.getTrack(track);
        for (const auto* event : *sequence) {
            const auto& message = event->message;
            check(!message.isPitchWheel() && !message.isController(),
                  "plain chord MIDI contains microtonal bends or MPE/RPN setup");
            if (message.isTimeSignatureMetaEvent()) {
                int numerator = 0, denominator = 0;
                message.getTimeSignatureInfo(numerator, denominator);
                foundMeter |= numerator == 11 && denominator == 16;
            }
            if (message.isEndOfTrackMetaEvent())
                foundEnd |= std::abs(message.getTimeStamp() / ppq - expected.length) < 1e-9;
            if (message.isNoteOn()) {
                check(message.getChannel() == 1 && event->noteOffObject,
                      "chord MIDI note lacks an ordinary channel-1 note-off");
                check(event->noteOffObject->message.getChannel() == 1, "chord MIDI note-off changed channel");
                notes.push_back(
                    {message.getTimeStamp() / ppq,
                     (event->noteOffObject->message.getTimeStamp() - message.getTimeStamp()) / ppq,
                     message.getNoteNumber(), int(message.getVelocity()), false, 0});
            }
        }
    }
    check(foundMeter && foundEnd && notes.size() == size_t(expected.count),
          "chord MIDI lost its meter, exact loop length or notes");
    std::vector<bool> matched(notes.size());
    for (int i = 0; i < expected.count; ++i) {
        const auto& wanted = expected.notes[i];
        bool found = false;
        for (size_t j = 0; j < notes.size(); ++j) {
            const auto& actual = notes[j];
            if (!matched[j] && actual.pitch == wanted.pitch && actual.velocity == wanted.velocity &&
                std::abs(actual.start - wanted.start) <= 1.0 / ppq &&
                std::abs(actual.duration - wanted.duration) <= 1.0 / ppq) {
                matched[j] = found = true;
                break;
            }
        }
        check(found && wanted.cents == 0, "exported chords differ from the exact voiced clip");
    }
    check(sameClip(expected, p->chordClipSnapshot()), "MIDI export mutated the saved voicing");
}

void testGeneratedHarmonyConditionsMelodies() {
    auto p = std::make_unique<Processor>();
    parameter(*p, "root", 0);
    parameter(*p, "scale", 0);
    parameter(*p, "bars", 1);
    parameter(*p, "chordOriginality", 1);
    parameter(*p, "chordRhythm", 2);
    parameter(*p, "chordResolve", 0);
    check(p->generateChords().wasOk(), "conditioning chord generation failed");
    const auto harmony = p->harmonySnapshot();
    const auto voicing = p->chordClipSnapshot();
    const auto text = p->customHarmonyText();
    check(harmony.count >= 2, "conditioning test requires a changing harmony");
    parameter(*p, "mode", 0);
    parameter(*p, "tension", 0);
    parameter(*p, "mutation", 0.35f);
    parameter(*p, "customSearch", 1);
    parameter(*p, "searchPopulation", 24);
    parameter(*p, "searchGenerations", 4);
    p->generate(false);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (p->isGenerating() && std::chrono::steady_clock::now() < deadline)
        juce::Thread::sleep(1);
    check(!p->isGenerating(), "melody conditioned on generated chords timed out");
    check(p->customHarmonyText() == text && sameClip(voicing, p->chordClipSnapshot()),
          "melody generation replaced the generated harmonic sequence");
    // Intervals define the nine supported chord qualities, independently of register/inversion.
    const int intervals[9][4]{{0, 4, 7, -1}, {0, 3, 7, -1}, {0, 3, 6, -1}, {0, 4, 8, -1}, {0, 4, 7, 10},
                              {0, 4, 7, 11}, {0, 3, 7, 10}, {0, 2, 7, -1}, {0, 5, 7, -1}};
    for (const auto& clip : p->bankSnapshot()) {
        check(clip.count > 0, "conditioned melody bank contains an empty phrase");
        for (int i = 0; i < clip.count; ++i) {
            const auto& note = clip.notes[i];
            const double phase = std::fmod(note.start, harmony.length);
            int chordIndex = 0;
            while (chordIndex + 1 < harmony.count && harmony.chords[chordIndex + 1].start <= phase + 1e-8)
                ++chordIndex;
            const auto& chord = harmony.chords[chordIndex];
            const int pitchClass = (note.pitch - chord.root + 120) % 12;
            bool belongs = false;
            for (int interval : intervals[chord.quality])
                belongs |= interval >= 0 && interval == pitchClass;
            check(belongs, "zero-tension melody ignored an actual generated chord at its onset");
        }
    }
}

void testInvalidVoicingRestoreIsAtomic() {
    auto p = std::make_unique<Processor>();
    check(p->setCustomHarmony("C:2 F:2").wasOk(), "invalid-state fixture harmony failed");
    p->selectClip(2);
    parameter(*p, "root", 0);
    const auto bank = p->bankSnapshot();
    const auto voicing = p->chordClipSnapshot();
    const auto harmony = p->customHarmonyText();
    const auto revision = p->documentRevision();
    const bool canUndo = p->canUndo();
    juce::MemoryBlock state;
    p->getStateInformation(state);
    const auto xml = juce::AudioProcessor::getXmlFromBinary(state.getData(), int(state.getSize()));
    check(xml != nullptr, "invalid-state fixture serialization failed");
    auto corrupt = juce::ValueTree::fromXml(*xml);
    auto voicedNotes = corrupt.getChildWithName("ChordVoicing");
    check(voicedNotes.isValid() && voicedNotes.getNumChildren() > 0, "saved chord voicing is missing");
    voicedNotes.getChild(0).setProperty("pitch", 61, nullptr); // C# cannot belong to the initial C major.
    // Other fields are valid but different: none may leak into the live document on rejection.
    corrupt.getChildWithName("Clip").getChild(0).setProperty("velocity", 1, nullptr);
    corrupt.getChildWithName("Parameters")
        .getChildWithProperty("id", "root")
        .setProperty("value", 8.0, nullptr);
    corrupt.setProperty("selected", 7, nullptr);
    const auto corruptXml = corrupt.createXml();
    juce::MemoryBlock badState;
    juce::AudioProcessor::copyXmlToBinary(*corruptXml, badState);
    p->setStateInformation(badState.getData(), int(badState.getSize()));
    check(p->customHarmonyText() == harmony && sameClip(voicing, p->chordClipSnapshot()) &&
              sameBank(bank, p->bankSnapshot()) && p->selectedIndex() == 2 &&
              p->parameters.getRawParameterValue("root")->load() == 0 && p->documentRevision() == revision &&
              p->canUndo() == canUndo,
          "invalid chord pitch class was accepted or partially changed the document");
}

struct Event {
    int sample;
    juce::MidiMessage message;
};

std::vector<Event> render(Processor& p, int blocks, int& elapsed, float* peak = nullptr) {
    std::vector<Event> events;
    juce::AudioBuffer<float> audio(2, 128);
    for (int block = 0; block < blocks; ++block) {
        juce::MidiBuffer midi;
        p.processBlock(audio, midi);
        for (const auto event : midi) {
            check(event.samplePosition >= 0 && event.samplePosition < 128,
                  "chord playback emitted an out-of-block MIDI timestamp");
            events.push_back({elapsed + event.samplePosition, event.getMessage()});
        }
        if (peak)
            *peak = std::max(*peak, audio.getMagnitude(0, 128));
        elapsed += 128;
    }
    return events;
}

void checkScheduledNotes(const std::vector<Event>& events, const composer::Clip& clip, int horizon) {
    constexpr double samplesPerBeat = 24000; // 48 kHz, 120 BPM.
    std::vector<bool> matched(events.size());
    for (double loop = 0; loop * samplesPerBeat < horizon; loop += clip.length)
        for (int i = 0; i < clip.count; ++i) {
            const auto& note = clip.notes[i];
            for (bool on : {true, false}) {
                const int when =
                    int(std::llround((loop + note.start + (on ? 0 : note.duration)) * samplesPerBeat));
                if (when >= horizon)
                    continue;
                bool found = false;
                for (size_t j = 0; j < events.size(); ++j) {
                    const auto& event = events[j];
                    if (!matched[j] && (on ? event.message.isNoteOn() : event.message.isNoteOff()) &&
                        std::abs(event.sample - when) <= 1 && event.message.getNoteNumber() == note.pitch &&
                        event.message.getChannel() == 1 &&
                        (!on || event.message.getVelocity() == note.velocity)) {
                        matched[j] = found = true;
                        break;
                    }
                }
                check(found, "chord preview changed an exact note, velocity, duration or loop timestamp");
            }
        }
    for (size_t i = 0; i < events.size(); ++i)
        if (events[i].message.isNoteOnOrOff())
            check(matched[i], "chord preview emitted an extra melody note or unmatched note-off");
}

void testChordPlaybackAndReturningToMelody() {
    auto p = std::make_unique<Processor>();
    composer::Clip melody;
    melody.length = 2;
    melody.count = 1;
    melody.notes[0] = {0, 2, 100, 105, false, -50};
    p->setClip(0, melody);
    p->selectClip(0);
    check(p->setCustomHarmony("C:0.75 F#m7:0.5 G7:0.75").wasOk(), "chord playback fixture failed");
    const auto chords = p->chordClipSnapshot();
    p->prepareToPlay(48000, 128);
    parameter(*p, "tempo", 120);
    parameter(*p, "tuning", 2);
    parameter(*p, "microEnabled", 1);
    parameter(*p, "microOutput", 1);
    parameter(*p, "snap", 1);
    parameter(*p, "swing", 0.75f);
    parameter(*p, "gate", 0.1f);
    parameter(*p, "follow", 1);
    parameter(*p, "monitor", 0);
    parameter(*p, "volume", 0);
    p->startChordAudition();
    check(p->isAuditioningChords() && p->parameters.getRawParameterValue("follow")->load() == 0,
          "chord audition did not leave Follow DAW mode");
    int elapsed = 0;
    float peak = 0;
    // Include the next loop's first notes, not just the initial chord.
    const int blocks = int(std::ceil(chords.length * 24000 / 128)) + 2;
    const auto events = render(*p, blocks, elapsed, &peak);
    check(p->isPlaying() && peak > 0, "chord audition is silent");
    for (const auto& event : events)
        check(!event.message.isPitchWheel() && !event.message.isController(),
              "chord audition followed the melody's microtonal output mode");
    checkScheduledNotes(events, chords, elapsed);
    check(sameClip(melody, p->selectedClip()), "auditioning chords replaced the melody");

    p->panic();
    const auto stopped = render(*p, 2, elapsed);
    int offs = 0;
    for (const auto& event : stopped) {
        check(!event.message.isNoteOn(), "panic restarted chord playback");
        offs += event.message.isNoteOff();
    }
    check(offs >= 3 && !p->isPlaying() && !p->isAuditioningChords(),
          "panic did not release the polyphonic chord preview");

    p->startChordAudition();
    render(*p, 1, elapsed);
    p->setPreview(false);
    const auto stopButton = render(*p, 2, elapsed);
    check(!p->isPlaying() && !p->isAuditioningChords(), "ordinary Stop left chord audition active");
    for (const auto& event : stopButton)
        check(!event.message.isNoteOn(), "Stop restarted a chord");

    // Switching directly to the melody must release all chord voices and restore MPE.
    p->startChordAudition();
    render(*p, 1, elapsed);
    parameter(*p, "snap", 0);
    parameter(*p, "swing", 0);
    parameter(*p, "gate", 1);
    p->startAudition();
    const auto returned = render(*p, 2, elapsed);
    int chordOffs = 0, melodyOns = 0;
    bool melodyBend = false;
    for (const auto& event : returned) {
        chordOffs += event.message.isNoteOff() && event.message.getChannel() == 1;
        if (event.message.isNoteOn()) {
            ++melodyOns;
            check(event.message.getNoteNumber() == 100 && event.message.getChannel() > 1,
                  "returning from chords did not resume the original melody in MPE");
        }
        melodyBend |= event.message.isPitchWheel() && event.message.getChannel() > 1 &&
                      event.message.getPitchWheelValue() < 8192;
    }
    check(chordOffs >= 3 && melodyOns == 1 && melodyBend && !p->isAuditioningChords(),
          "switching to melody lost chord note-offs or microtonal configuration");

    // A normal variation trigger is also an explicit return to melody playback.
    p->startChordAudition();
    render(*p, 1, elapsed);
    juce::AudioBuffer<float> triggerAudio(2, 128);
    juce::MidiBuffer triggerMidi;
    triggerMidi.addEvent(juce::MidiMessage::noteOn(1, 48, juce::uint8(100)), 23);
    p->processBlock(triggerAudio, triggerMidi);
    int triggerOns = 0, triggerOffs = 0;
    bool triggerBend = false;
    for (const auto event : triggerMidi) {
        const auto message = event.getMessage();
        if (message.isNoteOn()) {
            ++triggerOns;
            check(event.samplePosition == 23 && message.getNoteNumber() == 100 && message.getChannel() > 1,
                  "variation trigger did not return from chords to melody at its MIDI timestamp");
        }
        triggerOffs += message.isNoteOff() && message.getChannel() == 1 && event.samplePosition == 23;
        triggerBend |= message.isPitchWheel() && message.getChannel() > 1 &&
                       message.getPitchWheelValue() < 8192 && event.samplePosition == 23;
    }
    elapsed += 128;
    check(triggerOns == 1 && triggerOffs >= 3 && triggerBend && !p->isAuditioningChords(),
          "variation trigger left chord voices or failed to restore MPE");
    p->panic();
    render(*p, 1, elapsed);
}
} // namespace

void runChordPluginTests() {
    {
        auto p = std::make_unique<Processor>();
        parameter(*p, "bars", 0); // A one-bar riff must not force a one-chord progression.
        parameter(*p, "quality", 0);
        const auto oldBank = p->bankSnapshot();
        check(p->generateChords().wasOk() && p->harmonySnapshot().count == 4 &&
                  p->harmonySnapshot().length == 16 && sameBank(oldBank, p->bankSnapshot()),
              "default chord duration still silently follows a one-bar melody");
        const auto harmony = p->customHarmonyText();
        p->generate(false, true);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        while (p->isGenerating() && std::chrono::steady_clock::now() < deadline)
            juce::Thread::sleep(1);
        check(!p->isGenerating() && p->customHarmonyText() == harmony,
              "Melody from chords changed or failed to consume the active sequence");
        for (const auto& clip : p->bankSnapshot())
            check(clip.length == 16 && clip.count > 0, "melody stopped after the first chord");
        check(p->searchSummary().contains("40/40") && p->searchSummary().contains("2624"),
              "finished generation has no persistent actual-work summary");
        check(p->setCustomHarmony("Cm:1.5 G7:2").wasOk(), "partial bar chord setup");
        p->generate(false, true);
        const auto partialDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        while (p->isGenerating() && std::chrono::steady_clock::now() < partialDeadline)
            juce::Thread::sleep(1);
        check(!p->isGenerating() && p->selectedClip().length == 3.5,
              "Melody from chords rounded a custom sequence to whole bars");
    }
    testDocumentAndHistory();
    testGeneratedHarmonyConditionsMelodies();
    testInvalidVoicingRestoreIsAtomic();
    testPlainChordMidiExport();
    testChordPlaybackAndReturningToMelody();
}
