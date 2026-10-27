#pragma once
#include <JuceHeader.h>
#include <functional>
#include <memory>

class GeneticVSTComposerJUCEAudioProcessor;

// The main editor hosts this panel on its Chords page.
class ChordComposerPanel : public juce::Component, private juce::Timer {
  public:
    explicit ChordComposerPanel(GeneticVSTComposerJUCEAudioProcessor&);
    ~ChordComposerPanel() override;
    void refreshDisplay();
    void paint(juce::Graphics&) override;
    void resized() override;
    std::function<void()> onShowHelp;
    std::function<void()> onShowMelody;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    int timerRate = 30;
    void timerCallback() override;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChordComposerPanel)
};
