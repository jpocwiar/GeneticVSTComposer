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
        const std::string tonic;
        const int octaves;
        Scale(const std::string& note_, int octaves_);

    public:
        friend std::ostream& operator<<( std::ostream& outs, const Scale& scale );
        virtual std::vector<std::string> ascending() const = 0;
        std::vector<std::string> descending() const;
    };

// THE DIATONIC SCALES

    /*
    The diatonic scale.

    Example:
    >>> std::cout << Diatonic("C", {3, 7}) << std::endl;
    Ascending:  C D E F G A B C
    Descending: C B A G F E D C
    */
    class Diatonic : public Scale {
    public:
        const std::string type;
        const std::vector<int> semitones;
        const std::string name;

        // Create the diatonic scale starting on the chosen note.
        // The second parameter is a vector representing the position of semitones.
        Diatonic(const std::string& note_, const std::vector<int>& semitones_, const int octaves_ = 1);

        std::vector<std::string> ascending() const override;
    };

// ANCIENT SCALES

    /*
    The ionian scale.

    Example:
    >>> std::cout << Ionian("C") << std::endl;
    Ascending:  C D E F G A B C
    Descending: C B A G F E D C
    */
    class Ionian : public Scale {
    public:
        const std::string type;
        const std::string name;

        // Create the ionian mode scale starting on the chosen note.
        Ionian(const std::string& note_, const int octaves_ = 1);
        
        std::vector<std::string> ascending() const override;
    };

    /*
    The dorian scale.

    Example:
    >>> std::cout << Dorian("D") << std::endl;
    Ascending:  D E F G A B C D
    Descending: D C B A G F E D
    */
    class Dorian : public Scale {
    public:
        const std::string type;
        const std::string name;

        // Create the dorian mode scale starting on the chosen note.
        Dorian(const std::string& note_, const int octaves_ = 1);
        
        std::vector<std::string> ascending() const override;
    };

    /*
    The phrygian scale.

    Example:
    >>> std::cout << Phrygian("E") << std::endl;
    Ascending:  E F G A B C D E
    Descending: E D C B A G F E
    */
    class Phrygian : public Scale {
    public:
        const std::string type;
        const std::string name;

        // Create the phrygian mode scale starting on the chosen note.
        Phrygian(const std::string& note_, const int octaves_ = 1);
        
        std::vector<std::string> ascending() const override;
    };

    /*
    The lydian scale.

    Example:
    >>> std::cout << Lydian("F") << std::endl;
    Ascending:  F G A B C D E F
    Descending: F E D C B A G F
    */
    class Lydian : public Scale {
    public:
        const std::string type;
        const std::string name;

        // Create the lydian mode scale starting on the chosen note.
        Lydian(const std::string& note_, const int octaves_ = 1);
        
        std::vector<std::string> ascending() const override;
    };

    /*
    The mixolydian scale.

    Example:
    >>> std::cout << Mixolydian("G") << std::endl;
    Ascending:  G A B C D E F G
    Descending: G F E D C B A G
    */
    class Mixolydian : public Scale {
    public:
        const std::string type;
        const std::string name;

        // Create the mixolydian mode scale starting on the chosen note.
        Mixolydian(const std::string& note_, const int octaves_ = 1);
        
        std::vector<std::string> ascending() const override;
    };

    /*
    The aeolian scale.

    Example:
    >>> std::cout << Aeolian("A") << std::endl;
    Ascending:  A B C D E F G A
    Descending: A G F E D C B A
    */
    class Aeolian : public Scale {
    public:
        const std::string type;
        const std::string name;

        // Create the aeolian mode scale starting on the chosen note.
        Aeolian(const std::string& note_, const int octaves_ = 1);
        
        std::vector<std::string> ascending() const override;
    };

    /*
    The locrian scale.

    Example:
    >>> std::cout << Locrian("B") << std::endl;
    Ascending:  B C D E F G A B
    Descending: B A G F E D C B
    */
    class Locrian : public Scale {
    public:
        const std::string type;
        const std::string name;

        // Create the locrian mode scale starting on the chosen note.
        Locrian(const std::string& note_, const int octaves_ = 1);
        
        std::vector<std::string> ascending() const override;
    };

// MAJOR SCALES

    /*
    The major scale.

    Example:
    >>> std::cout << Major("A") << std::endl;
    Ascending:  A B C# D E F# G# A
    Descending: A G# F# E D C# B A
    */
    class Major : public Scale {
    public:
        const std::string type;
        const std::string name;

        // Create the major scale starting on the chosen note.
        Major(const std::string& note_, const int octaves_ = 1);
        
        std::vector<std::string> ascending() const override;
    };

    /*
    The harmonic major scale.

    Example:
    >>> std::cout << HarmonicMajor("C") << std::endl;
    Ascending:  C D E F G Ab B C
    Descending: C B Ab G F E D C
    */
    class HarmonicMajor : public Scale {
    public:
        const std::string type;
        const std::string name;

        // Create the harmonic major scale starting on the chosen note.
        HarmonicMajor(const std::string& note_, const int octaves_ = 1);
        
        std::vector<std::string> ascending() const override;
    };

