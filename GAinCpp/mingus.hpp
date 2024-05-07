#ifndef MINGUS_HPP
#define MINGUS_HPP

#include <iostream>
#include <map>
#include <string>
#include <stdexcept>

extern const std::map<std::string, int> noteDict;

bool isValidNote(std::string note);

int noteToInt(std::string note);

class Note {
private:
    std::string name;
    int octave;

public:
    Note(const std::string& name_, int octave_);

    int toInt() const;
};

#endif // MINGUS_HPP