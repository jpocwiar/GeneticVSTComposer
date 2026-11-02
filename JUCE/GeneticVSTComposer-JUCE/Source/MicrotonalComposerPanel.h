#pragma once
#include <JuceHeader.h>
#include <memory>
class GeneticVSTComposerJUCEAudioProcessor;

class MicrotonalComposerPanel : public juce::Component {
  public:
    explicit MicrotonalComposerPanel(GeneticVSTComposerJUCEAudioProcessor&);
    ~MicrotonalComposerPanel() override;
    void refreshDisplay();
    void togglePlayback();
    void resized() override;
    void paint(juce::Graphics&) override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MicrotonalComposerPanel)
};
