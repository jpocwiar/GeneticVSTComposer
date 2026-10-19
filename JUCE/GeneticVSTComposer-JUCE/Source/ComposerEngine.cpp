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
const std::vector<int>& scaleIntervals(int scale) {
    static const std::array<std::vector<int>, 9> scales{{{0, 2, 4, 5, 7, 9, 11},
                                                         {0, 2, 3, 5, 7, 8, 10},
                                                         {0, 2, 3, 5, 7, 9, 10},
                                                         {0, 2, 4, 5, 7, 9, 10},
                                                         {0, 2, 4, 7, 9},
                                                         {0, 3, 5, 7, 10},
                                                         {0, 2, 3, 5, 7, 8, 11},
                                                         {0, 2, 4, 6, 7, 9, 11},
                                                         {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}}};
    return scales[std::clamp(scale, 0, 8)];
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
void sanitise(Clip& clip) {
    clip.length = std::isfinite(clip.length) ? std::clamp(clip.length, 0.125, 128.0) : 16.0;
    clip.grid = std::isfinite(clip.grid) ? std::clamp(clip.grid, 0.03125, 1.0) : 0.25;
    clip.grid = std::min(clip.grid, clip.length);
    clip.numerator = std::clamp(clip.numerator, 1, 16);
    if (clip.denominator != 2 && clip.denominator != 4 && clip.denominator != 8 && clip.denominator != 16)
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
int harmonicPitch(int desired, double time, const Settings& s, bool rootOnly = false) {
    const int low = std::clamp(s.low, 0, 127), high = std::clamp(s.high, low, 127);
    const auto* chord = chordAt(s.harmony, time);
    if (!chord && s.progression == 0 && !rootOnly)
        return snapPitch(desired, s.root, s.scale, low, high);
    static constexpr int degrees[4][4]{{0, 3, 4, 0}, {0, 5, 3, 4}, {0, 3, 5, 4}, {1, 4, 0, 0}};
    const auto& scale = scaleIntervals(s.scale);
    const double bar = std::clamp(s.numerator, 1, 16) * 4.0 / std::clamp(s.denominator, 1, 16);
    const int degree =
        s.progression > 0 ? degrees[std::clamp(s.progression - 1, 0, 3)][int(time / bar) % 4] : 0;
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
            note.pitch = harmonicPitch(note.pitch + step, note.start + s.timeOffset, s);
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
                added.pitch = harmonicPitch(note.pitch + step, added.start + s.timeOffset, s);
                added.velocity = std::clamp(note.velocity - 5, 1, 127);
                note.duration = split;
                clip.notes[clip.count++] = added;
            }
        } else if (action == RegionAction::Answer) {
            // Invert the selected contour around its first pitch, then give the response
            // a settled final note. The rhythm remains recognisable.
            note.pitch = harmonicPitch(anchor - (note.pitch - anchor), note.start + s.timeOffset, s);
            if (position + 1 == editable.size())
                note.pitch = harmonicPitch(note.pitch, note.start + s.timeOffset, s, true);
        } else if (action == RegionAction::Resolve) {
            // Cadential motion is limited to the final two editable events.
            if (position + 1 == editable.size())
                note.pitch = harmonicPitch(note.pitch, note.start + s.timeOffset, s, true);
            else if (position + 2 == editable.size())
                note.pitch = harmonicPitch(note.pitch, note.start + s.timeOffset, s);
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
        if (pitches.count)
            note.pitch = pitches.notes[nearest(pitches, phase)].pitch;
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
        if (std::abs(a.notes[i].start - b.notes[i].start) < 0.01 && a.notes[i].pitch == b.notes[i].pitch)
            ++matches;
    return double(matches) / std::max(a.count, b.count);
}
namespace {
struct Evolution {
    Settings s;
    std::mt19937 rng;
    const Clip* parent;
    double bar, grid, length;
    explicit Evolution(Settings settings, const Clip* p)
        : s(settings), rng(s.seed), parent(p), bar(s.numerator * 4.0 / s.denominator), grid(4.0 / s.division),
          length(s.bars * bar) {
        if (parent) {
            length = parent->length;
            grid = parent->grid;
            bar = parent->numerator * 4.0 / parent->denominator;
        }
    }
    float random() { return std::generate_canonical<float, 24>(rng); }
    int integer(int lo, int hi) { return std::uniform_int_distribution<int>(lo, std::max(lo, hi))(rng); }
    bool chordTone(int pitch, double time) const {
        if (const auto* custom = chordAt(s.harmony, time))
            return belongsToChord(pitch, *custom);
        if (s.progression == 0)
            return true;
        static constexpr int degrees[4][4]{{0, 3, 4, 0}, {0, 5, 3, 4}, {0, 3, 5, 4}, {1, 4, 0, 0}};
        const auto& scale = scaleIntervals(s.scale);
        const int degree = degrees[std::clamp(s.progression - 1, 0, 3)][int(time / bar) % 4];
        const int pc = ((pitch - s.root) % 12 + 12) % 12;
        for (int third : {0, 2, 4})
            if (pc == scale[(degree + third) % scale.size()])
                return true;
        return false;
    }
    int pitch(int previous, double time) {
        int p = std::clamp(previous + integer(-int(3 + s.diversity * 9), int(3 + s.diversity * 9)), s.low,
                           s.high);
        if (random() > s.tension * 0.25f)
            p = snapPitch(p, s.root, s.scale, s.low, s.high);
        if ((s.harmony.count || (s.progression > 0 && std::fmod(time, 1.0) < 0.01)) && random() > s.tension)
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
        int previous = snapPitch((s.low + s.high) / 2, s.root, s.scale, s.low, s.high);
        for (double time = 0.0; time < length - 0.001 && c.count < maxNotes;) {
            const int maxSteps = std::max(1, int(std::round((0.3 + (1.0 - s.density) * 1.8) / grid)));
            int steps = integer(1, maxSteps);
            if (random() > s.syncopation) {
                const int options[]{1, 2, 4, 8};
                steps = options[integer(0, std::min(3, int(std::log2(maxSteps))))];
            }
            double duration = std::min(steps * grid, length - time);
            if (const auto* custom = chordAt(s.harmony, time)) {
                const double localTime = std::fmod(time, s.harmony.length);
                const double untilChange = custom->start + custom->duration - localTime;
                if (untilChange > 1e-8)
                    duration = std::min(duration, untilChange);
            }
            if (random() >= s.space || c.count == 0) {
                previous = s.mode == 1 ? snapPitch(60 + s.root, s.root, s.scale, s.low, s.high)
                                       : pitch(previous, time);
                c.notes[c.count++] = {time, duration, previous, integer(77, 108), false};
            }
            time += duration;
        }
        return c;
    }
    void mutate(Clip& c, float amount) {
        for (int i = 0; i < c.count; ++i) {
            auto& n = c.notes[i];
            if (n.locked || random() >= amount)
                continue;
            if (s.mode != 1)
                n.pitch = pitch(n.pitch, n.start);
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
        }
        if (!s.lockRhythm && s.mode != 2 && random() < amount && c.count > 2) {
            const int i = integer(0, c.count - 1);
            auto n = c.notes[i];
            if (!n.locked && n.duration >= 2 * grid && c.count < maxNotes) {
                c.notes[i].duration = grid;
                n.start += grid;
                n.duration -= grid;
                n.pitch = s.mode == 1 ? n.pitch : pitch(n.pitch, n.start);
                c.notes[c.count++] = n;
            }
        }
        sanitise(c);
    }
    double score(const Clip& c) const {
        if (c.count == 0)
            return -1000.0;
        double scaleFit = 0.0, harmonic = 0.0, movement = 0.0, repeated = 0.0, occupied = 0.0;
        int offbeats = 0, leaps = 0;
        std::array<bool, 128> unique{};
        for (int i = 0; i < c.count; ++i) {
            const auto& n = c.notes[i];
            unique[n.pitch] = true;
            occupied += n.duration;
            // Borrowed/custom chord tones are valid even when absent from the global scale.
            scaleFit += (snapPitch(n.pitch, s.root, s.scale) == n.pitch ||
                         (s.harmony.count && chordTone(n.pitch, n.start)))
                            ? 1.0
                            : 0.0;
            harmonic += chordTone(n.pitch, n.start) ? 1.0 : 0.0;
            offbeats += std::abs(n.start - std::round(n.start)) > 0.01 ? 1 : 0;
            if (i > 0) {
                const int delta = std::abs(n.pitch - c.notes[i - 1].pitch);
                movement += delta;
                leaps += delta > 12;
            }
            // Reward recognisable rhythmic/pitch motifs across bars.
            for (int j = 0; j < i; ++j)
                if (std::abs(n.start - c.notes[j].start - bar) < 0.01 && n.pitch == c.notes[j].pitch) {
                    repeated += 1.0;
                    break;
                }
        }
        const double count = c.count;
        const double targetNotes = c.length / (0.3 + (1.0 - s.density) * 1.1) * (1.0 - s.space * 0.7);
        double result = -6.0 * std::abs(scaleFit / count - (1.0 - s.tension * 0.25)) +
                        ((s.progression || s.harmony.count) ? 3.0 * harmonic / count : 0.0);
        result -= 2.0 * std::abs(count - targetNotes) / std::max(1.0, targetNotes);
        result -= 2.0 * std::abs(1.0 - std::min(1.0, occupied / c.length) - s.space);
        result -= std::abs(offbeats / count - s.syncopation) * 1.5;
        if (s.mode != 1) {
            result -= std::abs(movement / count - (2.0 + s.diversity * 5.0)) * 0.25;
            result -= leaps / count * 2.0;
            result += std::min(1.0, std::count(unique.begin(), unique.end(), true) / 8.0) * s.diversity;
            result += repeated / count * (1.0 - s.diversity) * 2.0;
        }
        if (parent)
            result -= std::abs(similarity(c, *parent) - (1.0 - s.mutation)) * 3.0;
        return result;
    }
};
} // namespace
std::vector<Clip> generate(Settings s, const Clip* source, const std::atomic<bool>& cancelled,
                           const std::function<void(float)>& progress) {
    s.bars = std::clamp(s.bars, 1, 8);
    s.low = std::clamp(s.low, 0, 126);
    s.high = std::clamp(s.high, s.low + 1, 127);
    s.numerator = std::clamp(s.numerator, 1, 12);
    if (s.denominator != 4 && s.denominator != 8)
        s.denominator = 4;
    if (s.division != 8 && s.division != 16 && s.division != 32)
        s.division = 16;
    s.density = unitAmount(s.density);
    s.diversity = unitAmount(s.diversity);
    s.syncopation = unitAmount(s.syncopation);
    s.tension = unitAmount(s.tension);
    s.space = unitAmount(s.space);
    s.mutation = unitAmount(s.mutation);
    sanitise(s.harmony);
    Clip parent;
    if (source) {
        parent = *source;
        sanitise(parent);
        if (parent.count == 0)
            source = nullptr;
        else
            source = &parent;
    }
    Evolution e(s, source);
    const int sizes[]{40, 72, 112}, generations[]{20, 40, 72};
    const int quality = std::clamp(s.quality, 0, 2), size = sizes[quality], rounds = generations[quality];
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
        for (int i = 0; i < size; ++i)
            scores[i] = e.score(population[i]);
        std::iota(order.begin(), order.end(), 0);
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return scores[a] > scores[b]; });
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
            // Pitch crossover keeps note boundaries and all locked events intact.
            if (s.mode != 1)
                for (int i = child.count / 2; i < std::min(child.count, other.count); ++i)
                    if (!child.notes[i].locked) {
                        child.notes[i].pitch = other.notes[i].pitch;
                        if (s.harmony.count && s.tension == 0.0f)
                            child.notes[i].pitch =
                                harmonicPitch(child.notes[i].pitch, child.notes[i].start, s);
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
        e.mutate(c, source ? s.mutation : 0.6f);
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
