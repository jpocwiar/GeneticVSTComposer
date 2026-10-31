#pragma once
#include <JuceHeader.h>
#include "ComposerTheme.h"

class ComposerGuide final : public juce::Component {
  public:
    ComposerGuide();
    std::function<void()> onClose;
    std::function<void(int)> onLanguage;
    void setLanguage(int languageIndex);
    void showChapter(int number);
    bool keyPressed(const juce::KeyPress& key) override;
    void paint(juce::Graphics& g) override;
    void resized() override;

  private:
    juce::Label title;
    juce::ComboBox chapter, language;
    juce::TextButton close{"Close / Esc"};
    juce::TextEditor body;
    void updateChapters();
    void updateText();
};
