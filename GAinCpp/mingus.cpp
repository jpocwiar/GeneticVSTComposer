#include <iostream>
#include "mingus.hpp"
#include <cctype>

// UTILITY FUNCTIONS
bool isLower(char c) {
    return (c >= 'a' && c <= 'z');
}

bool isLowerString(const std::string& str) {
    for (auto it = str.begin(); it != str.end(); ++it) {
        if (!isLower(*it)) {
            return false;
        }
    }
    return true;
}

std::string toLower(const std::string& str) {
    std::string result;
    for (auto it = str.begin(); it != str.end(); ++it) {
        result += std::tolower(*it);
    }
    return result;
}

std::vector<std::string> multiplyVectors(const std::vector<std::string>& vec, int n) {
    std::vector<std::string> result;
    for (int i = 0; i < n; ++i) {
        result.insert(result.end(), vec.begin(), vec.end());
    }
    return result;
}

// MINGUS UTILITY FUNCTIONS FOR NOTES
const std::map<std::string, int> noteDict {
    {"C", 0},
    {"D", 2},
    {"E", 4},
    {"F", 5},
    {"G", 7},
    {"A", 9},
    {"B", 11}
};

// Return true if note is in a recognised format. False if not.
bool isValidNote(std::string note) {
    auto it = noteDict.find(note);
    if (it == noteDict.end()) {
        return false;
    }

    for (size_t i = 1; i < note.length(); ++i) {
        if (note[i] != 'b' && note[i] != '#') {
            return false;
        }
    }
    return true;
}

// Convert notes in the form of C, C#, Cb, C##, etc. to an integer in the range of 0-11.
int noteToInt(std::string note) {
    if (!isValidNote(note)) {
        throw std::invalid_argument("Unknown note format '" + note + "'");
    }

    int val = noteDict.at(note.substr(0,1)); // note[0]

    for (size_t i = 1; i < note.length(); ++i) {

        if (note[i] == 'b') {
            val--;
        } else if (note[i] == '#') {
            val++;
        }
    }
    return val;
}

// MINGUS NOTE
Note::Note(const std::string& name_, int octave_) : name(name_), octave(octave_) {}
int Note::toInt() const {
    int result = octave * 12 + noteToInt(name.substr(0,1));
    for (size_t i = 1; i < name.length(); ++i) {

            if (name[i] == '#') {
                result++;
            } else if (name[i] == 'b') {
                result--;
            }
        }
    return result;
}



// MINGUS UTILITY FUNCTIONS FOR SCALES

// Map of keys
std::map<std::string, std::string> keys = {
    {"Cb", "ab"},  //  7 b
    {"Gb", "eb"},  //  6 b
    {"Db", "bb"},  //  5 b
    {"Ab", "f"},   //  4 b
    {"Eb", "c"},   //  3 b
    {"Bb", "g"},   //  2 b
    {"F", "d"},    //  1 b
    {"C", "a"},    //  nothing
    {"G", "e"},    //  1 #
    {"D", "b"},    //  2 #
    {"A", "f#"},   //  3 #
    {"E", "c#"},   //  4 #
    {"B", "g#"},   //  5 #
    {"F#", "d#"},  //  6 #
    {"C#", "a#"}   //  7 #
};

std::vector<std::string> base_scale = {"C", "D", "E", "F", "G", "A", "B"};

std::map<std::string, std::vector<std::string>> _key_cache;

// Function to check if a key is in a recognized format
bool isValidKey(const std::string& key) {
    for (auto it = keys.begin(); it != keys.end(); ++it) {
        if (key == it->first || key == it->second) {
            return true;
        }
    }
    return false;
}

// Function to get the key signature
int getKeySignature(const std::string& key = "C") {
    if (!isValidKey(key)) {
        throw std::invalid_argument("unrecognized format for key '" + key + "'");
    }

    for (auto it = keys.begin(); it != keys.end(); ++it) {
        if (key == it->first || key == it->second) {
            return std::distance(keys.begin(), it) - 7;
        }
    }
    return 0; // Default to C
}

// Function to get the key signature accidentals
std::vector<std::string> getKeySignatureAccidentals(const std::string& key = "C") {
    int accidentals = getKeySignature(key);
    std::vector<std::string> res;

    if (accidentals < 0) {
        for (int i = -accidentals - 1; i >= 0; --i) {
            auto it = keys.begin();
            std::advance(it, 6 - i);
            res.push_back(it->first + "b");
        }
    } else if (accidentals > 0) {
        for (int i = 0; i < accidentals; ++i) {
            auto it = keys.begin();
            std::advance(it, 7 + i);
            res.push_back(it->first + "#");
        }
    }
    return res;
}

// Function to get the notes in a natural key
std::vector<std::string> getNotes(const std::string& key = "C") {
    if (_key_cache.find(key) != _key_cache.end()) {
        return _key_cache[key];
    }

    if (!isValidKey(key)) {
        throw std::invalid_argument("unrecognized format for key '" + key + "'");
    }

    std::vector<std::string> result;

    // Calculate notes
    std::vector<std::string> alteredNotes = getKeySignatureAccidentals(key);

    std::string symbol;
    int keySignature = getKeySignature(key);
    if (keySignature < 0) {
        symbol = "b";
    } else if (keySignature > 0) {
        symbol = "#";
    }

    char rawTonicIndex = std::toupper(key[0]) - 'A';
    for (int i = rawTonicIndex; i < rawTonicIndex + 7; ++i) {
        std::string note = base_scale[i % 7];
        if (std::find(alteredNotes.begin(), alteredNotes.end(), note) != alteredNotes.end()) {
            result.push_back(note + symbol);
        } else {
            result.push_back(note);
        }
    }

    // Save result to cache
    _key_cache[key] = result;
    return result;
}

std::string augment(const std::string& note) {
    if (note.back() != 'b') {
        return note + "#";
    } else {
        return note.substr(0, note.size() - 1);
    }
}



// MINGUS SCALES
namespace Scales {
    Scale::Scale(const std::string& note_, int octaves_) : tonic(note_), octaves(octaves_) {
        if (isLowerString(note_)) {
            throw std::invalid_argument("Unrecognized note '" + note_ + "'");
        }
    }

    NaturalMinor::NaturalMinor(const std::string& note_, int octaves) : Scale(note_, octaves), name(tonic + " natural minor") {}
    std::vector<std::string> NaturalMinor::ascending() {
        std::vector<std::string> notes = getNotes(toLower(tonic));
        std::vector<std::string> result = multiplyVectors(notes, octaves);
        result.push_back(notes[0]);
        return result;
    }

    HarmonicMinor::HarmonicMinor(const std::string& note_, int octaves) : Scale(note_, octaves), name(tonic + " harmonic minor") {}
    std::vector<std::string> HarmonicMinor::ascending() {
        std::vector<std::string> notes = NaturalMinor(tonic).ascending();
        notes.pop_back();
        notes[6] = augment(notes[6]);
        std::vector<std::string> result = multiplyVectors(notes, octaves);
        result.push_back(notes[0]);
        return result;
    }
}
