#include <iostream>
#include "mingus.hpp"

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
        throw std::invalid_argument("Unknown note format");
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

Note::Note(const std::string& name_, int octave_) : name(name_), octave(octave_) {}

// Return the current octave multiplied by twelve and add noteToInt to it.
// This means a C-0 returns 0, C-1 returns 12, etc.
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
