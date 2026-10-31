#pragma once
#include "PluginProcessor.h"

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
