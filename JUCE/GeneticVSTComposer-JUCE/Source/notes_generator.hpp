#ifndef NOTES_GENERATOR_HPP
#define NOTES_GENERATOR_HPP

#include <vector>
#include <string>
#include <regex>
#include <map>

class NotesGenerator {
private:
    std::string key;

    std::pair<std::string, std::string> parseScaleName();

    std::vector<std::string> chooseScale();

public:
    static std::vector<int> g_scale_notes;
    NotesGenerator(const std::string& key = "C Major");

    std::vector<int> generateNotes(int numberOfOctaves = 1, int startOctave = 4);

    std::vector<int> generateChromaticNotes(const std::pair<int, int>& noteRange);
};

#endif // NOTES_GENERATOR_HPP
