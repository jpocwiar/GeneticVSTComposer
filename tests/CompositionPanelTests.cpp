#include "CompositionPanel.h"
#include "PianoRoll.h"
#include "PluginEditor.h"
#include <chrono>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {
using Processor = GeneticVSTComposerJUCEAudioProcessor;
void check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
juce::Component* findId(juce::Component& component, const juce::String& id) {
    if (component.getComponentID() == id)
        return &component;
    for (auto* child : component.getChildren())
        if (auto* found = findId(*child, id))
            return found;
    return nullptr;
}
template <class T> T& control(juce::Component& component, const char* id) {
    auto* found = dynamic_cast<T*>(findId(component, id));
    check(found != nullptr, "composition UI control missing");
    return *found;
}
void click(juce::Component& panel, const char* id) {
    auto& button = control<juce::Button>(panel, id);
    check(button.isVisible() && button.isEnabled() && bool(button.onClick),
          "composition action is not available");
    button.onClick();
}
composer::Clip fixture(int shift = 0) {
    composer::Clip clip;
    clip.length = 8;
    clip.seed = 42;
    clip.count = 16;
    const int pitches[]{60, 64, 67, 65, 64, 62, 60, 67};
    for (int i = 0; i < clip.count; ++i)
        clip.notes[size_t(i)] = {i * 0.5, 0.375, pitches[i % 8] + shift, 80 + i, i == 0, 0};
    return clip;
}
void validateLayout(CompositionPanel& panel) {
    std::vector<juce::Rectangle<int>> interactive;
    const auto visit = [&](auto&& self, juce::Component& component) -> void {
        if (!component.isVisible())
            return;
        const bool isControl =
            dynamic_cast<juce::Button*>(&component) || dynamic_cast<juce::ComboBox*>(&component) ||
            dynamic_cast<juce::Slider*>(&component) || dynamic_cast<juce::TextEditor*>(&component);
        if (isControl) {
            const auto bounds = panel.getLocalArea(&component, component.getLocalBounds());
            check(bounds.getWidth() > 0 && bounds.getHeight() > 0 &&
                      panel.getLocalBounds().expanded(1).contains(bounds),
                  "composition control is empty or outside the panel");
            for (const auto& prior : interactive)
                check(!prior.intersects(bounds), "composition controls overlap");
            interactive.push_back(bounds);
            return;
        }
        for (auto* child : component.getChildren())
            self(self, *child);
    };
    visit(visit, panel);
    auto& roll = control<PianoRoll>(panel, "compose-piano-roll");
    const auto bounds = panel.getLocalArea(&roll, roll.getLocalBounds());
    check(bounds.getHeight() > 100 && panel.getLocalBounds().contains(bounds),
          "composition piano roll is too small or clipped");
    for (const auto& rectangle : interactive)
        check(!rectangle.intersects(bounds), "composition controls overlap the piano roll");
}
} // namespace

