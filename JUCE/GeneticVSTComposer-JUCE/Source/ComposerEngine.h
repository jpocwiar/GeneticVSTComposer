#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace composer {
constexpr int maxNotes = 512, slotCount = 12;
struct Note {
    double start = 0.0, duration = 0.25;
    int pitch = 60, velocity = 96;
    bool locked = false;
    // Microtonal offset from the equal-tempered pitch, -50 to 50 cents.
    int cents = 0;
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
    // Musical architecture, independent of generation mode: riff, motif, melody.
    int phraseType = 0;
    float density = 0.5f, diversity = 0.5f, syncopation = 0.25f;
    float tension = 0.15f, space = 0.15f, mutation = 0.35f;
    float dynamics = 0.5f, valence = 0.5f, arousal = 0.5f, weirdness = 0.1f;
    bool lockRhythm = false;
    uint32_t seed = 1;
    Harmony harmony;
    // Explicit duration for the "Melody from chords" workflow, including partial bars.
    double lengthOverride = 0;
    // Harmonic position of a local section during region edits; generation ignores this.
    double timeOffset = 0.0;
    // Microtones are optional short passing-note genes, evaluated inside the search.
    // Tuning selects their pitch-class palette, not a replacement for the selected scale.
    // Tuning 0 with Microtones enabled uses the 24-EDO palette.
    bool microEnabled = false;
    int tuning = 0;
    // Legacy region-retuning amount; ignored by generation/evolution.
    float microtones = 1.0f;
    // Zero uses the selected preset. Explicit budgets are bounded by searchBudget().
    int population = 0, generations = 0;
    float crossover = 0.9f;
};
struct TimeSignature {
    int numerator, denominator;
};
const std::vector<TimeSignature>& timeSignatures();
bool parseMeter(const std::string&, TimeSignature&, std::string& error);
struct ProgressionPreset {
    const char* name;
    std::vector<int> degrees;
};
const std::vector<ProgressionPreset>& progressionPresets();
struct SearchBudget {
    int population, generations;
};
SearchBudget searchBudget(const Settings&);
struct SearchStats {
    int generation = 0, generations = 0, population = 0;
    uint64_t evaluations = 0, crossovers = 0;
    uint64_t microtonalEvaluations = 0;
    double bestFitness = 0, meanFitness = 0, initialBest = 0;
};
struct PhraseDiagnostics {
    double meanIntervalCents = 0, intervalPenalty = 0, smallStepRatio = 0;
    double loopIntervalCents = 0, repetition = 0, rhythmicRepetition = 0;
    double motifReturn = 0, middleContrast = 0, contourArc = 0;
    double rhythmVariation = 0, pitchSpan = 0, meanPitch = 0, upwardRatio = 0, offbeatRatio = 0;
    int microtonalNotes = 0, resolvedPassingNotes = 0, invalidPassingNotes = 0;
};
// Symmetric melodic movement cost in actual cents, including register-sized leaps.
double intervalCost(double cents);
PhraseDiagnostics analysePhrase(const Clip&, const Settings&);
// The same objective used by fresh generation, without the parent-similarity term.
double fitnessScore(const Clip&, const Settings&);
const std::vector<int>& scaleIntervals(int scale);
int snapPitch(int pitch, int root, int scale, int low = 0, int high = 127);
// Tunings give root-relative pitch classes a quarter-tone form. Inflections keep the selected
// scale; maqam, blues and 24-EDO tunings bring their own.
constexpr int tuningCount = 7;
int tuningScale(int tuning, int scale);
bool tuningOwnsScale(int tuning);
// Offset in cents of a root-relative pitch class's microtonal form, or 0 when it has none.
// 24-EDO forms may also take the negative offset.
int microOffset(int tuning, int pitchClass);
// Nearest scale note, or microtonal form of one, as {pitch, cents}.
std::pair<int, int> snapTuned(int pitch, int cents, int root, int scale, int tuning, int low = 0,
                              int high = 127);
// Gives unlocked notes starting in [start, end) their microtonal forms, deciding once per pitch
// class so the phrase stays in one mode. Pitches and timing are unchanged; microtones 0 clears.
Clip retuneRegion(const Clip& clip, double start, double end, const Settings& settings);
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
                           const std::function<void(float)>& progress = {},
                           const std::function<void(const SearchStats&)>& statistics = {});
double similarity(const Clip& a, const Clip& b);
} // namespace composer
