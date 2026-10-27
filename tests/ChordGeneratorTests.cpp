#include "ChordGenerator.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace composer;
void check(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
constexpr int intervals[9][4]{{0, 4, 7, -1}, {0, 3, 7, -1}, {0, 3, 6, -1}, {0, 4, 8, -1}, {0, 4, 7, 10},
                              {0, 4, 7, 11}, {0, 3, 7, 10}, {0, 2, 7, -1}, {0, 5, 7, -1}};
int pc(int pitch) {
    return (pitch % 12 + 12) % 12;
}
bool same(const Harmony& a, const Harmony& b) {
    if (a.count != b.count || a.length != b.length)
        return false;
    for (int i = 0; i < a.count; ++i) {
        const auto& x = a.chords[size_t(i)];
        const auto& y = b.chords[size_t(i)];
        if (x.root != y.root || x.quality != y.quality || x.start != y.start || x.duration != y.duration)
            return false;
    }
    return true;
}
void validateHarmony(const Harmony& harmony) {
    check(harmony.count > 0 && harmony.count <= 64, "chord generator: invalid chord count");
    check(harmony.length >= 0.125 && harmony.length <= 128, "chord generator: invalid length");
    double position = 0;
    for (int i = 0; i < harmony.count; ++i) {
        const auto& chord = harmony.chords[size_t(i)];
        check(chord.root >= 0 && chord.root < 12 && chord.quality >= 0 && chord.quality < 9,
              "chord generator: invalid root or quality");
        check(std::abs(chord.start - position) < 1e-8 && chord.duration >= 0.125,
              "chord generator: gap, overlap or tiny duration");
        position += chord.duration;
    }
    check(std::abs(position - harmony.length) < 1e-8, "chord generator: timeline did not fill phrase");
}
void validateVoicing(const Harmony& harmony, const Clip& clip, int style) {
    check(clip.length == harmony.length && clip.count <= maxNotes, "voicing: clip length or capacity");
    int position = 0;
    for (int i = 0; i < harmony.count; ++i) {
        const auto& chord = harmony.chords[size_t(i)];
        const int tones = intervals[chord.quality][3] < 0 ? 3 : 4;
        std::vector<int> seen;
        int priorPitch = -1;
        for (int voice = 0; voice < tones; ++voice) {
            check(position < clip.count, "voicing: chord missing a tone");
            const auto& note = clip.notes[size_t(position++)];
            check(note.start == chord.start && note.duration == chord.duration,
                  "voicing: notes escaped chord timeline");
            check(note.pitch > priorPitch && note.pitch >= 0 && note.pitch <= 127,
                  "voicing: crossed, repeated or invalid pitches");
            check(note.cents == 0 && note.velocity > 0 && note.velocity <= 127,
                  "voicing: nonstandard tuning or invalid velocity");
            priorPitch = note.pitch;
            const int relative = pc(note.pitch - chord.root);
            check(std::find(intervals[chord.quality], intervals[chord.quality] + tones, relative) !=
                      intervals[chord.quality] + tones,
                  "voicing: non-chord tone");
            check(std::find(seen.begin(), seen.end(), relative) == seen.end(),
                  "voicing: duplicate pitch class");
            seen.push_back(relative);
        }
        const auto& bass = clip.notes[size_t(position - tones)];
        if (style == 1)
            check(pc(bass.pitch) == chord.root, "voicing: root position has an inverted bass");
        if (style == 2)
            check(clip.notes[size_t(position - 1)].pitch - bass.pitch >= 12,
                  "voicing: open chord spans less than an octave");
        const auto name = voicedChordName(chord, clip);
        check((name.find('/') != std::string::npos) == (pc(bass.pitch) != chord.root),
              "voicing: slash chord label does not reflect audible bass");
    }
    check(position == clip.count, "voicing: extra notes after the last chord");
}
double novelty(const Harmony& harmony) {
    const auto& scale = scaleIntervals(0);
    double score = 0;
    for (int i = 0; i < harmony.count; ++i) {
        const auto& chord = harmony.chords[size_t(i)];
        score += chord.quality >= 4 ? 0.25 : 0;
        for (int interval : intervals[chord.quality])
            if (interval >= 0 &&
                std::find(scale.begin(), scale.end(), pc(chord.root + interval)) == scale.end())
                score += 1;
    }
    return score / harmony.count;
}
double triadMovement(const Clip& clip) {
    double total = 0;
    for (int i = 3; i < clip.count; ++i)
        total += std::abs(clip.notes[size_t(i)].pitch - clip.notes[size_t(i - 3)].pitch);
    return total;
}
} // namespace

