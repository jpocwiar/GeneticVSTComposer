#include <iostream>
#include <vector>
#include <string>
#include <regex>
#include <stdexcept>
#include <map>
#include "mingus.hpp"
#include "notes_generator.hpp"

std::vector<std::string> split(const std::string &s, char delim) {
    std::vector<std::string> tokens;
    std::string token;
    size_t start = 0;
    size_t end = 0;

    while ((end = s.find(delim, start)) != std::string::npos) {
        token = s.substr(start, end - start);
        tokens.push_back(token);
        start = end + 1;
    }

    token = s.substr(start);
    tokens.push_back(token);

    return tokens;
}

std::pair<std::string, std::string> NotesGenerator::parseScaleName() {
    std::regex pattern("([A-G]#?|Bb)(.*)");
    std::smatch match;
    if (std::regex_match(key, match, pattern)) {
        std::string note = match[1];
        std::string scaleType = match[2];
        return std::make_pair(note, scaleType);
    } else {
        throw std::invalid_argument("Wrong scale name");
    }
}

std::vector<std::string> NotesGenerator::chooseScale() {
    auto [note, scaleType] = parseScaleName();

    std::map<std::string, Scales::Scale (*)(const std::string&) > scaleMethods = {
        {"Harmonic Minor", [](const std::string& str) -> Scales::Scale { return Scales::HarmonicMinor(str); } },
        {"Natural Minor", [](const std::string& str) -> Scales::Scale { return Scales::NaturalMinor(str); } },
        {"Melodic Minor", [](const std::string& str) -> Scales::Scale { return Scales::MelodicMinor(str); } },
        {"Major", [](const std::string& str) -> Scales::Scale { return Scales::Major(str); } },
        {"Ionian", [](const std::string& str) -> Scales::Scale { return Scales::Ionian(str); } },
        {"Dorian", [](const std::string& str) -> Scales::Scale { return Scales::Dorian(str); } },
        {"Phrygian", [](const std::string& str) -> Scales::Scale { return Scales::Phrygian(str); } },
        {"Lydian", [](const std::string& str) -> Scales::Scale { return Scales::Lydian(str); } },
        {"Mixolydian", [](const std::string& str) -> Scales::Scale { return Scales::Mixolydian(str); } },
        {"Aeolian", [](const std::string& str) -> Scales::Scale { return Scales::Aeolian(str); } },
        {"Locrian", [](const std::string& str) -> Scales::Scale { return Scales::Locrian(str); } },
        {"Chromatic", [](const std::string& str) -> Scales::Scale { return Scales::Chromatic(str); } },
        {"Whole Tone", [](const std::string& str) -> Scales::Scale { return Scales::WholeTone(str); } },
        {"Octatonic", [](const std::string& str) -> Scales::Scale { return Scales::Octatonic(str); } },
    };

    if (scaleMethods.find(scaleType) != scaleMethods.end()) {
        auto scale = scaleMethods[scaleType](note).ascending();
        scale.pop_back();
        return scale;
    } else {
        std::vector scaleTypeSplitted = split(scaleType, ' ');
        for (const std::string& word : scaleTypeSplitted) {
            for (const auto& [key, value] : scaleMethods) {
                if (key.find(word) != std::string::npos) {
                    std::vector<std::string> scale = value(note).ascending();
                    scale.pop_back();
                    return scale;
                }
            }
        }
        std::cout << "Unknown scale. Choosing chromatic..." << std::endl;
        auto scale = Scales::Chromatic(note).ascending();
        scale.pop_back();
        return scale;
    }
}

NotesGenerator::NotesGenerator(const std::string& key) : key(key) {}

std::vector<int> NotesGenerator::generateNotes(int numberOfOctaves, int startOctave) {
    std::vector<std::string> scale = chooseScale();
    std::vector<int> notes;

    for (int octave = startOctave; octave < startOctave + numberOfOctaves; ++octave) {
        for (const std::string& note : scale) {
            int noteValue = Note(note, octave).toInt();
            if (!notes.empty() && noteValue <= notes.back()) {
                noteValue += 12;
            }
            notes.push_back(noteValue);
        }
    }

    return notes;
}

std::vector<int> NotesGenerator::generateChromaticNotes(const std::pair<int, int>& noteRange) {
    int startNote = noteRange.first;
    int endNote = noteRange.second;

    if (startNote <= endNote) {
        std::vector<int> chromaticNotes;
        for (int note = startNote; note <= endNote; ++note) {
            chromaticNotes.push_back(note);
        }
        return chromaticNotes;
    } else {
        throw std::invalid_argument("Wrong argument for notes range");
    }
}