void runCompositionPanelTests() {
    auto processor = std::make_unique<Processor>();
    processor->setClip(0, fixture());
    auto other = fixture(3);
    other.notes[3].duration = 0.125;
    other.notes[5].start += 0.25;
    processor->setClip(1, other);
    processor->selectClip(0);
    processor->prepareToPlay(48000, 256);
    auto editor = std::make_unique<GeneticVSTComposerJUCEAudioProcessorEditor>(*processor);
    processor->startAudition();
    click(*editor, "compose-main-tab");
    auto& panel = control<CompositionPanel>(*editor, "composition-panel");
    check(panel.isVisible() && processor->previewRequested(), "opening Compose stopped an existing audition");
    for (auto* child : editor->getChildren())
        if (dynamic_cast<PianoRoll*>(child))
            check(!child->isVisible(), "Compose did not hide the Melody piano roll");
    auto& roll = control<PianoRoll>(panel, "compose-piano-roll");
    roll.setRegion(0, 2);
    click(panel, "compose-capture-motif");
    check(processor->compositionSnapshot().preserveHook && processor->compositionSnapshot().hook.count > 0 &&
              processor->compositionSnapshot().hook.length <= 2.0001,
          "motif capture did not use the selected time range");
    auto& role = control<juce::ComboBox>(panel, "compose-role");
    role.setSelectedId(2, juce::sendNotificationSync);
    check(processor->compositionSnapshot().role == 1, "composition role did not reach the engine settings");
    auto& identity = control<juce::Slider>(panel, "compose-identity");
    identity.setValue(0.63, juce::sendNotificationSync);
    check(std::abs(processor->compositionSnapshot().identity - 0.63f) < 0.001f,
          "keyboard or text identity edits were not saved");
    auto& a = control<juce::ComboBox>(panel, "compose-preferred-variation");
    auto& b = control<juce::ComboBox>(panel, "compose-other-variation");
    a.setSelectedId(1, juce::sendNotificationSync);
    b.setSelectedId(2, juce::sendNotificationSync);
    click(panel, "compose-prefer");
    check(processor->compositionSnapshot().preference.comparisons == 1,
          "the preference action did not learn the explicit A/B choice");

    panel.showPage(1);
    click(panel, "compose-create-sections");
    check(processor->compositionSnapshot().sections.size() >= 3,
          "composition action did not build independent sections");
    const auto before = processor->compositionSnapshot();
    auto& name = control<juce::TextEditor>(panel, "compose-section-name");
    name.setText("Verse", false);
    name.onReturnKey();
    auto& bars = control<juce::ComboBox>(panel, "compose-section-bars");
    bars.setSelectedId(3, juce::sendNotificationSync);
    auto& harmony = control<juce::TextEditor>(panel, "compose-section-harmony");
    harmony.setText("Cm:4 Fm:4 G7:4", false);
    click(panel, "compose-apply-section-harmony");
    const auto section = processor->compositionSnapshot();
    check(section.sections[0].name == "Verse" && section.sections[0].melody.length == 12 &&
              section.sections[0].harmony.count == 3,
          "section name, length or harmony edits were not committed");
    check(section.sections[1].melody.length == before.sections[1].melody.length &&
              section.sections[1].melody.seed == before.sections[1].melody.seed,
          "editing one section changed a different section");
    auto changed = section.sections[0].melody;
    check(changed.count > 1, "section note fixture is empty");
    changed.notes[1].pitch = 70;
    roll.onEdit(changed);
    check(processor->compositionSnapshot().sections[0].melody.notes[1].pitch == 70,
          "section piano-roll editing was not committed");
    const auto count = processor->compositionSnapshot().sections.size();
    click(panel, "compose-add-section");
    check(processor->compositionSnapshot().sections.size() == count + 1, "add section action failed");
    click(panel, "compose-remove-section");
    check(processor->compositionSnapshot().sections.size() == count, "remove section action failed");
    check(processor->selectCompositionSection(1).wasOk(), "section reorder fixture");
    panel.refreshDisplay();
    const auto movingName = processor->compositionSnapshot().sections[1].name;
    click(panel, "compose-section-earlier");
    check(processor->compositionSnapshot().sections[0].name == movingName &&
              processor->compositionSnapshot().selected == 0,
          "moving a section did not retain its contents and selection");
    processor->auditionComposition(false);
    juce::AudioBuffer<float> audio(2, 256);
    juce::MidiBuffer midi;
    processor->processBlock(audio, midi);
    check(processor->isCompositionPlaying(), "composition audition fixture did not start");
    editor->showMelodyComposer();
    editor->refreshDisplay();
    bool identifiesComposition = false;
    for (auto* child : editor->getChildren())
        if (auto* label = dynamic_cast<juce::Label*>(child))
            identifiesComposition |= label->getText().startsWith("Composition / section") ||
                                     label->getText().startsWith("Kompozycja / sekcja");
    check(processor->isCompositionPlaying() && identifiesComposition,
          "returning to Melody stopped composition playback or labelled it as a bank variation");
    editor->showChordComposer();
    check(processor->isCompositionPlaying(), "opening Chords stopped composition playback");
    editor->showCompositionWorkspace();
    processor->stopComposition();
    midi.clear();
    processor->processBlock(audio, midi);
    processor->setPreview(false);

    for (int language : {0, 1}) {
        processor->setInterfaceLanguage(language);
        panel.refreshDisplay();
        for (int page = 0; page < 3; ++page) {
            panel.showPage(page);
            for (auto size : {juce::Point<int>{940, 660}, juce::Point<int>{760, 540}}) {
                panel.setSize(size.x, size.y);
                panel.refreshDisplay();
                validateLayout(panel);
            }
        }
    }
    editor->setSize(1180, 900);
    editor->showCompositionWorkspace();
    for (auto size : {juce::Point<int>{1180, 900}, juce::Point<int>{870, 630}}) {
        editor->setSize(size.x, size.y);
        auto& main = control<juce::Button>(*editor, "compose-main-tab");
        const auto mainBounds = main.getBoundsInParent();
        for (auto* child : editor->getChildren())
            if (child != &main && child->isVisible() &&
                (dynamic_cast<juce::ComboBox*>(child) || dynamic_cast<juce::Button*>(child)))
                check(!mainBounds.intersects(child->getBoundsInParent()),
                      "the fourth main tab overlaps another control");
    }
    editor->showMelodyComposer();
    check(!panel.isVisible(), "returning to Melody left Compose visible");
    editor->showMicrotonalComposer();
    editor->showCompositionWorkspace();
    check(!processor->isMicrotonalMode() && panel.isVisible(),
          "opening Compose did not leave the independent microtonal workspace");
    processor->releaseResources();
}

