#include "ComposerEngine.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <numeric>
#include <random>
#include <sstream>

namespace composer {
const std::vector<TimeSignature>& timeSignatures() {
    // Keep the original five first: their automation indices must remain stable.
    static const std::vector<TimeSignature> meters{
        {4, 4},  {3, 4},  {5, 4},  {7, 8},  {6, 8},  {2, 4},  {6, 4},   {7, 4},   {8, 4},
        {9, 4},  {2, 2},  {3, 2},  {4, 2},  {3, 8},  {5, 8},  {9, 8},   {10, 8},  {11, 8},
        {12, 8}, {13, 8}, {15, 8}, {5, 16}, {7, 16}, {9, 16}, {11, 16}, {13, 16}, {15, 16}};
    return meters;
}
bool parseMeter(const std::string& text, TimeSignature& result, std::string& error) {
    std::istringstream input(text);
    int numerator = 0, denominator = 0;
    char slash = 0;
    if (!(input >> numerator >> slash >> denominator) || slash != '/' || numerator < 1 || numerator > 32 ||
        denominator < 1 || denominator > 32 || (denominator & (denominator - 1)) != 0) {
        error = "Use numerator/denominator: 1-32 over 1, 2, 4, 8, 16 or 32 (e.g. 11/8).";
        return false;
    }
    input >> std::ws;
    if (!input.eof()) {
        error = "Enter one time signature, e.g. 11/8.";
        return false;
    }
    result = {numerator, denominator};
    error.clear();
    return true;
}
const std::vector<ProgressionPreset>& progressionPresets() {
    static const std::vector<ProgressionPreset> presets{
        {"Scale only", {}},
        {"I - IV - V - I", {0, 3, 4, 0}},
        {"I - VI - IV - V", {0, 5, 3, 4}},
        {"I - IV - VI - V", {0, 3, 5, 4}},
        {"II - V - I - I", {1, 4, 0, 0}},
        {"I - V - VI - IV", {0, 4, 5, 3}},
        {"VI - IV - I - V", {5, 3, 0, 4}},
        {"I - VI - II - V", {0, 5, 1, 4}},
        {"I - III - IV - V", {0, 2, 3, 4}},
        {"I - II - IV - V", {0, 1, 3, 4}},
        {"I - V - IV - I", {0, 4, 3, 0}},
        {"I - IV - I - V", {0, 3, 0, 4}},
        {"I - VI - IV - I", {0, 5, 3, 0}},
        {"VI - II - V - I", {5, 1, 4, 0}},
        {"II - V - I - VI", {1, 4, 0, 5}},
        {"I - IV - II - V", {0, 3, 1, 4}},
        {"VI - V - IV - V", {5, 4, 3, 4}},
        {"I - VII - VI - VII", {0, 6, 5, 6}},
        {"I - VI - III - VII", {0, 5, 2, 6}},
        {"I - VII - IV - I", {0, 6, 3, 0}},
        {"Circle / 8 bars", {0, 3, 6, 2, 5, 1, 4, 0}},
        {"I / IV / V / 12 bars", {0, 0, 0, 0, 3, 3, 0, 0, 4, 3, 0, 4}},
        {"I - IV", {0, 3}},
        {"I - V", {0, 4}},
        {"Tonic pedal / I", {0}},
        {"II - V - I", {1, 4, 0}}};
    return presets;
}
SearchBudget searchBudget(const Settings& s) {
    static constexpr SearchBudget presets[]{{64, 40}, {128, 100}, {256, 300}};
    const auto preset = presets[std::clamp(s.quality, 0, 2)];
    return {s.population > 0 ? std::clamp(s.population, 16, 512) : preset.population,
            s.generations > 0 ? std::clamp(s.generations, 1, 2000) : preset.generations};
}
namespace {
bool validDenominator(int d) {
    return d >= 1 && d <= 32 && (d & (d - 1)) == 0;
}
int progressionDegree(int progression, double time, double bar) {
    const auto& presets = progressionPresets();
    const auto& degrees = presets[size_t(std::clamp(progression, 0, int(presets.size()) - 1))].degrees;
    if (degrees.empty())
        return 0;
    return degrees[size_t(std::max(0.0, std::floor(time / bar))) % degrees.size()];
}
} // namespace
const std::vector<int>& scaleIntervals(int scale) {
    static const std::array<std::vector<int>, 10> scales{{{0, 2, 4, 5, 7, 9, 11},
                                                          {0, 2, 3, 5, 7, 8, 10},
                                                          {0, 2, 3, 5, 7, 9, 10},
                                                          {0, 2, 4, 5, 7, 9, 10},
                                                          {0, 2, 4, 7, 9},
                                                          {0, 3, 5, 7, 10},
                                                          {0, 2, 3, 5, 7, 8, 11},
                                                          {0, 2, 4, 6, 7, 9, 11},
                                                          {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11},
                                                          // Maqam Saba; selected only by its tuning.
                                                          {0, 2, 3, 4, 7, 8, 10}}};
    return scales[std::clamp(scale, 0, 9)];
}
int snapPitch(int pitch, int root, int scale, int low, int high) {
    low = std::clamp(low, 0, 127);
    high = std::clamp(high, low, 127);
    const auto& tones = scaleIntervals(scale);
    int result = std::clamp(pitch, low, high), best = 999;
    for (int p = low; p <= high; ++p) {
        const int pc = ((p - root) % 12 + 12) % 12;
        if (std::find(tones.begin(), tones.end(), pc) != tones.end() && std::abs(p - pitch) < best) {
            best = std::abs(p - pitch);
            result = p;
        }
    }
    return result;
}
namespace {
// Inflections (and 12-TET) keep the selected scale: -1.
constexpr int tuningScales[tuningCount]{-1, -1, 0, 1, 9, 5, 8};
} // namespace
int tuningScale(int tuning, int scale) {
    const int own = tuningScales[std::clamp(tuning, 0, tuningCount - 1)];
    return own < 0 ? std::clamp(scale, 0, 8) : own;
}
bool tuningOwnsScale(int tuning) {
    return tuningScales[std::clamp(tuning, 0, tuningCount - 1)] >= 0;
}
int microOffset(int tuning, int pitchClass) {
    static constexpr int offsets[tuningCount][12]{
        {},
        // Neutral 2nds, 3rds, 6ths and 7ths, midway between the minor and major forms.
        {0, 50, -50, 50, -50, 0, 0, 0, 50, -50, 50, -50},
        {0, 0, 0, 0, -50, 0, 0, 0, 0, 0, 0, -50},         // Rast: half-flat 3rd and 7th
        {0, 0, -50, 0, 0, 0, 0, 0, 0, 0, 0, 0},           // Bayati: half-flat 2nd
        {0, 0, -50, 0, 0, 0, 0, 0, 0, 0, 0, 0},           // Saba: half-flat 2nd
        {0, 0, 0, 50, 0, 0, 0, 0, 0, 0, 50, 0},           // blues: neutral 3rd and 7th
        {0, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50}}; // 24-EDO: a quarter tone either way
    return offsets[std::clamp(tuning, 0, tuningCount - 1)][(pitchClass % 12 + 12) % 12];
}
std::pair<int, int> snapTuned(int pitch, int cents, int root, int scale, int tuning, int low, int high) {
    low = std::clamp(low, 0, 127);
    high = std::clamp(high, low, 127);
    tuning = std::clamp(tuning, 0, tuningCount - 1);
    const auto& tones = scaleIntervals(tuning ? tuningScale(tuning, scale) : scale);
    const auto inScale = [&](int p) {
        return std::find(tones.begin(), tones.end(), ((p - root) % 12 + 12) % 12) != tones.end();
    };
    // A key's forms in rising pitch; 24-EDO forms go either way.
    const auto forms = [&](int p) {
        const int offset = microOffset(tuning, p - root);
        return std::array<int, 3>{tuning == tuningCount - 1 ? -offset : std::min(offset, 0), 0,
                                  std::max(offset, 0)};
    };
    if (pitch >= low && pitch <= high && inScale(pitch)) {
        // Already allowed: keep the note's own spelling.
        const auto own = forms(pitch);
        if (std::find(own.begin(), own.end(), cents) != own.end())
            return {pitch, cents};
    }
    const int target = std::clamp(pitch, -1, 128) * 100 + std::clamp(cents, -100, 100);
    std::pair<int, int> result{std::clamp(pitch, low, high), 0};
    int best = std::numeric_limits<int>::max();
    for (int p = low; p <= high; ++p)
        if (inScale(p))
            // Candidates rise in pitch, so ties resolve downwards as in snapPitch.
            for (int form : forms(p))
                if (std::abs(p * 100 + form - target) < best) {
                    best = std::abs(p * 100 + form - target);
                    result = {p, form};
                }
    return result;
}
void sanitise(Clip& clip) {
    clip.length = std::isfinite(clip.length) ? std::clamp(clip.length, 0.125, 128.0) : 16.0;
    clip.grid = std::isfinite(clip.grid) ? std::clamp(clip.grid, 0.03125, 1.0) : 0.25;
    clip.grid = std::min(clip.grid, clip.length);
    clip.numerator = std::clamp(clip.numerator, 1, 32);
    if (!validDenominator(clip.denominator))
        clip.denominator = 4;
    clip.count = std::clamp(clip.count, 0, maxNotes);
    int count = 0;
    for (int i = 0; i < clip.count; ++i) {
        auto n = clip.notes[i];
        if (!std::isfinite(n.start) || !std::isfinite(n.duration) || n.start < 0.0 ||
            n.start >= clip.length || n.duration <= 0.0)
            continue;
        n.duration = std::min(n.duration, clip.length - n.start);
        n.pitch = std::clamp(n.pitch, 0, 127);
        // Larger offsets move to the nearer key, keeping the sounding pitch where possible.
        n.cents = std::clamp(n.cents, -1200, 1200);
        for (; n.cents > 50 && n.pitch < 127; n.cents -= 100)
            ++n.pitch;
        for (; n.cents < -50 && n.pitch > 0; n.cents += 100)
            --n.pitch;
        n.cents = std::clamp(n.cents, -50, 50);
        n.velocity = std::clamp(n.velocity, 1, 127);
        clip.notes[count++] = n;
    }
    clip.count = count;
    std::stable_sort(clip.notes.begin(), clip.notes.begin() + count,
                     [](const auto& a, const auto& b) { return a.start < b.start; });
}
void sanitise(Harmony& harmony) {
    harmony.length = std::isfinite(harmony.length) ? std::clamp(harmony.length, 0.125, 128.0) : 16.0;
    harmony.count = std::clamp(harmony.count, 0, int(harmony.chords.size()));
    int count = 0;
    for (int i = 0; i < harmony.count; ++i) {
        auto chord = harmony.chords[i];
        if (!std::isfinite(chord.start) || !std::isfinite(chord.duration) || chord.start < 0.0 ||
            chord.start >= harmony.length || chord.duration <= 0.0)
            continue;
        chord.root = (chord.root % 12 + 12) % 12;
        chord.quality = std::clamp(chord.quality, 0, 8);
        harmony.chords[count++] = chord;
    }
    std::stable_sort(harmony.chords.begin(), harmony.chords.begin() + count,
                     [](const auto& a, const auto& b) { return a.start < b.start; });
    harmony.count = 0;
    for (int i = 0; i < count; ++i) {
        if (harmony.count &&
            std::abs(harmony.chords[harmony.count - 1].start - harmony.chords[i].start) < 1e-8)
            harmony.chords[harmony.count - 1] = harmony.chords[i];
        else
            harmony.chords[harmony.count++] = harmony.chords[i];
    }
    // Sustain chords through gaps in reference MIDI. Canonical contiguous durations
    // also make text export and reparsing lossless.
    if (harmony.count)
        harmony.chords[0].start = 0.0;
    for (int i = 0; i < harmony.count; ++i)
        harmony.chords[i].duration =
            (i + 1 < harmony.count ? harmony.chords[i + 1].start : harmony.length) - harmony.chords[i].start;
}
namespace {
constexpr int chordIntervals[9][4]{{0, 4, 7, -1}, {0, 3, 7, -1}, {0, 3, 6, -1}, {0, 4, 8, -1}, {0, 4, 7, 10},
                                   {0, 4, 7, 11}, {0, 3, 7, 10}, {0, 2, 7, -1}, {0, 5, 7, -1}};
constexpr const char* chordSuffixes[]{"", "m", "dim", "aug", "7", "maj7", "m7", "sus2", "sus4"};
bool validPassing(const Clip&, int, const Settings&);
const ChordEvent* chordAt(const Harmony& harmony, double time) {
    if (!harmony.count || !std::isfinite(time) || harmony.length <= 0.0)
        return nullptr;
    time = std::fmod(std::max(0.0, time), harmony.length);
    for (int i = harmony.count - 1; i >= 0; --i)
        if (time + 1e-8 >= harmony.chords[i].start)
            return &harmony.chords[i];
    return &harmony.chords[0];
}
bool belongsToChord(int pitch, const ChordEvent& chord) {
    const int pc = ((pitch - chord.root) % 12 + 12) % 12;
    for (int interval : chordIntervals[chord.quality])
        if (interval >= 0 && pc == interval)
            return true;
    return false;
}
float unitAmount(float value) {
    return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f;
}
// At 100%, half of all 24-EDO pitch classes take a quarter-tone form, so the 24 steps are about
// equally likely.
float microChance(const Settings& s) {
    return unitAmount(s.microtones) * (s.tuning == tuningCount - 1 ? 0.5f : 1.0f);
}
// Plain, or by chance the pitch class's microtonal form. 24-EDO forms go either way, so every
// quarter tone is reachable while the tonic stays equal-tempered: C#-50 is the step above C.
template <typename Random> int chooseForm(const Settings& s, int pitchClass, Random&& random) {
    const int offset = microOffset(s.tuning, pitchClass);
    if (!offset || random() >= microChance(s))
        return 0;
    return s.tuning == tuningCount - 1 && random() < 0.5f ? -offset : offset;
}
int harmonicPitch(int desired, double time, const Settings& s, bool rootOnly = false) {
    const int low = std::clamp(s.low, 0, 127), high = std::clamp(s.high, low, 127);
    const auto* chord = chordAt(s.harmony, time);
    if (!chord && s.progression == 0 && !rootOnly)
        return snapPitch(desired, s.root, s.scale, low, high);
    const auto& scale = scaleIntervals(s.scale);
    const double bar =
        std::clamp(s.numerator, 1, 32) * 4.0 / (validDenominator(s.denominator) ? s.denominator : 4);
    const int degree = progressionDegree(s.progression, time, bar);
    const int progressionRoot = s.root + (s.progression > 0 ? scale[degree % scale.size()] : 0);
    const int root = ((chord ? chord->root : progressionRoot) % 12 + 12) % 12;
    int best = std::clamp(desired, low, high), distance = 999;
    for (int p = low; p <= high; ++p) {
        bool eligible = p % 12 == root;
        if (!rootOnly && chord)
            eligible = belongsToChord(p, *chord);
        else if (!rootOnly && s.progression > 0) {
            const int relative = ((p - s.root) % 12 + 12) % 12;
            eligible = false;
            for (int third : {0, 2, 4})
                eligible |= relative == scale[(degree + third) % scale.size()];
        }
        if (eligible && std::abs(p - desired) < distance) {
            distance = std::abs(p - desired);
            best = p;
        }
    }
    return best;
}
} // namespace
bool parseHarmony(const std::string& text, Harmony& harmony, std::string& error) {
    error.clear();
    if (text.size() > 8192) {
        error = "Chord text is too long (maximum 64 chords).";
        return false;
    }
    Harmony parsed;
    double position = 0.0;
    std::istringstream input(text);
    input.imbue(std::locale::classic());
    std::string token;
    while (input >> token) {
        const auto colon = token.find(':');
        if (colon == std::string::npos || colon == 0 || colon + 1 == token.size() ||
            token.find(':', colon + 1) != std::string::npos) {
            error = "Use chord:beats, for example Cm:4 Fm:2 G7:2.";
            return false;
        }
        const std::string name = token.substr(0, colon);
        int root = -1;
        switch (std::toupper(static_cast<unsigned char>(name[0]))) {
        case 'C':
            root = 0;
            break;
        case 'D':
            root = 2;
            break;
        case 'E':
            root = 4;
            break;
        case 'F':
            root = 5;
            break;
        case 'G':
            root = 7;
            break;
        case 'A':
            root = 9;
            break;
        case 'B':
            root = 11;
            break;
        default:
            break;
        }
        size_t suffixStart = 1;
        if (name.size() > 1 && (name[1] == '#' || name[1] == 'b')) {
            root += name[1] == '#' ? 1 : -1;
            suffixStart = 2;
        }
        const auto suffix = name.substr(suffixStart);
        int quality = -1;
        for (int q = 0; q < 9; ++q)
            if (suffix == chordSuffixes[q])
                quality = q;
        const bool validLetter =
            std::string("ABCDEFG").find(char(std::toupper(static_cast<unsigned char>(name[0])))) !=
            std::string::npos;
        if (!validLetter || quality < 0) {
            error = "Unknown chord '" + name + "'. Use A-G, optional #/b, and m/dim/aug/7/maj7/m7/sus2/sus4.";
            return false;
        }
        std::istringstream durationInput(token.substr(colon + 1));
        durationInput.imbue(std::locale::classic());
        double duration = 0.0;
        char remainder = 0;
        if (!(durationInput >> duration) || (durationInput >> remainder) || !std::isfinite(duration) ||
            duration < 0.03125 || position + duration > 128.0 + 1e-8) {
            error =
                "Durations must be positive beats (minimum 0.03125); total length must not exceed 128 beats.";
            return false;
        }
        if (parsed.count == int(parsed.chords.size())) {
            error = "A progression can contain at most 64 chords.";
            return false;
        }
        parsed.chords[parsed.count++] = {position, duration, (root + 12) % 12, quality};
        position += duration;
    }
    if (parsed.count && position < 0.125) {
        error = "The progression must be at least 0.125 beats long.";
        return false;
    }
    parsed.length = parsed.count ? position : 16.0;
    sanitise(parsed);
    harmony = parsed;
    return true;
}
std::string harmonyText(const Harmony& source) {
    auto harmony = source;
    sanitise(harmony);
    static constexpr const char* names[]{"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
    std::ostringstream text;
    text.imbue(std::locale::classic());
    text << std::setprecision(std::numeric_limits<double>::max_digits10);
    for (int i = 0; i < harmony.count; ++i) {
        const auto& chord = harmony.chords[i];
        if (i)
            text << ' ';
        text << names[chord.root] << chordSuffixes[chord.quality] << ':' << chord.duration;
    }
    return text.str();
}
Harmony inferHarmony(const Clip& source) {
    auto clip = source;
    sanitise(clip);
    Harmony harmony;
    harmony.length = clip.length;
    std::vector<double> boundaries{0.0, clip.length};
    for (int i = 0; i < clip.count; ++i) {
        boundaries.push_back(clip.notes[i].start);
        boundaries.push_back(clip.notes[i].start + clip.notes[i].duration);
    }
    std::sort(boundaries.begin(), boundaries.end());
    boundaries.erase(std::unique(boundaries.begin(), boundaries.end(),
                                 [](double a, double b) { return std::abs(a - b) < 0.015625; }),
                     boundaries.end());
    for (size_t index = 0; index + 1 < boundaries.size(); ++index) {
        const double start = boundaries[index], end = boundaries[index + 1];
        // Ignore tiny transitions caused by imperfectly simultaneous MIDI notes.
        if (end - start < 0.03125)
            continue;
        const double time = (start + end) * 0.5;
        std::array<bool, 12> present{};
        int bass = 128;
        for (int i = 0; i < clip.count; ++i) {
            const auto& note = clip.notes[i];
            if (note.start <= time && note.start + note.duration > time) {
                present[note.pitch % 12] = true;
                bass = std::min(bass, note.pitch);
            }
        }
        const int pitchCount = int(std::count(present.begin(), present.end(), true));
        // Single notes and dyads are too ambiguous; hold the previous identified chord.
        if (pitchCount < 3)
            continue;
        int bestRoot = 0, bestQuality = 0;
        double bestScore = -1e9;
        for (int root = 0; root < 12; ++root)
            for (int quality = 0; quality < 9; ++quality) {
                int hits = 0, missing = 0;
                for (int interval : chordIntervals[quality])
                    if (interval >= 0) {
                        if (present[(root + interval) % 12])
                            ++hits;
                        else
                            ++missing;
                    }
                const double score =
                    hits * 3.0 - missing * 3.5 - (pitchCount - hits) * 2.0 + (bass % 12 == root ? 0.2 : 0.0);
                if (score > bestScore) {
                    bestScore = score;
                    bestRoot = root;
                    bestQuality = quality;
                }
            }
        if (harmony.count && harmony.chords[harmony.count - 1].root == bestRoot &&
            harmony.chords[harmony.count - 1].quality == bestQuality)
            continue;
        if (harmony.count == int(harmony.chords.size()))
            break;
        harmony.chords[harmony.count++] = {start, end - start, bestRoot, bestQuality};
    }
    sanitise(harmony);
    return harmony;
}
Clip transformRegion(const Clip& source, double start, double end, RegionAction action,
                     const Settings& settings) {
    auto clip = source;
    sanitise(clip);
    if (!std::isfinite(start) || !std::isfinite(end))
        return clip;
    start = std::clamp(start, 0.0, clip.length);
    end = std::clamp(end, start, clip.length);
    if (end - start < 1e-8)
        return clip;
    auto s = settings;
    sanitise(s.harmony);
    s.timeOffset = std::isfinite(s.timeOffset) ? std::max(0.0, s.timeOffset) : 0.0;
    s.numerator = clip.numerator;
    s.denominator = clip.denominator;
    s.tuning = std::clamp(s.tuning, 0, tuningCount - 1);
    std::vector<int> editable;
    for (int i = 0; i < clip.count; ++i)
        if (!clip.notes[i].locked && clip.notes[i].start >= start && clip.notes[i].start < end)
            editable.push_back(i);
    if (editable.empty())
        return clip;
    const auto protectedOverlap = [&](double onset, double finish) {
        for (int i = 0; i < clip.count; ++i) {
            const auto& note = clip.notes[i];
            if ((note.locked || note.start < start || note.start >= end) &&
                onset < note.start + note.duration - 1e-8 && finish > note.start + 1e-8)
                return true;
        }
        return false;
    };
    std::array<bool, maxNotes> remove{};
    const int anchor = clip.notes[editable.front()].pitch;
    std::mt19937 random(s.seed);
    std::uniform_int_distribution<int> direction(0, 1);
    // Region tools do not invent unscored microtonal genes. A moved note becomes
    // an ordinary anchor; unchanged valid passing notes can keep their offsets.
    const auto setPitch = [&](Note& note, int pitch, bool stable = false) {
        if (pitch == note.pitch && !stable)
            return;
        note.cents = 0;
        note.pitch = pitch;
    };
    for (size_t position = 0; position < editable.size(); ++position) {
        const int index = editable[position];
        auto& note = clip.notes[index];
        if (action == RegionAction::Simplify) {
            // Keep the opening and cadence while thinning the interior of the phrase.
            if ((position % 2 == 1 && position + 1 < editable.size()) ||
                (editable.size() == 2 && position == 1))
                remove[index] = true;
        } else if (action == RegionAction::MoreMovement) {
            const int step = direction(random) ? 2 : -2;
            setPitch(note, harmonicPitch(note.pitch + step, note.start + s.timeOffset, s));
            const double split =
                std::max(clip.grid, std::floor(note.duration / (2.0 * clip.grid)) * clip.grid);
            const double secondStart = note.start + split;
            double secondEnd = std::min(note.start + note.duration, end);
            // Existing following events keep their timing; do not add a note over them.
            for (int i = 0; i < clip.count; ++i)
                if (clip.notes[i].start > note.start + 1e-8)
                    secondEnd = std::min(secondEnd, clip.notes[i].start);
            if (note.duration >= 2.0 * clip.grid - 1e-8 && secondEnd - secondStart >= clip.grid - 1e-8 &&
                clip.count < maxNotes && !protectedOverlap(secondStart, secondEnd)) {
                Note added = note;
                added.start = secondStart;
                added.duration = secondEnd - secondStart;
                setPitch(added, harmonicPitch(note.pitch + step, added.start + s.timeOffset, s));
                added.velocity = std::clamp(note.velocity - 5, 1, 127);
                note.duration = split;
                clip.notes[clip.count++] = added;
            }
        } else if (action == RegionAction::Answer) {
            // Invert the selected contour around its first pitch, then give the response
            // a settled final note. The rhythm remains recognisable.
            setPitch(note, harmonicPitch(anchor - (note.pitch - anchor), note.start + s.timeOffset, s));
            if (position + 1 == editable.size())
                setPitch(note, harmonicPitch(note.pitch, note.start + s.timeOffset, s, true), true);
        } else if (action == RegionAction::Resolve) {
            // Cadential motion is limited to the final two editable events.
            if (position + 1 == editable.size())
                setPitch(note, harmonicPitch(note.pitch, note.start + s.timeOffset, s, true), true);
            else if (position + 2 == editable.size())
                setPitch(note, harmonicPitch(note.pitch, note.start + s.timeOffset, s));
        }
    }
    if (action == RegionAction::Simplify) {
        int retained = 0;
        for (int i = 0; i < clip.count; ++i)
            if (!remove[i])
                clip.notes[retained++] = clip.notes[i];
        clip.count = retained;
    }
    sanitise(clip);
    for (int i = clip.count - 1; i >= 0; --i) {
        auto& note = clip.notes[size_t(i)];
        if (!note.locked && note.start >= start && note.start < end &&
            (!s.microEnabled || !validPassing(clip, i, s)))
            note.cents = 0;
    }
    return clip;
}
Clip retuneRegion(const Clip& source, double start, double end, const Settings& settings) {
    auto clip = source;
    sanitise(clip);
    if (!std::isfinite(start) || !std::isfinite(end))
        return clip;
    auto s = settings;
    s.tuning = std::clamp(s.tuning, 0, tuningCount - 1);
    std::mt19937 random(s.seed);
    std::array<int, 12> forms{};
    for (int pc = 0; pc < 12; ++pc)
        forms[size_t(pc)] = chooseForm(s, pc, [&] { return std::generate_canonical<float, 24>(random); });
    for (int i = 0; i < clip.count; ++i) {
        auto& note = clip.notes[i];
        if (!note.locked && note.start >= start && note.start < end)
            note.cents = forms[((note.pitch - s.root) % 12 + 12) % 12];
    }
    return clip;
}
Clip applyGroove(const Clip& source, const Clip& referenceSource, float timingAmount, float velocityAmount) {
    auto target = source, reference = referenceSource;
    sanitise(target);
    sanitise(reference);
    timingAmount = unitAmount(timingAmount);
    velocityAmount = unitAmount(velocityAmount);
    if (reference.count == 0 || (timingAmount == 0.0f && velocityAmount == 0.0f))
        return target;
    const auto original = target;
    // Nearest grid positions on a looping reference. A one-bar groove repeats over
    // a longer target, while a long reference contributes its opening bars.
    for (int i = 0; i < target.count; ++i) {
        auto& note = target.notes[i];
        if (note.locked)
            continue;
        const double gridPosition = std::round(note.start / target.grid) * target.grid;
        const double phase = std::fmod(gridPosition, reference.length) / reference.length;
        int nearest = 0;
        double distance = 2.0;
        for (int j = 0; j < reference.count; ++j) {
            const double refGrid = std::round(reference.notes[j].start / reference.grid) * reference.grid;
            const double refPhase = std::fmod(refGrid, reference.length) / reference.length;
            const double direct = std::abs(phase - refPhase);
            const double candidate = std::min(direct, 1.0 - direct);
            // At a shared onset prefer the loudest accent from polyphonic references.
            if (candidate < distance - 1e-8 ||
                (std::abs(candidate - distance) < 1e-8 &&
                 reference.notes[j].velocity > reference.notes[nearest].velocity)) {
                distance = candidate;
                nearest = j;
            }
        }
        const auto& ref = reference.notes[nearest];
        const double refGrid = std::round(ref.start / reference.grid) * reference.grid;
        const double deviation = ref.start - refGrid;
        const double proposed = note.start + timingAmount * (gridPosition + deviation - note.start);
        // Keep the note's duration and the order of distinct onsets, including locked notes.
        // Exact simultaneous events receive the same offset.
        double lower = 0.0, upper = target.length - note.duration;
        for (int j = 0; j < original.count; ++j) {
            const auto& other = original.notes[j];
            if (other.start < original.notes[i].start - 1e-8)
                lower = std::max(lower, (other.start + original.notes[i].start) * 0.5);
            else if (other.start > original.notes[i].start + 1e-8)
                upper = std::min(upper, (other.start + original.notes[i].start) * 0.5);
            if (other.locked && other.pitch == note.pitch) {
                if (other.start + other.duration <= original.notes[i].start + 1e-8)
                    lower = std::max(lower, other.start + other.duration);
                if (other.start >= original.notes[i].start + note.duration - 1e-8)
                    upper = std::min(upper, other.start - note.duration);
            }
        }
        if (upper >= lower)
            note.start = std::clamp(proposed, lower, upper);
        note.velocity = std::clamp(
            int(std::lround(note.velocity + velocityAmount * (ref.velocity - note.velocity))), 1, 127);
    }
    sanitise(target);
    return target;
}
Clip combineParts(const Clip& rhythmSource, const Clip& pitchSource, const Clip& accentSource) {
    auto result = rhythmSource, pitches = pitchSource, accents = accentSource;
    sanitise(result);
    sanitise(pitches);
    sanitise(accents);
    const auto nearest = [](const Clip& source, double phase) {
        int index = 0;
        double distance = 2.0;
        for (int i = 0; i < source.count; ++i) {
            const double candidate = std::abs(source.notes[i].start / source.length - phase);
            if (candidate < distance) {
                distance = candidate;
                index = i;
            }
        }
        return index;
    };
    for (int i = 0; i < result.count; ++i) {
        auto& note = result.notes[i];
        if (note.locked)
            continue;
        const double phase = note.start / result.length;
        if (pitches.count) {
            const auto& source = pitches.notes[nearest(pitches, phase)];
            note.pitch = source.pitch;
            note.cents = source.cents;
        }
        if (accents.count)
            note.velocity = accents.notes[nearest(accents, phase)].velocity;
    }
    return result;
}
double similarity(const Clip& a, const Clip& b) {
    if (a.count == 0 || b.count == 0)
        return 0.0;
    int matches = 0;
    for (int i = 0; i < std::min(a.count, b.count); ++i)
        if (std::abs(a.notes[i].start - b.notes[i].start) < 0.01 && a.notes[i].pitch == b.notes[i].pitch &&
            a.notes[i].cents == b.notes[i].cents)
            ++matches;
    return double(matches) / std::max(a.count, b.count);
}
double intervalCost(double cents) {
    if (!std::isfinite(cents))
        return 20.0;
    const double distance = std::abs(cents);
    // Small steps are inexpensive. Register-sized leaps remain expensive rather
    // than disappearing through pitch-class modulo arithmetic.
    constexpr double positions[]{0, 50, 100, 200, 300, 400, 500, 600, 700, 900, 1200};
    constexpr double costs[]{0.03, 0.06, 0.10, 0.22, 0.40, 0.62, 0.83, 1.15, 1.05, 1.55, 2.0};
    for (int i = 1; i < 11; ++i)
        if (distance <= positions[i]) {
            const double fraction = (distance - positions[i - 1]) / (positions[i] - positions[i - 1]);
            return costs[i - 1] + fraction * (costs[i] - costs[i - 1]);
        }
    const double octaves = (distance - 1200.0) / 1200.0;
    return 2.0 + 3.0 * octaves + octaves * octaves;
}
namespace {
double actualPitch(const Note& note) {
    return note.pitch * 100.0 + note.cents;
}
double passingLimit(const Clip& clip) {
    return std::min(0.5, std::max(0.03125, clip.grid));
}
int genePalette(const Settings& s) {
    return s.microEnabled ? std::clamp(s.tuning ? s.tuning : tuningCount - 1, 1, tuningCount - 1) : 0;
}
bool validPassing(const Clip& clip, int index, const Settings& settings) {
    const auto& note = clip.notes[size_t(index)];
    if (!note.cents || note.duration > passingLimit(clip) + 1e-8 || index == 0 || index + 1 >= clip.count)
        return false;
    const int palette = genePalette(settings);
    const int offset = microOffset(palette, note.pitch - settings.root);
    if (!palette || !offset ||
        (note.cents != offset && !(palette == tuningCount - 1 && note.cents == -offset)))
        return false;
    for (int ahead = 1; ahead <= 2 && index + ahead < clip.count; ++ahead) {
        const auto& anchor = clip.notes[size_t(index + ahead)];
        if (anchor.cents == 0)
            return std::abs(actualPitch(anchor) - actualPitch(note)) <= 250.0 &&
                   anchor.start >= note.start + note.duration - 1e-8 &&
                   anchor.start - (note.start + note.duration) <= 0.5 + 1e-8;
    }
    return false;
}
struct Repetition {
    double exact = 0, rhythm = 0;
};
Repetition repetitionAt(const Clip& clip, double lag, double begin = -1) {
    Repetition result;
    if (lag <= 0 || clip.length < lag * 1.5)
        return result;
    int considered = 0;
    for (int i = 0; i < clip.count; ++i) {
        const auto& note = clip.notes[size_t(i)];
        if (note.start + 1e-8 < std::max(lag, begin))
            continue;
        ++considered;
        const double target = note.start - lag;
        const auto found =
            std::lower_bound(clip.notes.begin(), clip.notes.begin() + clip.count, target - 1e-8,
                             [](const Note& n, double time) { return n.start < time; });
        if (found != clip.notes.begin() + clip.count && std::abs(found->start - target) < 1e-7 &&
            std::abs(found->duration - note.duration) < 0.02) {
            result.rhythm += 1;
            if (std::abs(actualPitch(*found) - actualPitch(note)) < 1)
                result.exact += 1;
        }
    }
    if (considered) {
        result.exact /= considered;
        result.rhythm /= considered;
    }
    return result;
}
} // namespace
PhraseDiagnostics analysePhrase(const Clip& source, const Settings& settings) {
    auto clip = source;
    sanitise(clip);
    PhraseDiagnostics result;
    if (!clip.count)
        return result;
    double middle = 0, edges = 0, first = 0, third = 0;
    double durationSum = 0, durationSquared = 0;
    double lowest = 12800, highest = 0;
    int directed = 0, ascending = 0;
    int middleCount = 0, edgeCount = 0, firstCount = 0, thirdCount = 0;
    for (int i = 0; i < clip.count; ++i) {
        const auto& note = clip.notes[size_t(i)];
        if (clip.count > 1) {
            const auto& previous = clip.notes[size_t(i ? i - 1 : clip.count - 1)];
            const double delta = std::abs(actualPitch(note) - actualPitch(previous));
            result.meanIntervalCents += delta / clip.count;
            result.intervalPenalty += intervalCost(delta) / clip.count;
            result.smallStepRatio += delta > 0 && delta <= 250 ? 1.0 / clip.count : 0;
            if (i == 0)
                result.loopIntervalCents = delta;
            if (i > 0 && delta > 0) {
                ++directed;
                ascending += actualPitch(note) > actualPitch(previous);
            }
        }
        if (note.cents) {
            ++result.microtonalNotes;
            if (validPassing(clip, i, settings))
                ++result.resolvedPassingNotes;
            else
                ++result.invalidPassingNotes;
        }
        const double phase = note.start / clip.length;
        lowest = std::min(lowest, actualPitch(note));
        highest = std::max(highest, actualPitch(note));
        result.meanPitch += actualPitch(note) / (100.0 * clip.count);
        result.offbeatRatio += std::abs(note.start - std::round(note.start)) > 0.01 ? 1.0 / clip.count : 0;
        const double duration = std::log2(std::max(0.03125, note.duration));
        durationSum += duration;
        durationSquared += duration * duration;
        if (phase >= 0.25 && phase < 0.75) {
            middle += actualPitch(note);
            ++middleCount;
        } else {
            edges += actualPitch(note);
            ++edgeCount;
        }
        if (phase < 0.25) {
            first += actualPitch(note);
            ++firstCount;
        } else if (phase >= 0.5 && phase < 0.75) {
            third += actualPitch(note);
            ++thirdCount;
        }
    }
    const double bar = clip.numerator * 4.0 / clip.denominator;
    for (double lag : {bar * 0.5, bar, bar * 2.0}) {
        const auto repetition = repetitionAt(clip, lag);
        result.repetition = std::max(result.repetition, repetition.exact);
        result.rhythmicRepetition = std::max(result.rhythmicRepetition, repetition.rhythm);
    }
    result.motifReturn = repetitionAt(clip, clip.length * 0.75, clip.length * 0.75).exact;
    // The final quarter compares to the first even though the lag exceeds half the clip.
    if (clip.length >= clip.grid * 4) {
        int matches = 0, total = 0;
        for (int i = 0; i < clip.count; ++i) {
            const auto& note = clip.notes[size_t(i)];
            if (note.start < clip.length * 0.75 - 1e-8)
                continue;
            ++total;
            const double target = note.start - clip.length * 0.75;
            const auto found =
                std::lower_bound(clip.notes.begin(), clip.notes.begin() + clip.count, target - 1e-8,
                                 [](const Note& n, double time) { return n.start < time; });
            if (found != clip.notes.begin() + clip.count && std::abs(found->start - target) < 1e-7 &&
                std::abs(found->duration - note.duration) < 0.02 &&
                std::abs(actualPitch(*found) - actualPitch(note)) < 1)
                ++matches;
        }
        result.motifReturn = total ? double(matches) / total : 0;
    }
    if (middleCount && edgeCount)
        result.contourArc = (middle / middleCount - edges / edgeCount) / 1200.0;
    if (firstCount && thirdCount)
        result.middleContrast = std::abs(third / thirdCount - first / firstCount) / 1200.0;
    result.rhythmVariation =
        std::sqrt(std::max(0.0, durationSquared / clip.count - std::pow(durationSum / clip.count, 2)));
    result.pitchSpan = (highest - lowest) / 100.0;
    result.upwardRatio = directed ? double(ascending) / directed : 0.5;
    return result;
}
namespace {
Settings normaliseSearchSettings(Settings s) {
    s.bars = std::clamp(s.bars, 1, 8);
    s.low = std::clamp(s.low, 0, 126);
    s.high = std::clamp(s.high, s.low + 1, 127);
    s.root = (s.root % 12 + 12) % 12;
    s.scale = std::clamp(s.scale, 0, 9);
    s.phraseType = std::clamp(s.phraseType, 0, 2);
    s.numerator = std::clamp(s.numerator, 1, 32);
    if (!validDenominator(s.denominator))
        s.denominator = 4;
    if (s.division != 8 && s.division != 16 && s.division != 32)
        s.division = 16;
    s.density = unitAmount(s.density);
    s.diversity = unitAmount(s.diversity);
    s.syncopation = unitAmount(s.syncopation);
    s.tension = unitAmount(s.tension);
    s.space = unitAmount(s.space);
    s.mutation = unitAmount(s.mutation);
    s.crossover = unitAmount(s.crossover);
    s.dynamics = unitAmount(s.dynamics);
    s.valence = unitAmount(s.valence);
    s.arousal = unitAmount(s.arousal);
    s.weirdness = unitAmount(s.weirdness);
    s.tuning = std::clamp(s.tuning, 0, tuningCount - 1);
    sanitise(s.harmony);
    return s;
}
struct Evolution {
    Settings s;
    std::mt19937 rng;
    const Clip* parent;
    double bar, grid, length;
    explicit Evolution(Settings settings, const Clip* p)
        : s(settings), rng(s.seed), parent(p), bar(s.numerator * 4.0 / s.denominator), grid(4.0 / s.division),
          length(s.lengthOverride > 0 ? s.lengthOverride : s.bars * bar) {
        if (parent) {
            length = parent->length;
            grid = parent->grid;
            bar = parent->numerator * 4.0 / parent->denominator;
        }
    }
    float random() { return std::generate_canonical<float, 24>(rng); }
    int integer(int lo, int hi) { return std::uniform_int_distribution<int>(lo, std::max(lo, hi))(rng); }
    double activity() const { return std::clamp(double(s.density + (s.arousal - 0.5f) * 0.65f), 0.0, 1.0); }
    double targetDuration() const {
        // Reference generator scores rhythmic values in log2 space. At the
        // default activity this targets eighth-note motion with shorter pickups,
        // instead of the previous approximately four onsets per 4/4 bar.
        return std::max(grid, 0.25 * std::exp2(1.65 - 1.5 * activity()));
    }
    double offbeatTarget() const {
        return std::clamp(double(s.syncopation + (s.weirdness - 0.1f) * 0.55f), 0.0, 1.0);
    }
    // Pitch and cents are independent genes; changing the key keeps the offset
    // until the deterministic passing-note constraints below have been checked.
    void setPitch(Note& n, int pitch, bool reroll) {
        (void)reroll;
        n.pitch = pitch;
    }
    double cellLength() const {
        return s.phraseType == 0 ? std::min(bar, length)
                                 : std::max(std::min(grid, length), std::min(bar * 0.5, length / 4.0));
    }
    bool structured() const {
        if (s.lockRhythm || s.mode == 2 || s.phraseType >= 2)
            return false;
        return !parent || (s.mutation > 0 && parent->count && parent->notes[0].start < cellLength() &&
                           std::none_of(parent->notes.begin(), parent->notes.begin() + parent->count,
                                        [](const Note& note) { return note.locked; }));
    }
    void constrainMicro(Clip& c) const {
        // This repair only removes invalid genes. It never adds random cents
        // after scoring or changes locked events/rhythmic templates.
        for (int i = c.count - 1; i >= 0; --i) {
            auto& n = c.notes[size_t(i)];
            if (n.locked)
                continue;
            if (!s.microEnabled || s.mode == 1 || !validPassing(c, i, s)) {
                const bool wasPassing = n.cents != 0;
                n.cents = 0;
                // A rejected chromatic approach becomes a stable chord anchor.
                if (wasPassing && s.harmony.count && s.tension == 0 && s.mode != 1)
                    n.pitch = harmonicPitch(n.pitch, n.start, s);
            }
        }
    }
    void mutateMicro(Clip& c, int index) {
        auto& note = c.notes[size_t(index)];
        if (note.locked || !s.microEnabled || s.mode == 1 || index == 0 || index + 1 >= c.count)
            return;
        if (random() < 0.30f) {
            note.cents = 0;
            return;
        }
        const auto& target = c.notes[size_t(index + 1)];
        if (target.cents != 0 || target.start <= note.start ||
            target.start - (note.start + note.duration) > 0.5 + 1e-8)
            return;
        if (note.duration > passingLimit(c) + 1e-8) {
            if (s.lockRhythm || s.mode == 2)
                return;
            // Retain the onset and put a short approach immediately before the
            // next anchor only when the existing note already reaches it.
            if (target.start - note.start > passingLimit(c) + 1e-8)
                return;
            note.duration = std::min(note.duration, passingLimit(c));
        }
        std::array<std::pair<int, int>, 12> options{};
        int count = 0;
        const int palette = genePalette(s);
        for (int key = std::max(s.low, target.pitch - 2); key <= std::min(s.high, target.pitch + 2); ++key) {
            const int offset = microOffset(palette, key - s.root);
            // A resolving passing note may sit outside the chord even at zero
            // Jazziness. Stable anchors still follow the harmonic constraints.
            if (!offset)
                continue;
            for (int sign : {1, -1}) {
                if (sign < 0 && palette != tuningCount - 1)
                    continue;
                const int cents = sign * offset;
                if (std::abs(key * 100 + cents - target.pitch * 100) <= 250)
                    options[size_t(count++)] = {key, cents};
            }
        }
        if (count) {
            const auto selected = options[size_t(integer(0, count - 1))];
            note.pitch = selected.first;
            note.cents = selected.second;
        }
    }
    void shape(Clip& c) const {
        if (!structured() || c.count == 0)
            return;
        const double cell = cellLength();
        const auto source = c;
        c.count = 0;
        const int anchor = source.notes[0].pitch;
        const int segments = int(std::ceil(c.length / cell - 1e-9));
        for (int segment = 0; segment < segments && c.count < maxNotes; ++segment) {
            const double offset = segment * cell;
            const int section = std::min(3, int(offset / c.length * 4.0 + 1e-8));
            for (int i = 0; i < source.count && source.notes[size_t(i)].start < cell - 1e-8; ++i) {
                auto n = source.notes[size_t(i)];
                const double originalStart = n.start;
                n.start += offset;
                if (n.start >= c.length - 1e-8)
                    break;
                n.duration = std::min({n.duration, cell - originalStart, c.length - n.start});
                if (s.mode != 1 && segment) {
                    int desired = n.pitch;
                    if (s.phraseType == 1) {
                        if (section == 1 && originalStart >= cell * 0.5)
                            desired += 2;
                        else if (section == 2)
                            desired = anchor + 4 - (n.pitch - anchor);
                    }
                    if (s.progression || s.harmony.count)
                        desired +=
                            harmonicPitch(60, n.start, s, true) - harmonicPitch(60, originalStart, s, true);
                    n.pitch = n.cents != 0 ? std::clamp(desired, s.low, s.high)
                              : ((s.progression || s.harmony.count) &&
                                 (s.tension == 0 || std::abs(n.start - std::round(n.start)) < 0.01))
                                  ? harmonicPitch(desired, n.start, s)
                                  : snapPitch(desired, s.root, s.scale, s.low, s.high);
                }
                // An imported/custom harmonic boundary may not coincide with the
                // repeated cell's grid. Split there before the candidate is scored.
                while (n.duration > 1e-8 && c.count < maxNotes) {
                    auto piece = n;
                    if (const auto* custom = chordAt(s.harmony, n.start)) {
                        const double boundary =
                            custom->start + custom->duration - std::fmod(n.start, s.harmony.length);
                        if (boundary > 1e-8)
                            piece.duration = std::min(piece.duration, boundary);
                        if (s.tension == 0 && piece.cents == 0)
                            piece.pitch = harmonicPitch(piece.pitch, piece.start, s);
                    }
                    c.notes[size_t(c.count++)] = piece;
                    n.start += piece.duration;
                    n.duration -= piece.duration;
                }
            }
        }
    }
    bool chordTone(int pitch, double time) const {
        if (const auto* custom = chordAt(s.harmony, time))
            return belongsToChord(pitch, *custom);
        if (s.progression == 0)
            return true;
        const auto& scale = scaleIntervals(s.scale);
        const int degree = progressionDegree(s.progression, time, bar);
        const int pc = ((pitch - s.root) % 12 + 12) % 12;
        for (int third : {0, 2, 4})
            if (pc == scale[(degree + third) % scale.size()])
                return true;
        return false;
    }
    int pitch(int previous, double time) {
        // Mostly adjacent movement, with occasional wider gestures for diversity.
        int step = integer(-2, 2);
        if (random() < s.diversity * 0.16f + s.weirdness * 0.24f)
            step = integer(-int(4 + s.dynamics * 5 + s.weirdness * 5),
                           int(4 + s.dynamics * 5 + s.weirdness * 5));
        if (random() < std::abs(s.valence - 0.5f) * 0.5f)
            step = s.valence >= 0.5f ? std::abs(step) : -std::abs(step);
        int p = std::clamp(previous + step, s.low, s.high);
        if (random() > s.tension * 0.25f)
            p = snapPitch(p, s.root, s.scale, s.low, s.high);
        const bool strongBeat = std::abs(time - std::round(time)) < 0.01;
        // Preserve strict chord-only requests, but leave weak beats available
        // for diatonic movement. Previously custom chords snapped almost every
        // proposal to a triad, including all the connecting notes.
        const double chordChance = s.tension == 0 ? 1.0 : strongBeat ? 0.85 : 0.25;
        if ((s.harmony.count || s.progression > 0) && random() < chordChance * (1.0 - s.tension))
            for (int distance = 0; distance < 12; ++distance) {
                if (p - distance >= s.low && chordTone(p - distance, time))
                    return p - distance;
                if (p + distance <= s.high && chordTone(p + distance, time))
                    return p + distance;
            }
        return p;
    }
    Clip fresh() {
        Clip c;
        c.length = length;
        c.grid = grid;
        c.numerator = s.numerator;
        c.denominator = s.denominator;
        c.seed = s.seed;
        int previous =
            snapPitch((s.low + s.high) / 2 + int((s.valence - 0.5f) * 10), s.root, s.scale, s.low, s.high);
        for (double time = 0.0; time < length - 0.001 && c.count < maxNotes;) {
            const int maxSteps = std::max(1, int(std::round(targetDuration() * 2.5 / grid)));
            int steps = integer(1, maxSteps);
            if (random() > offbeatTarget()) {
                const int options[]{1, 2, 4, 8};
                steps = options[integer(0, std::min(3, int(std::log2(maxSteps))))];
            }
            if (random() > s.dynamics)
                steps = std::max(1, int(std::round(targetDuration() / grid)));
            double duration = std::min(steps * grid, length - time);
            if (const auto* custom = chordAt(s.harmony, time)) {
                const double localTime = std::fmod(time, s.harmony.length);
                const double untilChange = custom->start + custom->duration - localTime;
                if (untilChange > 1e-8)
                    duration = std::min(duration, untilChange);
            }
            if (random() >= s.space || c.count == 0) {
                int origin = previous;
                if (s.phraseType == 2 && s.mode != 1) {
                    const double arch = std::sin(3.141592653589793 * time / length);
                    const int contour = (s.low + s.high) / 2 - 3 + int((s.valence - 0.5f) * 10) +
                                        int(arch * (4 + s.arousal * 8));
                    origin = int(std::lround(previous * 0.70 + contour * 0.30));
                }
                previous = s.mode == 1 ? snapPitch(60 + s.root, s.root, s.scale, s.low, s.high)
                                       : pitch(origin, time);
                c.notes[c.count] = {time, duration, previous, integer(77, 108), false};
                ++c.count;
            }
            time += duration;
        }
        if (s.phraseType == 2 && s.mode != 1 && c.count)
            c.notes[size_t(c.count - 1)].pitch = harmonicPitch(c.notes[size_t(c.count - 1)].pitch,
                                                               c.notes[size_t(c.count - 1)].start, s, true);
        for (int i = c.count - 2; i > 0; --i)
            if (random() < 0.40f)
                mutateMicro(c, i);
        shape(c);
        constrainMicro(c);
        return c;
    }
    void cross(Clip& child, const Clip& other) {
        const bool protectedTiming = s.lockRhythm || s.mode == 2 ||
                                     std::any_of(child.notes.begin(), child.notes.begin() + child.count,
                                                 [](const Note& n) { return n.locked; });
        if (protectedTiming) {
            // Locked events and rhythmic templates must keep exact boundaries.
            for (int i = child.count / 2; i < std::min(child.count, other.count); ++i)
                if (!child.notes[i].locked) {
                    if (s.mode != 1) {
                        child.notes[i].pitch = other.notes[i].pitch;
                        child.notes[i].cents = other.notes[i].cents;
                        if (s.harmony.count && s.tension == 0.0f && child.notes[i].cents == 0)
                            setPitch(child.notes[i],
                                     harmonicPitch(child.notes[i].pitch, child.notes[i].start, s), false);
                    }
                    child.notes[i].velocity = other.notes[i].velocity;
                }
            return;
        }
        // One-point crossover exchanges phrase sections, including the rhythm.
        // Cutting on the grid keeps both parents' musical positions meaningful.
        const double editableLength = structured() ? cellLength() : child.length;
        const int steps = std::max(1, int(std::ceil(editableLength / grid)) - 1);
        const double cut = std::min(editableLength, integer(1, steps) * grid);
        Clip mixed = child;
        mixed.count = 0;
        for (int i = 0; i < child.count && mixed.count < maxNotes; ++i)
            if (child.notes[i].start < cut) {
                auto note = child.notes[i];
                note.duration = std::min(note.duration, cut - note.start);
                mixed.notes[mixed.count++] = note;
            }
        for (int i = 0; i < other.count && mixed.count < maxNotes; ++i)
            if (other.notes[i].start >= cut)
                mixed.notes[mixed.count++] = other.notes[i];
        child = mixed;
    }
    void mutateRhythm(Clip& c, float amount) {
        if (s.lockRhythm || s.mode == 2 || c.count < 2 ||
            std::any_of(c.notes.begin(), c.notes.begin() + c.count, [](const Note& n) { return n.locked; }))
            return;
        const double end = structured() ? cellLength() : c.length;
        int editable = 0;
        while (editable < c.count && c.notes[editable].start < end - 1e-8)
            ++editable;
        if (!editable || random() >= amount * 2.0f)
            return;
        const int operation = integer(0, 4), index = integer(0, editable - 1);
        if (operation == 0 && c.count < maxNotes) {
            // Turn a rest or a continuation into a fresh onset, as -1/-2 ->
            // pitch does in the reference grid chromosome. Any grid position is reachable.
            const double start = integer(1, std::max(1, int(std::ceil(end / grid)) - 1)) * grid;
            if (start >= end - 1e-8)
                return;
            double limit = end;
            int previous = c.notes[0].pitch;
            for (int i = 0; i < c.count; ++i) {
                auto& note = c.notes[i];
                if (std::abs(note.start - start) < 1e-8)
                    return;
                if (note.start > start)
                    limit = std::min(limit, note.start);
                else {
                    previous = note.pitch;
                    if (note.start + note.duration > start)
                        note.duration = start - note.start;
                }
            }
            const int key = s.mode == 1 ? c.notes[0].pitch : pitch(previous, start);
            c.notes[c.count++] = {
                start, std::min(limit - start, grid * integer(1, 2)), key, integer(78, 110), false, 0};
        } else if (operation == 1 && editable > 2 && index > 0) {
            // Remove an onset to introduce a rest; locks/templates were excluded above.
            for (int i = index; i + 1 < c.count; ++i)
                c.notes[i] = c.notes[i + 1];
            --c.count;
        } else if (operation == 2 && index > 0) {
            auto& note = c.notes[index];
            const double earlier = c.notes[index - 1].start + c.notes[index - 1].duration;
            const double later = index + 1 < editable ? c.notes[index + 1].start : end;
            const double start = note.start + (random() < .5f ? -grid : grid);
            if (start >= earlier - 1e-8 && start + grid <= later + 1e-8) {
                note.start = start;
                note.duration = std::min(note.duration, later - start);
                if (s.harmony.count && s.tension == 0 && !note.cents)
                    note.pitch = harmonicPitch(note.pitch, start, s);
            }
        } else if (operation == 3 && index + 1 < editable && editable > 2) {
            c.notes[index].duration =
                c.notes[index + 1].start + c.notes[index + 1].duration - c.notes[index].start;
            for (int i = index + 1; i + 1 < c.count; ++i)
                c.notes[i] = c.notes[i + 1];
            --c.count;
        } else if (operation == 4 && end >= grid * 4) {
            // Copy a short fragment, including its gaps, within the editable
            // phrase/cell. This restores the reference's fragment-copy operator.
            const double span =
                grid * integer(2, std::max(2, int(std::floor(std::min(bar, end) / (2 * grid)))));
            const int windows = int(std::floor(end / span));
            if (windows < 2)
                return;
            const int sourceWindow = integer(0, windows - 1);
            int destinationWindow = integer(0, windows - 2);
            if (destinationWindow >= sourceWindow)
                ++destinationWindow;
            const double source = sourceWindow * span, destination = destinationWindow * span;
            auto original = c;
            c.count = 0;
            for (int i = 0; i < original.count && c.count < maxNotes; ++i) {
                auto note = original.notes[i];
                if (note.start >= destination && note.start < destination + span)
                    continue;
                if (note.start < destination && note.start + note.duration > destination)
                    note.duration = destination - note.start;
                c.notes[c.count++] = note;
            }
            for (int i = 0; i < original.count && c.count < maxNotes; ++i) {
                auto note = original.notes[i];
                if (note.start < source || note.start >= source + span)
                    continue;
                note.duration = std::min(note.duration, source + span - note.start);
                note.start += destination - source;
                if (s.harmony.count && s.tension == 0 && !note.cents)
                    note.pitch = harmonicPitch(note.pitch, note.start, s);
                c.notes[c.count++] = note;
            }
            if (c.count == 0)
                c = original;
        }
        sanitise(c);
    }
    void mutate(Clip& c, float amount) {
        for (int i = 0; i < c.count; ++i) {
            auto& n = c.notes[i];
            if (structured() && n.start >= cellLength() - 1e-8)
                break;
            if (n.locked || random() >= amount)
                continue;
            if (s.mode != 1)
                setPitch(n, pitch(n.pitch, n.start), true);
            n.velocity = std::clamp(n.velocity + integer(-12, 12), 40, 120);
            if (!s.lockRhythm && s.mode != 2) {
                double end = c.length;
                for (int j = i + 1; j < c.count; ++j)
                    if (c.notes[j].start > n.start) {
                        end = c.notes[j].start;
                        break;
                    }
                n.duration = std::clamp(n.duration + integer(-1, 1) * grid, std::min(grid, end - n.start),
                                        end - n.start);
            }
            if (s.microEnabled && random() < 0.60f)
                mutateMicro(c, i);
        }
        if (!s.lockRhythm && s.mode != 2 && random() < amount && c.count > 2) {
            int editableCount = c.count;
            if (structured()) {
                editableCount = 0;
                while (editableCount < c.count && c.notes[size_t(editableCount)].start < cellLength() - 1e-8)
                    ++editableCount;
            }
            const int i = integer(0, std::max(0, editableCount - 1));
            auto n = c.notes[i];
            if (!n.locked && n.duration >= 2 * grid && c.count < maxNotes) {
                c.notes[i].duration = grid;
                n.start += grid;
                n.duration -= grid;
                if (s.mode != 1)
                    setPitch(n, pitch(n.pitch, n.start), true);
                c.notes[c.count++] = n;
            }
        }
        sanitise(c);
        mutateRhythm(c, amount);
        shape(c);
        constrainMicro(c);
    }
    double score(const Clip& c) const {
        if (c.count == 0)
            return -1000.0;
        double scaleFit = 0.0, harmonic = 0.0, occupied = 0.0, consonanceCost = 0.0, microDuration = 0.0;
        int offbeats = 0;
        std::array<bool, 384> unique{};
        for (int i = 0; i < c.count; ++i) {
            const auto& n = c.notes[i];
            unique[size_t(n.pitch * 3 + (n.cents < 0 ? 0 : n.cents > 0 ? 2 : 1))] = true;
            occupied += n.duration;
            if (n.cents != 0)
                microDuration += n.duration;
            // Borrowed/custom chord tones are valid even when absent from the global scale.
            scaleFit += (snapPitch(n.pitch, s.root, s.scale) == n.pitch ||
                         (s.harmony.count && chordTone(n.pitch, n.start)))
                            ? 1.0
                            : 0.0;
            // A short resolving approach earns partial harmonic credit through
            // its stable destination. Otherwise repeated passing genes lose the
            // entire chord reward on every copy despite resolving correctly.
            if (n.cents == 0)
                harmonic += chordTone(n.pitch, n.start) ? 1.0 : 0.0;
            else if (validPassing(c, i, s))
                for (int ahead = 1; ahead <= 2 && i + ahead < c.count; ++ahead) {
                    const auto& anchor = c.notes[size_t(i + ahead)];
                    if (anchor.cents == 0) {
                        harmonic += chordTone(anchor.pitch, anchor.start) ? 0.75 : 0.0;
                        break;
                    }
                }
            const auto* chord = chordAt(s.harmony, n.start);
            const int root = chord ? chord->root : s.root;
            double relative = std::fmod(actualPitch(n) - root * 100.0 + 13200.0, 1200.0) / 100.0;
            constexpr double tonalCosts[]{0, 0.9, 0.6, 0.2, 0.15, 0.1, 1.0, 0.0, 0.3, 0.2, 0.7, 0.8, 0};
            const int semitone = int(relative);
            consonanceCost += tonalCosts[semitone] +
                              (relative - semitone) * (tonalCosts[semitone + 1] - tonalCosts[semitone]);
            offbeats += std::abs(n.start - std::round(n.start)) > 0.01 ? 1 : 0;
        }
        const double count = c.count;
        const double targetNotes = c.length * (1.0 - s.space) / targetDuration();
        const double harmonicTarget = s.tension == 0 ? 1.0
                                                     : (s.phraseType == 0   ? 0.8
                                                        : s.phraseType == 1 ? 0.75
                                                                            : 0.6) -
                                                           s.tension * 0.25;
        double result = -6.0 * std::abs(scaleFit / count - (1.0 - s.tension * 0.25));
        if (s.progression || s.harmony.count)
            result -= 3.0 * std::abs(harmonic / count - harmonicTarget);
        result -= 3.5 * std::abs(count - targetNotes) / std::max(1.0, targetNotes);
        result -= 2.0 * std::abs(1.0 - std::min(1.0, occupied / c.length) - s.space);
        result -= std::abs(offbeats / count - offbeatTarget()) * 1.5;
        const auto musical = analysePhrase(c, s);
        result -= std::abs(musical.rhythmVariation - s.dynamics * 1.1) * 0.8;
        if (s.mode != 1) {
            result -= musical.intervalPenalty * (1.8 - s.diversity * 0.4 - s.weirdness * 0.6);
            result += musical.smallStepRatio * 0.65;
            const double variety =
                std::min(1.0, std::count(unique.begin(), unique.end(), true) / double(std::min(c.count, 8)));
            result -= 1.5 * std::abs(variety - (0.35 + s.diversity * 0.6));
            int unisons = 0, strong = 0, stable = 0;
            for (int i = 0; i < c.count; ++i) {
                if (i && actualPitch(c.notes[i]) == actualPitch(c.notes[i - 1]))
                    ++unisons;
                if (std::abs(c.notes[i].start - std::round(c.notes[i].start)) < .01) {
                    ++strong;
                    stable += chordTone(c.notes[i].pitch, c.notes[i].start) && c.notes[i].cents == 0;
                }
            }
            result -= 1.8 * std::max(0.0, double(unisons) / std::max(1, c.count - 1) -
                                              (0.15 + (1.0 - s.diversity) * 0.3));
            if ((s.harmony.count || s.progression) && strong)
                result += 0.75 * stable / strong;
            const double registerSpan = std::max(1, s.high - s.low);
            const double spanTarget = std::min(registerSpan, 4.0 + s.dynamics * 6.0 + s.arousal * 8.0);
            result -= std::abs(musical.pitchSpan - spanTarget) / 12.0 * 0.8;
            const double pitchTarget =
                (s.low + s.high) * 0.5 + (s.valence - 0.5) * 10.0 + (s.arousal - 0.5) * 4.0;
            result -= std::abs(musical.meanPitch - pitchTarget) / registerSpan * 1.5;
            result -= std::abs(musical.upwardRatio - (0.3 + s.valence * 0.4)) * 0.6;
            result -= consonanceCost / count * (0.2 + s.valence * 0.8);
            if (s.phraseType == 0) {
                result += musical.repetition * 1.8 + musical.rhythmicRepetition * 0.7;
                result -= std::max(0.0, musical.loopIntervalCents - 500.0) / 1200.0;
            } else if (s.phraseType == 1) {
                result += musical.motifReturn * 1.5 + musical.rhythmicRepetition * 0.5;
                result -= std::abs(musical.middleContrast - 0.30) * 1.4;
                result -= std::abs(musical.repetition - 0.50) * 0.7;
            } else {
                result -= std::abs(musical.contourArc - 0.32) * 2.2;
                result -= std::max(0.0, musical.repetition - 0.45) * 1.5;
                const auto& final = c.notes[size_t(c.count - 1)];
                const int destination = harmonicPitch(final.pitch, final.start, s, true);
                result -= intervalCost(actualPitch(final) - destination * 100.0) * 0.7;
            }
            if (s.microEnabled) {
                // Optional but audible embellishment, not a fixed percentage
                // painted onto the winner. Both absence and excess affect fitness.
                // Short repeated approaches may be a third of the events but
                // only a small part of the sounding time. The target measures
                // duration, avoiding an accidental penalty for short motifs.
                result -= std::abs(microDuration / std::max(0.03125, occupied) - 0.14) * 3.0;
                result += musical.resolvedPassingNotes / count * 1.6;
                result -= musical.invalidPassingNotes / count * 5.0;
            }
        }
        if (parent)
            result -= std::abs(similarity(c, *parent) - (1.0 - s.mutation)) * 3.0;
        return result;
    }
};
} // namespace
double fitnessScore(const Clip& source, const Settings& settings) {
    auto clip = source;
    sanitise(clip);
    auto s = normaliseSearchSettings(settings);
    s.numerator = clip.numerator;
    s.denominator = clip.denominator;
    return Evolution(s, nullptr).score(clip);
}
std::vector<Clip> generate(Settings s, const Clip* source, const std::atomic<bool>& cancelled,
                           const std::function<void(float)>& progress,
                           const std::function<void(const SearchStats&)>& statistics) {
    s = normaliseSearchSettings(s);
    Clip parent;
    if (source) {
        parent = *source;
        sanitise(parent);
        if (parent.count == 0)
            source = nullptr;
        else {
            source = &parent;
            s.numerator = parent.numerator;
            s.denominator = parent.denominator;
        }
    }
    if (!source &&
        (!std::isfinite(s.lengthOverride) || s.lengthOverride < 0 ||
         (s.lengthOverride > 0 ? s.lengthOverride : s.bars * s.numerator * 4.0 / s.denominator) > 128.0))
        return {};
    Evolution e(s, source);
    const auto budget = searchBudget(s);
    const int size = budget.population, rounds = budget.generations;
    SearchStats stats;
    stats.population = size;
    stats.generations = rounds;
    std::vector<Clip> population(size);
    for (auto& clip : population) {
        if (cancelled.load())
            return {};
        clip = source ? *source : e.fresh();
        clip.favourite = false;
        clip.seed = s.seed;
        if (source)
            e.mutate(clip, s.mutation);
    }
    std::vector<double> scores(size);
    std::vector<int> order(size);
    for (int generation = 0; generation <= rounds; ++generation) {
        if (cancelled.load())
            return {};
        for (int i = 0; i < size; ++i) {
            if (cancelled.load())
                return {};
            scores[i] = e.score(population[i]);
            if (std::any_of(population[i].notes.begin(), population[i].notes.begin() + population[i].count,
                            [](const Note& n) { return n.cents != 0; }))
                ++stats.microtonalEvaluations;
        }
        std::iota(order.begin(), order.end(), 0);
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return scores[a] > scores[b]; });
        stats.generation = generation;
        stats.evaluations += uint64_t(size);
        stats.bestFitness = scores[order[0]];
        stats.meanFitness = std::accumulate(scores.begin(), scores.end(), 0.0) / size;
        if (generation == 0)
            stats.initialBest = stats.bestFitness;
        if (statistics)
            statistics(stats);
        if (progress)
            progress(float(generation) / rounds);
        if (generation == rounds)
            break;
        std::vector<Clip> next;
        next.reserve(size);
        next.push_back(population[order[0]]);
        next.push_back(population[order[1]]);
        while (int(next.size()) < size) {
            if (cancelled.load())
                return {};
            auto select = [&] {
                int best = e.integer(0, size - 1);
                for (int t = 0; t < 3; ++t) {
                    int n = e.integer(0, size - 1);
                    if (scores[n] > scores[best])
                        best = n;
                }
                return best;
            };
            Clip child = population[select()];
            const auto& other = population[select()];
            if (e.random() < s.crossover) {
                ++stats.crossovers;
                e.cross(child, other);
            }
            e.mutate(child, source ? s.mutation * 0.4f : 0.25f);
            next.push_back(child);
        }
        population = std::move(next);
    }
    std::vector<Clip> results;
    // Pick a useful spread of alternatives, not twelve copies of the winner.
    for (int index : order) {
        bool duplicate = false;
        for (const auto& c : results)
            if (similarity(c, population[index]) > 0.92) {
                duplicate = true;
                break;
            }
        if (!duplicate)
            results.push_back(population[index]);
        if (results.size() == slotCount)
            break;
    }
    while (results.size() < slotCount) {
        if (cancelled.load())
            return {};
        auto c = population[order[results.size() % size]];
        // Every output is an evaluated chromosome, including cents. A small
        // search may return related alternatives rather than unscored mutations.
        results.push_back(c);
    }
    return results;
}
Bank makeInitialBank() {
    Bank bank;
    const int motif[]{60, 63, 67, 70, 67, 65, 63, 62, 60, 63, 67, 72, 70, 67, 65, 63};
    for (int slot = 0; slot < slotCount; ++slot) {
        auto& c = bank[slot];
        c.seed = uint32_t(slot + 1);
        for (int i = 0; i < 16; ++i) {
            const double start = i + ((i % 4 == 3) ? 0.25 : 0.0);
            int p = motif[i] + (slot / 4) * 12;
            if (slot % 4 && i % 4 == 2)
                p = snapPitch(p + slot % 4, 0, 1);
            c.notes[c.count++] = {start, i % 4 == 3 ? 0.5 : 0.75, p, 88 + (i % 4 == 0 ? 16 : 0), false};
        }
    }
    return bank;
}
} // namespace composer
