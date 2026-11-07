#include "ChordGenerator.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>

// Seeded random inputs for the parsers and every clip transform. The sanitizer CI job runs this
// suite too, so out-of-range reads and undefined arithmetic are caught as well as crashes.
namespace {
using namespace composer;
void check(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
void validate(const Clip& clip, const char* where) {
    check(clip.count >= 0 && clip.count <= maxNotes && std::isfinite(clip.length) && clip.length > 0 &&
              clip.length <= 128 && clip.grid > 0,
          where);
    for (int i = 0; i < clip.count; ++i) {
        const auto& note = clip.notes[size_t(i)];
        check(std::isfinite(note.start) && std::isfinite(note.duration) && note.start >= 0 &&
                  note.start < clip.length && note.duration > 0 &&
                  note.start + note.duration <= clip.length + 1e-9 && note.pitch >= 0 && note.pitch <= 127 &&
                  note.velocity >= 1 && note.velocity <= 127 && note.cents >= -50 && note.cents <= 50 &&
                  (i == 0 || clip.notes[size_t(i - 1)].start <= note.start),
              where);
    }
}
struct Source {
    std::mt19937 engine{20261107};
    int integer(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(engine); }
    double real(double lo, double hi) { return std::uniform_real_distribution<double>(lo, hi)(engine); }
    // Mostly plausible numbers, sometimes NaN, infinities or extreme magnitudes.
    double wild(double lo, double hi) {
        switch (integer(0, 11)) {
        case 0:
            return std::numeric_limits<double>::quiet_NaN();
        case 1:
            return integer(0, 1) ? std::numeric_limits<double>::infinity()
                                 : -std::numeric_limits<double>::infinity();
        case 2:
            return real(-1e9, 1e9);
        default:
            return real(lo, hi);
        }
    }
    std::string text() {
        static const char* pieces[]{
            "C",       "D",    "E",    "F",   "G",   "A",    "B",        "H",    "c",     "#",   "b",
            "m",       "maj7", "7",    "dim", "aug", "sus2", "sus4",     "m7b5", "dim7",  "6",   "m6",
            "add9",    "9",    "maj9", ":",   "::",  "4",    "0.5",      "-1",   "1e400", "nan", "128",
            "0.03125", " ",    "  ",   "\t",  "/",   "x",    "\xc3\xb3", "\xff"};
        std::string result;
        for (int i = integer(0, 24); i > 0; --i)
            result += pieces[integer(0, int(std::size(pieces)) - 1)];
        return result;
    }
    Clip clip() {
        Clip c;
        c.count = integer(-4, maxNotes + 40);
        c.length = wild(0, 140);
        c.grid = wild(0, 1.2);
        c.numerator = integer(-3, 40);
        c.denominator = integer(-3, 40);
        for (int i = 0; i < std::clamp(c.count, 0, maxNotes); ++i)
            c.notes[size_t(i)] = {wild(-4, 140),     wild(-1, 20),       integer(-20, 150),
                                  integer(-20, 150), integer(0, 3) == 0, integer(-3000, 3000)};
        return c;
    }
};
} // namespace

void runFuzzTests() {
    Source random;
    for (int round = 0; round < 6000; ++round) {
        Harmony harmony;
        std::string error;
        const auto text = random.text();
        if (parseHarmony(text, harmony, error)) {
            check(harmony.count >= 0 && harmony.count <= 64 && harmony.length > 0 && harmony.length <= 128,
                  "parsed harmony out of range");
            Harmony again;
            check(parseHarmony(harmonyText(harmony), again, error) && again.count == harmony.count,
                  "harmony text does not round-trip");
            for (int i = 0; i < harmony.count; ++i)
                check(again.chords[size_t(i)].root == harmony.chords[size_t(i)].root &&
                          again.chords[size_t(i)].quality == harmony.chords[size_t(i)].quality,
                      "harmony text changed a chord");
        } else
            check(!error.empty(), "a rejected harmony needs an explanation");
        TimeSignature meter{};
        if (parseMeter(text, meter, error))
            check(meter.numerator >= 1 && meter.numerator <= 32 && meter.denominator >= 1 &&
                      meter.denominator <= 32 && (meter.denominator & (meter.denominator - 1)) == 0,
                  "parsed meter out of range");
    }
    for (int round = 0; round < 1500; ++round) {
        auto clip = random.clip();
        sanitise(clip);
        validate(clip, "sanitise left an invalid clip");
        if (clip.count)
            check(similarity(clip, clip) == 1, "a sanitised clip differs from itself");
        auto other = random.clip();
        sanitise(other);
        const double alike = similarity(clip, other);
        check(alike >= 0 && alike <= 1, "similarity out of range");
        const int level = metricLevel(random.wild(-10, 200), random.integer(-3, 40), random.integer(-3, 40));
        check(level >= 0 && level <= 3, "metric level out of range");
        Settings s;
        s.root = random.integer(-30, 30);
        s.scale = random.integer(-3, 20);
        s.progression = random.integer(-3, 30);
        s.low = random.integer(-10, 140);
        s.high = random.integer(-10, 140);
        s.tension = float(random.wild(-1, 2));
        s.microEnabled = random.integer(0, 1);
        s.tuning = random.integer(-2, 9);
        s.seed = uint32_t(round);
        s.timeOffset = random.wild(-10, 100);
        if (random.integer(0, 1)) {
            std::string error;
            parseHarmony(random.text(), s.harmony, error);
        }
        const double start = random.wild(-2, clip.length + 2), end = random.wild(-2, clip.length + 2);
        validate(transformRegion(clip, start, end, RegionAction(random.integer(0, 3)), s),
                 "region transform");
        validate(retuneRegion(clip, start, end, s), "region retuning");
        validate(applyGroove(clip, other, float(random.wild(-1, 2)), float(random.wild(-1, 2))), "groove");
        validate(combineParts(clip, other, random.clip()), "combination");
        auto inferred = inferHarmony(clip);
        check(inferred.count >= 0 && inferred.count <= 64, "inferred harmony out of range");
        ChordGenerationSettings chords;
        chords.root = s.root;
        chords.scale = s.scale;
        chords.originality = float(random.wild(-1, 2));
        chords.harmonicRhythm = random.integer(-2, 5);
        chords.rhythmShape = random.integer(-2, 5);
        chords.seed = uint32_t(round);
        const auto fitted = harmonizeMelody(clip, chords);
        check(fitted.count >= 0 && fitted.count <= 64 && (clip.count == 0 || fitted.count > 0),
              "melody harmonisation out of range");
        const auto voiced = voiceHarmony(fitted, clip.numerator, clip.denominator,
                                         {random.integer(-40, 140), random.integer(-3, 5)});
        validate(voiced, "voiced harmonisation");
        check(std::isfinite(fitnessScore(clip, s)), "nonfinite fitness");
        double sum = 0;
        for (const auto& term : fitnessBreakdown(clip, s))
            sum += term.value;
        check(std::abs(sum - fitnessScore(clip, s)) < 1e-9, "fitness breakdown disagrees with the score");
    }
    // A complete search with random settings, kept small. Every phrase must be valid.
    std::atomic<bool> cancelled{false};
    for (int round = 0; round < 24; ++round) {
        Settings s;
        s.root = random.integer(0, 11);
        s.scale = random.integer(0, 15);
        s.bars = random.integer(1, 4);
        s.numerator = random.integer(1, 13);
        s.denominator = 1 << random.integer(1, 4);
        s.mode = random.integer(0, 2);
        s.phraseType = random.integer(0, 2);
        s.riffCellBars = std::array<double, 3>{0.5, 1.0, 2.0}[size_t(random.integer(0, 2))];
        s.progression = random.integer(0, 25);
        s.density = float(random.real(0, 1));
        s.space = float(random.real(0, 0.85));
        s.weirdness = float(random.real(0, 1));
        s.tension = float(random.real(0, 1));
        s.microEnabled = random.integer(0, 1);
        s.tuning = random.integer(0, 6);
        s.population = 16;
        s.generations = 3;
        s.seed = uint32_t(round + 1);
        auto parent = random.clip();
        sanitise(parent);
        const bool evolve = random.integer(0, 1) && parent.count > 0;
        for (const auto& clip : generate(s, evolve ? &parent : nullptr, cancelled))
            validate(clip, "generated phrase is invalid");
    }
}
