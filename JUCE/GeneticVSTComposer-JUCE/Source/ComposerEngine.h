#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace composer {
constexpr int maxNotes = 512, slotCount = 12;
struct Note {
    double start = 0.0, duration = 0.25;
    int pitch = 60, velocity = 96;
    bool locked = false;
};
struct Clip {
    std::array<Note, maxNotes> notes{};
    int count = 0, numerator = 4, denominator = 4;
    double length = 16.0, grid = 0.25;
    bool favourite = false;
    uint32_t seed = 0;
};
using Bank = std::array<Clip, slotCount>;
enum class RegionAction { Simplify, MoreMovement, Answer, Resolve };
// Times are quarter-note beats. Qualities: major, minor, diminished, augmented,
// dominant seventh, major seventh, minor seventh, suspended second/fourth.
struct ChordEvent {
    double start = 0.0, duration = 4.0;
    int root = 0, quality = 0;
};
struct Harmony {
    std::array<ChordEvent, 64> chords{};
    int count = 0;
    double length = 16.0;
};
struct Settings {
    int root = 0, scale = 1, bars = 4, division = 16;
    int numerator = 4, denominator = 4, low = 48, high = 84;
    int mode = 0, progression = 0, quality = 1;
    float density = 0.5f, diversity = 0.5f, syncopation = 0.25f;
    float tension = 0.15f, space = 0.15f, mutation = 0.35f;
    bool lockRhythm = false;
    uint32_t seed = 1;
    Harmony harmony;
    // Harmonic position of a local section during region edits; generation ignores this.
    double timeOffset = 0.0;
};
const std::vector<int>& scaleIntervals(int scale);
int snapPitch(int pitch, int root, int scale, int low = 0, int high = 127);
void sanitise(Clip& clip);
void sanitise(Harmony& harmony);
Harmony inferHarmony(const Clip& clip);
// "Cm:4 Fm:2 G7:2"; durations are beats. A failed parse leaves harmony unchanged.
bool parseHarmony(const std::string& text, Harmony& harmony, std::string& error);
std::string harmonyText(const Harmony& harmony);
// Operates on unlocked note onsets in [start, end), preserving all other events.
Clip transformRegion(const Clip& clip, double start, double end, RegionAction action,
                     const Settings& settings);
Clip applyGroove(const Clip& target, const Clip& reference, float timingAmount, float velocityAmount);
Clip combineParts(const Clip& rhythm, const Clip& pitches, const Clip& accents);
Bank makeInitialBank();
// A cancelled run returns no results. The caller keeps its previous bank.
std::vector<Clip> generate(Settings settings, const Clip* parent, const std::atomic<bool>& cancelled,
                           const std::function<void(float)>& progress = {});
double similarity(const Clip& a, const Clip& b);
} // namespace composer
