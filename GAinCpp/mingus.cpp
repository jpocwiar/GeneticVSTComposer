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

std::string vectorToString(const std::vector<int>& data) {
    if (data.empty()) {
        return "";
    }
    std::string ss = std::to_string(data[0]);
    for (int i = 1; i < data.size(); ++i) {
        ss += ", " + std::to_string(data[i]);
    }
    return ss;
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

std::string diminish(const std::string& note) {
    if (note.back() != '#') {
        return note + 'b';
    } else {
        return note.substr(0, note.size() - 1);
    }
}

int measure(const std::string& note1, const std::string& note2) {
    int res = noteToInt(note2) - noteToInt(note1);
    if (res < 0) {
        return 12 - res * -1;
    } else {
        return res;
    }
}

std::string augmentOrDiminishUntilTheIntervalIsRight(std::string note1, std::string note2, int interval) {
    int cur = measure(note1, note2);
    while (cur != interval) {
        if (cur > interval) {
            note2 = diminish(note2);
        } else if (cur < interval) {
            note2 = augment(note2);
        }
        cur = measure(note1, note2);
    }

    // Calculating the alteration value
    int val = 0;
    for (size_t i = 1; i < note2.size(); ++i) {
        if (note2[i] == '#') {
            val += 1;
        } else if (note2[i] == 'b') {
            val -= 1;
        }
    }

    // Adjusting alteration value if necessary
    if (val > 6) {
        val %= 12;
        val = -12 + val;
    } else if (val < -6) {
        val %= -12;
        val = 12 + val;
    }

    // Rebuilding the note
    std::string result = note2.substr(0, 1);
    while (val > 0) {
        result = augment(result);
        val -= 1;
    }
    while (val < 0) {
        result = diminish(result);
        val += 1;
    }
    return result;
}

std::string interval(const std::string& key, const std::string& startNote, int interval) {
    if (!isValidNote(startNote)) {
        throw std::invalid_argument("The start note '" + startNote + "' is not a valid note");
    }
    std::vector<std::string> notesInKey = getNotes(key);
    size_t index = 0;
    for (size_t i = 0; i < notesInKey.size(); ++i) {
        if (notesInKey[i][0] == startNote[0]) {
            index = i;
            break;
        }
    }
    return notesInKey[(index + interval) % 7];
}


std::string unison(const std::string& note) { return interval(note, note, 0); }
std::string minorUnison(const std::string& note) { return diminish(note); }
std::string majorUnison(const std::string& note) { return note; }
std::string augmentedUnison(const std::string& note) { return augment(note); }

std::string second(const std::string& note, const std::string& key) { return interval(key, note, 1); }
std::string minorSecond(const std::string& note) { std::string sec = second(note.substr(0, 1), "C"); return augmentOrDiminishUntilTheIntervalIsRight(note, sec, 1); }
std::string majorSecond(const std::string& note) { std::string sec = second(note.substr(0, 1), "C"); return augmentOrDiminishUntilTheIntervalIsRight(note, sec, 2); }

std::string third(const std::string& note, const std::string& key) { return interval(key, note, 2); }
std::string minorThird(const std::string& note) { std::string trd = third(note.substr(0, 1), "C"); return augmentOrDiminishUntilTheIntervalIsRight(note, trd, 3); }
std::string majorThird(const std::string& note) { std::string trd = third(note.substr(0, 1), "C"); return augmentOrDiminishUntilTheIntervalIsRight(note, trd, 4); }

std::string fourth(const std::string& note, const std::string& key) { return interval(key, note, 3); }
std::string minorFourth(const std::string& note) { std::string frt = fourth(note.substr(0, 1), "C"); return augmentOrDiminishUntilTheIntervalIsRight(note, frt, 4); }
std::string majorFourth(const std::string& note) { std::string frt = fourth(note.substr(0, 1), "C"); return augmentOrDiminishUntilTheIntervalIsRight(note, frt, 5); }
std::string perfectFourth(const std::string& note) { return majorFourth(note); }

std::string fifth(const std::string& note, const std::string& key) { return interval(key, note, 4); }
std::string minorFifth(const std::string& note) { std::string fif = fifth(note.substr(0, 1), "C"); return augmentOrDiminishUntilTheIntervalIsRight(note, fif, 6); }
std::string majorFifth(const std::string& note) { std::string fif = fifth(note.substr(0, 1), "C"); return augmentOrDiminishUntilTheIntervalIsRight(note, fif, 7); }
std::string perfectFifth(const std::string& note) { return majorFifth(note); }

std::string sixth(const std::string& note, const std::string& key) { return interval(key, note, 5); }
std::string minorSixth(const std::string& note) { std::string sth = sixth(note.substr(0, 1), "C"); return augmentOrDiminishUntilTheIntervalIsRight(note, sth, 8); }
std::string majorSixth(const std::string& note) { std::string sth = sixth(note.substr(0, 1), "C"); return augmentOrDiminishUntilTheIntervalIsRight(note, sth, 9); }

std::string seventh(const std::string& note, const std::string& key) { return interval(key, note, 6); }
std::string minorSeventh(const std::string& note) { std::string sth = seventh(note.substr(0, 1), "C"); return augmentOrDiminishUntilTheIntervalIsRight(note, sth, 10); }
std::string majorSeventh(const std::string& note) { std::string sth = seventh(note.substr(0, 1), "C"); return augmentOrDiminishUntilTheIntervalIsRight(note, sth, 11); }


// MINGUS SCALES
namespace Scales {
    Scale::Scale(const std::string& note_, const int octaves_) : tonic(note_), octaves(octaves_) {
        if (isLowerString(note_)) {
            throw std::invalid_argument("Unrecognized note '" + note_ + "'");
        }
    }

    Diatonic::Diatonic(const std::string& note_, const std::vector<int>& semitones_, const int octaves_) :
        Scale(note_, octaves_),
        semitones(semitones_),
        name(tonic + " diatonic, semitones in (" + vectorToString(semitones_) + ")"),
        type("diatonic") {}
    std::vector<std::string> Diatonic::ascending() const {
        std::vector<std::string> notes = { tonic };
        for (int n = 1; n < 7; ++n) {
            if (std::find(semitones.begin(), semitones.end(), n) != semitones.end()) {
                notes.push_back(minorSecond(notes.back()));
            } else {
                notes.push_back(majorSecond(notes.back()));
            }
        }
        std::vector<std::string> result = multiplyVectors(notes, octaves);
        result.push_back(notes[0]);
        return result;
    }

    Ionian::Ionian(const std::string& note_, const int octaves_) :
        Scale(note_, octaves_),
        name(tonic + " ionian"),
        type("ancient") {}
    std::vector<std::string> Ionian::ascending() const {
        std::vector<std::string> notes = Diatonic(tonic, {3, 7}).ascending();
        notes.pop_back();
        std::vector<std::string> result = multiplyVectors(notes, octaves);
        result.push_back(notes[0]);
        return result;
    }

    Dorian::Dorian(const std::string& note_, const int octaves_) :
        Scale(note_, octaves_),
        name(tonic + " dorian"),
        type("ancient") {}
    std::vector<std::string> Dorian::ascending() const {
        std::vector<std::string> notes = Diatonic(tonic, {2, 6}).ascending();
        notes.pop_back();
        std::vector<std::string> result = multiplyVectors(notes, octaves);
        result.push_back(notes[0]);
        return result;
    }

    Phrygian::Phrygian(const std::string& note_, const int octaves_) :
        Scale(note_, octaves_),
        name(tonic + " phrygian"),
        type("ancient") {}
    std::vector<std::string> Phrygian::ascending() const {
        std::vector<std::string> notes = Diatonic(tonic, {1, 5}).ascending();
        notes.pop_back();
        std::vector<std::string> result = multiplyVectors(notes, octaves);
        result.push_back(notes[0]);
        return result;
    }

    Lydian::Lydian(const std::string& note_, const int octaves_) :
        Scale(note_, octaves_),
        name(tonic + " lydian"),
        type("ancient") {}
    std::vector<std::string> Lydian::ascending() const {
        std::vector<std::string> notes = Diatonic(tonic, {4, 7}).ascending();
        notes.pop_back();
        std::vector<std::string> result = multiplyVectors(notes, octaves);
        result.push_back(notes[0]);
        return result;
    }

    Mixolydian::Mixolydian(const std::string& note_, const int octaves_) :
        Scale(note_, octaves_),
        name(tonic + " mixolydian"),
        type("ancient") {}
    std::vector<std::string> Mixolydian::ascending() const {
        std::vector<std::string> notes = Diatonic(tonic, {3, 6}).ascending();
        notes.pop_back();
        std::vector<std::string> result = multiplyVectors(notes, octaves);
        result.push_back(notes[0]);
        return result;
    }

    Aeolian::Aeolian(const std::string& note_, const int octaves_) :
        Scale(note_, octaves_),
        name(tonic + " aeolian"),
        type("ancient") {}
    std::vector<std::string> Aeolian::ascending() const {
        std::vector<std::string> notes = Diatonic(tonic, {2, 5}).ascending();
        notes.pop_back();
        std::vector<std::string> result = multiplyVectors(notes, octaves);
        result.push_back(notes[0]);
        return result;
    }

    Locrian::Locrian(const std::string& note_, const int octaves_) :
        Scale(note_, octaves_),
        name(tonic + " locrian"),
        type("ancient") {}
    std::vector<std::string> Locrian::ascending() const {
        std::vector<std::string> notes = Diatonic(tonic, {1, 4}).ascending();
        notes.pop_back();
        std::vector<std::string> result = multiplyVectors(notes, octaves);
        result.push_back(notes[0]);
        return result;
    }

    Major::Major(const std::string& note_, const int octaves_) :
        Scale(note_, octaves_),
        name(tonic + " major"),
        type("major") {}
    std::vector<std::string> Major::ascending() const {
        std::vector<std::string> notes = getNotes(tonic);
        std::vector<std::string> result = multiplyVectors(notes, octaves);
        result.push_back(notes[0]);
        return result;
    }

    HarmonicMajor::HarmonicMajor(const std::string& note_, const int octaves_) :
        Scale(note_, octaves_),
        name(tonic + " harmonic major"),
        type("major") {}
    std::vector<std::string> HarmonicMajor::ascending() const {
        std::vector<std::string> notes = Major(tonic).ascending();
        notes.pop_back();
        notes[5] = diminish(notes[5]);
        std::vector<std::string> result = multiplyVectors(notes, octaves);
        result.push_back(notes[0]);
        return result;
    }   

    NaturalMinor::NaturalMinor(const std::string& note_, const int octaves_) :
        Scale(note_, octaves_),
        name(tonic + " natural minor"),
        type("minor") {}
    std::vector<std::string> NaturalMinor::ascending() const {
        std::vector<std::string> notes = getNotes(toLower(tonic));
        std::vector<std::string> result = multiplyVectors(notes, octaves);
        result.push_back(notes[0]);
        return result;
    }

    HarmonicMinor::HarmonicMinor(const std::string& note_, const int octaves_) :
        Scale(note_, octaves_),
        name(tonic + " harmonic minor"),
        type("minor") {}
    std::vector<std::string> HarmonicMinor::ascending() const {
        std::vector<std::string> notes = NaturalMinor(tonic).ascending();
        notes.pop_back();
        notes[6] = augment(notes[6]);
        std::vector<std::string> result = multiplyVectors(notes, octaves);
        result.push_back(notes[0]);
        return result;
    }
}
