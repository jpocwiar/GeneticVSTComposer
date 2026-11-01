#include "ComposerEngine.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace {
using namespace composer;
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
bool sameNote(const Note& a, const Note& b) {
    return a.start == b.start && a.duration == b.duration && a.pitch == b.pitch && a.cents == b.cents &&
           a.velocity == b.velocity && a.locked == b.locked;
}
void sameStructure(const Clip& a, const Clip& b) {
    check(a.count == b.count && a.length == b.length && a.grid == b.grid && a.numerator == b.numerator &&
              a.denominator == b.denominator && a.groups == b.groups && a.groupCount == b.groupCount &&
              a.groupUnit == b.groupUnit && a.seed == b.seed && a.favourite == b.favourite,
          "melody adaptation changed clip structure or metadata");
    for (int i = 0; i < a.count; ++i)
        check(a.notes[i].start == b.notes[i].start && a.notes[i].duration == b.notes[i].duration &&
                  a.notes[i].velocity == b.notes[i].velocity && a.notes[i].locked == b.notes[i].locked,
              "melody adaptation changed rhythm, dynamics or locks");
}
const ChordEvent& chordAt(const Harmony& harmony, double beat) {
    const double phase = std::fmod(beat, harmony.length);
    for (int i = harmony.count - 1; i > 0; --i)
        if (harmony.chords[size_t(i)].start <= phase + 1e-8)
            return harmony.chords[size_t(i)];
    return harmony.chords[0];
}
bool chordTone(const ChordEvent& chord, int pitch) {
    const auto tones = chordPitchClasses(chord);
    return std::find(tones.begin(), tones.end(), pitch % 12) != tones.end();
}
Clip fixture() {
    Clip clip;
    clip.length = 8;
    clip.count = 8;
    clip.seed = 41;
    clip.favourite = true;
    clip.numerator = 8;
    clip.denominator = 8;
    clip.groups = {3, 3, 2};
    clip.groupCount = 3;
    clip.groupUnit = 8;
    const int pitches[]{60, 62, 64, 65, 67, 69, 71, 72};
    for (int i = 0; i < clip.count; ++i)
        clip.notes[size_t(i)] = {double(i), 0.75, pitches[i], 70 + i * 3, false, 0};
    return clip;
}
} // namespace