void runChordGeneratorTests() {
    using namespace composer;
    ChordGenerationSettings settings;
    settings.scale = 0;
    settings.bars = 8;
    for (int scale = 0; scale < 10; ++scale) {
        settings.scale = scale;
        for (float originality : {0.0f, 0.35f, 1.0f}) {
            settings.originality = originality;
            settings.root = 0;
            const auto reference = generateHarmony(settings);
            validateHarmony(reference);
            check(same(reference, generateHarmony(settings)), "chord generator: seed is not reproducible");
            check(reference.chords[size_t(reference.count - 1)].root == 0,
                  "chord generator: requested tonic resolution missing");
            for (int root = 1; root < 12; ++root) {
                settings.root = root;
                const auto transposed = generateHarmony(settings);
                check(transposed.count == reference.count && transposed.length == reference.length,
                      "chord generator: key changed structure");
                for (int i = 0; i < reference.count; ++i)
                    check(transposed.chords[size_t(i)].root == pc(reference.chords[size_t(i)].root + root) &&
                              transposed.chords[size_t(i)].quality == reference.chords[size_t(i)].quality,
                          "chord generator: key did not transpose harmonic decisions");
            }
            for (int style = 0; style < 3; ++style)
                for (int centre : {36, 60, 84}) {
                    const auto voiced = voiceHarmony(reference, 4, 4, {centre, style});
                    validateVoicing(reference, voiced, style);
                    const auto repeated = voiceHarmony(reference, 4, 4, {centre, style});
                    check(repeated.count == voiced.count, "voicing: deterministic count");
                    for (int i = 0; i < voiced.count; ++i)
                        check(repeated.notes[size_t(i)].pitch == voiced.notes[size_t(i)].pitch,
                              "voicing: deterministic inversion choice");
                }
        }
    }

    settings = {};
    settings.scale = 0;
    settings.bars = 8;
    double scores[4]{};
    constexpr float levels[]{0, 0.35f, 0.7f, 1};
    bool varied = false;
    Harmony first;
    for (uint32_t seed = 1; seed <= 256; ++seed) {
        settings.seed = seed;
        for (int level = 0; level < 4; ++level) {
            settings.originality = levels[level];
            const auto harmony = generateHarmony(settings);
            scores[level] += novelty(harmony);
            if (level == 3) {
                if (seed == 1)
                    first = harmony;
                else
                    varied |= !same(first, harmony);
            }
        }
    }
    check(varied, "chord generator: different seeds never vary harmony");
    check(scores[0] == 0 && scores[1] > scores[0] && scores[2] > scores[1] && scores[3] > scores[2] &&
              scores[3] / 256.0 > 0.25,
          "chord generator: originality does not increase harmonic novelty across seeds");

    settings = {};
    settings.bars = 3;
    auto meters = timeSignatures();
    meters.push_back({1, 32});
    meters.push_back({3, 32});
    for (const auto& meter : meters) {
        settings.numerator = meter.numerator;
        settings.denominator = meter.denominator;
        for (int rhythm = 0; rhythm < 3; ++rhythm) {
            settings.harmonicRhythm = rhythm;
            const auto harmony = generateHarmony(settings);
            validateHarmony(harmony);
            check(harmony.length == settings.bars * meter.numerator * 4.0 / meter.denominator,
                  "chord generator: odd meter or partial final segment lost");
            const auto voiced = voiceHarmony(harmony, meter.numerator, meter.denominator, {});
            check(voiced.numerator == meter.numerator && voiced.denominator == meter.denominator,
                  "voicing: meter was discarded");
            validateVoicing(harmony, voiced, 0);
        }
    }
    settings.numerator = 1;
    settings.denominator = 4;
    settings.bars = 128;
    settings.harmonicRhythm = 2;
    const auto capacity = generateHarmony(settings);
    validateHarmony(capacity);
    check(capacity.count == 64 && capacity.length == 128, "chord generator: capacity lost phrase length");
    validateVoicing(capacity, voiceHarmony(capacity, 1, 4, {}), 0);
    settings.bars = 129;
    check(generateHarmony(settings).count == 0, "chord generator: oversized phrase silently truncated");
    settings = {};
    settings.originality = std::numeric_limits<float>::quiet_NaN();
    validateHarmony(generateHarmony(settings));
    settings.originality = 0;
    settings.resolve = false;
    settings.scale = 0;
    const auto unresolved = generateHarmony(settings);
    check(unresolved.chords[size_t(unresolved.count - 1)].root != 0,
          "chord generator: disabling tonic resolution had no effect");

    Harmony cadence;
    std::string error;
    check(parseHarmony("C:4 F:4 G:4 C:4", cadence, error), "voicing: test cadence parse");
    const auto smooth = voiceHarmony(cadence, 4, 4, {60, 0});
    const auto roots = voiceHarmony(cadence, 4, 4, {60, 1});
    check(triadMovement(smooth) < triadMovement(roots), "voicing: inversions do not reduce voice movement");
    bool inverted = false;
    for (int i = 0; i < cadence.count; ++i)
        inverted |= pc(smooth.notes[size_t(i * 3)].pitch) != cadence.chords[size_t(i)].root;
    check(inverted, "voicing: smooth style never uses inversions");
    check(chordName({0, 4, 0, 1}) == "Cm" && chordName({0, 4, 10, 5}) == "Bbmaj7",
          "voicing: chord name qualities");
    Clip inversion;
    inversion.notes[inversion.count++] = {0, 4, 63, 90, false, 0};
    inversion.notes[inversion.count++] = {0, 4, 67, 90, false, 0};
    inversion.notes[inversion.count++] = {0, 4, 72, 90, false, 0};
    check(voicedChordName({0, 4, 0, 1}, inversion) == "Cm/Eb", "voicing: expected slash chord name");
    check(voiceHarmony({}, 4, 4, {}).count == 0, "voicing: empty harmony invented notes");
}
