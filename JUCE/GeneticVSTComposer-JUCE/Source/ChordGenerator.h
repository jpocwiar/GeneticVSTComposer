#pragma once
#include "ComposerEngine.h"

namespace composer {
struct ChordGenerationSettings {
    int root = 0, scale = 1, bars = 4, numerator = 4, denominator = 4;
    float originality = 0.35f;
    // One chord per two bars, per bar, or per half-bar.
    int harmonicRhythm = 1;
    bool resolve = true;
    uint32_t seed = 1;
};
struct VoicingSettings {
    int centre = 60;
    // Smooth inversions, root position, or open smooth voicings.
    int style = 0;
};
// Seeded functional templates become less familiar through diatonic substitutions,
// extensions, modal borrowing and applied dominants as originality increases.
// Phrases longer than 128 quarter-note beats are rejected with an empty Harmony.
// Pentatonic/chromatic scales use their parent major/minor harmonic vocabulary;
// the Saba tuning uses a minor vocabulary for these conventional 12-TET chords.
Harmony generateHarmony(const ChordGenerationSettings&);
// Chooses registers and inversions across the entire progression. Every chord
// tone appears once, with conventional 12-TET pitches and no microtonal offsets.
Clip voiceHarmony(const Harmony&, int numerator, int denominator, const VoicingSettings&);
std::string chordName(const ChordEvent&);
std::string voicedChordName(const ChordEvent&, const Clip&);
} // namespace composer