// MINOR SCALES

    /*
    The natural minor scale.

    Example:
    >>> std::cout << NaturalMinor("A") << std::endl;
    Ascending:  A B C D E F G A
    Descending: A G F E D C B A
    */
    class NaturalMinor : public Scale {
    public:
        const std::string type;
        const std::string name;

        // Return the natural minor scale starting on the chosen note.
        NaturalMinor(const std::string& note_, int octaves_ = 1);

        std::vector<std::string> ascending() const override;
    };

    /*
    The harmonic minor scale.

    Example:
    >>> std::cout << HarmonicMinor("A") << std::endl;
    Ascending:  A B C D E F G# A
    Descending: A G# F E D C B A
    */
    class HarmonicMinor : public Scale {
    public:
        const std::string type;
        const std::string name;

        // Create the harmonic minor scale starting on the chosen note.
        HarmonicMinor(const std::string& note_, int octaves_ = 1);

        std::vector<std::string> ascending() const override;
    };

    /*
    The melodic minor scale.

    Example:
    >>> std::cout << MelodicMinor("A") << std::endl;
    Ascending:  A B C D E F# G# A
    Descending: A G F E D C B A
    */
    class MelodicMinor : public Scale {
    public:
        const std::string type;
        const std::string name;

        // Create the melodic minor scale starting on the chosen note.
        MelodicMinor(const std::string& note_, int octaves_ = 1);

        std::vector<std::string> ascending() const override;
        std::vector<std::string> descending() const;
    };

    /*
    The Bachian scale.

    Example:
    >>> std::cout << Bachian("A") << std::endl;
    Ascending:  A B C D E F# G# A
    Descending: A G# F# E D C B A
    */
    class Bachian : public Scale {
    public:
        const std::string type;
        const std::string name;

        // Create the Bachian (also known as "real melodic minor" and "jazz") scale starting on the chosen note.
        Bachian(const std::string& note_, int octaves_ = 1);

        std::vector<std::string> ascending() const override;
    };

    /*
    The minor Neapolitan scale.

    Example:
    >>> std::cout << MinorNeapolitan("A") << std::endl; 
    Ascending:  A Bb C D E F G# A
    Descending: A G F E D C Bb A
    */
    class MinorNeapolitan : public Scale {
    public:
        const std::string type;
        const std::string name;

        // Create the minor Neapolitan scale starting on the chosen note.
        MinorNeapolitan(const std::string& note_, int octaves_ = 1);

        std::vector<std::string> ascending() const override;
        std::vector<std::string> descending() const;
    };

// OTHER SCALES

    /*
    The chromatic scale.

    Examples:
    >>> std::cout << Chromatic("C") << std::endl;
    Ascending:  C C# D D# E F F# G G# A A# B C
    Descending: C B Bb A Ab G Gb F E Eb D Db C
    >>> std::cout << Chromatic("f") << std::endl;
    Ascending:  F F# G Ab A Bb B C Db D Eb E F
    Descending: F E Eb D Db C B Bb A Ab G Gb F
    */
    class Chromatic : public Scale {
    public:
        const std::string type;
        const std::string name;
        const std::string key;

        // Create the chromatic scale in the chosen key.
        Chromatic(const std::string& key_, int octaves_ = 1);

        std::vector<std::string> ascending() const override;
        std::vector<std::string> descending() const;
    };

    /*
    The whole tone scale.

    Example:
    >>> std::cout << WholeTone("C") << std::endl;
    Ascending:  C D E F# G# A# C
    Descending: C A# G# F# E D C
    */
    class WholeTone : public Scale {
    public:
        const std::string type;
        const std::string name;

        // Create the whole tone scale starting on the chosen note.
        WholeTone(const std::string& note_, int octaves_ = 1);

        std::vector<std::string> ascending() const override;
    };

    /*
    The octatonic scale.

    Example:
    >>> std::cout << Octatonic("C") << std::endl;
    Ascending:  C D Eb F Gb Ab A B C
    Descending: C B A Ab Gb F Eb D C
    */
    class Octatonic : public Scale {
    public:
        const std::string type;
        const std::string name;

        // Create the octatonic (also known as "diminished") scale starting on the chosen note.
        Octatonic(const std::string& note_, int octaves_ = 1);

        std::vector<std::string> ascending() const override;
    };

}

#endif // MINGUS_HPP
