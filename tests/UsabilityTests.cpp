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
    auto p = std::make_unique<Processor>();
    // Original normalized automation ranges are unchanged; extensions have separate IDs.
    check(dynamic_cast<juce::AudioParameterChoice*>(p->parameters.getParameter("meter"))->choices.size() == 5,
          "legacy meter automation range changed");
    check(dynamic_cast<juce::AudioParameterChoice*>(p->parameters.getParameter("progression"))
                  ->choices.size() == 5,
          "legacy progression automation range changed");
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
        // Restore queues one block of note-offs before new playback may begin.
        for (int block = 0; block < 2; ++block) {
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
    roll.setEditable(false);
    roll.mouseDown(mouse(roll, start, start));
    roll.mouseDrag(mouse(roll, {430, 160}, start, true));
    roll.mouseUp(mouse(roll, {430, 160}, start, true));
    check(drags == 2 && edits == 0, "draft piano roll must still export");
}
