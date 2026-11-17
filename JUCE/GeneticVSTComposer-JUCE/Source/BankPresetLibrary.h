#pragma once
#include "PluginProcessor.h"

// A bank preset is the complete committed document, using the existing .genetic format.
// The directory can be supplied explicitly for portable libraries and isolated tests.
class BankPresetLibrary {
  public:
    explicit BankPresetLibrary(juce::File directory = defaultDirectory()) : folder(std::move(directory)) {}

    static juce::File defaultDirectory() {
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("Genetic Composer").getChildFile("Presets");
    }
    const juce::File& directory() const { return folder; }
    juce::Array<juce::File> files() const {
        auto result = folder.findChildFiles(juce::File::findFiles, false, "*.genetic");
        result.sort();
        return result;
    }
    static juce::Result validateName(const juce::String& name) {
        if (name.isEmpty() || name.length() > 80 || name != name.trim() ||
            name.endsWithChar('.') || name.containsAnyOf("<>:\"/\\|?*"))
            return juce::Result::fail("Use 1-80 characters without file-name symbols or trailing dots.");
        for (auto c : name)
            if (c < 32)
                return juce::Result::fail("Use 1-80 characters without file-name symbols or trailing dots.");
        const auto stem = name.upToFirstOccurrenceOf(".", false, false).toUpperCase();
        if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL" ||
            (stem.length() == 4 && (stem.startsWith("COM") || stem.startsWith("LPT")) &&
             stem[3] >= '1' && stem[3] <= '9'))
            return juce::Result::fail("Choose another preset name.");
        return juce::Result::ok();
    }
    juce::File find(const juce::String& name) const {
        if (validateName(name).failed())
            return {};
        for (const auto& file : files())
            if (file.getFileNameWithoutExtension().equalsIgnoreCase(name))
                return file;
        return {};
    }
    juce::Result save(GeneticVSTComposerJUCEAudioProcessor& processor, const juce::String& name,
                      bool overwrite = false) const {
        if (auto result = validateName(name); result.failed())
            return result;
        auto file = find(name);
        if (file != juce::File() && !overwrite)
            return juce::Result::fail("A preset with this name already exists.");
        if (auto result = folder.createDirectory(); result.failed())
            return juce::Result::fail("Could not create the preset folder.");
        if (file == juce::File())
            file = folder.getChildFile(name + ".genetic");
        // Preserve the spelling of an existing name on case-insensitive filesystems.
        const auto savedName = file.getFileNameWithoutExtension();
        if (auto result = processor.saveBank(file, savedName); result.failed())
            return result;
        processor.setBankPresetName(savedName);
        return juce::Result::ok();
    }

  private:
    juce::File folder;
};