void renderCompositionPanelPreviews(const juce::File& directory) {
    check(directory.createDirectory().wasOk(), "composition preview output directory");
    auto processor = std::make_unique<Processor>();
    processor->setClip(0, fixture());
    processor->selectClip(0);
    check(processor->setCustomHarmony("Cm:4 Fm:4").wasOk(), "composition preview harmony");
    check(processor->captureMotif(0, 2).wasOk(), "composition preview motif");
    check(processor->createComposition().wasOk(), "composition preview sections");
    check(processor->updateCompositionSection(0, "A / Theme", 2, 0.35f, 0.0f).wasOk(),
          "composition preview section edit");
    if (processor->compositionSnapshot().sections.size() > 2)
        check(processor->updateCompositionSection(2, "B / Answer", 2, 0.7f, 0.65f).wasOk(),
              "composition preview answer edit");
    processor->preferVariation(0, 1);
    auto editor = std::make_unique<GeneticVSTComposerJUCEAudioProcessorEditor>(*processor);
    editor->setVisible(true);
    editor->showCompositionWorkspace();
    auto& panel = control<CompositionPanel>(*editor, "composition-panel");
    const char* names[]{"ideas", "sections", "context"};
    for (int language : {0, 1}) {
        processor->setInterfaceLanguage(language);
        for (int page = 0; page < 3; ++page) {
            panel.showPage(page);
            for (bool compact : {false, true}) {
                editor->setSize(compact ? 870 : 1180, compact ? 630 : 900);
                editor->refreshDisplay();
                auto snapshot = editor->createComponentSnapshot(editor->getLocalBounds());
                const auto filename = juce::String("ui-compose-") + names[page] +
                                      (language == 1 ? "-pl" : "-en") + (compact ? "-compact.png" : ".png");
                juce::FileOutputStream stream(directory.getChildFile(filename));
                check(stream.openedOk(), "composition preview output file");
                stream.setPosition(0);
                stream.truncate();
                juce::PNGImageFormat png;
                check(png.writeImageToStream(snapshot, stream), "composition preview PNG write");
            }
        }
    }
}
