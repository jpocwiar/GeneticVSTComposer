#include "PluginEditor.h"
#include "ChordComposerPanel.h"
#include "ChordGenerator.h"
#include "ComposerGuide.h"
#include "EditorFormat.h"
#include "InterfaceText.h"
#include "MicrotonalComposerPanel.h"
#include <cmath>
using namespace palette;
VariationButton::VariationButton(int i) : juce::Button("Variation " + juce::String(i + 1)), slot(i) {
    addAndMakeVisible(pin);
    pin.setTooltip("Pin this variation to preserve it when generating a new bank.");
    pin.onClick = [this] {
        if (onPin)
            onPin();
    };
    setTooltip("Click to select. Double-click to audition. MIDI note " + juce::String(48 + i) +
               " triggers this variation.");
}
void VariationButton::setClip(const composer::Clip& c, bool active, bool showMicrotones) {
    clip = c;
    selected = active;
    microtonal = showMicrotones;
    pin.setButtonText(c.favourite ? "*" : "+");
    pin.setColour(juce::TextButton::textColourOffId, c.favourite ? gold : muted);
    repaint();
}
void VariationButton::setPlaybackState(bool isAudible, bool isQueued) {
    if (audible != isAudible || queued != isQueued) {
        audible = isAudible;
        queued = isQueued;
        repaint();
    }
}
void VariationButton::resized() {
    pin.setBounds(getWidth() - 29, 7, 22, 22);
}
void VariationButton::mouseDoubleClick(const juce::MouseEvent&) {
    if (onAudition)
        onAudition();
}
void VariationButton::advanceAnimation(double beat, float pulse, bool animate, float elapsed) {
    const float amount = animate ? 1.0f - std::exp(-elapsed * 16.0f) : 1.0f;
    const float nextHover = hoverMix + (float(isMouseOver(true)) - hoverMix) * amount;
    const float nextSelect = selectMix + (float(selected) - selectMix) * amount;
    const float nextEnergy = audible ? pulse : 0.0f;
    const bool changed = std::abs(nextHover - hoverMix) > 0.002f ||
                         std::abs(nextSelect - selectMix) > 0.002f ||
                         std::abs(nextEnergy - energy) > 0.005f || (audible && beat != beatPosition);
    hoverMix = nextHover;
    selectMix = nextSelect;
    energy = nextEnergy;
    beatPosition = beat;
    const float pinHover = float(pin.getProperties().getWithDefault("hoverAmount", 0.0f));
    const float nextPin = pinHover + (float(pin.isMouseOver()) - pinHover) * amount;
    pin.getProperties().set("hoverAmount", nextPin);
    if (std::abs(pinHover - nextPin) > 0.002f)
        pin.repaint();
    if (changed)
        repaint();
}
void VariationButton::paintButton(juce::Graphics& g, bool, bool down) {
    auto bounds = getLocalBounds().toFloat().reduced(0.7f);
    const auto tint = queued ? gold : selected ? accent : teal;
    const auto fill =
        panel.interpolatedWith(surface, hoverMix * 0.7f).interpolatedWith(tint, selectMix * 0.07f);
    g.setGradientFill(juce::ColourGradient(fill.brighter(down ? 0.01f : 0.05f), bounds.getTopLeft(),
                                           fill.darker(0.12f), bounds.getBottomLeft(), false));
    g.fillRoundedRectangle(bounds, 8);
    g.setColour(line.interpolatedWith(tint, queued ? 0.75f : selectMix * 0.8f + hoverMix * 0.15f));
    g.drawRoundedRectangle(bounds, 8, 1);
    if (selected || queued || audible) {
        g.setColour(tint.withAlpha(0.7f + energy * 0.25f));
        g.fillRoundedRectangle(bounds.getX() + 9, bounds.getY(), bounds.getWidth() - 18, 2, 1);
    }
    g.setColour(selected ? accent : palette::text.withAlpha(0.8f));
    g.setFont(font(11, true));
    g.drawText(juce::String(slot + 1).paddedLeft('0', 2), 10, 6, 24, 18, juce::Justification::centredLeft);
    g.setFont(font(9));
    g.setColour(muted.withAlpha(0.8f));
    g.drawText(noteName(48 + slot), 36, 6, 40, 18, juce::Justification::centredLeft);
    if (audible || queued) {
        const float x = float(getWidth() - 37);
        g.setColour(tint.withAlpha(0.12f + energy * 0.15f));
        g.fillEllipse(x - 3, 9, 10, 10);
        g.setColour(tint);
        g.fillEllipse(x, 12, 4, 4);
    }
    auto r = bounds.reduced(10, 0).withTop(29).withBottom(bounds.getBottom() - 7);
    int lo = 127, hi = 0;
    for (int i = 0; i < clip.count; ++i) {
        lo = std::min(lo, clip.notes[i].pitch);
        hi = std::max(hi, clip.notes[i].pitch);
    }
    hi = std::max(hi, lo + 12);
    if (audible && beatPosition >= 0) {
        const float x = r.getX() + float(std::fmod(beatPosition, clip.length) / clip.length) * r.getWidth();
        g.setColour(teal.withAlpha(0.45f));
        g.drawLine(x, r.getY(), x, r.getBottom(), 1.0f);
    }
    for (int i = 0; i < clip.count; ++i) {
        const auto& n = clip.notes[i];
        const bool sounding = audible && beatPosition >= n.start && beatPosition < n.start + n.duration;
        g.setColour(n.locked                     ? gold
                    : sounding                   ? palette::text
                    : microtonal && n.cents != 0 ? violet
                                                 : (selected ? accent : teal.withAlpha(0.65f)));
        const float pitch = n.pitch + (microtonal ? n.cents / 100.0f : 0.0f);
        g.fillRoundedRectangle(r.getX() + float(n.start / clip.length) * r.getWidth(),
                               r.getY() + (hi - pitch) / (hi - lo) * std::max(1.0f, r.getHeight() - 3),
                               std::max(2.0f, float(n.duration / clip.length) * r.getWidth() - 1), 2.5f, 1);
    }
}

void TransportButton::setPlaying(bool active) {
    if (playing != active) {
        playing = active;
        setButtonText(active ? "Stop" : "Play");
        repaint();
    }
}
void TransportButton::paintButton(juce::Graphics& g, bool over, bool down) {
    const auto background = findColour(juce::TextButton::buttonColourId);
    const bool light = background.getPerceivedBrightness() > 0.5f;
    getLookAndFeel().drawButtonBackground(g, *this, background, over, down);
    g.setColour(isEnabled() ? (light ? palette::background : palette::accent) : palette::muted);
    const float y = getHeight() * 0.5f;
    if (playing)
        g.fillRoundedRectangle(14, y - 5, 10, 10, 1);
    else {
        juce::Path triangle;
        triangle.startNewSubPath(14, y - 6);
        triangle.lineTo(14, y + 6);
        triangle.lineTo(24, y);
        triangle.closeSubPath();
        g.fillPath(triangle);
    }
    g.setFont(font(13, true));
    g.setColour(isEnabled() ? (light ? palette::background : palette::text) : palette::muted);
    g.drawText(getButtonText(), 34, 0, getWidth() - 39, getHeight(), juce::Justification::centredLeft);
}

