#include "ChordComposerPanel.h"
#include "ChordGenerator.h"
#include "InterfaceText.h"
#include "PluginEditor.h"
#include <cmath>
#include <set>

namespace {
juce::Font chordFont(float size, bool bold = false) {
#if JUCE_MAC
    return juce::Font(juce::FontOptions("Avenir Next", size, bold ? juce::Font::bold : juce::Font::plain));
#else
    return juce::Font(juce::FontOptions(size, bold ? juce::Font::bold : juce::Font::plain));
#endif
}
juce::String pitchName(int pitch) {
    return juce::MidiMessage::getMidiNoteName(pitch, true, true, 4);
}
void caption(juce::Graphics& g, const juce::String& text, juce::Rectangle<int> bounds,
             juce::Colour colour = palette::muted, float size = 12.0f, bool bold = false) {
    g.setColour(colour);
    g.setFont(chordFont(size, bold));
    g.drawText(text, bounds, juce::Justification::centredLeft);
}

class ChordCards : public juce::Component {
  public:
    void setSequence(const composer::Harmony& h, const composer::Clip& c) {
        harmony = h;
        clip = c;
        repaint();
    }
    void setPlayback(double position) {
        int next = -1;
        for (int i = 0; i < harmony.count && position >= 0; ++i)
            if (position >= harmony.chords[i].start &&
                position < harmony.chords[i].start + harmony.chords[i].duration)
                next = i;
        if (next != active) {
            active = next;
            repaint();
        }
    }
    int sequenceCount() const { return harmony.count; }
    void paint(juce::Graphics& g) override {
        if (harmony.count == 0) {
            caption(g, "Use New chords only or New chords + melody to begin.", getLocalBounds(),
                    palette::muted, 14.0f);
            return;
        }
        const float step = float(getWidth()) / harmony.count;
        for (int i = 0; i < harmony.count; ++i) {
            const auto& chord = harmony.chords[i];
            const juce::Rectangle<float> box(i * step, 0.0f, step - 6.0f, float(getHeight() - 2));
            const bool sounding = active == i;
            g.setGradientFill(
                juce::ColourGradient(sounding ? palette::teal.withAlpha(0.23f) : palette::surface,
                                     box.getTopLeft(), palette::panel, box.getBottomRight(), false));
            g.fillRoundedRectangle(box, 9);
            g.setColour(sounding ? palette::teal : palette::line.withAlpha(0.7f));
            g.drawRoundedRectangle(box.reduced(0.5f), 9, 1);
            auto bounds = box.toNearestInt().reduced(11, 5);
            caption(g, juce::String(i + 1).paddedLeft('0', 2), bounds.removeFromTop(14),
                    sounding ? palette::teal : palette::muted, 10.5f, true);
            caption(g, juce::String(composer::voicedChordName(chord, clip)), bounds.removeFromTop(27),
                    sounding ? palette::teal : palette::text, 20.0f, true);
            juce::StringArray names;
            for (int n = 0; n < clip.count; ++n)
                if (std::abs(clip.notes[n].start - chord.start) < 1.0e-8)
                    names.add(pitchName(clip.notes[n].pitch));
            caption(g, names.joinIntoString(" "), bounds, palette::muted, 11.0f);
        }
    }

  private:
    composer::Harmony harmony;
    composer::Clip clip;
    int active = -1;
};
} // namespace

struct ChordComposerPanel::Impl : public juce::Component {
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    ChordComposerPanel& owner;
    GeneticVSTComposerJUCEAudioProcessor& processor;
    ComposerLookAndFeel look;
    juce::TooltipWindow tooltips{this, 600};
    juce::Slider originality, centre;
    juce::ComboBox rhythm, rhythmShape, voicing, length, key, scale;
    juce::ToggleButton resolve{"Resolve to tonic"};
    juce::TextButton generateChords{"New chords only"}, generateMelody{"Melody from these chords"},
        newPair{"New chords + melody"}, revoice{"Revoice"}, melodySettings{"Melody settings"},
        exportFile{"Export..."}, help{"?"};
    TransportButton audition, together;
    MidiDragButton drag;
    PianoRoll roll;
    ChordCards cards;
    juce::Viewport cardViewport;
    juce::Label context, originalityInfo, status;
    std::vector<std::unique_ptr<SliderAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<ComboAttachment>> comboAttachments;
    std::unique_ptr<ButtonAttachment> resolveAttachment;
    std::unique_ptr<juce::FileChooser> chooser;
    std::vector<juce::File> dragFiles;
    uint64_t shownRevision = 0;
    double lastFrame = 0;
    int chordCount = 0;
    bool busy = false;

