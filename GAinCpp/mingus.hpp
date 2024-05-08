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

// THE DIATONIC SCALES

    /*
    The diatonic scale.

    Example:
    >>> std::cout << Diatonic("C", {3, 7}) << std::endl;
    Ascending:  C D E F G A B C
    Descending: C B A G F E D C
    */
    class Diatonic : Scale {
    public:
        const std::string type;
        const std::vector<int> semitones;
        const std::string name;

        // Create the diatonic scale starting on the chosen note.
        // The second parameter is a vector representing the position of semitones.
        Diatonic(const std::string& note_, const std::vector<int>& semitones_, const int octaves_ = 1);

        std::vector<std::string> ascending() const;
    };

// ANCIENT SCALES

    /*
    The ionian scale.

    Example:
    >>> std::cout << Ionian("C") << std::endl;
    Ascending:  C D E F G A B C
    Descending: C B A G F E D C
    */
    class Ionian : Scale {
    public:
        const std::string type;
        const std::string name;

        // Create the ionian mode scale starting on the chosen note.
        Ionian(const std::string& note_, const int octaves_ = 1);
        
        std::vector<std::string> ascending() const;
    };

    /*
    The dorian scale.

    Example:
    >>> std::cout << Dorian("D") << std::endl;
    Ascending:  D E F G A B C D
    Descending: D C B A G F E D
    */
    class Dorian : Scale {
    public:
        const std::string type;
        const std::string name;

        // Create the dorian mode scale starting on the chosen note.
        Dorian(const std::string& note_, const int octaves_ = 1);
        
        std::vector<std::string> ascending() const;
    };

    /*
    The phrygian scale.

    Example:
    >>> std::cout << Phrygian("E") << std::endl;
    Ascending:  E F G A B C D E
    Descending: E D C B A G F E
    */
    class Phrygian : Scale {
    public:
        const std::string type;
        const std::string name;

        // Create the phrygian mode scale starting on the chosen note.
        Phrygian(const std::string& note_, const int octaves_ = 1);
        
        std::vector<std::string> ascending() const;
    };

    /*
    The lydian scale.

    Example:
    >>> std::cout << Lydian("F") << std::endl;
    Ascending:  F G A B C D E F
    Descending: F E D C B A G F
    */
    class Lydian : Scale {
    public:
        const std::string type;
        const std::string name;

        // Create the lydian mode scale starting on the chosen note.
        Lydian(const std::string& note_, const int octaves_ = 1);
        
        std::vector<std::string> ascending() const;
    };

    /*
    The mixolydian scale.

    Example:
    >>> std::cout << Mixolydian("G") << std::endl;
    Ascending:  G A B C D E F G
    Descending: G F E D C B A G
    */
    class Mixolydian : Scale {
    public:
        const std::string type;
        const std::string name;

        // Create the mixolydian mode scale starting on the chosen note.
        Mixolydian(const std::string& note_, const int octaves_ = 1);
        
        std::vector<std::string> ascending() const;
    };

    /*
    The aeolian scale.

    Example:
    >>> std::cout << Aeolian("A") << std::endl;
    Ascending:  A B C D E F G A
    Descending: A G F E D C B A
    */
    class Aeolian : Scale {
    public:
        const std::string type;
        const std::string name;

        // Create the aeolian mode scale starting on the chosen note.
        Aeolian(const std::string& note_, const int octaves_ = 1);
        
        std::vector<std::string> ascending() const;
    };

    /*
    The locrian scale.

    Example:
    >>> std::cout << Locrian("B") << std::endl;
    Ascending:  B C D E F G A B
    Descending: B A G F E D C B
    */
    class Locrian : Scale {
    public:
        const std::string type;
        const std::string name;

        // Create the locrian mode scale starting on the chosen note.
        Locrian(const std::string& note_, const int octaves_ = 1);
        
        std::vector<std::string> ascending() const;
    };







    /*
    The natural minor scale.

    Example:
    >>> std::cout << NaturalMinor("A") << std::endl;
    Ascending:  A B C D E F G A
    Descending: A G F E D C B A
    */
    class NaturalMinor : Scale {
    public:
        const std::string type;
        const std::string name;

        // Return the natural minor scale starting on the chosen note.
        NaturalMinor(const std::string& note_, int octaves_ = 1);

        std::vector<std::string> ascending() const;
    };

    /*
    The harmonic minor scale.

    Example:
    >>> std::cout << HarmonicMinor("A") << std::endl;
    Ascending:  A B C D E F G# A
    Descending: A G# F E D C B A
    */
    class HarmonicMinor : Scale {
    public:
        const std::string type;
        const std::string name;

        // Create the harmonic minor scale starting on the chosen note.
        HarmonicMinor(const std::string& note_, int octaves_ = 1);

        std::vector<std::string> ascending() const;
    };

}

#endif // MINGUS_HPP
