#include "AllocationGuard.h"
#include "PluginProcessor.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <iostream>
#include <set>
#include <stdexcept>
#include <thread>

namespace {
using Processor = GeneticVSTComposerJUCEAudioProcessor;
void check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
void parameter(Processor& processor, const char* id, float value) {
    auto* p = processor.parameters.getParameter(id);
    p->setValueNotifyingHost(p->convertTo0to1(value));
}
composer::Clip phrase(int count = 8) {
    composer::Clip clip;
    clip.length = 4;
    clip.seed = 45;
    clip.count = count;
    for (int i = 0; i < count; ++i)
        clip.notes[size_t(i)] = {i * 4.0 / count, 3.0 / count, 60 + i % 7, 80 + i % 25, i == 0, 0};
    return clip;
}
bool sameNote(const composer::Note& a, const composer::Note& b) {
    return a.start == b.start && a.duration == b.duration && a.pitch == b.pitch && a.cents == b.cents &&
           a.velocity == b.velocity && a.locked == b.locked;
}
bool samePhrase(const composer::Clip& a, const composer::Clip& b) {
    if (a.count != b.count || a.length != b.length)
        return false;
    for (int i = 0; i < a.count; ++i)
        if (!sameNote(a.notes[size_t(i)], b.notes[size_t(i)]))
            return false;
    return true;
}
void testDocument() {
    auto p = std::make_unique<Processor>();
    const auto source = phrase();
    p->setClip(0, source);
    p->selectClip(0);
    check(p->captureMotif(0, 1).wasOk(), "capture composition motif");
    check(p->compositionSnapshot().hook.count == 2, "motif range was not cropped");
    check(p->setCompositionRole(2).wasOk() && p->setMotifIdentity(0.8f).wasOk(), "composition role/identity");
    check(p->developSelectedMotif(2).wasOk(), "develop motif");
    check(sameNote(p->selectedClip().notes[0], source.notes[0]), "development changed an exact lock");
    p->undo();
    check(samePhrase(p->selectedClip(), source), "motif development undo");
    auto other = source;
    for (int i = 1; i < other.count; ++i)
        other.notes[size_t(i)].pitch += i % 2 ? 9 : -9;
    p->setClip(1, other);
    check(p->preferVariation(0, 1).wasOk(), "explicit pairwise preference");
    check(p->compositionSnapshot().preference.comparisons == 1, "preference was not learned");
    p->setCompositionRole(1);
    check(p->compositionSnapshot().preference.comparisons == 0, "riff preferences leaked into the bass role");
    p->setCompositionRole(2);
    check(p->compositionSnapshot().preference.comparisons == 1, "role switch lost learned riff preferences");
    check(p->setCustomHarmony("C:4").wasOk() && p->createComposition().wasOk(),
          "create independent sections");
    const auto before = p->compositionSnapshot();
    check(before.sections.size() == 4, "composition must have four related sections");
    check(p->updateCompositionSection(1, "Bridge", 3, 0.8f, 0.9f).wasOk(), "edit section metadata");
    check(p->compositionSnapshot().sections[1].melody.length == 12, "independent section duration");
    check(samePhrase(p->compositionSnapshot().sections[0].melody, before.sections[0].melody),
          "section resize changed another section");
    check(p->setCompositionSectionHarmony(1, "Am:4 F:4 G7:4").wasOk(), "independent section harmony");
    check(p->developCompositionSection(1).wasOk(), "develop selected section in harmony");
    check(p->moveCompositionSection(1, 1).wasOk() && p->compositionSnapshot().sections[2].name == "Bridge",
          "section reorder");
    p->undo();
    check(p->compositionSnapshot().sections[1].name == "Bridge", "section reorder undo");
    juce::MemoryBlock state;
    p->getStateInformation(state);
    auto restored = std::make_unique<Processor>();
    restored->setStateInformation(state.getData(), int(state.getSize()));
    const auto workspace = restored->compositionSnapshot();
    check(workspace.sections.size() == 4 && workspace.sections[1].name == "Bridge" &&
              workspace.sections[1].harmony.count == 3 && workspace.preference.comparisons == 1 &&
              workspace.preserveHook && workspace.role == 2 && workspace.hook.count == 2,
          "composition state round trip lost its musical context");
    check(samePhrase(workspace.sections[1].melody, p->compositionSnapshot().sections[1].melody),
          "section notes changed on restore");
    // An invalid workspace must reject the whole state atomically.
    const auto xml = juce::AudioProcessor::getXmlFromBinary(state.getData(), int(state.getSize()));
    auto tree = juce::ValueTree::fromXml(*xml);
    tree.getChildWithName("CompositionWorkspace").setProperty("identity", "nan", nullptr);
    juce::MemoryBlock invalid;
    juce::AudioProcessor::copyXmlToBinary(*tree.createXml(), invalid);
    restored->setStateInformation(invalid.getData(), int(invalid.getSize()));
    check(restored->compositionSnapshot().identity == workspace.identity &&
              restored->compositionSnapshot().sections[1].name == "Bridge",
          "invalid workspace partially committed");
    // Old schemas restore the old musical behavior without carrying new composition state.
    tree = juce::ValueTree::fromXml(*xml);
    tree.setProperty("version", 6, nullptr);
    juce::AudioProcessor::copyXmlToBinary(*tree.createXml(), invalid);
    restored->setStateInformation(invalid.getData(), int(invalid.getSize()));
    check(!restored->compositionSnapshot().enabled && restored->compositionSnapshot().sections.empty(),
          "legacy session activated composition mode");
    p->resetPreferences();
    check(p->compositionSnapshot().preference.comparisons == 0, "reset learned preferences");
}
void testLongExportAndPlayback() {
    auto p = std::make_unique<Processor>();
    p->setClip(0, phrase(200));
    p->selectClip(0);
    check(p->createComposition().wasOk(), "long composition fixture");
    const auto workspace = p->compositionSnapshot();
    int expected = 0;
    double length = 0;
    for (const auto& section : workspace.sections) {
        expected += section.melody.count;
        length += section.melody.length;
    }
    check(expected > composer::maxNotes, "fixture must exceed a single clip capacity");
    const auto file = juce::File::createTempFile(".mid");
    check(p->exportCompositionMidi(file).wasOk(), "whole composition export");
    juce::FileInputStream input(file);
    juce::MidiFile midi;
    check(midi.readFrom(input) && midi.getNumTracks() == 2, "composition must export type 1 MIDI");
    int actual = 0, markers = 0;
    for (int i = 0; i < midi.getTrack(0)->getNumEvents(); ++i) {
        const auto& message = midi.getTrack(0)->getEventPointer(i)->message;
        actual += message.isNoteOn();
        markers += message.isTextMetaEvent() && message.getMetaEventType() == 6;
    }
    check(actual == expected && markers >= 4 && std::abs(midi.getLastTimestamp() / 960 - length) < 1e-6,
          "composition export truncated sections or lost section markers");
    file.deleteFile();

    // A fresh sparse composition makes timing and lifetime assertions unambiguous.
    p->setClip(0, phrase(2));
    check(p->createComposition().wasOk(), "playback composition fixture");
    auto second = p->compositionSnapshot().sections[1].melody;
    second.count = 1;
    second.notes[0] = {0, 1, 79, 100, false, 0};
    check(p->setCompositionSectionClip(1, second).wasOk(), "distinct section onset");
    p->prepareToPlay(8000, 128);
    parameter(*p, "tempo", 120);
    parameter(*p, "auditionReverb", 0);
    p->auditionComposition(true);
    juce::AudioBuffer<float> audio(2, 128);
    juce::MidiBuffer events;
    events.ensureSize(65536);
    std::set<std::pair<int, int>> sounding;
    bool heardSecond = false;
    long allocations = 0;
    for (int block = 0; block < 530; ++block) {
        events.clear();
        {
            composertest::AllocationGuard guard;
            p->processBlock(audio, events);
            allocations += guard.count();
        }
        for (const auto metadata : events) {
            const auto message = metadata.getMessage();
            if (message.isNoteOn()) {
                sounding.insert({message.getChannel(), message.getNoteNumber()});
                if (message.getNoteNumber() == 79 &&
                    std::abs(block * 128 + metadata.samplePosition - 16000) <= 1)
                    heardSecond = true;
            } else if (message.isNoteOff())
                sounding.erase({message.getChannel(), message.getNoteNumber()});
        }
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = 0; sample < 128; ++sample)
                check(std::isfinite(audio.getSample(channel, sample)),
                      "composition emitted non-finite audio");
    }
    check(heardSecond, "section transition was not scheduled at its musical boundary");
    check(sounding.empty() && !p->isCompositionPlaying(), "finished composition left hanging notes");
    check(allocations == 0, "composition playback allocated on the audio thread");
    p->selectCompositionSection(1);
    p->auditionComposition(false);
    events.clear();
    p->processBlock(audio, events);
    check(p->audibleCompositionSection() == 1, "section preview used the wrong section");
    p->panic();
    events.clear();
    p->processBlock(audio, events);
    check(!p->isCompositionPlaying(), "panic did not stop composition preview");
}
} // namespace
void runCompositionWorkflowTests() {
    testDocument();
    testLongExportAndPlayback();
}
void runAiCompositionSmoke() {
    auto p = std::make_unique<GeneticVSTComposerJUCEAudioProcessor>();
    const auto source = phrase();
    p->setClip(0, source);
    p->selectClip(0);
    check(p->captureMotif(0, 1).wasOk(), "AI motif capture");
    p->toggleFavourite(1);
    const auto pinned = p->bankSnapshot()[1];
    p->setPhraseBarCount(2);
    parameter(*p, "customSearch", 1);
    parameter(*p, "searchPopulation", 16);
    parameter(*p, "searchGenerations", 2);
    check(p->generateAiIdeas().wasOk(), "start integrated AI idea generation");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(180);
    while (p->isGenerating() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    check(!p->isGenerating() && p->statusText().startsWith("AI ideas developed"),
          "integrated native AI and genetic search did not complete");
    const auto bank = p->bankSnapshot();
    check(bank[1].favourite && samePhrase(bank[1], pinned), "AI replaced a pinned variation");
    for (int slot = 0; slot < composer::slotCount; ++slot) {
        if (slot == 1)
            continue;
        const auto& clip = bank[size_t(slot)];
        check(std::any_of(clip.notes.begin(), clip.notes.begin() + clip.count,
                          [&](const composer::Note& note) { return sameNote(note, source.notes[0]); }),
              "AI MIDI handoff lost an exact source note lock");
    }
    std::cout << "Real local AI + genetic search: bank publication, pinned variation and exact source locks passed.\n";
}