    Impl(ChordComposerPanel& panel, GeneticVSTComposerJUCEAudioProcessor& p) : owner(panel), processor(p) {
        setLookAndFeel(&look);
        setOpaque(true);
        for (auto* component : std::initializer_list<juce::Component*>{
                 &originality,    &centre,       &rhythm,         &rhythmShape,     &voicing, &length,
                 &key,
                 &scale,          &resolve,      &generateChords, &generateMelody,  &newPair, &revoice,
                 &melodySettings, &audition,     &together,       &exportFile,      &help,    &drag,
                 &roll,           &cardViewport, &context,        &originalityInfo, &status})
            addAndMakeVisible(component);
        for (auto* label : {&context, &originalityInfo, &status}) {
            label->setFont(chordFont(12));
            label->setColour(juce::Label::textColourId, palette::muted);
            label->setMinimumHorizontalScale(1.0f);
        }
        context.setJustificationType(juce::Justification::centredRight);
        status.setFont(chordFont(11.5f));
        for (auto* slider : {&originality, &centre}) {
            slider->setSliderStyle(juce::Slider::LinearHorizontal);
            slider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 51, 22);
            slider->setColour(juce::Slider::thumbColourId, palette::teal);
            slider->setColour(juce::Slider::trackColourId, palette::teal);
        }
        sliderAttachments.push_back(
            std::make_unique<SliderAttachment>(processor.parameters, "chordOriginality", originality));
        originality.textFromValueFunction = [](double v) {
            return juce::String(int(std::lround(v * 100.0))) + "%";
        };
        originality.valueFromTextFunction = [](const juce::String& value) {
            return value.getDoubleValue() / 100.0;
        };
        originality.updateText();
        sliderAttachments.push_back(
            std::make_unique<SliderAttachment>(processor.parameters, "chordCentre", centre));
        centre.textFromValueFunction = [](double value) { return pitchName(int(std::lround(value))); };
        centre.setTextBoxIsEditable(false);
        centre.updateText();
        auto attachChoice = [&](juce::ComboBox& combo, const char* id) {
            if (auto* parameter =
                    dynamic_cast<juce::AudioParameterChoice*>(processor.parameters.getParameter(id)))
                combo.addItemList(parameter->choices, 1);
            comboAttachments.push_back(std::make_unique<ComboAttachment>(processor.parameters, id, combo));
        };
        attachChoice(rhythm, "chordRhythm");
        attachChoice(rhythmShape, "chordRhythmShape");
        rhythmShape.setTooltip("Even keeps one chord per harmonic-rhythm step. Long-short and short-long "
                               "join neighbouring steps of that same grid.");
        attachChoice(voicing, "chordVoicing");
        attachChoice(key, "root");
        scale.addItemList(GeneticVSTComposerJUCEAudioProcessor::extendedScaleNames(), 1);
        scale.onChange = [this] {
            if (scale.getSelectedId() > 0)
                processor.setScaleChoice(scale.getSelectedId() >= 10 ? scale.getSelectedId()
                                                                     : scale.getSelectedId() - 1);
        };
        length.addItemList({"Follow melody", "1 bar", "2 bars", "3 bars", "4 bars", "6 bars", "8 bars",
                            "12 bars", "16 bars"},
                           1);
        length.onChange = [this] {
            if (length.getSelectedId() > 0)
                processor.setChordLengthMenu(length.getSelectedId());
        };
        length.setTooltip("Length of the next chord sequence, independent of the melody. Melody from these "
                          "chords uses the full active sequence.");
        key.setTooltip(
            "Shared key for new chords and melodies. Existing notes are unchanged until generation.");
        scale.setTooltip(
            "Shared scale for new chords and melodies. Existing notes are unchanged until generation.");
        resolveAttachment = std::make_unique<ButtonAttachment>(processor.parameters, "chordResolve", resolve);
        originality.setTooltip("Low: familiar functional progressions. Higher: substitutions, extensions, "
                               "borrowed chords and secondary dominants. Tonal context and voice leading "
                               "still guide the result. Applies to the next chord generation.");
        rhythm.setTooltip("How often the harmony changes, in bars of the main plugin's meter. Applies "
                          "to the next chord generation.");
        voicing.setTooltip("Smooth inversions minimise voice movement across the sequence. Root position "
                           "keeps the chord root in the bass. Open voicings spread the voices. Click Revoice "
                           "to update the current chords.");
        centre.setTooltip("The preferred register of the chord voices. Click Revoice to update the "
                          "current sequence without changing its harmony.");
        resolve.setTooltip("Bring the progression home to its tonic at the end. Applies to the next "
                           "chord generation.");
        generateChords.setColour(juce::TextButton::buttonColourId, palette::teal);
        generateChords.setColour(juce::TextButton::textColourOffId, palette::background);
        generateMelody.setColour(juce::TextButton::textColourOffId, palette::accent);
        generateChords.setTooltip("Create and activate a new chord sequence. The existing melodies stay "
                                  "as they are until you generate or evolve them.");
        generateMelody.setTooltip(
            "Run the genetic melody search over the active chord sequence, using "
            "its full duration and meter. Keeps these exact chords; replaces unpinned melody variations.");
        newPair.setTooltip("Create a new chord progression, then generate melodies over its full duration. "
                           "Two steps in one action.");
        newPair.setColour(juce::TextButton::buttonColourId, palette::accent);
        newPair.setColour(juce::TextButton::textColourOffId, palette::background);
        revoice.setTooltip("Keep every chord and its timing; find new registers and inversions using "
                           "the current voicing and centre settings.");
        drag.setButtonText("Drag chord MIDI");
        drag.setColour(juce::TextButton::textColourOffId, palette::teal);
        drag.setTooltip("Drag to another instrument track. The MIDI contains the exact chord voices "
                        "shown below, including their inversions. Click to save a MIDI file.");
        audition.setTooltip("Hear this chord sequence with the internal audition sound. Double-click "
                            "the chord piano roll for the same action.");
        together.setButtonText("Play together");
        together.setTooltip("Play the selected melody with this chord sequence through separate internal "
                            "sound layers. MIDI exports remain separate.");
        help.setTooltip("How to compose harmony and export chord MIDI.");
        roll.setEditable(false);
        roll.setNoteEditing(false);
        roll.setTooltip("Drag the chord notes to another instrument track to export their exact MIDI "
                        "voicings. Double-click to audition the sequence. Chords use standard 12-TET.");
        roll.onMidiDrag = [this] { startDrag(&roll); };
        roll.onAudition = [this] {
            processor.startChordAudition();
            refresh();
        };
        generateChords.onClick = [this] {
            report(processor.generateChords());
            refresh();
        };
        generateMelody.onClick = [this] {
            if (processor.isGenerating())
                processor.cancelGeneration();
            else {
                processor.parameters.getParameter("auditionChords")->setValueNotifyingHost(1.0f);
                processor.generate(false, true);
                if (owner.onShowMelody)
                    owner.onShowMelody();
            }
            refresh();
        };
        newPair.onClick = [this] {
            const auto result = processor.generateChords();
            report(result);
            if (result.wasOk()) {
                processor.parameters.getParameter("auditionChords")->setValueNotifyingHost(1.0f);
                processor.generate(false, true);
                if (owner.onShowMelody)
                    owner.onShowMelody();
            }
            refresh();
        };
        melodySettings.onClick = [this] {
            if (owner.onShowMelody)
                owner.onShowMelody();
        };
        melodySettings.setTooltip("Return to Melody to choose the key, scale, phrase type and meter, "
                                  "or play the melody together with these chords.");
        revoice.onClick = [this] {
            report(processor.revoiceChords());
            refresh();
        };
        audition.onClick = [this] {
            if (processor.isAuditioningChords() && processor.previewRequested())
                processor.setPreview(false);
            else
                processor.startChordAudition();
            refresh();
        };
        together.onClick = [this] {
            if (processor.previewRequested() && !processor.isAuditioningChords() &&
                parameter("auditionChords") > 0.5f)
                processor.setPreview(false);
            else {
                auto* p = processor.parameters.getParameter("auditionChords");
                p->setValueNotifyingHost(1.0f);
                processor.startAudition();
            }
            refresh();
        };
        exportFile.onClick = [this] { chooseExport(); };
        drag.onClick = [this] { chooseExport(); };
        drag.onDrag = [this] { startDrag(&drag); };
        help.onClick = [this] {
            if (owner.onShowHelp)
                owner.onShowHelp();
            else
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::InfoIcon, "Chord Composer",
                    "1. Set key, scale and Length here; set Meter on Melody. Chord Length defaults to 4 "
                    "bars.\n\n"
                    "2. Choose Originality and harmonic rhythm, then New chords only. The new harmony "
                    "is active immediately. Existing melodies are unchanged.\n\n"
                    "3. Press Play below the chord piano roll. Choose a voicing and centre, then Revoice to "
                    "change their "
                    "inversions and register while keeping the progression.\n\n"
                    "4. Melody from these chords keeps the sequence and generates a full-length bank. New "
                    "chords + melody creates both. On Melody, enable "
                    "With chords and press Play to hear both layers together.\n\n"
                    "5. Drag the chord piano roll or Drag chord MIDI to a separate instrument track. "
                    "Export saves the same notes to a MIDI file.\n\n"
                    "A slash name shows the actual bass: C/E means a C chord with E at the bottom. "
                    "Chord MIDI uses standard 12-tone equal temperament, even when the melody uses Micro.");
        };
        cardViewport.setViewedComponent(&cards, false);
        cardViewport.setScrollBarsShown(false, true);
        cardViewport.setScrollBarThickness(8);
        refresh();
    }

    ~Impl() override {
        chooser.reset();
        cardViewport.setViewedComponent(nullptr, false);
        for (const auto& file : dragFiles)
            file.deleteFile();
        setLookAndFeel(nullptr);
    }

    float parameter(const char* id) const { return processor.parameters.getRawParameterValue(id)->load(); }
    void report(const juce::Result& result) {
        if (result.failed())
            processor.setStatus(result.getErrorMessage());
    }

    void refresh() {
        const auto revision = processor.documentRevision();
        if (shownRevision != revision) {
            const auto harmony = processor.harmonySnapshot();
            const auto clip = processor.chordClipSnapshot();
            chordCount = clip.count;
            roll.setClip(clip);
            roll.setHarmony(harmony);
            cards.setSequence(harmony, clip);
            shownRevision = revision;
            resized();
        }
        const int scaleNow = processor.scaleIndex();
        const int scaleId = scaleNow >= 10 ? scaleNow : scaleNow + 1;
        if (!scale.isPopupActive() && scale.getSelectedId() != scaleId)
            scale.setSelectedId(scaleId, juce::dontSendNotification);
        if (!length.isPopupActive() && length.getSelectedId() != processor.chordLengthMenuId())
            length.setSelectedId(processor.chordLengthMenuId(), juce::dontSendNotification);
        const auto meter = processor.timeSignature();
        const auto sequence = processor.harmonySnapshot();
        const auto activeClip = processor.chordClipSnapshot();
        std::set<std::pair<int, int>> distinct;
        for (int i = 0; i < sequence.count; ++i)
            distinct.emplace(sequence.chords[i].root, sequence.chords[i].quality);
        const auto text = [this](const juce::String& english) {
            return interfaceText(english, processor.interfaceLanguageIndex());
        };
        context.setText(text(sequence.count ? juce::String(sequence.count) + " positions / " +
                                                  juce::String(int(distinct.size())) + " distinct chords / " +
                                                  juce::String(activeClip.length / (activeClip.numerator * 4.0 /
                                                                                    activeClip.denominator),
                                                               1) +
                                                  " bars"
                                            : "No chord sequence yet"),
                        juce::dontSendNotification);
        context.setTooltip("This describes the active sequence. Controls above the actions configure the "
                           "next generation. Meter: " +
                           juce::String(meter.numerator) + "/" + juce::String(meter.denominator));
        const float value = parameter("chordOriginality");
        originalityInfo.setText(text(value < 0.3f   ? "Familiar movement, clear tonal direction."
                                      : value < 0.7f ? "Fresh substitutions with a recognisable tonal centre."
                                                     : "Borrowed colours, richer chords, unexpected turns."),
                                juce::dontSendNotification);
        busy = processor.isGenerating();
        generateChords.setEnabled(!busy);
        newPair.setEnabled(!busy);
        revoice.setEnabled(!busy && chordCount > 0);
        generateMelody.setEnabled(busy || chordCount > 0);
        generateMelody.setButtonText(text(busy ? "Cancel search " +
                                                    juce::String(int(processor.generationProgress() * 100)) + "%"
                                              : "Melody from these chords"));
        audition.setEnabled(chordCount > 0);
        together.setEnabled(chordCount > 0 && processor.displayedClip().count > 0);
        drag.setEnabled(chordCount > 0);
        exportFile.setEnabled(chordCount > 0);
        const bool hearingChords = processor.audibleChordLayer();
        audition.setPlaying(processor.isAuditioningChords() && processor.previewRequested());
        const bool playingTogether = processor.previewRequested() && !processor.isAuditioningChords() &&
                                     parameter("auditionChords") > 0.5f;
        together.setPlaying(playingTogether);
        together.setButtonText(text(playingTogether ? "Stop mix" : "Play together"));
        generateChords.setButtonText(text("New chords only"));
        newPair.setButtonText(text("New chords + melody"));
        revoice.setButtonText(text("Revoice"));
        melodySettings.setButtonText(text("Melody settings"));
        exportFile.setButtonText(text("Export..."));
        resolve.setButtonText(text("Resolve to tonic"));
        drag.setButtonText(text("Drag chord MIDI"));
        audition.setButtonText(text(audition.getButtonText()));
        roll.setInterfaceLanguage(processor.interfaceLanguageIndex());
        const double position = hearingChords && processor.isPlaying() ? processor.chordPlayPosition() : -1.0;
        roll.setPlayhead(position);
        cards.setPlayback(position);
        const double now = juce::Time::getMillisecondCounterHiRes();
        const float elapsed = float(juce::jlimit(0.001, 0.1, (now - lastFrame) * 0.001));
        lastFrame = now;
        roll.setActivity(hearingChords ? processor.chordVisualState()
                                       : GeneticVSTComposerJUCEAudioProcessor::VisualState{},
                         hearingChords, true, elapsed);
        const auto message = processor.statusText();
        status.setText(message, juce::dontSendNotification);
        status.setTooltip(message);
    }

    void chooseExport() {
        chooser = std::make_unique<juce::FileChooser>(
            "Export voiced chord MIDI",
            juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Chords.mid"),
            "*.mid;*.midi");
        juce::Component::SafePointer<Impl> safe(this);
        chooser->launchAsync(
            juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
            [safe](const juce::FileChooser& dialog) {
                if (!safe || dialog.getResult() == juce::File())
                    return;
                auto file = dialog.getResult();
                if (!file.hasFileExtension("mid;midi"))
                    file = file.withFileExtension("mid");
                const auto result = safe->processor.exportChordMidi(file);
                if (result.failed())
                    safe->report(result);
                else
                    safe->processor.setStatus("Chord MIDI exported with the displayed voicings.");
                safe->refresh();
            });
    }

    void startDrag(juce::Component* source) {
        auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                        .getNonexistentChildFile("Genetic-Chords", ".mid", false);
        const auto result = processor.exportChordMidi(file);
        if (result.failed()) {
            report(result);
            return;
        }
        dragFiles.push_back(file);
        if (!juce::DragAndDropContainer::performExternalDragDropOfFiles({file.getFullPathName()}, false,
                                                                        source))
            processor.setStatus("Drag could not start. Use Export to save the chord MIDI file.");
    }

    void resized() override {
        const int w = getWidth(), h = getHeight();
        const int split = int(w * 0.43f), right = split + 36, rightWidth = w - right - 36;
        context.setBounds(w - 400, 20, 376, 25);
        help.setBounds(w - 52, 52, 28, 24);
        key.setBounds(20, 69, 70, 28);
        scale.setBounds(100, 69, 210, 28);
        length.setBounds(322, 69, 120, 28);
        rhythmShape.setBounds(452, 69, 150, 28);
        originality.setBounds(34, 117, split - 24, 35);
        originalityInfo.setBounds(34, 172, split - 24, 25);
        const int rhythmWidth = int(rightWidth * 0.4f);
        rhythm.setBounds(right, 119, rhythmWidth - 10, 30);
        voicing.setBounds(right + rhythmWidth, 119, rightWidth - rhythmWidth, 30);
        centre.setBounds(right, 176, rhythmWidth - 10, 29);
        resolve.setBounds(right + rhythmWidth, 176, rightWidth - rhythmWidth, 29);
        generateChords.setBounds(20, 232, 160, 35);
        generateMelody.setBounds(190, 232, 216, 35);
        newPair.setBounds(416, 232, 196, 35);
        revoice.setBounds(w - 124, 232, 104, 35);
        melodySettings.setBounds(w - 188, 52, 126, 25);
        cardViewport.setBounds(20, 302, w - 40, 82);
        const int cardWidth = std::max(w - 40, cards.sequenceCount() * 137);
        cards.setSize(cardWidth, 72);
        roll.setBounds(20, 420, w - 40, std::max(135, h - 536));
        audition.setBounds(20, h - 104, 86, 31);
        together.setBounds(116, h - 104, 160, 31);
        drag.setBounds(w - 262, h - 104, 146, 31);
        exportFile.setBounds(w - 106, h - 104, 86, 31);
        status.setBounds(20, h - 30, w - 40, 23);
    }

    void paint(juce::Graphics& g) override {
        const int w = getWidth(), h = getHeight();
        const auto text = [this](const char* english) {
            return interfaceText(english, processor.interfaceLanguageIndex());
        };
        g.setGradientFill(juce::ColourGradient(palette::background.brighter(0.045f), 0, 0,
                                               palette::background, float(w), float(h), false));
        g.fillAll();
        caption(g, text("CHORDS"), {20, 17, 370, 30}, palette::text, 24, true);
        caption(g, text("KEY / SCALE"), {20, 48, 290, 19}, palette::muted, 11);
        caption(g, text("SEQUENCE LENGTH"), {322, 48, 140, 19}, palette::muted, 11);
        caption(g, text("RHYTHM SHAPE"), {452, 48, 150, 19}, palette::muted, 11);
        const juce::Rectangle<float> controls(20, 107, float(w - 40), 109);
        g.setGradientFill(juce::ColourGradient(palette::panel.brighter(0.045f), controls.getTopLeft(),
                                               palette::panel, controls.getBottomRight(), false));
        g.fillRoundedRectangle(controls, 12);
        g.setColour(palette::line.withAlpha(0.7f));
        g.drawRoundedRectangle(controls.reduced(0.5f), 12, 1);
        const int split = int(w * 0.43f), right = split + 36, rightWidth = w - right - 36;
        const int rhythmWidth = int(rightWidth * 0.4f);
        caption(g, text("ORIGINALITY"), {36, 97, split - 36, 19}, palette::teal, 11.5f, true);
        caption(g, text("Familiar"), {38, 150, 150, 20}, palette::muted, 11);
        g.setColour(palette::muted);
        g.setFont(chordFont(11));
        g.drawText(text("Adventurous"), juce::Rectangle<int>(split - 144, 150, 100, 20),
                   juce::Justification::centredRight);
        caption(g, text("HARMONIC RHYTHM"), {right, 97, rhythmWidth - 10, 18}, palette::muted, 11, true);
        caption(g, text("VOICING"), {right + rhythmWidth, 97, rightWidth - rhythmWidth, 18}, palette::muted, 11,
                true);
        caption(g, text("VOICE CENTRE"), {right, 156, rhythmWidth - 10, 18}, palette::muted, 11, true);
        caption(g,
                text("New chords only keeps your melodies. Melody from these chords rewrites the unpinned "
                     "melodies to this full sequence."),
                {20, 274, w - 40, 20}, palette::muted, 12);
        caption(g, text("VOICED CHORDS"), {20, 396, 270, 25}, palette::teal, 12, true);
        caption(
            g,
            text("Play previews these chords. Melody > With chords plays both layers. Drag notes for chord MIDI."),
            {20, h - 61, w - 40, 22}, palette::muted, 11.5f);
        caption(g, text("Chord MIDI: standard 12-TET, also when the melody uses Micro."),
                {20, h - 44, w - 40, 18}, palette::muted, 10.5f);
    }
};

ChordComposerPanel::ChordComposerPanel(GeneticVSTComposerJUCEAudioProcessor& processor)
    : impl(std::make_unique<Impl>(*this, processor)) {
    setOpaque(true);
    addAndMakeVisible(*impl);
    setSize(940, 660);
    startTimerHz(timerRate);
}

ChordComposerPanel::~ChordComposerPanel() {
    stopTimer();
}

void ChordComposerPanel::refreshDisplay() {
    impl->refresh();
}

void ChordComposerPanel::paint(juce::Graphics& g) {
    g.fillAll(palette::background);
}

void ChordComposerPanel::resized() {
    const float scale = std::min(1.0f, std::min(float(getWidth()) / 940.0f, float(getHeight()) / 660.0f));
    if (scale <= 0)
        return;
    impl->setTransform(juce::AffineTransform::scale(scale));
    impl->setBounds(0, 0, int(std::ceil(getWidth() / scale)), int(std::ceil(getHeight() / scale)));
}

void ChordComposerPanel::timerCallback() {
    const int wantedRate = isShowing() ? 30 : 4;
    if (wantedRate != timerRate) {
        timerRate = wantedRate;
        startTimerHz(timerRate);
    }
    if (isShowing())
        refreshDisplay();
}
