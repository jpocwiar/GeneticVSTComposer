#pragma once
#include "PluginProcessor.h"

#include "ActivityDisplay.h"
#include "ComposerTheme.h"

class PianoRoll : public juce::Component, public juce::SettableTooltipClient {
  public:
    PianoRoll();
    void setClip(const composer::Clip&, bool resetView = true);
    void setReference(const composer::Clip&, bool visible);
    void setHarmony(const composer::Harmony&);
    void setEditable(bool enabled) { editable = enabled; }
    void setNoteEditing(bool enabled);
    void setMicrotonal(bool enabled);
    void setInterfaceLanguage(int language) { interfaceLanguage = language; }
    void selectAllTime();
    double regionStart() const { return selectionStart; }
    double regionEnd() const { return selectionEnd; }
    void setPlayhead(double beat);
    void setActivity(const GeneticVSTComposerJUCEAudioProcessor::VisualState&, bool showingAudible,
                     bool animate, float elapsed);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed(const juce::KeyPress&) override;
    std::function<void(composer::Clip)> onEdit;
    std::function<void()> onMidiDrag, onAudition;
    std::function<void(double, double)> onRegionChange;

  private:
    composer::Clip clip;
    composer::Clip reference;
    composer::Harmony harmony;
    int low = 48, high = 79, chosen = -1;
    double playhead = -1;
    std::array<float, 128> noteActivity{};
    std::array<float, composer::maxNotes> sourceActivity{};
    bool changed = false, resizingNote = false, changingVelocity = false, selectingTime = false;
    bool editable = true, showReference = false, microtonal = false;
    int interfaceLanguage = 0;
    bool noteEditing = false, dragArmed = false, midiDragged = false;
    double selectionStart = 0, selectionEnd = 16, selectionAnchor = 0;
    composer::Note original;
    juce::Rectangle<float> gridArea() const;
    juce::Rectangle<float> noteRect(const composer::Note&) const;
    double beatAt(float x) const;
    int pitchAt(float y) const;
};
class VariationButton : public juce::Button {
  public:
    explicit VariationButton(int index);
    void setClip(const composer::Clip&, bool selected, bool microtonal = false);
    void setPlaybackState(bool audible, bool queued);
    void advanceAnimation(double beat, float pulse, bool animate, float elapsed);
    void paintButton(juce::Graphics&, bool, bool) override;
    void resized() override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    std::function<void()> onPin, onAudition;

  private:
    int slot;
    bool selected = false, microtonal = false;
    bool audible = false, queued = false;
    float hoverMix = 0, selectMix = 0, energy = 0;
    double beatPosition = 0;
    composer::Clip clip;
    juce::TextButton pin{"+"};
};
class MidiDragButton : public juce::TextButton {
  public:
    MidiDragButton() : TextButton("Drag MIDI") {}
    void mouseDrag(const juce::MouseEvent& e) override {
        if (!dragged && e.getDistanceFromDragStart() > 5) {
            dragged = true;
            if (onDrag)
                onDrag();
        }
    }
    void mouseDown(const juce::MouseEvent& e) override {
        dragged = false;
        TextButton::mouseDown(e);
    }
    void mouseUp(const juce::MouseEvent& e) override {
        if (dragged)
            setState(juce::Button::buttonNormal);
        else
            TextButton::mouseUp(e);
    }
    std::function<void()> onDrag;

  private:
    bool dragged = false;
};
class ComposerGuide;
class ChordComposerPanel;
class TransportButton : public juce::TextButton {
  public:
    TransportButton() : TextButton("Play") {}
    void setPlaying(bool);
    void paintButton(juce::Graphics&, bool, bool) override;

