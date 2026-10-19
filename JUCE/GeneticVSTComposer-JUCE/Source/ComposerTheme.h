#pragma once
#include <JuceHeader.h>

namespace palette {
inline const juce::Colour background{0xff10141d}, panel{0xff191f2a}, surface{0xff252d3a}, line{0xff374252};
inline const juce::Colour text{0xffeef3f3}, muted{0xff9baaba}, accent{0xffbcf28a}, teal{0xff77d6cc},
    gold{0xffedc689}, violet{0xffb3a3ed}, rose{0xffee9caa};
} // namespace palette

// The editor supplies optional hoverAmount / pressAmount properties for smooth
// interaction. The theme remains usable without a timer or editor dependency.
class ComposerLookAndFeel : public juce::LookAndFeel_V4 {
  public:
    ComposerLookAndFeel();
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool, bool) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool, bool) override;
    void drawRotarySlider(juce::Graphics&, int, int, int, int, float, float, float, juce::Slider&) override;
    void drawLinearSlider(juce::Graphics&, int, int, int, int, float, float, float, juce::Slider::SliderStyle,
                          juce::Slider&) override;
    void drawComboBox(juce::Graphics&, int, int, bool, int, int, int, int, juce::ComboBox&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
};