using Editor = GeneticVSTComposerJUCEAudioProcessorEditor;
void Editor::attach(juce::ComboBox& box, const char* id) {
    auto* p = dynamic_cast<juce::AudioParameterChoice*>(processor.parameters.getParameter(id));
    box.addItemList(p->choices, 1);
    addAndMakeVisible(box);
    comboAttachments.push_back(std::make_unique<ComboAttachment>(processor.parameters, id, box));
}
void Editor::attach(juce::Slider& s, const char* id, bool rotary) {
    s.setLookAndFeel(&look);
    s.setSliderStyle(rotary ? juce::Slider::RotaryHorizontalVerticalDrag : juce::Slider::LinearHorizontal);
    s.setTextBoxStyle(rotary ? juce::Slider::TextBoxBelow : juce::Slider::TextBoxRight, false,
                      rotary ? 52 : 42, 18);
    if (rotary) {
        s.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f,
                              true);
        s.textFromValueFunction = [](double v) { return juce::String(int(std::round(v * 100))) + "%"; };
    }
    s.setDoubleClickReturnValue(true, processor.parameters.getParameter(id)->convertFrom0to1(
                                          processor.parameters.getParameter(id)->getDefaultValue()));
    addAndMakeVisible(s);
    sliderAttachments.push_back(std::make_unique<SliderAttachment>(processor.parameters, id, s));
    if (rotary) {
        s.textFromValueFunction = [](double v) { return juce::String(int(std::round(v * 100))) + "%"; };
        s.valueFromTextFunction = [](const juce::String& value) { return value.getDoubleValue() / 100.0; };
        s.updateText();
    }
}
void Editor::attach(juce::ToggleButton& b, const char* id) {
    addAndMakeVisible(b);
    buttonAttachments.push_back(std::make_unique<ButtonAttachment>(processor.parameters, id, b));
}
GeneticVSTComposerJUCEAudioProcessorEditor::GeneticVSTComposerJUCEAudioProcessorEditor(
    GeneticVSTComposerJUCEAudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p) {
    setLookAndFeel(&look);
    setOpaque(true);
    setWantsKeyboardFocus(true);
    attach(root, "root");
    addAndMakeVisible(scale);
    scale.addItemList(GeneticVSTComposerJUCEAudioProcessor::extendedScaleNames(), 1);
    scale.onChange = [this] {
        if (scale.getSelectedId() > 0)
            processor.setScaleChoice(scale.getSelectedId() >= 10 ? scale.getSelectedId()
                                                                 : scale.getSelectedId() - 1);
    };
    scale.setTooltip("The main scale guides generation. Extra scales keep the original Scale automation "
                     "range and are stored separately.");
    addAndMakeVisible(bars);
    bars.addItemList({"1 bar", "2 bars", "3 bars", "4 bars", "6 bars", "8 bars", "12 bars", "16 bars"}, 1);
    bars.onChange = [this] {
        constexpr int values[]{1, 2, 3, 4, 6, 8, 12, 16};
        const int id = bars.getSelectedId();
        if (id >= 1 && id <= 8)
            processor.setPhraseBarCount(values[id - 1]);
    };
    bars.setTooltip("Phrase length for the next melody. 3, 6, 12 and 16 bars keep the original Length "
                    "automation on 1, 2, 4 and 8.");
    attach(grid, "grid");
    addAndMakeVisible(meter);
    meter.setEditableText(true);
    for (const auto& m : composer::timeSignatures())
        meter.addItem(juce::String(m.numerator) + "/" + juce::String(m.denominator), meter.getNumItems() + 1);
    meter.setTooltip("Choose a meter or type your own, e.g. 11/8. Numerator 1-32; denominator 1, 2, 4, 8, 16 "
                     "or 32. Applies to new generations.");
    meter.onChange = [this] { report(processor.setTimeSignature(meter.getText())); };
    addAndMakeVisible(grouping);
    grouping.setTooltip("Metric groups inside one bar, such as 2+2+3. The meter default stays in use until you "
                        "choose another grouping.");
    grouping.onChange = [this] {
        if (grouping.getSelectedId() > 0)
            applyGroupingText(grouping.getText());
    };
    addAndMakeVisible(groupingEntry);
    groupingEntry.setFont(font(13));
    groupingEntry.setTextToShowWhenEmpty("2+2+3", muted);
    groupingEntry.setTooltip("Type groups separated by +. Add @8 to count eighths in a quarter meter, for "
                             "example 3+3+2@8.");
    groupingEntry.onReturnKey = [this] { applyGroupingText(groupingEntry.getText()); };
    groupingEntry.onFocusLost = [this] { applyGroupingText(groupingEntry.getText()); };
    addAndMakeVisible(hostMeter);
    attach(hostMeter, "hostMeter");
    hostMeter.setTooltip("While enabled, the host time signature replaces the meter. If the host has none, the "
                         "current meter stays.");
    attach(mode, "mode");
    attach(phraseType, "phraseType");
    phraseType.setTooltip("Riff: a short repeating hook. Motif: a recognisable idea with small changes. "
                          "Melody: a longer phrase with development and an ending.");
    addAndMakeVisible(melodyTab);
    addAndMakeVisible(chordsTab);
    addAndMakeVisible(microtonalTab);
    microtonalTab.getProperties().set("segment", true);
    microtonalTab.onClick = [this] { showMicrotonalComposer(); };
    microtonalTab.setTooltip("Independent EDO and ratio-based generation, with its own pitches and fitness.");
    melodyTab.getProperties().set("segment", true);
    chordsTab.getProperties().set("segment", true);
    melodyTab.onClick = [this] { showMelodyComposer(); };
    chordsTab.onClick = [this] { showChordComposer(); };
    melodyTab.setTooltip("Generate, play and export a melody, riff or motif.");
    chordsTab.setTooltip("Create the harmonic sequence, choose inversions and drag chord MIDI.");
    addAndMakeVisible(harmony);
    for (const auto& progression : composer::progressionPresets())
        harmony.addItem(progression.name, harmony.getNumItems() + 1);
    harmony.addItem("Custom chords...", 1000);
    harmony.addItem("Chord composer...", 1001);
    harmony.onChange = [this] {
        if (harmony.getSelectedId() == 1001) {
            showChordComposer();
            refresh();
        } else if (harmony.getSelectedId() == 1000) {
            showWorkflowPage(1);
            chordText.grabKeyboardFocus();
        } else
            processor.setProgressionPreset(harmony.getSelectedId() - 1);
    };
    attach(quality, "quality");
    attach(auditionSound, "auditionSound");
    auditionSound.setTooltip("Built-in audition tone. These sounds follow microtonal tuning too.");
    attach(customSearch, "customSearch");
    attach(searchPopulation, "searchPopulation");
    attach(searchGenerations, "searchGenerations");
    attach(crossover, "crossover");
    crossover.textFromValueFunction = [](double v) { return juce::String(int(std::round(v * 100))) + "%"; };
    crossover.valueFromTextFunction = [](const juce::String& value) { return value.getDoubleValue() / 100; };
    crossover.updateText();
    quality.setTooltip("Quick: 64 x 40. Balanced: 128 x 100. Deep: 256 x 300. Search tab: custom budget and "
                       "actual statistics.");
    searchPopulation.setTooltip("Individuals in each generation, 16-512. Applies when Custom budget is on.");
    searchGenerations.setTooltip(
        "Evolutionary generations, 1-2000. The initial population is evaluated separately.");
    crossover.setTooltip("Chance of crossing two tournament-selected parents before mutation.");
    addAndMakeVisible(searchDetails);
    searchDetails.onClick = [this] { showWorkflowPage(6); };
    addAndMakeVisible(searchInfo);
    searchInfo.setFont(font(10.5f));
    searchInfo.setColour(juce::Label::textColourId, muted);
    const char* ids[]{"diversity", "dynamics", "tension",  "valence", "arousal",
                      "weirdness", "space",    "mutation", "density"};
    const char* tips[]{
        "Diversity: variety of pitches and intervals while keeping the phrase recognisable.",
        "Dynamics: melodic motion and changes of register. This shapes the notes, not output volume.",
        "Jazziness: chromatic tension and less predictable harmonic colours. Turn Fit to scale off to hear "
        "the original pitches.",
        "Valence: preference for brighter consonance and upward melodic direction; a musical bias, not a "
        "mood measurement.",
        "Arousal: rhythmic energy, faster movement and a wider, higher register.",
        "Weirdness: unexpected rhythmic placement and larger melodic jumps.",
        "Pause Amount: room for silence between notes. Higher settings favour more space.",
        "Evolution: how far Evolve moves from the selected phrase. Locked notes always stay intact.",
        "Density: how many notes the search aims for. Turn up for busier riffs; Pause Amount independently "
        "controls silence."};
    for (int i = 0; i < int(knobs.size()); ++i) {
        attach(knobs[i], ids[i], true);
        knobs[i].setTooltip(tips[i]);
    }
    attach(low, "low");
    attach(high, "high");
    attach(swing, "swing");
    attach(gate, "gate");
    attach(humanize, "humanize");
    humanize.setTooltip("Small timing and velocity variation during playback and export. The piano roll "
                        "keeps the original notes. Zero leaves the performance unchanged.");
    attach(tempo, "tempo");
    attach(volume, "volume");
    low.textFromValueFunction = [](double v) { return noteName(int(v)); };
    high.textFromValueFunction = low.textFromValueFunction;
    low.valueFromTextFunction = [](const juce::String& input) {
        const auto value = input.trim();
        for (int pitch = 0; pitch < 128; ++pitch)
            if (noteName(pitch).equalsIgnoreCase(value))
                return double(pitch);
        return value.getDoubleValue();
    };
    high.valueFromTextFunction = low.valueFromTextFunction;
    low.updateText();
    high.updateText();
    for (auto* s : {&swing, &gate, &volume}) {
        s->textFromValueFunction = [](double v) { return juce::String(int(std::round(v * 100))) + "%"; };
        s->valueFromTextFunction = [](const juce::String& value) { return value.getDoubleValue() / 100.0; };
        s->updateText();
    }
    attach(snap, "snap");
    attach(withChords, "auditionChords");
    withChords.setTooltip("Play the melody and the active chord sequence together through the internal "
                          "sounds. Melody MIDI and chord MIDI remain separate exports.");
    attach(chordMidi, "chordMidiOut");
    chordMidi.setTooltip("While With chords is playing, also send the chord voices as MIDI notes on channel 16. "
                         "Turned off while MPE uses channels 2-16.");
    attach(lockRhythm, "lockRhythm");
    attach(follow, "follow");
    attach(monitor, "monitor");
    attach(quantiseSwitch, "quantiseSwitch");
    follow.setTooltip(
        "Start, stop and seek with the DAW transport. Audition turns this off for independent preview.");
    monitor.setTooltip("Built-in audition sound. Turn off when routing MIDI to another instrument.");
    lockRhythm.setTooltip("Keep all note starts and durations when evolving a phrase.");
    harmony.setTooltip("Choose a progression, enter Custom chords, or open Chord composer for creative "
                       "progressions, voiced chord audition and separate chord MIDI export.");
    mode.setTooltip("Melody on rhythm uses the selected variation as a rhythmic template.");
    preset.addItemList({"Starting point: Balanced", "Airy / spacious", "Rhythmic / driving",
                        "Chromatic / restless", "Bass / restrained"},
                       1);
    preset.setTextWhenNothingSelected("Starting points...");
    addAndMakeVisible(preset);
    preset.onChange = [this] { processor.applyPreset(preset.getSelectedId() - 1); };
    for (auto* b : {&generate, &evolve, &panic, &undo, &redo, &save, &load, &import, &exportBoth, &exportFile,
                    &help})
        addAndMakeVisible(*b);
    addAndMakeVisible(play);
    addAndMakeVisible(drag);
    addAndMakeVisible(roll);
    for (auto* l : {&status, &phraseTitle, &phraseInfo, &tempoInfo}) {
        addAndMakeVisible(*l);
        l->setFont(font(11));
        l->setColour(juce::Label::textColourId, muted);
    }
    phraseTitle.setFont(font(20, true));
    phraseTitle.setColour(juce::Label::textColourId, text);
    phraseInfo.setJustificationType(juce::Justification::centredRight);
    generate.setColour(juce::TextButton::buttonColourId, accent);
    generate.setColour(juce::TextButton::textColourOffId, background);
    play.setColour(juce::TextButton::buttonColourId, accent);
    play.setColour(juce::TextButton::textColourOffId, background);
    generate.onClick = [this] {
        if (processor.isGenerating())
            processor.cancelGeneration();
        else
            processor.generate(false);
    };
    evolve.onClick = [this] {
        if (roll.hasTimeSelection())
            processor.evolveRegion(roll.regionStart(), roll.regionEnd());
        else
            processor.generate(true);
    };
    play.onClick = [this] {
        if (processor.previewRequested() && !processor.isAuditioningChords())
            processor.setPreview(false);
        else
            processor.startAudition();
    };
    play.setTooltip("Audition the phrase with the built-in sound. Enables Internal sound and turns Follow "
                    "DAW off. Click again to stop.");
    panic.onClick = [this] {
        if (microPage)
            processor.stopMicrotonalAudition();
        else
            processor.panic();
    };
    undo.onClick = [this] {
        processor.undo();
        refresh();
    };
    redo.onClick = [this] {
        processor.redo();
        refresh();
    };
    save.onClick = [this] { chooseFile(2); };
    load.onClick = [this] { chooseFile(3); };
    import.onClick = [this] { chooseFile(0); };
    exportBoth.onClick = [this] { chooseFile(7); };
    exportBoth.setTooltip("Export a type 1 MIDI file. Track 1 is the melody, including swing, gate, Fit to "
                          "scale, humanize and MPE. Track 2 is the exact chord voicing on channel 1.");
    exportFile.onClick = [this] { chooseFile(1); };
    drag.onClick = [this] { chooseFile(1); };
    drag.onDrag = [this] { startMidiDrag(); };
    drag.setTooltip("Drag this button onto an instrument track in your DAW. Click to save a MIDI file. "
                    "Export includes swing, gate and Fit to scale.");
    help.onClick = [this] { showGuide(); };
    help.setTooltip("Read the in-plugin guide: Polish / English, examples and shortcuts.");
    roll.onEdit = [this](composer::Clip c) {
        if (!processor.hasDraft())
            processor.setClip(processor.selectedIndex(), c);
    };
    roll.onMidiDrag = [this] { startMidiDrag(&roll); };
    roll.onAudition = [this] { processor.startAudition(); };
    addAndMakeVisible(capturePhrase);
    addAndMakeVisible(finishCapture);
    addAndMakeVisible(evolveEachLoop);
    attach(capturePhrase, "capturePhrase");
    attach(evolveEachLoop, "evolveEachLoop");
    capturePhrase.setTooltip("Record incoming MIDI as a phrase. Notes are not variation triggers while this is "
                             "armed.");
    finishCapture.setTooltip("Turn the captured notes into the selected variation. An empty capture leaves the "
                             "bank unchanged.");
    evolveEachLoop.setTooltip("Evolve at each loop with the current Evolution amount. A time selection limits "
                              "changes to that range; otherwise select the best new variation.");
    finishCapture.onClick = [this] {
        if (processor.parameters.getRawParameterValue("capturePhrase")->load() > 0.5f) {
            if (auto* parameter = processor.parameters.getParameter("capturePhrase"))
                parameter->setValueNotifyingHost(0.0f);
        } else
            processor.finishCapture();
        refresh();
    };
    addAndMakeVisible(editNotes);
    editNotes.onClick = [this] { roll.setNoteEditing(editNotes.getToggleState()); };
    editNotes.setTooltip("Optional note drawing and editing. Off: drag the piano roll to export MIDI; "
                         "double-click to audition.");
    roll.onRegionChange = [this](double start, double end) {
        processor.setPlaybackRegion(start, end, roll.hasTimeSelection());
        updateSelectionControls();
    };
    for (auto* button : {&captureA,     &compare,       &clearA,        &allTime,      &exportRange,
                         &simplify,     &movement,      &answer,        &resolve,      &applyHarmony,
                         &clearHarmony, &importHarmony, &chordComposer, &adaptMelody, &importGroove, &captureGroove,
                         &applyGroove,  &arrange,       &combine,       &applyDraft,   &discardDraft})
        addAndMakeVisible(*button);
    for (auto* value : {&playbackInfo, &workflowInfo, &regionInfo}) {
        addAndMakeVisible(*value);
        value->setFont(font(11));
        value->setColour(juce::Label::textColourId, muted);
    }
    playbackInfo.setJustificationType(juce::Justification::centredRight);
    captureA.onClick = [this] {
        processor.captureComparison();
        refresh();
    };
    compare.onClick = [this] {
        processor.toggleComparison();
        refresh();
    };
    clearA.onClick = [this] {
        processor.clearComparison();
        refresh();
    };
    clearA.setTooltip("Remove the A reference and its ghost notes; return to B.");
    captureA.setTooltip("Save the displayed phrase as reference A, independently of the bank.");
    compare.setTooltip(
        "Compare saved A with the selected phrase B. Export always writes the displayed phrase.");
    quantiseSwitch.setTooltip(
        "Switch variations or A/B at the end of the audible loop. Immediate while stopped.");
    const char* tabs[]{"Edit region", "Chord text", "Groove", "Arrange", "Combine", "Tuning / MIDI", "Search"};
    const char* tabTips[]{
        "Change only the selected time range. Locked notes stay intact.",
        "Enter chord names or import a harmony timeline. The Chords page also generates voiced progressions.",
        "Transfer timing and velocity from a MIDI reference or a selected variation.",
        "Develop the selected idea into a four-part arrangement before keeping it.",
        "Combine rhythm, pitches and accents from different variations.",
        "Configure the microtonal palette and MIDI output. Enable Microtones in the Composition panel.",
        "Set the genetic search budget and inspect actual generations, evaluations, crossovers and fitness."};
    for (int i = 0; i < 7; ++i) {
        auto& tab = workflowTabs[i];
        tab.setButtonText(tabs[i]);
        tab.setTooltip(tabTips[i]);
        addAndMakeVisible(tab);
        tab.onClick = [this, i] { showWorkflowPage(i); };
    }
    allTime.onClick = [this] { roll.selectAllTime(); };
    allTime.setTooltip("Clear the time selection and return to the whole phrase. You can also double-click the bar ruler.");
    exportRange.onClick = [this] { chooseFile(6); };
    exportRange.setTooltip("Export just the selected ruler range, including a section of an arrangement.");
    auto regionAction = [this](composer::RegionAction action) {
        report(processor.applyRegion(roll.regionStart(), roll.regionEnd(), action));
    };
    simplify.onClick = [regionAction] { regionAction(composer::RegionAction::Simplify); };
    movement.onClick = [regionAction] { regionAction(composer::RegionAction::MoreMovement); };
    answer.onClick = [regionAction] { regionAction(composer::RegionAction::Answer); };
    resolve.onClick = [regionAction] { regionAction(composer::RegionAction::Resolve); };
    simplify.setTooltip("Thin unlocked notes whose onsets lie in the selected time range.");
    movement.setTooltip("Increase melodic movement inside the selected range.");
    answer.setTooltip("Create an answering variation inside the selected range.");
    resolve.setTooltip("Resolve the ending of this range towards the current chord or scale.");
    addAndMakeVisible(chordText);
    chordText.setFont(font(14));
    chordText.setTextToShowWhenEmpty("Cm:4 Fm:2 G7:2", muted);
    chordText.setTooltip("Chord:duration, separated by spaces. Durations are quarter-note beats, not bars.");
    chordText.onReturnKey = [this] { report(processor.setCustomHarmony(chordText.getText())); };
    applyHarmony.onClick = [this] { report(processor.setCustomHarmony(chordText.getText())); };
    adaptMelody.onClick = [this] { report(processor.adaptMelodyToChords()); };
    adaptMelody.setColour(juce::TextButton::textColourOffId, teal);
    adaptMelody.setTooltip("Adapt the selected melody to these exact chords, keeping its rhythm, dynamics "
                           "and contour as far as possible. A melody time selection limits the edit. "
                           "Locked notes and other variations stay unchanged. Undo restores the melody.");
    clearHarmony.onClick = [this] {
        processor.clearCustomHarmony();
        chordText.clear();
        refresh();
    };
    importHarmony.onClick = [this] { chooseFile(4); };
    chordComposer.onClick = [this] { showChordComposer(); };
    chordComposer.setColour(juce::TextButton::textColourOffId, teal);
    chordComposer.setTooltip("Create creative progressions with Originality, audition inversions and "
                             "drag their MIDI from the Chords tab.");
    importGroove.onClick = [this] { chooseFile(5); };
    captureGroove.onClick = [this] {
        processor.useSelectedAsGroove();
        refresh();
    };
    for (auto* slider : {&grooveTiming, &grooveVelocity}) {
        addAndMakeVisible(*slider);
        slider->setSliderStyle(juce::Slider::LinearHorizontal);
        slider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 22);
        slider->setRange(0, 1, 0.01);
        slider->setValue(0.5, juce::dontSendNotification);
        slider->textFromValueFunction = [](double value) {
            return juce::String(int(std::round(value * 100))) + "%";
        };
        slider->valueFromTextFunction = [](const juce::String& value) {
            return value.getDoubleValue() / 100;
        };
        slider->updateText();
    }
    grooveTiming.setTooltip(
        "Strength of note-start alignment to the groove reference. Locked notes stay intact.");
    grooveVelocity.setTooltip("Strength of accent transfer from the groove reference.");
    applyGroove.onClick = [this] {
        report(processor.applyGroove(float(grooveTiming.getValue()), float(grooveVelocity.getValue())));
    };
    arrange.onClick = [this] { report(processor.previewArrangement()); };
    addAndMakeVisible(arrangementForm);
    arrangementForm.onChange = [this] {
        if (arrangementForm.getSelectedId() > 0)
            processor.setArrangementForm(arrangementForm.getSelectedId() - 1);
    };
    arrangementForm.setTooltip("Arrangement keeps A A' B A''. AAAB develops only the last bar. ABAC answers "
                               "then returns. Call / Response answers, develops and resolves.");
    attach(auditionReverb, "auditionReverb", false);
    auditionReverb.setTooltip("Wet mix of the internal audition reverb. Zero leaves the dry monitor unchanged. "
                              "Outgoing MIDI is never reverberated.");
    for (auto* box : {&rhythmSource, &pitchSource, &accentSource}) {
        addAndMakeVisible(*box);
        for (int i = 1; i <= 12; ++i)
            box->addItem("Variation " + juce::String(i).paddedLeft('0', 2), i);
        box->setSelectedId(1, juce::dontSendNotification);
    }
    rhythmSource.setTooltip("Provides note starts, durations and phrase length.");
    pitchSource.setTooltip("Provides pitches, mapped by relative position through the source phrase.");
    accentSource.setTooltip(
        "Provides velocity accents, mapped by relative position through the source phrase.");
    combine.onClick = [this] {
        report(processor.previewCombination(rhythmSource.getSelectedId() - 1, pitchSource.getSelectedId() - 1,
                                            accentSource.getSelectedId() - 1));
    };
    applyDraft.onClick = [this] {
        processor.applyDraft();
        refresh();
    };
    discardDraft.onClick = [this] {
        processor.discardDraft();
        refresh();
    };
    applyDraft.setColour(juce::TextButton::buttonColourId, gold);
    applyDraft.setColour(juce::TextButton::textColourOffId, background);
    applyDraft.setTooltip("Replace the selected variation with this preview. Undo can restore it.");
    discardDraft.setTooltip("Discard the temporary preview and return to the committed phrase.");
    attach(microEnabled, "microEnabled");
    microEnabled.getProperties().set("prominent", true);
    microEnabled.getProperties().set("accentColour", int(violet.getARGB()));
    addAndMakeVisible(tuning);
    const auto tuningOptions = GeneticVSTComposerJUCEAudioProcessor::tuningNames();
    for (int i = 1; i < tuningOptions.size(); ++i)
        tuning.addItem(tuningOptions[i], i);
    tuning.onChange = [this] {
        if (tuning.getSelectedId() > 0) {
            auto* parameter = processor.parameters.getParameter("tuning");
            parameter->setValueNotifyingHost(parameter->convertTo0to1(float(tuning.getSelectedId())));
        }
    };
    microEnabled.onClick = [this] {
        if (microEnabled.getToggleState() &&
            processor.parameters.getRawParameterValue("tuning")->load() < 1) {
            auto* parameter = processor.parameters.getParameter("tuning");
            parameter->setValueNotifyingHost(parameter->convertTo0to1(1));
        }
    };
    attach(microOutput, "microOutput");
    attach(bendRange, "bendRange");
    bendRange.textFromValueFunction = [](double v) {
        return juce::String::fromUTF8("±") + juce::String(int(v));
    };
    bendRange.valueFromTextFunction = [](const juce::String& value) {
        return value.retainCharacters("0123456789").getDoubleValue();
    };
    bendRange.updateText();
    microEnabled.setTooltip("Enable microtonal generation. The genetic search chooses short passing notes "
                            "and resolves them into the phrase; the chord layer stays in 12-TET.");
    tuning.setTooltip("The microtonal colour available to the generator. Micro switches the feature on or "
                      "off; the algorithm chooses useful passing notes rather than retuning every note.");
    microOutput.setTooltip("Pitch bend: channel 1 for one melody voice. MPE: independent pitch per note "
                           "on channels 2-16. Internal preview needs no external setup.");
    bendRange.setTooltip("Set the receiving synth's pitch-bend range to this value. MPE uses ±48.");
    addAndMakeVisible(microInfo);
    microInfo.setFont(font(11));
    microInfo.setColour(juce::Label::textColourId, muted);
    for (int i = 0; i < 12; ++i) {
        variants[i] = std::make_unique<VariationButton>(i);
        addAndMakeVisible(*variants[i]);
        variants[i]->onClick = [this, i] {
            processor.selectClip(i);
            refresh();
        };
        variants[i]->onPin = [this, i] {
            processor.toggleFavourite(i);
            refresh();
        };
        variants[i]->onAudition = [this, i] {
            processor.selectClip(i);
            processor.startAudition();
            refresh();
        };
    }
    setResizable(true, true);
    setResizeLimits(870, 630, 1600, 1100);
    addAndMakeVisible(activity);
    addAndMakeVisible(motion);
    motion.setToggleState(true, juce::dontSendNotification);
    motion.setTooltip(
        "Animate playback highlights, hover transitions and MIDI activity. Turn off for reduced motion.");
    for (int i = 0; i < int(knobs.size()); ++i) {
        knobs[i].getProperties().set("accentColour", int((i == 7   ? violet
                                                          : i == 2 ? gold
                                                          : i == 5 ? teal
                                                                   : accent)
                                                             .getARGB()));
        knobs[i].getProperties().set("displayValue", knobs[i].valueToProportionOfLength(knobs[i].getValue()));
    }
    for (auto& tab : workflowTabs)
        tab.getProperties().set("segment", true);
    evolve.setColour(juce::TextButton::textColourOffId, violet);
    addAndMakeVisible(interfaceLanguageButton);
    interfaceLanguageButton.onClick = [this] {
        storeInterfaceLanguage(processor.interfaceLanguageIndex() == 1 ? 0 : 1);
    };
    interfaceLanguageButton.setTooltip("Interface language for this editor. Automation names stay in English.");
    if (juce::SystemStats::getEnvironmentVariable("COMPOSER_DISABLE_UI_PREFS", {}).isEmpty()) {
        juce::PropertiesFile::Options options;
        options.applicationName = "GeneticComposer";
        options.filenameSuffix = ".settings";
        options.osxLibrarySubFolder = "Preferences";
        options.folderName = "Genetic Composer";
        options.storageFormat = juce::PropertiesFile::storeAsXML;
        preferences = std::make_unique<juce::PropertiesFile>(options);
        storeInterfaceLanguage(preferences->getIntValue("language", 0));
    } else
        applyInterfaceLanguage();
    setSize(1240, 900);
    updateMainPage();
    timerCallback();
    startTimerHz(60);
}
juce::String Editor::tr(const juce::String& english) const {
    return interfaceText(english, processor.interfaceLanguageIndex());
}
void Editor::storeInterfaceLanguage(int language) {
    processor.setInterfaceLanguage(language);
    if (preferences) {
        preferences->setValue("language", processor.interfaceLanguageIndex());
        preferences->saveIfNeeded();
    }
    if (guide)
        guide->setLanguage(processor.interfaceLanguageIndex());
    applyInterfaceLanguage();
    repaint();
}
void Editor::applyInterfaceLanguage() {
    const auto text = [this](juce::Button& button, const char* english) {
        button.setButtonText(tr(english));
    };
    text(generate, "Generate melody bank");
    text(evolve, "Evolve selected");
    text(panic, "Stop all");
    text(undo, "Undo");
    text(redo, "Redo");
    text(save, "Save bank");
    text(load, "Open bank");
    text(import, "Import MIDI");
    text(exportBoth, "Melody + chords");
    text(exportFile, "Export...");
    text(melodyTab, "Melody");
    text(chordsTab, "Chords");
    microtonalTab.setButtonText(processor.interfaceLanguageIndex() == 1 ? "Mikrotonowe" : "Microtonal");
    text(withChords, "With chords");
    text(chordMidi, "Chord MIDI");
    text(snap, "Fit to scale");
    text(lockRhythm, "Keep rhythm");
    text(follow, "Follow DAW");
    text(monitor, "Internal sound");
    text(microEnabled, "Microtones");
    text(quantiseSwitch, "Switch at loop");
    text(editNotes, "Edit notes");
    text(motion, "Motion");
    text(allTime, "Whole phrase");
    text(exportRange, "Export range...");
    text(simplify, "Simplify");
    text(movement, "More movement");
    text(answer, "Answer");
    text(resolve, "Resolve");
    text(applyHarmony, "Apply chords");
    text(clearHarmony, "Clear");
    text(importHarmony, "Import harmony MIDI");
    text(chordComposer, "Chord composer...");
    text(adaptMelody, "Adapt melody");
    text(importGroove, "Import groove MIDI");
    text(captureGroove, "Use selected");
    text(applyGroove, "Apply groove");
    text(arrange, "Preview arrangement");
    text(hostMeter, "Host");
    text(capturePhrase, "Capture phrase");
    text(finishCapture, "Finish capture");
    text(evolveEachLoop, "Each loop");
    arrangementForm.clear(juce::dontSendNotification);
    const char* forms[]{"A A' B A''", "AAAB", "ABAC", "Call / Response"};
    for (int i = 0; i < 4; ++i)
        arrangementForm.addItem(tr(forms[i]), i + 1);
    arrangementForm.setSelectedId(processor.arrangementFormIndex() + 1, juce::dontSendNotification);
    text(combine, "Preview combination");
    text(applyDraft, "Keep preview");
    text(discardDraft, "Discard");
    text(captureA, "Capture A");
    text(clearA, "Clear A");
    microInfo.setText(tr("Internal preview needs no MIDI setup. For an external instrument, match Pitch bend or MPE and the bend range."),
                      juce::dontSendNotification);
    const char* tabs[]{"Edit region", "Chord text", "Groove", "Arrange", "Combine", "Tuning / MIDI", "Search"};
    for (int i = 0; i < 7; ++i)
        workflowTabs[i].setButtonText(tr(tabs[i]));
    interfaceLanguageButton.setButtonText(processor.interfaceLanguageIndex() == 1 ? "PL" : "EN");
    preset.setTextWhenNothingSelected(tr("Starting points..."));
    searchDetails.setButtonText(tr("Search settings..."));
    shownLanguage = processor.interfaceLanguageIndex();
    shownSearchSerial = ~uint64_t{0};
    shownStatusSerial = 0;
    roll.setInterfaceLanguage(shownLanguage);
}
GeneticVSTComposerJUCEAudioProcessorEditor::~GeneticVSTComposerJUCEAudioProcessorEditor() {
    stopTimer();
    chordPanel.reset();
    microPanel.reset();
    chooser.reset();
    setLookAndFeel(nullptr);
    for (auto& file : dragFiles)
        file.deleteFile();
}
void Editor::label(juce::Graphics& g, const juce::String& s, juce::Rectangle<int> r, bool heading) {
    g.setColour(heading ? text : muted);
    g.setFont(font(heading ? 11 : 12, heading));
    g.drawText(tr(s), r, juce::Justification::centredLeft);
}
void Editor::paint(juce::Graphics& g) {
    g.fillAll(background);
    const float zoom = std::min(1.0f, std::min(getWidth() / 1180.0f, getHeight() / 900.0f));
    g.addTransform(juce::AffineTransform::scale(zoom));
    const int w = int(getWidth() / zoom), h = int(getHeight() / zoom);
    g.setGradientFill(juce::ColourGradient(background.brighter(0.03f), 0, 0, background.darker(0.2f),
                                           float(w), float(h), false));
    g.fillRect(0, 0, w, h);
    g.setGradientFill(juce::ColourGradient(violet.withAlpha(0.045f), float(w) * 0.45f, 0,
                                           violet.withAlpha(0.0f), float(w) * 0.45f, 450, true));
    g.fillRect(0, 0, w, h);
    auto panelSurface = [&](juce::Rectangle<float> bounds) {
        g.setColour(juce::Colours::black.withAlpha(0.17f));
        g.fillRoundedRectangle(bounds.translated(0, 3), 12);
        g.setGradientFill(juce::ColourGradient(panel.brighter(0.02f), bounds.getTopLeft(),
                                               panel.darker(0.08f), bounds.getBottomLeft(), false));
        g.fillRoundedRectangle(bounds, 12);
        g.setColour(line.withAlpha(0.5f));
        g.drawRoundedRectangle(bounds.reduced(0.5f), 12, 1);
        g.setColour(juce::Colours::white.withAlpha(0.025f));
        g.drawLine(bounds.getX() + 13, bounds.getY() + 1, bounds.getRight() - 13, bounds.getY() + 1);
    };
    if ((!chordPage && !microPage)) {
        panelSurface({20, 92, 262, 300});
        panelSurface({20, 404, 262, float(h - 446)});
        panelSurface({296, 92, float(w - 308), 438});
    }
    g.setColour(line.withAlpha(0.65f));
    g.drawHorizontalLine(71, 20, float(w - 20));
    g.drawHorizontalLine(h - 64, 0, float(w));
    // A small, code-drawn double helix: the visual identity belongs to the instrument.
    for (int i = 0; i < 7; ++i) {
        const float y = 22 + i * 4.7f, x = 38 + std::sin(float(i * 0.8 + motionPhase)) * 11;
        g.setColour(teal.withAlpha(0.28f + onsetPulse * 0.22f));
        g.drawLine(x, y, 76 - x, y, 1);
        g.setColour(accent);
        g.fillEllipse(x - 2, y - 2, 4, 4);
        g.setColour(teal);
        g.fillEllipse(76 - x - 2, y - 2, 4, 4);
    }
    g.setColour(text);
    g.setFont(font(26, true));
    g.drawText("GENETIC", 70, 14, 200, 34, juce::Justification::centredLeft);
    g.setFont(font(10));
    g.setColour(muted);
    g.drawText("M I D I   C O M P O S E R", 72, 45, 200, 16, juce::Justification::centredLeft);
    if (chordPage || microPage) {
        if (microPage)
            return;
        label(g, "Reverb", {592, h - 53, 48, 22});
        label(g, "BPM", {730, h - 53, 34, 22});
        label(g, "Level", {844, h - 53, 34, 22});
        return;
    }
    label(g, "COMPOSITION", {36, 105, 95, 18}, true);
    label(g, "Root", {36, 131, 62, 16});
    label(g, "Scale", {108, 131, 168, 16});
    label(g, "Length", {36, 188, 74, 16});
    label(g, "Grid", {118, 188, 66, 16});
    label(g, "Meter", {192, 188, 74, 16});
    label(g, "Grouping", {36, 244, 170, 16});
    label(g, "Phrase type", {36, 274, 100, 16});
    label(g, "Source mode", {146, 274, 120, 16});
    label(g, shownHarmony.isNotEmpty() ? "Using sequence from Chords" : "Harmony preset / or open Chords",
          {36, 330, 230, 16});
    label(g, "CHARACTER", {36, 408, 230, 16}, true);
    const juce::String names[]{"Diversity", "Dynamics",     "Jazziness", "Valence", "Arousal",
                               "Weirdness", "Pause Amount", "Evolution", "Density"};
    for (int i = 0; i < int(knobs.size()); ++i) {
        g.setFont(font(10));
        g.setColour(muted);
        g.drawText(tr(names[i]), 36 + (i % 3) * 79, 486 + (i / 3) * 76, 72, 16, juce::Justification::centred);
    }
    label(g, "Low note", {36, 658, 104, 16});
    label(g, "High note", {158, 658, 108, 16});
    label(g, "Search", {36, 717, 56, 18});
    const int mainX = 304, mainW = w - mainX - 20;
    g.setColour(line);
    g.drawRoundedRectangle(float(mainX) - 0.5f, 173.5f, float(mainW) + 1, 271, 5, 1);
    label(g, "Swing", {mainX + 236, 498, 42, 24});
    label(g, "Gate", {mainX + 390, 498, 32, 24});
    label(g, "Humanize", {mainX + 530, 498, 64, 24});
    panelSurface({float(mainX), 570, float(mainW), 102});
    if (workflowPage == 2) {
        label(g, "Timing", {mainX + 12, 638, 46, 26});
        label(g, "Velocity", {mainX + 244, 638, 54, 26});
    }
    if (workflowPage == 3) {
        const char* parts[]{"A / original", "A' / develop", "B / answer", "A'' / ending"};
        const int partWidth = (mainW - 40) / 4;
        for (int i = 0; i < 4; ++i) {
            auto cell = juce::Rectangle<int>(mainX + 12 + i * (partWidth + 4), 603, partWidth, 28);
            g.setColour(i == 0 ? accent.withAlpha(0.15f) : surface);
            g.fillRoundedRectangle(cell.toFloat(), 4);
            g.setColour(i == 0 ? accent : text);
            g.setFont(font(12, true));
            g.drawText(tr(parts[i]), cell, juce::Justification::centred);
        }
    }
    if (workflowPage == 4) {
        const int width = (mainW - 48) / 3;
        label(g, "Rhythm", {mainX + 12, 595, width, 16});
        label(g, "Pitches", {mainX + 24 + width, 595, width, 16});
        label(g, "Accents", {mainX + 36 + width * 2, 595, width, 16});
    }
    if (workflowPage == 5) {
        label(g, "Palette", {mainX + 12, 602, 72, 28});
        label(g, "Bend", {mainX + 622, 602, 40, 28});
    }
    if (workflowPage == 6) {
        label(g, "Population", {mainX + 156, 602, 78, 28});
        label(g, "Generations", {mainX + 378, 602, 84, 28});
        label(g, "Crossover", {mainX + 610, 602, 76, 28});
    }
    label(g, "VARIATIONS", {mainX, 682, 150, 20}, true);
    g.setColour(muted);
    g.setFont(font(11));
    g.drawText(tr("Select  /  Double-click to play  /  + to pin"), mainX + 145, 682, mainW - 145, 20,
               juce::Justification::centredRight);
    label(g, "Reverb", {592, h - 53, 48, 22});
    label(g, "BPM", {730, h - 53, 34, 22});
    label(g, "Level", {844, h - 53, 34, 22});
    if (processor.isGenerating()) {
        g.setColour(line);
        g.fillRoundedRectangle(36, 743, 230, 3, 1.5f);
        g.setColour(accent);
        g.fillRoundedRectangle(36, 743, 230 * processor.generationProgress(), 3, 1.5f);
    }
}
void Editor::resized() {
    const float zoom = std::min(1.0f, std::min(getWidth() / 1180.0f, getHeight() / 900.0f));
    const int w = int(getWidth() / zoom), h = int(getHeight() / zoom), x = 304, mw = w - x - 20;
    melodyTab.setBounds(304, 23, 110, 32);
    chordsTab.setBounds(420, 23, 110, 32);
    microtonalTab.setBounds(536, 23, 106, 32);
    activity.setBounds(20, h - 58, 224, 34);
    preset.setBounds(w - 532, 25, 170, 30);
    undo.setBounds(w - 350, 25, 54, 30);
    redo.setBounds(w - 290, 25, 54, 30);
    load.setBounds(w - 224, 25, 82, 30);
    save.setBounds(w - 136, 25, 82, 30);
    help.setBounds(w - 44, 25, 24, 30);
    interfaceLanguageButton.setBounds(210, 28, 42, 28);
    root.setBounds(36, 151, 62, 30);
    scale.setBounds(108, 151, 168, 30);
    bars.setBounds(36, 208, 74, 30);
    grid.setBounds(118, 208, 66, 30);
    meter.setBounds(192, 208, 74, 30);
    grouping.setBounds(36, 260, 108, 26);
    groupingEntry.setBounds(148, 260, 64, 26);
    hostMeter.setBounds(216, 260, 50, 26);
    phraseType.setBounds(36, 294, 100, 30);
    mode.setBounds(146, 294, 120, 30);
    harmony.setBounds(36, 350, 230, 30);
    for (int i = 0; i < int(knobs.size()); ++i)
        knobs[i].setBounds(36 + (i % 3) * 79, 426 + (i / 3) * 76, 72, 60);
    low.setBounds(32, 679, 116, 24);
    high.setBounds(154, 679, 116, 24);
    quality.setBounds(94, 711, 172, 28);
    generate.setBounds(36, 750, 230, 34);
    evolve.setBounds(36, 792, 230, 34);
    searchDetails.setBounds(36, 834, 230, 24);
    lockRhythm.setBounds(x + 106, 498, 118, 26);
    editNotes.setBounds(x + 418, 456, 100, 30);
    phraseTitle.setBounds(x, 96, 320, 32);
    phraseInfo.setBounds(x + 330, 100, mw - 330, 26);
    applyDraft.setBounds(w - 240, 98, 132, 28);
    discardDraft.setBounds(w - 100, 98, 80, 28);
    int toolsX = x;
    for (auto* button : std::initializer_list<juce::Component*>{&captureA, &compare, &clearA, &quantiseSwitch,
                                                              &evolveEachLoop, &capturePhrase, &finishCapture}) {
        if (!button->isVisible())
            continue;
        const int width = button == &captureA ? 94 : button == &compare ? 76 : button == &clearA ? 72 :
                          button == &quantiseSwitch || button == &finishCapture ? 118 :
                          button == &evolveEachLoop ? 108 : 128;
        button->setBounds(toolsX, 134, width, 30);
        toolsX += width + 8;
    }
    playbackInfo.setBounds(toolsX, 135, std::max(0, x + mw - toolsX), 28);
    roll.setBounds(x, 174, mw, 270);
    play.setBounds(x, 456, 72, 30);
    withChords.setBounds(x + 76, 456, 108, 30);
    chordMidi.setBounds(x + 188, 456, 108, 30);
    snap.setBounds(x + 300, 456, 112, 30);
    import.setBounds(x, 498, 96, 26);
    exportBoth.setBounds(w - 350, 456, 132, 30);
    exportFile.setBounds(w - 210, 456, 72, 30);
    drag.setBounds(w - 130, 456, 110, 30);
    swing.setBounds(x + 278, 498, 108, 24);
    gate.setBounds(x + 424, 498, 100, 24);
    humanize.setBounds(x + 596, 498, 110, 24);
    tempoInfo.setBounds(x + 716, 498, mw - 716, 24);
    allTime.setBounds(x + 716, 496, mw - 716, 28);
    const int tabWidth = (mw - 6 * 6) / 7;
    for (int i = 0; i < 7; ++i)
        workflowTabs[i].setBounds(x + i * (tabWidth + 6), 538, tabWidth, 28);
    workflowInfo.setBounds(x + 12, 573, mw - 24, 24);
    regionInfo.setBounds(x + 12, 602, mw - 176, 28);
    exportRange.setBounds(w - 164, 602, 132, 28);
    const int regionWidth = (mw - 48) / 4;
    std::array<juce::TextButton*, 4> actions{&simplify, &movement, &answer, &resolve};
    for (int i = 0; i < 4; ++i)
        actions[i]->setBounds(x + 12 + i * (regionWidth + 8), 638, regionWidth, 28);
    chordText.setBounds(x + 12, 602, mw - 24, 28);
    applyHarmony.setBounds(x + 12, 638, 118, 28);
    clearHarmony.setBounds(x + 138, 638, 66, 28);
    importHarmony.setBounds(x + 212, 638, 188, 28);
    adaptMelody.setBounds(x + 408, 638, 208, 28);
    chordComposer.setBounds(x + 624, 638, mw - 636, 28);
    importGroove.setBounds(x + 12, 602, 174, 28);
    captureGroove.setBounds(x + 194, 602, 124, 28);
    grooveTiming.setBounds(x + 60, 638, 174, 26);
    grooveVelocity.setBounds(x + 300, 638, 174, 26);
    applyGroove.setBounds(w - 160, 638, 128, 28);
    arrange.setBounds(x + 12, 638, 168, 28);
    arrangementForm.setBounds(x + 188, 638, 196, 28);
    const int sourceWidth = (mw - 48) / 3;
    rhythmSource.setBounds(x + 12, 612, sourceWidth, 26);
    pitchSource.setBounds(x + 24 + sourceWidth, 612, sourceWidth, 26);
    accentSource.setBounds(x + 36 + 2 * sourceWidth, 612, sourceWidth, 26);
    combine.setBounds(x + 12, 642, 186, 26);
    microEnabled.setBounds(138, 99, 128, 30);
    tuning.setBounds(x + 88, 602, 312, 28);
    microOutput.setBounds(x + 410, 602, 202, 28);
    bendRange.setBounds(x + 666, 603, mw - 678, 26);
    microInfo.setBounds(x + 12, 636, mw - 24, 31);
    customSearch.setBounds(x + 12, 602, 136, 28);
    searchPopulation.setBounds(x + 234, 603, 136, 26);
    searchGenerations.setBounds(x + 462, 603, 140, 26);
    crossover.setBounds(x + 686, 603, mw - 698, 26);
    searchInfo.setBounds(x + 12, 635, mw - 24, 34);
    const int gap = 8, cardW = (mw - 5 * gap) / 6, cardH = (h - 64 - 712 - 20) / 2;
    for (int i = 0; i < 12; ++i)
        variants[i]->setBounds(x + (i % 6) * (cardW + gap), 712 + (i / 6) * (cardH + 8), cardW, cardH);
    auditionSound.setBounds(256, h - 54, 108, 30);
    monitor.setBounds(368, h - 54, 112, 30);
    follow.setBounds(484, h - 54, 104, 30);
    auditionReverb.setBounds(640, h - 53, 84, 26);
    tempo.setBounds(768, h - 53, 70, 26);
    volume.setBounds(880, h - 53, 78, 26);
    panic.setBounds(w - 250, h - 54, 80, 30);
    motion.setBounds(w - 116, h - 54, 96, 30);
    status.setBounds(20, h - 22, w - 40, 19);
    if (chordPanel)
        chordPanel->setBounds(20, 84, w - 40, h - 154);
    if (microPanel)
        microPanel->setBounds(20, 84, w - 40, h - 154);
    if (guide)
        guide->setBounds(0, 0, w, h);
    for (auto* child : getChildren())
        if (dynamic_cast<juce::ResizableCornerComponent*>(child) == nullptr &&
            dynamic_cast<juce::ResizableBorderComponent*>(child) == nullptr)
            child->setTransform(juce::AffineTransform::scale(zoom));
}
void Editor::applyGroupingText(const juce::String& text) {
    const auto trimmed = text.trim();
    if (trimmed == processor.groupingText())
        return;
    if (auto result = processor.setGroupingText(trimmed); result.failed()) {
        processor.setStatus(result.getErrorMessage());
        groupingEntry.setText(processor.groupingText(), false);
    }
    refresh();
}
void Editor::report(const juce::Result& result) {
    if (result.failed())
        processor.setStatus(result.getErrorMessage());
    refresh();
}
void Editor::showWorkflowPage(int page) {
    if (chordPage || microPage)
        showMelodyComposer();
    workflowPage = juce::jlimit(0, 6, page);
    refresh();
    updateWorkflow();
    repaint();
}
void Editor::showGuide() {
    if (!guide) {
        guide = std::make_unique<ComposerGuide>();
        guide->onClose = [this] {
            guide->setVisible(false);
            grabKeyboardFocus();
        };
        guide->onLanguage = [this](int language) { storeInterfaceLanguage(language); };
        addChildComponent(*guide);
    }
    guide->setLanguage(processor.interfaceLanguageIndex());
    guide->setVisible(true);
    guide->toFront(true);
    resized();
}
void Editor::showChordComposer() {
    microPage = false;
    processor.setMicrotonalMode(false);
    chordPage = true;
    if (!chordPanel) {
        chordPanel = std::make_unique<ChordComposerPanel>(processor);
        chordPanel->onShowHelp = [this] {
            showGuide();
            guide->showChapter(11);
        };
        chordPanel->onShowMelody = [this] { showMelodyComposer(); };
        addChildComponent(*chordPanel);
    }
    chordPanel->refreshDisplay();
    updateMainPage();
    resized();
    repaint();
}
void Editor::showMelodyComposer() {
    microPage = false;
    processor.setMicrotonalMode(false);
    chordPage = false;
    updateMainPage();
    resized();
    repaint();
}
void Editor::showMicrotonalComposer() {
    chordPage = false;
    microPage = true;
    processor.setMicrotonalMode(true);
    if (!microPanel) {
        microPanel = std::make_unique<MicrotonalComposerPanel>(processor);
        addChildComponent(*microPanel);
    }
    microPanel->refreshDisplay();
    updateMainPage();
    resized();
    repaint();
}
void Editor::updateMainPage() {
    for (auto* child : getChildren()) {
        const bool global = child == &melodyTab || child == &chordsTab || child == &microtonalTab ||
                            child == &activity || child == &preset || child == &undo || child == &redo ||
                            child == &load || child == &save || child == &help || child == &auditionSound ||
                            child == &monitor || child == &follow || child == &tempo || child == &volume ||
                            child == &panic || child == &motion || child == &interfaceLanguageButton ||
                            child == &status;
        if (child == chordPanel.get() || child == microPanel.get() || child == guide.get() ||
            dynamic_cast<juce::ResizableCornerComponent*>(child) != nullptr ||
            dynamic_cast<juce::ResizableBorderComponent*>(child) != nullptr)
            continue;
        const bool sharedWithMicro = child == &melodyTab || child == &chordsTab || child == &microtonalTab ||
                                     child == &activity || child == &load || child == &save ||
                                     child == &help || child == &panic || child == &motion ||
                                     child == &interfaceLanguageButton || child == &status;
        child->setVisible(microPage ? sharedWithMicro : (global || (!chordPage && !microPage)));
    }
    if (chordPanel)
        chordPanel->setVisible(chordPage);
    if (microPanel)
        microPanel->setVisible(microPage);
    microtonalTab.setColour(juce::TextButton::buttonColourId, microPage ? palette::violet : palette::surface);
    microtonalTab.setColour(juce::TextButton::textColourOffId,
                            microPage ? palette::background : palette::muted);
    const bool melodyPage = !chordPage && !microPage;
    melodyTab.setColour(juce::TextButton::buttonColourId, melodyPage ? palette::teal : palette::surface);
    melodyTab.setColour(juce::TextButton::textColourOffId, melodyPage ? palette::background : palette::muted);
    chordsTab.setColour(juce::TextButton::buttonColourId, chordPage ? palette::teal : palette::surface);
    chordsTab.setColour(juce::TextButton::textColourOffId, chordPage ? palette::background : palette::muted);
    if ((!chordPage && !microPage)) {
        updateWorkflow();
        refresh();
    }
}
void Editor::updateWorkflow() {
    for (int i = 0; i < 7; ++i) {
        workflowTabs[i].setColour(juce::TextButton::buttonColourId, i == workflowPage ? teal : surface);
        workflowTabs[i].setColour(juce::TextButton::textColourOffId, i == workflowPage ? background : muted);
    }
    for (auto* component : std::initializer_list<juce::Component*>{&regionInfo, &simplify, &movement, &answer, &resolve})
        component->setVisible((!chordPage && !microPage) && workflowPage == 0);
    for (auto* component : std::initializer_list<juce::Component*>{&chordText, &applyHarmony, &clearHarmony,
                                                                   &importHarmony, &chordComposer, &adaptMelody})
        component->setVisible((!chordPage && !microPage) && workflowPage == 1);
    for (auto* component : std::initializer_list<juce::Component*>{
             &importGroove, &captureGroove, &grooveTiming, &grooveVelocity, &applyGroove})
        component->setVisible((!chordPage && !microPage) && workflowPage == 2);
    arrange.setVisible((!chordPage && !microPage) && workflowPage == 3);
    arrangementForm.setVisible((!chordPage && !microPage) && workflowPage == 3);
    for (auto* component :
         std::initializer_list<juce::Component*>{&rhythmSource, &pitchSource, &accentSource, &combine})
        component->setVisible((!chordPage && !microPage) && workflowPage == 4);
    microEnabled.setVisible((!chordPage && !microPage));
    for (auto* component : std::initializer_list<juce::Component*>{&tuning, &microOutput,
                                                                   &bendRange, &microInfo})
        component->setVisible((!chordPage && !microPage) && workflowPage == 5);
    for (auto* component : std::initializer_list<juce::Component*>{
             &customSearch, &searchPopulation, &searchGenerations, &crossover, &searchInfo})
        component->setVisible((!chordPage && !microPage) && workflowPage == 6);
    const char* descriptions[]{
        "Drag the bar ruler to loop and edit a section. Double-click the ruler to return to the whole phrase.",
        "Enter chord:beats, e.g. Cm:4 Fm:2 G7:2, or open Chord composer to generate and export voiced "
        "chords.",
        "Import a reference or capture a variation, then apply its timing and accents.",
        "Turn the selected motif into four sections. Audition the preview before keeping it.",
        "Choose the sources below. Notes are matched by relative phrase position.",
        "",
        "Real genetic search: islands, one elite per island, crossover and adaptive mutation. No artificial "
        "delay."};
    workflowInfo.setText(workflowPage == 5 ? microDescription() : tr(descriptions[workflowPage]),
                         juce::dontSendNotification);
    updateSelectionControls();
}
void Editor::updateSelectionControls() {
    const bool scoped = roll.hasTimeSelection();
    evolve.setButtonText(tr(scoped ? "Optimise range" : "Evolve selected"));
    evolve.setTooltip(tr(scoped
                            ? "Optimise unlocked notes in the selected range of this variation. Other notes and "
                              "variations stay unchanged. Undo restores the previous phrase."
                            : "Evolve the selected phrase into new variations. Select time on the ruler to optimise only that section."));
    allTime.setVisible((!chordPage && !microPage) && scoped);
    tempoInfo.setVisible((!chordPage && !microPage) && !scoped);
    exportRange.setVisible((!chordPage && !microPage) && workflowPage == 0 && scoped);
    regionInfo.setColour(juce::Label::textColourId, scoped ? teal : muted);
    regionInfo.setText(scoped ? tr("Loop: beats ") + juce::String(roll.regionStart(), 2) + " - " +
                                   juce::String(roll.regionEnd(), 2)
                             : tr("Scope: whole phrase"),
                       juce::dontSendNotification);
}
juce::String Editor::microDescription() const {
    const bool enabled = processor.parameters.getRawParameterValue("microEnabled")->load() > 0.5f;
    return tr(enabled ? "Microtones ON: the next search may add passing notes. Set their palette and MIDI output below."
                      : "Microtones OFF: plain 12-tone playback. Enable Microtones in Composition.");
}
void Editor::refresh() {
    const auto meterNow = processor.timeSignature();
    if (!meter.hasKeyboardFocus(true) && (meterNow.numerator != shownMeterNumerator ||
                                           meterNow.denominator != shownMeterDenominator)) {
        meter.setText(juce::String(meterNow.numerator) + "/" + juce::String(meterNow.denominator),
                      juce::dontSendNotification);
        shownMeterNumerator = meterNow.numerator;
        shownMeterDenominator = meterNow.denominator;
    }
    if (meterNow.numerator != shownGroupNumerator || meterNow.denominator != shownGroupDenominator) {
        shownGroupNumerator = meterNow.numerator;
        shownGroupDenominator = meterNow.denominator;
        grouping.clear(juce::dontSendNotification);
        int id = 1;
        for (const auto& choice : composer::groupingChoices(meterNow.numerator, meterNow.denominator))
            grouping.addItem(juce::String(choice), id++);
    }
    const auto groupsNow = processor.groupingText();
    if (!groupingEntry.hasKeyboardFocus(true) && groupingEntry.getText() != groupsNow)
        groupingEntry.setText(groupsNow, false);
    if (arrangementForm.getNumItems() > 0 &&
        arrangementForm.getSelectedId() != processor.arrangementFormIndex() + 1)
        arrangementForm.setSelectedId(processor.arrangementFormIndex() + 1, juce::dontSendNotification);
    const bool custom = customSearch.getToggleState();
    quality.setEnabled(!custom);
    searchPopulation.setEnabled(custom);
    searchGenerations.setEnabled(custom);
    const bool busy = processor.isGenerating();
    const auto budget = processor.configuredSearchBudget();
    const auto searchSerial = processor.searchSerial();
    if (searchSerial != shownSearchSerial || busy != shownSearchBusy ||
        budget.population != shownBudgetPopulation || budget.generations != shownBudgetGenerations ||
        workflowPage != shownSearchPage) {
        if (searchSerial != shownSearchSerial || busy != shownSearchBusy ||
            budget.population != shownBudgetPopulation || budget.generations != shownBudgetGenerations) {
            const auto completedSearch = processor.searchSummary();
            shownSearchDescription = processor.searchDescription();
            searchDetails.setButtonText(completedSearch.isNotEmpty()
                                            ? tr(completedSearch)
                                            : juce::String(budget.population) + tr(" individuals / ") +
                                                  juce::String(budget.generations) + tr(" generations..."));
            searchDetails.setTooltip(tr(shownSearchDescription) + "\n" +
                                     tr("Click for budget and full search statistics."));
            shownSearchSerial = searchSerial;
            shownSearchBusy = busy;
            shownBudgetPopulation = budget.population;
            shownBudgetGenerations = budget.generations;
        }
        if (workflowPage == 6)
            searchInfo.setText(tr(shownSearchDescription), juce::dontSendNotification);
        shownSearchPage = workflowPage;
    }
    const auto version = processor.documentRevision();
    const int progression = processor.progressionIndex();
    if (version != shownRevision || progression != shownProgression) {
        harmony.setSelectedId(processor.customHarmonyText().isNotEmpty() ? 1000 : progression + 1,
                              juce::dontSendNotification);
        shownProgression = progression;
    }
    const int scaleNow = processor.scaleIndex();
    const int scaleId = scaleNow >= 10 ? scaleNow : scaleNow + 1;
    if (!scale.isPopupActive() && scale.getSelectedId() != scaleId)
        scale.setSelectedId(scaleId, juce::dontSendNotification);
    if (!bars.isPopupActive() && bars.getSelectedId() != processor.phraseLengthMenuId())
        bars.setSelectedId(processor.phraseLengthMenuId(), juce::dontSendNotification);
    const auto slot = processor.selectedIndex();
    const bool draft = processor.hasDraft();
    const int tuningNow = processor.tuningIndex(), output = processor.microtonalOutput();
    const int bend = int(processor.parameters.getRawParameterValue("bendRange")->load());
    const bool microtonal = processor.parameters.getRawParameterValue("microEnabled")->load() > 0.5f;
    const int rawTuning = std::max(1, int(processor.parameters.getRawParameterValue("tuning")->load()));
    tuning.setSelectedId(rawTuning, juce::dontSendNotification);
    const bool tuningChanged = tuningNow != shownTuning || output != shownOutput || bend != shownBend;
    if (tuningChanged) {
        if (workflowPage == 5)
            workflowInfo.setText(microDescription(), juce::dontSendNotification);
        scale.setEnabled(true);
        scale.setTooltip("The main scale guides generation. Micro adds passing-note colours without "
                         "replacing this scale.");
        roll.setMicrotonal(microtonal);
        shownTuning = tuningNow;
        shownOutput = output;
        shownBend = bend;
    }
    const auto region = processor.playbackRegion();
    const bool regionChanged = region.slot == slot &&
                               (region.enabled != roll.hasTimeSelection() || region.start != roll.regionStart() ||
                                region.end != roll.regionEnd());
    if (version != shownRevision || slot != shownSlot || tuningChanged || regionChanged) {
        auto bank = processor.bankSnapshot();
        for (int i = 0; i < 12; ++i)
            variants[i]->setClip(bank[i], i == slot, microtonal);
        const auto c = processor.displayedClip();
        roll.setClip(c, slot != shownSlot);
        if (region.slot == slot && region.enabled) {
            roll.setRegion(region.start, region.end);
        } else {
            roll.selectAllTime();
        }
        roll.setReference(processor.comparisonClip(), processor.hasComparison());
        roll.setHarmony(processor.harmonySnapshot());
        phraseTitle.setText(draft ? (processor.draftDescription().startsWith("Arrangement")
                                         ? tr("PREVIEW / Arrangement")
                                         : tr("PREVIEW / Combined phrase"))
                                  : tr("Variation ") + juce::String(slot + 1).paddedLeft('0', 2),
                            juce::dontSendNotification);
        phraseTitle.setTooltip(draft ? tr(processor.draftDescription()) : juce::String());
        phraseTitle.setColour(juce::Label::textColourId, draft ? gold : text);
        phraseTitle.setFont(font(draft ? 17 : 20, true));
        int locked = 0, microtonalNotes = 0;
        for (int i = 0; i < c.count; ++i) {
            locked += c.notes[i].locked;
            microtonalNotes += c.notes[i].cents != 0;
        }
        phraseInfo.setText(tr(juce::String(c.count) + " notes   /   " +
                               juce::String(c.length / (c.numerator * 4.0 / c.denominator), 1) +
                               " bars   /   " + juce::String(locked) + " locked" +
                               (microtonal ? "   /   " + juce::String(microtonalNotes) + " microtonal" : "")),
                           juce::dontSendNotification);
        const auto currentHarmony = processor.customHarmonyText();
        if (currentHarmony != shownHarmony) {
            chordText.setText(currentHarmony, false);
            shownHarmony = currentHarmony;
        }
        snap.setEnabled(true);
        snap.setTooltip("Fit melody pitches to the selected scale during playback and export, keeping "
                        "microtonal offsets. This can change borrowed chord tones. Turn off to hear "
                        "the generator's original pitches.");
        shownRevision = version;
        shownSlot = slot;
        repaint();
    }
    const bool hasChords = processor.chordClipSnapshot().count > 0;
    withChords.setEnabled(hasChords);
    chordMidi.setEnabled(hasChords && output != 2);
    if (output == 2 && processor.parameters.getRawParameterValue("chordMidiOut")->load() > 0.5f) {
        if (auto* chordMidiParameter = processor.parameters.getParameter("chordMidiOut"))
            chordMidiParameter->setValueNotifyingHost(0.0f);
        processor.setStatus("Chord MIDI stays off while MPE uses channels 2-16.");
    }
    withChords.setTooltip(
        hasChords ? "Play melody and chords together through separate internal sound layers. Enable Chord MIDI "
                    "to also send the chords on channel 16."
                  : "Create chords in the Chords tab first, then enable this to hear both parts together.");
    chordMidi.setTooltip(output == 2 ? "MPE uses channels 2-16 for the melody, so chord MIDI on channel 16 stays off."
                                     : "Send the sounding chord voices on MIDI channel 16 while With chords plays. "
                                       "Internal sound does not have to be on.");
    roll.setEditable(!draft);
    applyDraft.setVisible(draft && (!chordPage && !microPage));
    discardDraft.setVisible(draft && (!chordPage && !microPage));
    phraseInfo.setVisible(!draft && (!chordPage && !microPage));
    for (auto* action : {&simplify, &movement, &answer, &resolve})
        action->setEnabled(!draft && !processor.isGenerating());
    applyGroove.setEnabled(!draft && processor.hasGrooveReference() && !processor.isGenerating());
    adaptMelody.setEnabled(!processor.isGenerating() && processor.canAdaptMelodyToChords());
    adaptMelody.setButtonText(tr(roll.hasTimeSelection() ? "Adapt range" : "Adapt melody"));
    microOutput.setEnabled(microtonal);
    tuning.setEnabled(microtonal);
    bendRange.setEnabled(output == 1);

    compare.setEnabled(processor.hasComparison());
    clearA.setEnabled(processor.hasComparison());
    bool toolsChanged = false;
    auto showTool = [&](juce::Component& tool, bool visible) {
        toolsChanged |= tool.isVisible() != visible;
        tool.setVisible(visible);
    };
    showTool(compare, (!chordPage && !microPage) && processor.hasComparison());
    showTool(clearA, (!chordPage && !microPage) && processor.hasComparison());
    showTool(finishCapture, (!chordPage && !microPage) && capturePhrase.getToggleState());
    if (toolsChanged)
        resized();
    compare.setButtonText(processor.isComparing() ? tr("Hear B") : tr("Hear A"));
    compare.setColour(juce::TextButton::textColourOffId, processor.isComparing() ? gold : text);
    captureGroove.setTooltip(processor.hasGrooveReference()
                                 ? "Replace the saved groove reference with the selected variation."
                                 : "Capture the selected variation as a groove reference.");
    undo.setEnabled(processor.canUndo() && !draft);
    redo.setEnabled(processor.canRedo() && !draft);
}
void Editor::advanceVisuals() {
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const float elapsed = lastFrame > 0 ? float(juce::jlimit(0.001, 0.1, now - lastFrame)) : 1.0f / 60;
    lastFrame = now;
    const bool animate = motion.getToggleState() && !(guide && guide->isVisible());
    const auto state = processor.visualState();
    float midiEnergy = 0;
    for (float level : state.notes)
        midiEnergy = std::max(midiEnergy, level);
    if (state.noteOnSerial > seenNoteSerial)
        onsetPulse = 1.0f;
    else
        onsetPulse *= std::exp(-elapsed * 7.0f);
    seenNoteSerial = state.noteOnSerial;
    const bool running = processor.isPlaying(), searching = processor.isGenerating();
    const double reported = processor.playPosition();
    if (reported != lastReportedBeat) {
        lastReportedBeat = reported;
        beatReceivedAt = now;
    }
    // Extrapolate only between fresh audio block reports, and never across a
    // stopped transport. The audio thread remains the source of musical time.
    double beat = reported;
    if (running && animate && reported >= 0)
        beat += std::min(0.04, now - beatReceivedAt) * processor.tempo() / 60.0;
    const bool showingAudible = running && !processor.audibleChords() && !processor.audibleComparison() &&
                                processor.audibleIndex() == processor.selectedIndex();
    if (showingAudible)
        beat = std::fmod(std::max(0.0, beat), processor.displayedClip().length);
    roll.setPlayhead(showingAudible ? beat : -1);
    roll.setActivity(processor.audibleChords() ? GeneticVSTComposerJUCEAudioProcessor::VisualState{} : state,
                     showingAudible, animate, elapsed);
    activity.update(state.outputPeak, midiEnergy, state.noteOnSerial, running, searching,
                    processor.generationProgress(), reported, animate);
    for (auto& variant : variants)
        variant->advanceAnimation(reported, animate ? onsetPulse : midiEnergy, animate, elapsed);
    const float transition = animate ? 1.0f - std::exp(-elapsed * 18.0f) : 1.0f;
    for (auto* child : getChildren()) {
        if (!child->isVisible())
            continue;
        auto* button = dynamic_cast<juce::Button*>(child);
        auto* slider = dynamic_cast<juce::Slider*>(child);
        if (!button && !slider && !dynamic_cast<juce::ComboBox*>(child))
            continue;
        auto& properties = child->getProperties();
        const float hover = float(properties.getWithDefault("hoverAmount", 0.0f));
        const float next =
            hover + (float(child->isMouseOver(true) && child->isEnabled()) - hover) * transition;
        const float press = float(properties.getWithDefault("pressAmount", 0.0f));
        const float nextPress = press + (float(button && button->isDown()) - press) * transition;
        properties.set("hoverAmount", next);
        properties.set("pressAmount", nextPress);
        bool changed = std::abs(next - hover) > 0.002f || std::abs(nextPress - press) > 0.002f;
        if (slider && slider->getSliderStyle() == juce::Slider::RotaryHorizontalVerticalDrag) {
            const float target = float(slider->valueToProportionOfLength(slider->getValue()));
            const float shown = float(properties.getWithDefault("displayValue", target));
            const float value = shown + (target - shown) * transition;
            properties.set("displayValue", value);
            changed |= std::abs(value - shown) > 0.0001f;
        }
        if (changed)
            child->repaint();
    }
    if (animate && (running || searching || onsetPulse > 0.01f)) {
        motionPhase += elapsed * (searching ? 2.8 : running ? processor.tempo() / 80.0 : 0.4);
        const float zoom = std::min(1.0f, std::min(getWidth() / 1180.0f, getHeight() / 900.0f));
        repaint(juce::Rectangle<float>(18, 15, 45, 46)
                    .transformedBy(juce::AffineTransform::scale(zoom))
                    .toNearestInt());
    }
}
void Editor::timerCallback() {
    if (!isShowing() && shownRevision != 0) {
        if (timerRate != 10) {
            timerRate = 10;
            startTimerHz(timerRate);
        }
        return;
    }
    refreshDisplay();
}
void Editor::refreshDisplay() {
    if (processor.isMicrotonalMode() != microPage) {
        if (processor.isMicrotonalMode())
            showMicrotonalComposer();
        else
            showMelodyComposer();
    }
    if (microPage) {
        if (shownLanguage != processor.interfaceLanguageIndex())
            applyInterfaceLanguage();
        microPanel->refreshDisplay();
        status.setText(processor.statusText(), juce::dontSendNotification);
        advanceVisuals();
        return;
    }
    const int desiredRate = motion.getToggleState() && !(guide && guide->isVisible()) ? 60 : 30;
    if (timerRate != desiredRate) {
        timerRate = desiredRate;
        startTimerHz(timerRate);
    }
    refresh();
    const auto statusSerial = processor.statusSerial();
    if (statusSerial != shownStatusSerial) {
        status.setText(processor.statusText(), juce::dontSendNotification);
        shownStatusSerial = statusSerial;
    }
    const bool busy = processor.isGenerating();
    generate.setButtonText(busy ? tr("Cancel search  ") +
                                      juce::String(int(processor.generationProgress() * 100)) + "%"
                                : tr("Generate melody bank"));
    evolve.setEnabled(!busy && !processor.hasDraft());
    updateSelectionControls();
    generate.setEnabled(busy || !processor.hasDraft());
    const bool melodyPreview = processor.previewRequested() && !processor.isAuditioningChords();
    play.setPlaying(melodyPreview);
    play.setButtonText(tr(melodyPreview ? "Stop" : "Play"));
    roll.setInterfaceLanguage(processor.interfaceLanguageIndex());
    tempoInfo.setText(juce::String(processor.tempo(), 1) + " BPM  /  " +
                          tr(processor.isPlaying() ? "PLAYING" : "READY"),
                      juce::dontSendNotification);
    const bool hearingChords = processor.audibleChords();
    const int audible = processor.audibleIndex(), queued = hearingChords ? -1 : processor.queuedIndex();
    const bool hearingA = processor.audibleComparison();
    const bool pendingCompare = !hearingChords && processor.isComparing() != hearingA;
    juce::String audibleText =
        hearingA ? "A reference"
                 : (processor.hasDraft() ? "Preview" : "B / " + juce::String(audible + 1).paddedLeft('0', 2));
    juce::String playback =
        processor.isPlaying() ? (hearingChords ? "Hearing chords" : "Hearing " + audibleText) : "Ready";
    if (processor.isPlaying() && (queued >= 0 || pendingCompare))
        playback +=
            "  >  " + juce::String(processor.isComparing() ? "A" : "B") +
            (queued >= 0 && !processor.isComparing() ? " / " + juce::String(queued + 1).paddedLeft('0', 2)
                                                     : "") +
            " next loop";
    playbackInfo.setText(tr(playback), juce::dontSendNotification);
    playbackInfo.setColour(juce::Label::textColourId, queued >= 0 || pendingCompare ? gold : muted);
    for (int i = 0; i < 12; ++i)
        variants[i]->setPlaybackState(processor.isPlaying() && !hearingChords && !hearingA &&
                                          !processor.hasDraft() && i == audible,
                                      i == queued);
    advanceVisuals();
    if (busy || busy != shownBusy) {
        const float zoom = std::min(1.0f, std::min(getWidth() / 1180.0f, getHeight() / 900.0f));
        repaint(juce::Rectangle<float>(32, 709, 238, 12)
                    .transformedBy(juce::AffineTransform::scale(zoom))
                    .toNearestInt());
    }
    shownBusy = busy;
}
bool Editor::keyPressed(const juce::KeyPress& k) {
    if (guide && guide->isVisible()) {
        if (k.getKeyCode() == juce::KeyPress::escapeKey)
            guide->onClose();
        return true;
    }
    for (auto* focused = juce::Component::getCurrentlyFocusedComponent(); focused != nullptr;
         focused = focused->getParentComponent()) {
        if (dynamic_cast<juce::TextEditor*>(focused) != nullptr)
            return false;
        if (focused == this)
            break;
    }
    if (k.getKeyCode() == juce::KeyPress::escapeKey) {
        if (microPage)
            processor.stopMicrotonalAudition();
        else
            processor.panic();
        return true;
    }
    if (k.getKeyCode() == juce::KeyPress::spaceKey) {
        if (microPage && (processor.previewRequested() || processor.isPlaying()))
            processor.stopMicrotonalAudition();
        else if (processor.previewRequested())
            processor.setPreview(false);
        else if (microPage)
            microPanel->togglePlayback();
        else if (chordPage)
            processor.startChordAudition();
        else
            processor.startAudition();
        return true;
    }
    if (k.getModifiers().isCommandDown() &&
        (k.getTextCharacter() == 'z' || k.getTextCharacter() == 'Z' || k.getKeyCode() == 'Z')) {
        if (microPage) {
            if (k.getModifiers().isShiftDown())
                processor.redoMicrotonal();
            else
                processor.undoMicrotonal();
            microPanel->refreshDisplay();
            return true;
        }
        if (k.getModifiers().isShiftDown())
            processor.redo();
        else
            processor.undo();
        refresh();
        return true;
    }
    return false;
}
bool Editor::isInterestedInFileDrag(const juce::StringArray& files) {
    if (microPage)
        return files.size() == 1 && files[0].endsWithIgnoreCase(".genetic");
    return files.size() == 1 &&
           (files[0].endsWithIgnoreCase(".mid") || files[0].endsWithIgnoreCase(".midi") ||
            files[0].endsWithIgnoreCase(".genetic"));
}
void Editor::filesDropped(const juce::StringArray& files, int, int) {
    if (!isInterestedInFileDrag(files))
        return;
    juce::File file(files[0]);
    auto result = file.hasFileExtension("genetic") ? processor.loadBank(file) : processor.importMidi(file);
    if (result.failed())
        processor.setStatus(result.getErrorMessage());
    refresh();
}
void Editor::chooseFile(int action) {
    const bool saving = action == 1 || action == 2 || action == 6 || action == 7;
    const bool bankFile = action == 2 || action == 3;
    const juce::String title = action == 4   ? "Import harmony reference MIDI"
                               : action == 5 ? "Import groove reference MIDI"
                               : action == 6 ? "Export selected time range"
                               : action == 7 ? "Export melody and chords"
                               : saving      ? (bankFile ? "Save phrase bank" : "Export MIDI")
                                             : (bankFile ? "Open phrase bank" : "Import MIDI");
    chooser =
        std::make_unique<juce::FileChooser>(title,
                                            juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                                                .getChildFile(saving ? (bankFile      ? "My phrases.genetic"
                                                                        : action == 6 ? "Selected range.mid"
                                                                        : action == 7 ? "Melody and chords.mid"
                                                                                      : "Variation.mid")
                                                                     : ""),
                                            bankFile ? "*.genetic" : "*.mid;*.midi");
    const int flags =
        saving ? (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting)
               : (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles);
    const double rangeStart = roll.regionStart(), rangeEnd = roll.regionEnd();
    juce::Component::SafePointer<Editor> safe(this);
    chooser->launchAsync(flags, [safe, action, rangeStart, rangeEnd](const juce::FileChooser& dialog) {
        if (!safe)
            return;
        auto file = dialog.getResult();
        if (file == juce::File())
            return;
        if (action == 1 || action == 6 || action == 7)
            file = file.withFileExtension("mid");
        if (action == 2)
            file = file.withFileExtension("genetic");
        auto& p = safe->processor;
        auto result = action == 0   ? p.importMidi(file)
                      : action == 1 ? p.exportMidi(file)
                      : action == 2 ? p.saveBank(file)
                      : action == 3 ? p.loadBank(file)
                      : action == 4 ? p.importHarmonyMidi(file)
                      : action == 5 ? p.importGrooveMidi(file)
                      : action == 7 ? p.exportMelodyAndChords(file)
                                    : p.exportMidiRange(file, rangeStart, rangeEnd);
        if (result.failed())
            p.setStatus(result.getErrorMessage());
        else if (action == 1 || action == 6 || action == 7)
            p.setStatus(action == 6   ? "Selected time range exported."
                        : action == 7 ? "Melody and chords exported."
                                      : "MIDI exported.");
        else if (action == 2 || action == 3)
            p.setStatus(action == 2 ? "Phrase bank saved." : "Phrase bank restored.");
        safe->refresh();
    });
}
void Editor::startMidiDrag(juce::Component* source) {
    auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getNonexistentChildFile(
                        "Genetic-Variation-" + juce::String(processor.selectedIndex() + 1), ".mid", false);
    auto result = processor.exportMidi(file);
    if (result.failed()) {
        processor.setStatus(result.getErrorMessage());
        return;
    }
    dragFiles.push_back(file);
    if (!juce::DragAndDropContainer::performExternalDragDropOfFiles({file.getFullPathName()}, false,
                                                                    source ? source : &drag))
        processor.setStatus("Drag could not start. Use Export to save this MIDI file.");
}
