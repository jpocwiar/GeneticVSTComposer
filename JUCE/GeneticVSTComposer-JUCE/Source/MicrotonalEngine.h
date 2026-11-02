#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// An independent pitch domain. A degree is never a MIDI key or a diatonic pitch class.
namespace microtonal {
constexpr int maxDegrees = 72, maxEvents = 512, slotCount = 12;
struct Ratio {
    int numerator = 1, denominator = 1;
};
struct Tuning {
    // EDO: octave / divisions. Ratios: an ordered, octave-repeating rational scale.
    bool rational = false;
    int divisions = 7;
    std::array<Ratio, maxDegrees> ratios{};
};
struct Settings {
    Tuning tuning;
    double baseHz = 130.8127826503, beats = 16, tempo = 120;
    double consonance = 0.7, diversity = 0.5, motion = 0.35, density = 0.6, mutation = 0.25;
    int octaves = 2, population = 64, generations = 80;
    uint32_t seed = 1;
    bool monitor = true, followHost = false, mpe = false;
    // bars == 0 retains explicit quarter-note length for older workspaces/imported phrases.
    int bars = 0, numerator = 4, denominator = 4, division = 8;
    std::array<int, 16> groups{};
    int groupCount = 0, groupUnit = 4;
    int phraseType = 2, mode = 0;
    double dynamics = 0.5, syncopation = 0.25, space = 0.15, arousal = 0.5, weirdness = 0.1;
    double swing = 0, gate = 1, humanize = 0, crossover = 0.9;
    bool lockRhythm = false;
    bool hostMeter = false, evolveEachLoop = false;
};
struct Event {
    double start = 0, duration = 0.5;
    int degree = 0, velocity = 96;
    bool locked = false;
};
struct Clip {
    Tuning tuning;
    double baseHz = 130.8127826503, length = 16;
    int count = 0;
    uint32_t seed = 0;
    std::array<Event, maxEvents> notes{};
    int numerator = 4, denominator = 4;
    double grid = 0.5;
    std::array<int, 16> groups{};
    int groupCount = 0, groupUnit = 4;
    bool polyphonic = false, favourite = false;
};
using Bank = std::array<Clip, slotCount>;
struct ChordSettings {
    // 0 follows melody length; otherwise a separate number of bars.
    int bars = 0, rhythm = 0, rhythmShape = 0, voices = 3, voicing = 0;
    double originality = 0.5, colour = 0.45, brightness = 0.5;
    bool resolve = true;
};
struct RatioMatch {
    Ratio ratio;
    double errorCents = 0, simplicity = 0, affinity = 0;
};
struct Score {
    double total = 0, harmonicity = 0, variety = 0, movement = 0, closure = 0;
};
struct Stats {
    int generation = 0;
    uint64_t evaluations = 0;
    uint64_t rhythmEvaluations = 0;
    int rhythmGeneration = 0, rhythmGenerations = 0;
    double initialBest = 0;
    Score best;
};
bool valid(const Tuning&);
bool valid(const Settings&);
bool valid(const Clip&);
bool valid(const ChordSettings&);
double phraseLength(const Settings&);
int metricWeight(double beat, const Settings&);
bool setGrouping(Settings&, const std::string&, std::string& error);
bool sameTuning(const Tuning&, const Tuning&);
bool parseRatios(const std::string&, Tuning&, std::string& error);
std::string ratioText(const Tuning&);
double cents(const Tuning&, int degree);
double frequency(const Clip&, int degree);
// Octave-equivalent low-integer approximation; the EDO pitch itself is never retuned.
RatioMatch approximateRatio(double intervalCents);
Score evaluate(const Clip&, const Settings&, const Clip* harmony = nullptr);
Clip generate(const Settings&, const Clip* parent, const std::atomic<bool>& cancelled,
              const std::function<void(const Stats&)>& progress = {},
              std::vector<Clip>* alternatives = nullptr, const Clip* harmony = nullptr);
Clip generateChords(const Settings&, const ChordSettings&, const std::atomic<bool>& cancelled,
                    const std::function<void(const Stats&)>& progress = {});
Score evaluateChords(const Clip&, const Settings&, const ChordSettings&);
Clip revoiceChords(const Clip&, const Settings&, const ChordSettings&);
Clip combine(const Clip& melody, const Clip& chords);
Event performedEvent(const Clip&, int index, const Settings&);
} // namespace microtonal