  private:
    bool playing = false;
};
class GeneticVSTComposerJUCEAudioProcessorEditor : public juce::AudioProcessorEditor,
                                                   private juce::Timer,
                                                   public juce::FileDragAndDropTarget {
  public:
    explicit GeneticVSTComposerJUCEAudioProcessorEditor(GeneticVSTComposerJUCEAudioProcessor&);
    ~GeneticVSTComposerJUCEAudioProcessorEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;
    bool isInterestedInFileDrag(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray&, int, int) override;
    void showGuide();
    void showChordComposer();
    void showMelodyComposer();
    void refreshDisplay();
    void showWorkflowPage(int page);

  private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    GeneticVSTComposerJUCEAudioProcessor& processor;
    ComposerLookAndFeel look;
    juce::TooltipWindow tooltips{this, 650};
    PianoRoll roll;
    ActivityDisplay activity;
    juce::ToggleButton motion{"Motion"};
    juce::TextButton interfaceLanguageButton{"EN"};
    std::unique_ptr<juce::PropertiesFile> preferences;
    int shownLanguage = -1;
    std::array<std::unique_ptr<VariationButton>, 12> variants;
    juce::ComboBox preset, root, scale, bars, grid, meter, mode, phraseType, harmony, quality, auditionSound;
    juce::TextButton melodyTab{"Melody"}, chordsTab{"Chords"};
    bool chordPage = false;
    juce::ToggleButton editNotes{"Edit notes"}, customSearch{"Custom budget"};
    juce::Slider searchPopulation, searchGenerations, crossover;
    juce::TextButton searchDetails{"Search settings..."};
    juce::Label searchInfo;
    std::array<juce::Slider, 9> knobs;
    juce::Slider low, high, swing, gate, humanize, tempo, volume;
    juce::ToggleButton snap{"Fit to scale"}, lockRhythm{"Keep rhythm"}, follow{"Follow DAW"},
        monitor{"Internal sound"}, withChords{"With chords"}, chordMidi{"Chord MIDI"}, microEnabled{"Micro"};
    juce::ToggleButton quantiseSwitch{"Switch at loop"};
    juce::TextButton generate{"Generate melody bank"}, evolve{"Evolve selected"}, panic{"Stop all"};
    TransportButton play;
    juce::TextButton undo{"Undo"}, redo{"Redo"}, save{"Save bank"}, load{"Open bank"};
    juce::TextButton import{"Import MIDI"}, exportBoth{"Melody + chords"}, exportFile{"Export..."}, help{"?"};
    MidiDragButton drag;
    juce::Label status, phraseTitle, phraseInfo, tempoInfo;
    juce::TextButton captureA{"Capture A"}, compare{"Hear A"}, clearA{"Clear A"};
    juce::Label playbackInfo, workflowInfo, regionInfo;
    std::array<juce::TextButton, 7> workflowTabs;
    int workflowPage = 0;
    juce::TextButton allTime{"Whole phrase"}, exportRange{"Export range..."}, simplify{"Simplify"},
        movement{"More movement"}, answer{"Answer"}, resolve{"Resolve"};
    juce::TextEditor chordText;
    juce::TextButton applyHarmony{"Apply chords"}, clearHarmony{"Clear"},
        importHarmony{"Import harmony MIDI"}, chordComposer{"Chord composer..."};
    juce::TextButton importGroove{"Import groove MIDI"}, captureGroove{"Use selected"},
        applyGroove{"Apply groove"};
    juce::Slider grooveTiming, grooveVelocity;
    juce::TextButton arrange{"Preview arrangement"}, combine{"Preview combination"},
        applyDraft{"Keep preview"}, discardDraft{"Discard"};
    juce::ComboBox rhythmSource, pitchSource, accentSource;
    juce::ComboBox tuning, microOutput;
    juce::Slider bendRange;
    juce::Label microInfo;
    std::unique_ptr<ComposerGuide> guide;
    std::unique_ptr<ChordComposerPanel> chordPanel;
    std::vector<std::unique_ptr<SliderAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<ComboAttachment>> comboAttachments;
    std::vector<std::unique_ptr<ButtonAttachment>> buttonAttachments;
    std::unique_ptr<juce::FileChooser> chooser;
    std::vector<juce::File> dragFiles;
    uint64_t shownRevision = 0;
    int shownSlot = -1, shownTuning = -1, shownOutput = -1, shownBend = -1;
    bool shownBusy = false;
    int shownMeterNumerator = -1, shownMeterDenominator = -1;
    int shownProgression = -2;
    bool shownCustomHarmony = false;
    uint64_t shownSearchSerial = ~uint64_t{0};
    uint64_t shownStatusSerial = 0;
    int shownBudgetPopulation = -1, shownBudgetGenerations = -1;
    bool shownSearchBusy = false;
    int shownSearchPage = -1;
    juce::String shownSearchDescription;
    double lastFrame = 0, motionPhase = 0, lastReportedBeat = -1, beatReceivedAt = 0;
    float onsetPulse = 0;
    uint64_t seenNoteSerial = 0;
    int timerRate = 60;
    void advanceVisuals();
    juce::String shownHarmony;
    void timerCallback() override;
    void chooseFile(int action);
    void applyInterfaceLanguage();
    void storeInterfaceLanguage(int language);
    juce::String tr(const juce::String&) const;
    void startMidiDrag(juce::Component* source = nullptr);
    void refresh();
    void updateWorkflow();
    void updateMainPage();
    juce::String microDescription() const;
    void report(const juce::Result&);
    void label(juce::Graphics&, const juce::String&, juce::Rectangle<int>, bool heading = false);
    void attach(juce::ComboBox&, const char*);
    void attach(juce::Slider&, const char*, bool rotary = false);
    void attach(juce::ToggleButton&, const char*);
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GeneticVSTComposerJUCEAudioProcessorEditor)
};
