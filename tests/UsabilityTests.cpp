#include "PluginEditor.h"
#include <chrono>
#include <memory>
#include <stdexcept>

namespace {
using Processor = GeneticVSTComposerJUCEAudioProcessor;
void check(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
void parameter(Processor& p, const char* id, float value) {
    auto* param = p.parameters.getParameter(id);
    param->setValueNotifyingHost(param->convertTo0to1(value));
}
void awaitSearch(Processor& p) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (p.isGenerating() && std::chrono::steady_clock::now() < deadline)
        juce::Thread::sleep(1);
    check(!p.isGenerating(), "search timeout");
}
juce::MouseEvent mouse(PianoRoll& roll, juce::Point<float> point, juce::Point<float> down,
                       bool dragged = false) {
    return {juce::Desktop::getInstance().getMainMouseSource(),
            point,
            juce::ModifierKeys::leftButtonModifier,
            1,
            0,
            0,
            0,
            0,
            &roll,
            &roll,
            juce::Time::getCurrentTime(),
            down,
            juce::Time::getCurrentTime(),
            1,
            dragged};
}
} // namespace
void runUsabilityTests() {
    {
        Processor scoped;
        scoped.setInterfaceLanguage(1);
        auto editor = std::make_unique<GeneticVSTComposerJUCEAudioProcessorEditor>(scoped);
        auto findButton = [&](const juce::String& name) -> juce::Button* {
            for (auto* child : editor->getChildren())
                if (auto* button = dynamic_cast<juce::Button*>(child);
                    button && button->getButtonText() == name)
                    return button;
            return nullptr;
        };
        auto findRoll = [&]() -> PianoRoll* {
            for (auto* child : editor->getChildren())
                if (auto* value = dynamic_cast<PianoRoll*>(child))
                    return value;
            return nullptr;
        };
        auto* micro = findButton(juce::String::fromUTF8("Mikrotony"));
        check(micro != nullptr && micro->isVisible() && findRoll() != nullptr,
              "microtones are not visible on the main composition panel");
        check(micro->getBounds().getRight() < findRoll()->getBounds().getX(),
              "microtones are still buried inside the advanced workflow");
        check(findButton("Loop range") == nullptr && findButton(juce::String::fromUTF8("Zapętl fragment")) == nullptr,
              "a separate loop toggle is still required");
        auto* allTime = findButton(juce::String::fromUTF8("Cała fraza"));
        check(allTime && !allTime->isVisible(), "clear selection is shown without a selection");
        const auto revision = scoped.documentRevision();
        findRoll()->setRegion(2, 4);
        auto range = scoped.playbackRegion();
        check(range.enabled && range.start == 2 && range.end == 4 && range.slot == scoped.selectedIndex(),
              "ruler selection did not automatically enable its playback loop");
        check(findButton(juce::String::fromUTF8("Optymalizuj fragment")) != nullptr && allTime->isVisible(),
              "the main action and selection reset did not follow the ruler scope");
        editor->showWorkflowPage(5);
        check(micro->isVisible() && allTime->isVisible(), "changing workflow tabs hid the main controls");
        check(scoped.documentRevision() == revision, "a preview range invalidated the document revision");
        scoped.setPlaybackRegion(3, 5, true);
        editor->refreshDisplay();
        check(findRoll()->regionStart() == 3 && findRoll()->regionEnd() == 5 && findRoll()->hasTimeSelection(),
              "the editor did not refresh a processor-owned range");
        editor.reset();
        editor = std::make_unique<GeneticVSTComposerJUCEAudioProcessorEditor>(scoped);
        check(findRoll()->regionStart() == 3 && findRoll()->regionEnd() == 5 && findRoll()->hasTimeSelection(),
              "closing the editor lost the active loop selection");
        parameter(scoped, "customSearch", 1);
        parameter(scoped, "searchPopulation", 16);
        parameter(scoped, "searchGenerations", 2);
        const auto before = scoped.bankSnapshot();
        auto* optimise = findButton(juce::String::fromUTF8("Optymalizuj fragment"));
        check(optimise && optimise->getBounds().getX() < findRoll()->getBounds().getX(),
              "range optimisation is not the main evolution action");
        optimise->onClick();
        awaitSearch(scoped);
        const auto after = scoped.bankSnapshot();
        for (int i = 1; i < composer::slotCount; ++i)
            check(before[i].seed == after[i].seed && before[i].count == after[i].count,
                  "the contextual evolution action changed another variation");
        allTime = findButton(juce::String::fromUTF8("Cała fraza"));
        allTime->onClick();
        check(!scoped.playbackRegion().enabled && !findRoll()->hasTimeSelection() && !allTime->isVisible(),
              "Whole phrase did not clear the loop and restore the default scope");
        check(findButton(juce::String::fromUTF8("Rozwiń wybrany")) != nullptr,
              "clearing the range did not restore phrase evolution");
        findRoll()->setRegion(2, 4);
        findRoll()->mouseDoubleClick(mouse(*findRoll(), {100, 10}, {100, 10}));
        check(!findRoll()->hasTimeSelection() && !scoped.playbackRegion().enabled && !scoped.previewRequested(),
              "double-clicking the ruler auditioned notes instead of clearing the selection");
        scoped.prepareToPlay(48000, 128);
        findRoll()->setRegion(0, 0.5);
        parameter(scoped, "evolveEachLoop", 1);
        scoped.setPreview(true);
        const auto loopSerial = scoped.searchSerial();
        const auto loopBank = scoped.bankSnapshot();
        juce::AudioBuffer<float> loopAudio(2, 128);
        juce::MidiBuffer loopMidi;
        for (int block = 0; block < 200 && scoped.searchSerial() == loopSerial; ++block) {
            loopMidi.clear();
            scoped.processBlock(loopAudio, loopMidi);
            scoped.serviceMessageThread();
        }
        check(scoped.searchSerial() != loopSerial, "Each loop did not evolve the time selection");
        awaitSearch(scoped);
        parameter(scoped, "evolveEachLoop", 0);
        scoped.setPreview(false);
        const auto loopResult = scoped.bankSnapshot();
        check(scoped.selectedIndex() == 0, "Each loop switched away from the selected section");
        for (int i = 1; i < composer::slotCount; ++i)
            check(loopResult[i].seed == loopBank[i].seed && loopResult[i].count == loopBank[i].count,
                  "Each loop regenerated other variations while a time selection was active");
        findRoll()->selectAllTime();
        for (const auto& text : {"Słuchaj A", "Wyczyść A", "Zakończ nagranie"}) {
            auto* button = findButton(juce::String::fromUTF8(text));
            check(button && !button->isVisible(), "an unavailable secondary action is still visible");
        }
        scoped.captureComparison();
        parameter(scoped, "capturePhrase", 1);
        editor->refreshDisplay();
        check(findButton(juce::String::fromUTF8("Słuchaj A"))->isVisible() &&
                  findButton(juce::String::fromUTF8("Zakończ nagranie"))->isVisible(),
              "secondary actions were not revealed when needed");
        parameter(scoped, "capturePhrase", 0);
        findButton("PL")->onClick();
        editor->refreshDisplay();
        check(findButton("Play") && findButton("Microtones"), "returning to English left primary actions in Polish");
    }
    auto p = std::make_unique<Processor>();
    // Original normalized automation ranges are unchanged; extensions have separate IDs.
    check(dynamic_cast<juce::AudioParameterChoice*>(p->parameters.getParameter("meter"))->choices.size() == 5,
          "legacy meter automation range changed");
    check(dynamic_cast<juce::AudioParameterChoice*>(p->parameters.getParameter("progression"))
                  ->choices.size() == 5,
          "legacy progression automation range changed");
    check(dynamic_cast<juce::AudioParameterChoice*>(p->parameters.getParameter("bars"))->choices.size() == 4,
          "legacy phrase-length automation range changed");
    check(dynamic_cast<juce::AudioParameterChoice*>(p->parameters.getParameter("scale"))->choices.size() == 9,
          "legacy scale automation range changed");
    p->setPhraseBarCount(6);
    check(p->phraseBarCount() == 6 && int(p->parameters.getRawParameterValue("bars")->load()) == 2,
          "six-bar length rewrote the original Length parameter");
    p->setPhraseBarCount(4);
    p->setScaleChoice(10);
    check(p->scaleIndex() == 10, "Phrygian was not selected");
    p->setScaleChoice(1);
    check(p->scaleIndex() == 1 && p->parameters.getRawParameterValue("extendedScale")->load() < 0.5f,
          "returning to a base scale left the extension active");
    p->applyPreset(2);
    check(p->parameters.getRawParameterValue("arousal")->load() > 0.7f &&
              int(p->parameters.getRawParameterValue("phraseType")->load()) == 0,
          "rhythmic starting point did not raise arousal");
    p->setChordLengthMenu(8);
    check(p->chordBarCount() == 12, "12-bar chord length was not applied");
    p->setChordLengthMenu(1);
    check(p->chordBarCount() == p->phraseBarCount(), "Follow melody ignored the melody length");
    check(p->setTimeSignature("11/16").wasOk(), "typed meter rejected");
    check(p->setTimeSignature("9/7").failed() && p->timeSignature().numerator == 11,
          "invalid meter changed settings");
    p->setProgressionPreset(21);
    parameter(*p, "customSearch", 1);
    parameter(*p, "searchPopulation", 24);
    parameter(*p, "searchGenerations", 9);
    parameter(*p, "crossover", 0.75f);
    parameter(*p, "bars", 0);
    p->generate(false);
    awaitSearch(*p);
    check(p->selectedClip().numerator == 11 && p->selectedClip().denominator == 16 &&
              p->selectedClip().length == 2.75,
          "processor did not use custom meter");
    check(p->searchDescription().contains("9/9") && p->searchDescription().contains("Evaluations 240"),
          "processor search statistics missing");
    juce::MemoryBlock state;
    p->getStateInformation(state);
    auto restored = std::make_unique<Processor>();
    restored->setStateInformation(state.getData(), int(state.getSize()));
    check(restored->timeSignature().numerator == 11 && restored->timeSignature().denominator == 16 &&
              restored->progressionIndex() == 21,
          "extended meter/progression not restored");
    check(restored->configuredSearchBudget().population == 24 &&
              restored->configuredSearchBudget().generations == 9,
          "search budget not restored");
    auto legacyXml = juce::AudioProcessor::getXmlFromBinary(state.getData(), int(state.getSize()));
    auto* legacyParameters = legacyXml->getChildByName("Parameters");
    for (const char* id :
         {"meterOverride", "meterNumerator", "meterDenominator", "extendedProgression", "customSearch",
          "searchPopulation", "searchGenerations", "crossover", "auditionSound"})
        legacyParameters->removeChildElement(legacyParameters->getChildByAttribute("id", id), true);
    legacyParameters->getChildByAttribute("id", "meter")->setAttribute("value", 3.0);
    legacyParameters->getChildByAttribute("id", "progression")->setAttribute("value", 4.0);
    juce::MemoryBlock legacyState;
    juce::AudioProcessor::copyXmlToBinary(*legacyXml, legacyState);
    restored->setStateInformation(legacyState.getData(), int(legacyState.getSize()));
    check(restored->timeSignature().numerator == 7 && restored->timeSignature().denominator == 8 &&
              restored->progressionIndex() == 4 && restored->configuredSearchBudget().population == 128 &&
              restored->configuredSearchBudget().generations == 100,
          "old sessions must clear extended overrides and retain original meter/progression indices");
    const auto beforeOversize = p->selectedClip();
    p->setTimeSignature("32/1");
    parameter(*p, "bars", 1);
    p->generate(false);
    awaitSearch(*p);
    check(p->statusText().contains("exceed 128") &&
              composer::similarity(beforeOversize, p->selectedClip()) == 1,
          "oversize generation silently truncated or replaced the bank");
    p->setInterfaceLanguage(1);
    check(p->statusText().contains("taktów") && !p->statusText().contains("exceed 128"),
          "Polish interface left the status in English");
    p->setInterfaceLanguage(0);
    check(p->statusText().contains("exceed 128"), "English interface did not restore the status");
    p->setStateInformation(state.getData(), int(state.getSize()));
    check(restored->setCustomHarmony("Cm:4 Ab:4 Eb:4 Bb:4").wasOk(), "custom harmony entry failed");
    restored->setProgressionPreset(5);
    check(restored->customHarmonyText().isEmpty() && restored->progressionIndex() == 5,
          "preset did not replace custom harmony");
    restored->setTimeSignature("4/4");
    check(restored->timeSignature().numerator == 4 && restored->timeSignature().denominator == 4,
          "return to original meter failed");

    auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getNonexistentChildFile("composer-meter", ".mid", false);
    check(p->exportMidi(file).wasOk(), "custom meter MIDI export failed");
    auto stream = file.createInputStream();
    juce::MidiFile midiFile;
    check(stream && midiFile.readFrom(*stream), "exported MIDI not readable");
    bool signatureFound = false;
    for (int t = 0; t < midiFile.getNumTracks(); ++t)
        for (const auto* event : *midiFile.getTrack(t))
            if (event->message.isTimeSignatureMetaEvent()) {
                int n = 0, d = 0;
                event->message.getTimeSignatureInfo(n, d);
                signatureFound |= n == 11 && d == 16;
            }
    check(signatureFound, "custom meter missing from exported MIDI");
    file.deleteFile();
    // Starting audition must be audible even after silent MIDI-only/host-follow use.
    for (int sound = 0; sound < 3; ++sound) {
        p->prepareToPlay(48000, 512);
        parameter(*p, "follow", 1);
        parameter(*p, "monitor", 0);
        parameter(*p, "volume", 0);
        parameter(*p, "auditionSound", float(sound));
        p->startAudition();
        juce::AudioBuffer<float> audio(2, 512);
        juce::MidiBuffer midi;
        float peak = 0;
        // Restore queues one block of note-offs before new playback may begin. A generated
        // phrase may also open with a rest of up to an eighth note, so listen for one beat.
        for (int block = 0; block < 50 && peak == 0; ++block) {
            midi.clear();
            p->processBlock(audio, midi);
            peak = std::max(peak, audio.getMagnitude(0, 512));
        }
        check(p->isPlaying() && peak > 0,
              "audition did not enable an audible internal sound");
        p->setPreview(false);
        midi.clear();
        p->processBlock(audio, midi);
        check(!p->isPlaying(), "audition Stop did not stop preview");
    }
    // Gesture routing can be checked without invoking an OS drag or touching the DAW.
    PianoRoll roll;
    roll.setSize(850, 270);
    roll.setVisible(true);
    roll.setClip(p->selectedClip());
    int edits = 0, drags = 0, auditions = 0, regions = 0;
    roll.onEdit = [&](composer::Clip) { ++edits; };
    roll.onMidiDrag = [&] { ++drags; };
    roll.onAudition = [&] { ++auditions; };
    roll.onRegionChange = [&](double, double) { ++regions; };
    const juce::Point<float> start{400, 150};
    roll.mouseDown(mouse(roll, start, start));
    roll.mouseUp(mouse(roll, start, start));
    check(edits == 0 && drags == 0, "plain piano-roll click altered notes or started drag");
    roll.mouseDown(mouse(roll, start, start));
    roll.mouseDrag(mouse(roll, {430, 160}, start, true));
    roll.mouseDrag(mouse(roll, {450, 160}, start, true));
    roll.mouseUp(mouse(roll, {450, 160}, start, true));
    check(drags == 1 && edits == 0, "piano-roll drag must export once and preserve notes");
    roll.mouseDoubleClick(mouse(roll, start, start));
    check(auditions == 1, "piano-roll double-click did not audition");
    roll.mouseDown(mouse(roll, {100, 10}, {100, 10}));
    roll.mouseDrag(mouse(roll, {200, 10}, {100, 10}, true));
    roll.mouseUp(mouse(roll, {200, 10}, {100, 10}, true));
    check(regions >= 2 && drags == 1, "range selection triggered a file drag");
    roll.mouseDown(mouse(roll, {849, 10}, {849, 10}));
    roll.mouseUp(mouse(roll, {849, 10}, {849, 10}));
    check(roll.regionStart() < roll.regionEnd(), "ruler click at the right edge selected an empty range");
    roll.setEditable(false);
    roll.mouseDown(mouse(roll, start, start));
    roll.mouseDrag(mouse(roll, {430, 160}, start, true));
    roll.mouseUp(mouse(roll, {430, 160}, start, true));
    check(drags == 2 && edits == 0, "draft piano roll must still export");
    const auto plain = juce::File::getSpecialLocation(juce::File::tempDirectory)
                           .getNonexistentChildFile("humanize-off", ".mid", false);
    const auto varied = juce::File::getSpecialLocation(juce::File::tempDirectory)
                            .getNonexistentChildFile("humanize-on", ".mid", false);
    parameter(*p, "humanize", 0);
    check(p->exportMidi(plain).wasOk(), "plain export failed");
    parameter(*p, "humanize", 1);
    check(p->exportMidi(varied).wasOk() && p->exportMidi(varied).wasOk(), "humanized export failed");
    juce::MemoryBlock first, second, repeat;
    check(plain.loadFileAsData(first) && varied.loadFileAsData(second), "could not read humanize MIDI");
    check(p->exportMidi(varied).wasOk() && varied.loadFileAsData(repeat), "humanize repeat export failed");
    check(first != second && second == repeat, "humanize was not deterministic or did not change the MIDI");
    plain.deleteFile();
    varied.deleteFile();
    parameter(*p, "humanize", 0);
}
