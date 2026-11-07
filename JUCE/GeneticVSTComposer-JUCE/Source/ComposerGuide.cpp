#include "ComposerGuide.h"
#include "BinaryData.h"
#include "EditorFormat.h"

ComposerGuide::ComposerGuide() {
    addAndMakeVisible(title);
    title.setText("Instrukcja / Guide", juce::dontSendNotification);
    title.setFont(font(23, true));
    title.setColour(juce::Label::textColourId, palette::text);
    addAndMakeVisible(language);
    language.addItemList({"Polski", "English"}, 1);
    language.setSelectedId(1, juce::dontSendNotification);
    language.onChange = [this] {
        updateChapters();
        if (onLanguage)
            onLanguage(language.getSelectedId() == 1 ? 1 : 0);
    };
    addAndMakeVisible(chapter);
    chapter.onChange = [this] { updateText(); };
    addAndMakeVisible(close);
    close.onClick = [this] {
        if (onClose)
            onClose();
    };
    addAndMakeVisible(body);
    body.setMultiLine(true, true);
    body.setReadOnly(true);
    body.setScrollbarsShown(true);
    body.setCaretVisible(false);
    body.setFont(font(16));
    body.setColour(juce::TextEditor::backgroundColourId, palette::panel);
    body.setColour(juce::TextEditor::textColourId, palette::text);
    body.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    body.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    body.setIndents(18, 14);
    body.onEscapeKey = [this] {
        if (onClose)
            onClose();
    };
    updateChapters();
    setWantsKeyboardFocus(true);
}
void ComposerGuide::setLanguage(int languageIndex) {
    const int id = languageIndex == 1 ? 1 : 2;
    if (language.getSelectedId() == id)
        return;
    language.setSelectedId(id, juce::dontSendNotification);
    updateChapters();
}
void ComposerGuide::showChapter(int number) {
    chapter.setSelectedId(juce::jlimit(1, std::max(1, chapter.getNumItems()), number),
                          juce::dontSendNotification);
    updateText();
}
bool ComposerGuide::keyPressed(const juce::KeyPress& key) {
    if (key.getKeyCode() == juce::KeyPress::escapeKey && onClose) {
        onClose();
        return true;
    }
    return false;
}
void ComposerGuide::paint(juce::Graphics& g) {
    g.fillAll(palette::background.withAlpha(0.96f));
    g.setColour(palette::panel);
    g.fillRoundedRectangle(getLocalBounds().reduced(24).toFloat(), 12);
    g.setColour(palette::line);
    g.drawRoundedRectangle(getLocalBounds().reduced(24).toFloat(), 12, 1);
}
void ComposerGuide::resized() {
    auto r = getLocalBounds().reduced(44);
    auto header = r.removeFromTop(38);
    close.setBounds(header.removeFromRight(76));
    header.removeFromRight(12);
    language.setBounds(header.removeFromRight(130));
    title.setBounds(header);
    r.removeFromTop(14);
    chapter.setBounds(r.removeFromTop(34));
    r.removeFromTop(12);
    body.setBounds(r);
}
namespace {
struct GuideChapter {
    juce::String title, body;
};
// The guide text lives in Resources/guide_pl.txt and guide_en.txt. A chapter starts with a
// "## title" line; the lines up to the next title are its body.
std::vector<GuideChapter> guideChapters(const char* data, int size) {
    std::vector<GuideChapter> chapters;
    juce::StringArray lines;
    lines.addLines(juce::String::fromUTF8(data, size));
    for (const auto& line : lines) {
        if (line.startsWith("## "))
            chapters.push_back({line.substring(3).trim(), {}});
        else if (!chapters.empty())
            chapters.back().body << line << "\n";
    }
    for (auto& chapter : chapters)
        chapter.body = chapter.body.trim();
    return chapters;
}
const std::vector<GuideChapter>& guide(int languageId) {
    static const auto polish = guideChapters(BinaryData::guide_pl_txt, BinaryData::guide_pl_txtSize);
    static const auto english = guideChapters(BinaryData::guide_en_txt, BinaryData::guide_en_txtSize);
    return languageId == 1 ? polish : english;
}
} // namespace
void ComposerGuide::updateChapters() {
    const int previous = std::max(1, chapter.getSelectedId());
    chapter.clear(juce::dontSendNotification);
    int id = 1;
    for (const auto& entry : guide(language.getSelectedId()))
        chapter.addItem(entry.title, id++);
    chapter.setSelectedId(juce::jlimit(1, std::max(1, chapter.getNumItems()), previous),
                          juce::dontSendNotification);
    updateText();
}
void ComposerGuide::updateText() {
    const auto& chapters = guide(language.getSelectedId());
    if (chapters.empty()) {
        body.clear();
        return;
    }
    body.setText(
        chapters[size_t(juce::jlimit(0, int(chapters.size()) - 1, chapter.getSelectedId() - 1))].body, false);
    body.moveCaretToTop(false);
}
