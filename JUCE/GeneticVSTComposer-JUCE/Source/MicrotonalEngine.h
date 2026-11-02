#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

// An independent pitch domain. A degree is never a MIDI key or a diatonic pitch class.
namespace microtonal {
constexpr int maxDegrees = 72, maxEvents = 128;
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
};
struct Event {
    double start = 0, duration = 0.5;
    int degree = 0, velocity = 96;
};
struct Clip {
    Tuning tuning;
    double baseHz = 130.8127826503, length = 16;
    int count = 0;
    uint32_t seed = 0;
    std::array<Event, maxEvents> notes{};
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
    double initialBest = 0;
    Score best;
};
bool valid(const Tuning&);
bool valid(const Settings&);
bool valid(const Clip&);
bool sameTuning(const Tuning&, const Tuning&);
bool parseRatios(const std::string&, Tuning&, std::string& error);
std::string ratioText(const Tuning&);
double cents(const Tuning&, int degree);
double frequency(const Clip&, int degree);
// Octave-equivalent low-integer approximation; the EDO pitch itself is never retuned.
RatioMatch approximateRatio(double intervalCents);
Score evaluate(const Clip&, const Settings&);
Clip generate(const Settings&, const Clip* parent, const std::atomic<bool>& cancelled,
              const std::function<void(const Stats&)>& progress = {});
} // namespace microtonal