void runMelodyAdaptationTests() {
    using namespace composer;
    Settings s;
    s.scale = 0;
    std::string error;
    check(parseHarmony("Dm:2 G7:2 Cmaj9:2 F#m7:2", s.harmony, error), "adaptation harmony fixture");
    const auto source = fixture();
    const auto adapted = adaptToHarmony(source, 0, source.length, s);
    sameStructure(source, adapted);
    bool changed = false, chromatic = false;
    const auto repeat = adaptToHarmony(source, 0, source.length, s);
    double movement = 0;
    for (int i = 0; i < source.count; ++i) {
        const auto& note = adapted.notes[size_t(i)];
        check(chordTone(chordAt(s.harmony, note.start), note.pitch) && note.cents == 0,
              "structural note does not fit the actual target chord");
        check(sameNote(note, repeat.notes[size_t(i)]), "melody adaptation is not deterministic");
        movement += std::abs(note.pitch - source.notes[size_t(i)].pitch);
        changed |= note.pitch != source.notes[size_t(i)].pitch;
        chromatic |= snapPitch(note.pitch, 0, 0) != note.pitch;
    }
    check(changed && chromatic && movement < 24, "adaptation flattened the melody or merely fitted C major");
    const auto again = adaptToHarmony(adapted, 0, adapted.length, s);
    for (int i = 0; i < source.count; ++i)
        check(sameNote(adapted.notes[size_t(i)], again.notes[size_t(i)]), "already fitting melody was rewritten");

    auto protectedClip = source;
    protectedClip.notes[0].cents = 25;
    protectedClip.notes[3].locked = true;
    protectedClip.notes[3].cents = -25;
    const auto partial = adaptToHarmony(protectedClip, 1.25, 4.75, s);
    sameStructure(protectedClip, partial);
    for (int i = 0; i < partial.count; ++i)
        if (protectedClip.notes[size_t(i)].locked || protectedClip.notes[size_t(i)].start < 1.25 ||
            protectedClip.notes[size_t(i)].start >= 4.75)
            check(sameNote(protectedClip.notes[size_t(i)], partial.notes[size_t(i)]),
                  "adaptation changed a locked or out-of-range event");
    check(chordTone(s.harmony.chords[1], partial.notes[2].pitch), "partial adaptation reset the harmonic phase");

    Clip held;
    held.length = 4;
    held.count = 1;
    held.notes[0] = {0, 4, 60, 100, false, 0};
    check(parseHarmony("C:2 G:2", s.harmony, error), "held note fixture");
    const auto sustained = adaptToHarmony(held, 0, 4, s);
    check(sustained.notes[0].pitch % 12 == 7 && sustained.notes[0].duration == 4,
          "adaptation ignored a chord change under a held note");

    check(parseHarmony("Dm:1 E:1", s.harmony, error), "repeating harmony fixture");
    const auto looped = adaptToHarmony(source, 0, source.length, s);
    for (int i = 0; i < looped.count; ++i)
        check(chordTone(s.harmony.chords[size_t(i % 2)], looped.notes[size_t(i)].pitch),
              "a shorter target harmony did not repeat over the original melody length");

    Clip passing;
    passing.length = 2;
    passing.count = 3;
    passing.notes[0] = {0, 0.25, 60, 90, false, 0};
    passing.notes[1] = {0.25, 0.25, 62, 80, false, 0};
    passing.notes[2] = {0.5, 0.5, 64, 100, false, 0};
    check(parseHarmony("C:2", s.harmony, error), "passing note fixture");
    const auto kept = adaptToHarmony(passing, 0, 2, s);
    for (int i = 0; i < passing.count; ++i)
        check(sameNote(passing.notes[size_t(i)], kept.notes[size_t(i)]), "a fitting passing note was flattened");
    passing.notes[1].pitch = 63;
    passing.notes[1].cents = 50;
    s.microEnabled = true;
    s.tuning = 6;
    check(isValidMicrotonalPassing(passing, 1, s), "microtonal adaptation fixture is not a valid passing note");
    const auto micro = adaptToHarmony(passing, 0, 2, s);
    check(micro.notes[1].cents == 50 && isValidMicrotonalPassing(micro, 1, s),
          "adaptation erased a valid existing microtonal passing note");
    s.microEnabled = false;
    const auto plain = adaptToHarmony(passing, 0, 2, s);
    check(plain.notes[1].cents == 0, "Micro OFF adaptation created or retained an unlocked microtonal offset");

    for (double edge : {std::numeric_limits<double>::quiet_NaN(), -1.0, 9.0}) {
        const auto invalid = adaptToHarmony(source, edge, source.length, s);
        for (int i = 0; i < source.count; ++i)
            check(sameNote(source.notes[size_t(i)], invalid.notes[size_t(i)]), "invalid adaptation range changed notes");
    }
    const auto noHarmony = adaptToHarmony(source, 0, source.length, {});
    for (int i = 0; i < source.count; ++i)
        check(sameNote(source.notes[size_t(i)], noHarmony.notes[size_t(i)]), "empty target harmony changed notes");
    Clip polyphony;
    polyphony.length = 4;
    polyphony.count = 5;
    polyphony.notes[0] = {0, 3, 60, 90, false, 0};
    polyphony.notes[1] = {0.5, 0.5, 64, 90, false, 0};
    polyphony.notes[2] = {1, 1, 67, 90, false, 0};
    polyphony.notes[3] = {1.5, 1, 69, 90, false, 0};
    polyphony.notes[4] = {2, 0.5, 72, 90, false, 0};
    check(parseHarmony("F#m:4", s.harmony, error), "polyphonic adaptation fixture");
    const auto poly = adaptToHarmony(polyphony, 0, 4, s);
    sameStructure(polyphony, poly);
    for (int i = 0; i < poly.count; ++i) {
        check(chordTone(s.harmony.chords[0], poly.notes[size_t(i)].pitch), "polyphonic adaptation ignored the chord");
        for (int j = 0; j < i; ++j)
            if (poly.notes[size_t(j)].start + poly.notes[size_t(j)].duration > poly.notes[size_t(i)].start + 1e-8)
                check(poly.notes[size_t(j)].pitch != poly.notes[size_t(i)].pitch,
                      "adaptation introduced a same-key overlap that would cut a held voice");
    }
    Clip full;
    full.length = 16;
    full.grid = 0.03125;
    full.count = maxNotes;
    for (int i = 0; i < full.count; ++i)
        full.notes[size_t(i)] = {i * full.grid, full.grid, 60 + i % 8, 90, i % 17 == 0, 0};
    const auto capacity = adaptToHarmony(full, 0, full.length, s);
    sameStructure(full, capacity);
    for (int i = 0; i < full.count; ++i)
        check(capacity.notes[size_t(i)].pitch >= 0 && capacity.notes[size_t(i)].pitch <= 127 &&
                  (!full.notes[size_t(i)].locked || sameNote(full.notes[size_t(i)], capacity.notes[size_t(i)])),
              "full-capacity adaptation lost a protected event or produced an invalid key");
    // All chord qualities and both MIDI pitch limits must remain feasible.
    for (int quality = 0; quality < 17; ++quality)
        for (int pitch : {0, 1, 60, 126, 127}) {
            s.harmony.count = 1;
            s.harmony.length = 4;
            s.harmony.chords[0] = {0, 4, 6, quality};
            held.notes[0].pitch = pitch;
            const auto edge = adaptToHarmony(held, 0, 4, s);
            check(edge.notes[0].pitch >= 0 && edge.notes[0].pitch <= 127 &&
                      chordTone(s.harmony.chords[0], edge.notes[0].pitch), "chord vocabulary or MIDI boundary lost compatibility");
        }
}
