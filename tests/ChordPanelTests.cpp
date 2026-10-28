#include "ChordComposerPanel.h"
#include "PluginEditor.h"
#include <cmath>
#include <memory>
#include <set>
#include <stdexcept>

namespace {
using Processor = GeneticVSTComposerJUCEAudioProcessor;
void check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
void parameter(Processor& processor, const char* id, float value) {
    auto* p = processor.parameters.getParameter(id);
    check(p != nullptr, "chord panel parameter missing");
    p->setValueNotifyingHost(p->convertTo0to1(value));
}
PianoRoll* findRoll(juce::Component& component) {
    if (auto* roll = dynamic_cast<PianoRoll*>(&component))
        return roll;
    for (auto* child : component.getChildren())
        if (auto* roll = findRoll(*child))
            return roll;
    return nullptr;
}
juce::TextButton* findButton(juce::Component& component, const juce::String& text) {
    if (auto* button = dynamic_cast<juce::TextButton*>(&component))
        if (button->getButtonText() == text)
            return button;
    for (auto* child : component.getChildren())
        if (auto* button = findButton(*child, text))
            return button;
    return nullptr;
}

juce::ToggleButton* findToggle(juce::Component& component, const juce::String& text) {
    if (auto* button = dynamic_cast<juce::ToggleButton*>(&component))
        if (button->getButtonText() == text)
            return button;
    for (auto* child : component.getChildren())
        if (auto* button = findToggle(*child, text))
            return button;
    return nullptr;
}
ChordComposerPanel* findChordPanel(juce::Component& component) {
    if (auto* panel = dynamic_cast<ChordComposerPanel*>(&component))
        return panel;
    for (auto* child : component.getChildren())
        if (auto* panel = findChordPanel(*child))
            return panel;
    return nullptr;
}
PianoRoll* findVisibleRoll(juce::Component& component) {
    if (!component.isVisible())
        return nullptr;
    if (auto* roll = dynamic_cast<PianoRoll*>(&component))
        return roll;
    for (auto* child : component.getChildren())
        if (auto* roll = findVisibleRoll(*child))
            return roll;
    return nullptr;
}

juce::MouseEvent mouse(PianoRoll& roll, juce::Point<float> position, juce::Point<float> down,
                       bool dragged = false, bool shift = false) {
    return {juce::Desktop::getInstance().getMainMouseSource(),
            position,
            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier |
                               (shift ? juce::ModifierKeys::shiftModifier : 0)),
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
bool sameNotes(const composer::Clip& a, const composer::Clip& b) {
    if (a.count != b.count || a.length != b.length)
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
void checkLayout(ChordComposerPanel& panel) {
    auto* roll = findRoll(panel);
    check(roll != nullptr, "chord panel piano roll missing");
    auto visibleInside = [&](juce::Component& component) {
        check(component.isVisible() && component.getWidth() > 0 && component.getHeight() > 0,
              "chord panel control is hidden or has empty bounds");
        for (auto* ancestor = component.getParentComponent(); ancestor != nullptr;
             ancestor = ancestor->getParentComponent()) {
            check(ancestor->isVisible(), "chord panel control has a hidden parent");
            if (ancestor == &panel)
                break;
        }
        const auto bounds = panel.getLocalArea(&component, component.getLocalBounds());
        check(panel.getLocalBounds().expanded(1).contains(bounds),
              "chord panel scaled control falls outside its window");
    };
    visibleInside(*roll);
    for (const char* name : {"New chords only", "Melody from these chords", "New chords + melody", "Revoice",
                             "Play", "Play together", "Drag chord MIDI", "Export..."}) {
        auto* button = findButton(panel, name);
        check(button != nullptr, "chord panel primary action missing");
        visibleInside(*button);
        check(!panel.getLocalArea(button, button->getLocalBounds())
                   .intersects(panel.getLocalArea(roll, roll->getLocalBounds())),
              "chord panel primary action overlaps the piano roll");
    }
}
} // namespace

void runChordPanelTests() {
    auto processor = std::make_unique<Processor>();
    processor->prepareToPlay(48000, 512);
    composer::Clip melody;
    melody.length = 16;
    melody.count = 2;
    melody.notes[0] = {0, 1, 100, 96, false};
    melody.notes[1] = {8, 1, 101, 96, false};
    processor->setClip(0, melody);
    processor->selectClip(0);

    ChordComposerPanel panel(*processor);
    panel.setVisible(true);
    panel.setSize(940, 660);
    panel.refreshDisplay();
    checkLayout(panel);
    panel.setSize(760, 540);
    panel.refreshDisplay();
    checkLayout(panel);

    auto* generate = findButton(panel, "New chords only");
    check(generate->isEnabled() && bool(generate->onClick), "chord generation action is unavailable");
    generate->onClick();
    panel.refreshDisplay();
    const auto generated = processor->chordClipSnapshot();
    check(generated.count >= 3 && processor->harmonySnapshot().count > 0,
          "Generate chords button did not create active voiced harmony");
    check(sameNotes(melody, processor->selectedClip()), "Generate chords changed existing melody notes");
    checkLayout(panel);

    const auto harmony = processor->customHarmonyText();
    parameter(*processor, "chordCentre", 42);
    auto* revoice = findButton(panel, "Revoice");
    check(revoice->isEnabled() && bool(revoice->onClick), "Revoice action is unavailable");
    revoice->onClick();
    panel.refreshDisplay();
    const auto voiced = processor->chordClipSnapshot();
    check(!sameNotes(generated, voiced) && harmony == processor->customHarmonyText(),
          "Revoice button did not change register while preserving the progression");
    check(sameNotes(melody, processor->selectedClip()), "Revoice changed melody notes");

    auto* roll = findRoll(panel);
    int edits = 0, drags = 0;
    roll->onEdit = [&](composer::Clip) { ++edits; };
    roll->onMidiDrag = [&] { ++drags; };
    const juce::Point<float> start{float(roll->getWidth()) * 0.55f, float(roll->getHeight()) * 0.55f};
    roll->mouseDown(mouse(*roll, start, start));
    roll->mouseUp(mouse(*roll, start, start));
    check(edits == 0 && drags == 0, "chord piano-roll click altered notes or started a drag");
    roll->mouseDown(mouse(*roll, start, start));
    roll->mouseDrag(mouse(*roll, start.translated(20, 0), start, true));
    roll->mouseDrag(mouse(*roll, start.translated(35, 0), start, true));
    roll->mouseUp(mouse(*roll, start.translated(35, 0), start, true));
    check(drags == 1 && edits == 0, "chord piano-roll gesture did not export once without editing");
    roll->mouseDown(mouse(*roll, start, start, false, true));
    roll->mouseUp(mouse(*roll, start, start, false, true));
    check(edits == 0 && sameNotes(voiced, processor->chordClipSnapshot()) &&
              sameNotes(melody, processor->selectedClip()),
          "read-only chord roll changed chord or melody notes");

    // Invoke the panel's real audition action, but render audio without a native window or a DAW.
    auto* audition = findButton(panel, "Play");
    check(audition != nullptr && audition->isEnabled(), "chord audition action is unavailable");
    audition->onClick();
    check(processor->isAuditioningChords() && processor->previewRequested(),
          "chord audition action did not select the chord source");
    juce::AudioBuffer<float> audio(2, 512);
    juce::MidiBuffer midi;
    std::set<int> heard, expected;
    for (int i = 0; i < voiced.count; ++i)
        if (std::abs(voiced.notes[i].start) < 1.0e-8)
            expected.insert(voiced.notes[i].pitch);
    for (int block = 0; block < 2; ++block) {
        midi.clear();
        processor->processBlock(audio, midi);
        for (auto event : midi)
            if (event.getMessage().isNoteOn())
                heard.insert(event.getMessage().getNoteNumber());
    }
    check(processor->audibleChords() && heard == expected,
          "chord panel audition played the wrong source or voicing");
    panel.refreshDisplay();
    auto* stop = findButton(panel, "Stop");
    check(stop != nullptr, "chord audition did not expose a Stop action");
    stop->onClick();
    midi.clear();
    processor->processBlock(audio, midi);
    check(!processor->previewRequested() && !processor->isPlaying(),
          "chord panel Stop did not stop playback");

    // Navigation stays inside one editor, with a transport directly under each piano roll.
    using Editor = GeneticVSTComposerJUCEAudioProcessorEditor;
    auto editor = std::unique_ptr<Editor>(static_cast<Editor*>(processor->createEditor()));
    editor->setVisible(true);
    for (const auto size : {juce::Point<int>(1240, 900), juce::Point<int>(870, 630)}) {
        editor->setSize(size.x, size.y);
        editor->showMelodyComposer();
        editor->refreshDisplay();
        auto* melodyRoll = findVisibleRoll(*editor);
        auto* melodyPlay = findButton(*editor, "Play");
        auto* chordsToggle = findToggle(*editor, "With chords");
        auto* snap = findToggle(*editor, "Fit to scale");
        check(melodyRoll && melodyPlay && melodyPlay->isVisible() && chordsToggle &&
                  chordsToggle->isVisible() && chordsToggle->isEnabled() && snap && snap->isEnabled(),
              "melody transport or Fit to scale missing with active harmony");
        const auto rollBounds = editor->getLocalArea(melodyRoll, melodyRoll->getLocalBounds());
        const auto playBounds = editor->getLocalArea(melodyPlay, melodyPlay->getLocalBounds());
        check(playBounds.getY() >= rollBounds.getBottom() && editor->getLocalBounds().contains(playBounds),
              "melody Play is not visible below its piano roll");
        const int desktops = juce::Desktop::getInstance().getNumComponents();
        auto* chordsNav = findButton(*editor, "Chords");
        check(chordsNav && bool(chordsNav->onClick), "Chords navigation missing");
        chordsNav->onClick();
        editor->refreshDisplay();
        auto* embedded = findChordPanel(*editor);
        check(embedded && embedded->isVisible() && !melodyRoll->isVisible(),
              "Chords navigation did not replace the melody page in the editor");
        check(juce::Desktop::getInstance().getNumComponents() == desktops,
              "Chords navigation opened a separate native window");
        auto* chordRoll = findVisibleRoll(*embedded);
        auto* chordPlay = findButton(*embedded, "Play");
        check(chordRoll && chordPlay &&
                  embedded->getLocalArea(chordPlay, chordPlay->getLocalBounds()).getY() >=
                      embedded->getLocalArea(chordRoll, chordRoll->getLocalBounds()).getBottom(),
              "chord Play is not below its piano roll");
        auto* melodyNav = findButton(*editor, "Melody");
        check(melodyNav && bool(melodyNav->onClick), "Melody navigation missing");
        melodyNav->onClick();
        check(melodyRoll->isVisible() && !embedded->isVisible(),
              "Melody navigation did not restore the page");
    }
    editor->showWorkflowPage(5);
    auto* micro = findToggle(*editor, "Micro");
    check(micro && micro->isVisible(), "explicit Micro switch missing");
    check(!findButton(*editor, "Apply to range") && !findButton(*editor, "Clear microtones"),
          "manual blanket microtone controls are still exposed");
    editor->showMelodyComposer();
    auto* chordsToggle = findToggle(*editor, "With chords");
    chordsToggle->setToggleState(true, juce::sendNotification);
    auto* melodyPlay = findButton(*editor, "Play");
    melodyPlay->onClick();
    for (int block = 0; block < 2; ++block) {
        midi.clear();
        processor->processBlock(audio, midi);
    }
    check(processor->audibleChordLayer() && !processor->audibleChords(),
          "With chords + Play did not start a simultaneous melody/chord preview");
    editor->showChordComposer();
    check(processor->previewRequested(), "changing tabs stopped combined preview");
    processor->setPreview(false);
    midi.clear();
    processor->processBlock(audio, midi);
    editor->keyPressed(juce::KeyPress(juce::KeyPress::spaceKey));
    midi.clear();
    processor->processBlock(audio, midi);
    check(processor->audibleChords(), "Space on Chords started melody instead of chords");
    editor->keyPressed(juce::KeyPress(juce::KeyPress::spaceKey));
    midi.clear();
    processor->processBlock(audio, midi);
    check(!processor->previewRequested(), "Space did not stop chord preview");
    parameter(*processor, "bars", 0);
    parameter(*processor, "quality", 0);
    const auto retainedHarmony = processor->customHarmonyText();
    auto* melodyFromChords = findButton(panel, "Melody from these chords");
    check(melodyFromChords && bool(melodyFromChords->onClick), "melody dependency action missing");
    melodyFromChords->onClick();
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 15000;
    while (processor->isGenerating() && juce::Time::getMillisecondCounterHiRes() < deadline)
        juce::Thread::sleep(1);
    check(!processor->isGenerating() && processor->customHarmonyText() == retainedHarmony &&
              processor->selectedClip().length == processor->harmonySnapshot().length,
          "Melody from these chords did not preserve/use the full progression");
    auto* pair = findButton(panel, "New chords + melody");
    check(pair && bool(pair->onClick), "one-action chords and melody workflow missing");
    const auto beforePair = processor->documentRevision();
    pair->onClick();
    const auto pairDeadline = juce::Time::getMillisecondCounterHiRes() + 15000;
    while (processor->isGenerating() && juce::Time::getMillisecondCounterHiRes() < pairDeadline)
        juce::Thread::sleep(1);
    check(!processor->isGenerating() && processor->documentRevision() > beforePair &&
              processor->harmonySnapshot().count == 4 && processor->selectedClip().length == 16,
          "New chords + melody did not create a complete pair");
}
