#pragma once
#include <JuceHeader.h>
#include <functional>
#include <memory>

class GeneticVSTComposerJUCEAudioProcessor;

// A document-backed composition workspace embedded in the main editor.
class CompositionPanel : public juce::Component, private juce::Timer {
  public:
    explicit CompositionPanel(GeneticVSTComposerJUCEAudioProcessor&);
    ~CompositionPanel() override;
    void refreshDisplay();
    void showPage(int page);
    int pageIndex() const;
    void paint(juce::Graphics&) override;
    void resized() override;
    std::function<void()> onShowMelody;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    void timerCallback() override;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CompositionPanel)
};
