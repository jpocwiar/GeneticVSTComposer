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
    // groupCount 0 means the meter default. groupUnit is the grouping denominator (4 = quarter).
    std::array<int, 16> groups{};
    int groupCount = 0, groupUnit = 4;
};
using Bank = std::array<Clip, slotCount>;
enum class RegionAction { Simplify, MoreMovement, Answer, Resolve };
// Times are quarter-note beats. Qualities: major, minor, diminished, augmented,
// dominant seventh, major seventh, minor seventh, suspended second/fourth.
struct ChordEvent {
    double start = 0.0, duration = 4.0;
    int root = 0, quality = 0;
    // Pitch class of a slash bass, or -1 when the chord has no slash.
    int bass = -1;
};
struct Harmony {
    std::array<ChordEvent, 64> chords{};
    int count = 0;
    double length = 16.0;
};
struct Settings {
    int root = 0, scale = 1, bars = 4, division = 16;
    int numerator = 4, denominator = 4, low = 48, high = 84;
    // Same meaning as Clip: groupCount 0 selects the default grouping for this meter.
    std::array<int, 16> groups{};
    int groupCount = 0, groupUnit = 4;
    int mode = 0, progression = 0, quality = 1;
    // Musical architecture, independent of generation mode: riff, motif, melody.
    int phraseType = 0;
    // Bars in one repeated Riff cell: 0.5, 1 or 2.
    double riffCellBars = 1.0;
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
    // Optional evolution scope. Protected onsets and locked notes retain every stored field.
    bool regionOnly = false;
    double regionStart = 0, regionEnd = 0;
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
// 3 on a barline, 2 at a group start, 1 on a subdivision, 0 elsewhere.
// A groupCount of 0 uses the meter default, so 4/4 still treats integer quarters as strong.
int metricWeight(double beat, const Settings&);
void defaultGrouping(int numerator, int denominator, std::array<int, 16>& groups, int& groupCount,
                     int& groupUnit);
std::string groupingText(const std::array<int, 16>& groups, int count);
// "2+2+3" or "2 + 2 + 3". Groups are whole numbers 1-32, at most 16 of them.
bool parseGrouping(const std::string& text, std::array<int, 16>& groups, int& count, std::string& error);
// Compositions of 2 and 3 (and the meter default) that fill one bar.
std::vector<std::string> groupingChoices(int numerator, int denominator);
struct FitnessTerm {
    const char* name;
    double value;
};
// The signed terms of fitnessScore(), in evaluation order. They add up to fitnessScore().
std::vector<FitnessTerm> fitnessBreakdown(const Clip&, const Settings&);
const std::vector<int>& scaleIntervals(int scale);
// Seven-note scale whose degrees build chords: pentatonic, chromatic and whole-tone scales use
// major, blues and Saba use natural minor, and seven-note scales map to themselves.
int harmonicScale(int scale);
// 3 at a bar start, 2 on a beat, 1 on a beat subdivision, 0 elsewhere. Compound meters (6/8,
// 9/8, 12/8) beat in dotted units; other x/8 and x/16 meters group twos and end with a
// three, so 7/8 is 2+2+3.
int metricLevel(double time, int numerator, int denominator);
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
// Retarget pitches to actual chord events, keeping timing, velocity, note count,
// locks and clip metadata. Only unlocked onsets in [start, end) are editable.
// Uses minimal pitch movement and contour costs; short passing notes may remain.
Clip adaptToHarmony(const Clip&, double start, double end, const Settings&);
// Pitch classes of the stored chord vocabulary; unused entries are -1.
std::array<int, 4> chordPitchClasses(const ChordEvent&);
bool isValidMicrotonalPassing(const Clip&, int index, const Settings&);
Clip applyGroove(const Clip& target, const Clip& reference, float timingAmount, float velocityAmount);
Clip combineParts(const Clip& rhythm, const Clip& pitches, const Clip& accents);
Bank makeInitialBank();
// A cancelled run returns no results. The caller keeps its previous bank.
// mates are extra crossover parents for evolution. Fresh generation ignores them.
std::vector<Clip> generate(Settings settings, const Clip* parent, const std::atomic<bool>& cancelled,
                           const std::function<void(float)>& progress = {},
                           const std::function<void(const SearchStats&)>& statistics = {},
                           const std::vector<Clip>* mates = nullptr);
// Share of notes, out of the larger clip, matched by onset, key and cents. Matching follows
// the onsets, so an inserted or removed note does not misalign the rest. Clips must be sorted.
double similarity(const Clip& a, const Clip& b);
} // namespace composer
