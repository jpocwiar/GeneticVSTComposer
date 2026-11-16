#include "BankPresetLibrary.h"
#include "PluginEditor.h"
#include <memory>
#include <stdexcept>

namespace {
using Processor = GeneticVSTComposerJUCEAudioProcessor;
void check(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
juce::ValueTree state(Processor& p) {
    juce::MemoryBlock data;
    p.getStateInformation(data);
    return juce::ValueTree::fromXml(
        *juce::AudioProcessor::getXmlFromBinary(data.getData(), int(data.getSize())));
}
struct TestLibrary {
    juce::File directory = juce::File::getSpecialLocation(juce::File::tempDirectory)
                               .getNonexistentChildFile("genetic-preset-test", "", false);
    ~TestLibrary() { directory.deleteRecursively(); }
};
void snapshot(juce::Component& component, const juce::File& directory, const char* name) {
    if (directory == juce::File())
        return;
    check(directory.createDirectory().wasOk(), "preset snapshot directory");
    juce::FileOutputStream stream(directory.getChildFile(name));
    check(stream.openedOk(), "preset snapshot output");
    stream.setPosition(0);
    stream.truncate();
    juce::PNGImageFormat png;
    png.writeImageToStream(component.createComponentSnapshot(component.getLocalBounds()), stream);
}
} // namespace

void runBankPresetTests(const juce::File& screenshots) {
    TestLibrary temp;
    BankPresetLibrary library(temp.directory);
    check(library.files().isEmpty() && !temp.directory.exists(), "opening library created files");
    for (const char* name : {"", "../escape", "a/b", "a\\b", "CON", "LPT1", "NUL.x", "trailing.", " leading"})
        check(BankPresetLibrary::validateName(name).failed(), "unsafe preset name accepted");
    check(BankPresetLibrary::validateName(juce::String::repeatedString("a", 81)).failed(), "long name accepted");
    check(BankPresetLibrary::validateName("Riff #01 @ 140").wasOk(), "valid musical preset name rejected");
    const auto name = juce::String::fromUTF8("Nocny motyw łąka");
    auto p = std::make_unique<Processor>();
    auto clip = p->selectedClip();
    clip.notes[0].locked = true;
    clip.notes[0].velocity = 61;
    clip.favourite = true;
    p->setClip(4, clip);
    p->selectClip(4);
    check(p->setCustomHarmony("Cm:4 Ab:4 Eb:4 Bb:4").wasOk(), "preset harmony setup");
    p->useSelectedAsGroove();
    p->captureComparison();
    auto* swing = p->parameters.getParameter("swing");
    swing->setValueNotifyingHost(swing->convertTo0to1(0.31f));
    // Both workspaces belong to a bank preset, including their exact stored notes.
    auto micro = p->microtonalSnapshot().clip;
    micro.count = 1;
    micro.notes[0] = {0, 0.5, 3, 83, true};
    p->setMicrotonalClip(micro);
    p->setMicrotonalMode(true);
    check(library.save(*p, name).wasOk(), "named preset save failed");
    check(p->bankPresetName() == name && library.files().size() == 1, "saved preset not in library");
    const auto saved = library.find(name);
    const auto originalBytes = saved.loadFileAsString();
    auto restored = std::make_unique<Processor>();
    check(restored->loadBank(saved).wasOk(), "named preset load failed");
    check(state(*p).isEquivalentTo(state(*restored)), "preset failed to restore the complete document");
    check(!restored->previewRequested() && !restored->hasDraft(), "preset restored transient playback");
    // Duplicate names never silently replace data, including different letter casing.
    p->setBankPresetName("Unsaved edit");
    clip.notes[0].velocity = 105;
    p->setClip(4, clip);
    check(library.save(*p, name.toUpperCase()).failed(), "duplicate name overwritten without approval");
    check(p->bankPresetName() == "Unsaved edit" && saved.loadFileAsString() == originalBytes,
          "failed save changed preset identity or file");
    check(library.save(*p, name.toUpperCase(), true).wasOk(), "explicit preset update failed");
    check(library.files().size() == 1 && p->bankPresetName() == name, "update duplicated or renamed preset");
    check(restored->loadBank(saved).wasOk() && restored->selectedClip().notes[0].velocity == 105,
          "preset update did not reach disk");
    check(library.save(*p, "Another bank").wasOk() && library.files().size() == 2, "save as lost original preset");

    // A failed replacement leaves both the previous destination and current name intact.
    const auto blocked = temp.directory.getChildFile("Blocked.genetic");
    check(blocked.createDirectory().wasOk(), "failed-write fixture");
    check(p->saveBank(blocked, "Blocked").failed() && blocked.isDirectory() &&
              p->bankPresetName() == "Another bank", "failed write changed destination or current preset");
    const auto exportFile = temp.directory.getChildFile("Portable.genetic");
    check(p->saveBank(exportFile).wasOk() && restored->loadBank(exportFile).wasOk(), "portable bank export");
    check(restored->bankPresetName() == "Another bank", "portable export lost preset name");

    // Older .genetic files have no name metadata; derive a useful name from the file.
    auto legacy = state(*p);
    legacy.removeProperty("presetName", nullptr);
    juce::MemoryBlock bytes;
    juce::AudioProcessor::copyXmlToBinary(*legacy.createXml(), bytes);
    const auto legacyFile = temp.directory.getChildFile("Legacy bank.genetic");
    check(legacyFile.replaceWithData(bytes.getData(), bytes.getSize()) && restored->loadBank(legacyFile).wasOk(),
          "old bank file rejected");
    check(restored->bankPresetName() == "Legacy bank", "old bank did not acquire its filename");
    auto session = std::make_unique<Processor>();
    session->setStateInformation(bytes.getData(), int(bytes.getSize()));
    check(session->bankPresetName().isEmpty(), "old DAW session inherited a preset name");
    const auto invalid = temp.directory.getChildFile("Invalid.genetic");
    check(invalid.replaceWithText("not a bank"), "invalid preset fixture");
    const auto before = state(*restored);
    check(restored->loadBank(invalid).failed() && before.isEquivalentTo(state(*restored)),
          "invalid preset changed the current document");

    // The name is carried inside the DAW state, without depending on a library path.
    juce::MemoryBlock daw;
    p->getStateInformation(daw);
    session->setStateInformation(daw.getData(), int(daw.getSize()));
    check(session->bankPresetName() == "Another bank", "DAW restore lost preset identity");

    p->setInterfaceLanguage(1);
    p->setMicrotonalMode(false);
    auto editor = std::make_unique<GeneticVSTComposerJUCEAudioProcessorEditor>(*p);
    editor->setVisible(true);
    juce::Button* save = nullptr;
    juce::Button* selector = nullptr;
    for (auto* child : editor->getChildren())
        if (auto* button = dynamic_cast<juce::Button*>(child)) {
            if (button->getButtonText() == "Another bank")
                selector = button;
            if (button->getButtonText() == "Zapisz jako...")
                save = button;
        }
    check(save && selector, "preset controls missing or untranslated");
    snapshot(*editor, screenshots, "presets-pl.png");
    editor->showMicrotonalComposer();
    check(selector->isVisible() && save->isVisible(), "preset controls hidden in microtonal workspace");
    // File import must display the workspace stored in the preset immediately.
    editor->filesDropped({saved.getFullPathName()}, 0, 0);
    check(p->isMicrotonalMode(), "file import changed the restored workspace");
    save->onClick();
    auto* dialog = dynamic_cast<juce::AlertWindow*>(juce::ModalComponentManager::getInstance()->getModalComponent(0));
    check(dialog && dialog->getTextEditorContents("name") == name, "save dialog did not suggest current name");
    auto* input = dialog->getTextEditor("name");
    input->setText("../escape", false);
    input->onTextChange();
    check(!dialog->getButton(0)->isEnabled(), "save dialog accepted unsafe name");
    input->setText(juce::String::fromUTF8("Wieczorny riff"), false);
    input->onTextChange();
    check(dialog->getButton(0)->isEnabled(), "save dialog rejected valid name");
    snapshot(*dialog, screenshots, "save-preset-pl.png");
    dialog->exitModalState(0);
    // Closing an editor with an outstanding dialog must release its modal state safely.
    editor.reset();
}
