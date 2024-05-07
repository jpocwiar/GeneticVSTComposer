#ifndef MINGUS_HPP
#define MINGUS_HPP

#include <iostream>
#include <map>
#include <string>
#include <stdexcept>

class Note {
private:
    std::string name;
    int octave;

public:
    Note(const std::string& name_, int octave_);

    // Return the current octave multiplied by twelve and add noteToInt to it.
    // This means a C-0 returns 0, C-1 returns 12, etc.
    int toInt() const;
};

namespace Scales {
    class Scale {
    protected:
        std::string tonic;
        int octaves;
        Scale(const std::string& note_, int octaves_);
    };

    /*
    The natural minor scale.

    Example:
    >>> std::cout<<NaturalMinor('A')
    Ascending:  A B C D E F G A
    Descending: A G F E D C B A
    */
    class NaturalMinor : Scale {
    public:
        const std::string type;
        const std::string name;

        NaturalMinor(const std::string& note_, int octaves = 1);

        std::vector<std::string> ascending();
    };

    /*
    The harmonic minor scale.

    Example:
    >>> std::cout<<HarmonicMinor('A')
    Ascending:  A B C D E F G# A
    Descending: A G# F E D C B A
    */
    class HarmonicMinor : Scale {
    public:
        const std::string type;
        const std::string name;

        HarmonicMinor(const std::string& note_, int octaves = 1);

        std::vector<std::string> ascending();
    };

}

#endif // MINGUS_HPP
