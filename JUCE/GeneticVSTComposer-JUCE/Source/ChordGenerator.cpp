#include "ChordGenerator.h"
#include "HarmonyModelData.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <random>
#include <vector>

namespace composer {
namespace {
constexpr int intervals[17][4]{{0, 4, 7, -1}, {0, 3, 7, -1}, {0, 3, 6, -1},  {0, 4, 8, -1},
                               {0, 4, 7, 10}, {0, 4, 7, 11}, {0, 3, 7, 10}, {0, 2, 7, -1},
                               {0, 5, 7, -1}, {0, 3, 6, 10}, {0, 3, 6, 9},  {0, 4, 7, 9},
                               {0, 3, 7, 9},  {0, 4, 7, 14}, {0, 4, 10, 14}, {0, 4, 11, 14},
                               {0, 3, 7, 11}};
constexpr const char* suffixes[]{"",    "m",   "dim", "aug", "7",   "maj7", "m7",  "sus2",
                                 "sus4", "m7b5", "dim7", "6",  "m6", "add9", "9",   "maj9",
                                 "mM7"};
constexpr const char* names[]{"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
int pc(int pitch) {
    return (pitch % 12 + 12) % 12;
}
bool denominatorValid(int denominator) {
    return denominator >= 1 && denominator <= 32 && (denominator & (denominator - 1)) == 0;
}
float amount(float value) {
    return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f;
}
struct Random {
    explicit Random(uint32_t seed) : engine(seed) {}
    // Explicit conversion keeps seeded decisions consistent across standard libraries.
    float unit() { return float(engine() >> 8) / 16777216.0f; }
    int index(int count) { return int(engine() % uint32_t(count)); }
    std::mt19937 engine;
};
int harmonicScale(int scale) {
    switch (scale) {
    case 4:
    case 8:
    case 13: // Whole tone uses the major chord vocabulary.
        return 0;
    case 5:
    case 9:
    case 14: // Hexatonic blues uses the minor chord vocabulary.
        return 1;
    default:
        return std::clamp(scale, 0, 15);
    }
}
ChordEvent degreeChord(const std::vector<int>& scale, int degree, int root) {
    const int third = pc(scale[size_t((degree + 2) % 7)] - scale[size_t(degree)]);
    const int fifth = pc(scale[size_t((degree + 4) % 7)] - scale[size_t(degree)]);
    int quality = 0;
    if (third == 3)
        quality = fifth == 6 ? 2 : 1;
    else if (fifth == 8)
        quality = 3;
    return {0, 4, pc(root + scale[size_t(degree)]), quality};
}
void extendChord(ChordEvent& chord, const std::vector<int>& scale, int keyRoot) {
    const auto belongs = [&](int pitch) {
        return std::find(scale.begin(), scale.end(), pc(pitch - keyRoot)) != scale.end();
    };
    if (chord.quality == 0) {
        if (belongs(chord.root + 11))
            chord.quality = 5;
        else if (belongs(chord.root + 10))
            chord.quality = 4;
    } else if (chord.quality == 1 && belongs(chord.root + 10)) {
        chord.quality = 6;
    } else if (chord.quality == 2 && belongs(chord.root + 10)) {
        // iiø7 in minor: the diminished triad plus the minor seventh already in the scale.
        chord.quality = 9;
    }
}
#include "HarmonicSearch.h"

struct Voicing {
    std::array<int, 4> pitches{};
    int count = 0;
    double positionCost = 0;
};
std::vector<Voicing> candidates(const ChordEvent& chord, int centre, int style) {
    std::vector<Voicing> result;
    const auto& tones = intervals[std::clamp(chord.quality, 0, 16)];
    const int count = tones[3] < 0 ? 3 : 4;
    for (int octave = 1; octave <= 8; ++octave) {
        for (int inversion = 0; inversion < (style == 1 ? 1 : count); ++inversion) {
            Voicing candidate;
            candidate.count = count;
            for (int i = 0; i < count; ++i)
                candidate.pitches[size_t(i)] =
                    pc(chord.root) + octave * 12 + tones[i] + (i < inversion ? 12 : 0);
            std::sort(candidate.pitches.begin(), candidate.pitches.begin() + count);
            if (style == 2) {
                // Drop-2 spreads the middle voice below the close voicing.
                candidate.pitches[size_t(count - 2)] -= 12;
                std::sort(candidate.pitches.begin(), candidate.pitches.begin() + count);
            }
            if (candidate.pitches[0] < std::max(12, centre - 24) ||
                candidate.pitches[size_t(count - 1)] > std::min(115, centre + 24))
                continue;
            double average = 0;
            for (int i = 0; i < count; ++i)
                average += candidate.pitches[size_t(i)];
            average /= count;
            candidate.positionCost = 0.045 * (average - centre) * (average - centre);
            result.push_back(candidate);
        }
    }
    return result;
}
double movement(int from, int to) {
    const double delta = std::abs(to - from);
    return delta + 0.12 * delta * delta + 0.5 * std::max(0.0, delta - 5.0);
}
double transition(const Voicing& first, const Voicing& second) {
    if (first.count == second.count) {
        double cost = 0;
        for (int i = 0; i < first.count; ++i)
            cost += movement(first.pitches[size_t(i)], second.pitches[size_t(i)]);
        return cost / first.count;
    }
    // Match three ordered voices to three of the four; a seventh may enter or leave
    // without forcing the other voices to cross or changing the chord's pitch classes.
    const auto& fewer = first.count < second.count ? first : second;
    const auto& more = first.count < second.count ? second : first;
    double best = std::numeric_limits<double>::max();
    for (int omitted = 0; omitted < 4; ++omitted) {
        double cost = 0;
        int j = 0, nearest = 127;
        for (int i = 0; i < 4; ++i) {
            if (i == omitted)
                continue;
            cost += movement(fewer.pitches[size_t(j++)], more.pitches[size_t(i)]);
        }
        for (int i = 0; i < 3; ++i)
            nearest = std::min(nearest, std::abs(more.pitches[size_t(omitted)] - fewer.pitches[size_t(i)]));
        best = std::min(best, (cost + 0.35 * nearest) / 3.0);
    }
    return best;
}
} // namespace

Harmony generateHarmony(const ChordGenerationSettings& settings) {
    Harmony result;
    const int numerator = std::clamp(settings.numerator, 1, 32);
    const int denominator = denominatorValid(settings.denominator) ? settings.denominator : 4;
    const double bar = numerator * 4.0 / denominator;
    const double length = std::max(1, settings.bars) * bar;
    if (length > 128.0 || !std::isfinite(length))
        return result;
    result.length = length;
    const int rhythm = std::clamp(settings.harmonicRhythm, 0, 2);
    double step = std::max(0.125, bar * (rhythm == 0 ? 2.0 : rhythm == 1 ? 1.0 : 0.5));
    const int requested = int(std::ceil(length / step - 1e-9));
    if (requested > 64)
        step *= std::ceil(requested / 64.0);
    const int evenCount = std::min(64, int(std::ceil(length / step - 1e-9)));
    std::vector<double> durations;
    const int shape = std::clamp(settings.rhythmShape, 0, 2);
    if (shape == 0) {
        for (int i = 0; i < evenCount; ++i)
            durations.push_back(std::min(step, length - i * step));
    } else {
        // Group the same grid: long-short holds two steps then one, short-long the reverse.
        constexpr int pattern[2][2]{{2, 1}, {1, 2}};
        double consumed = 0;
        for (int index = 0; consumed < length - 1e-9 && durations.size() < 64; ++index) {
            double duration = step * pattern[shape - 1][index % 2];
            if (consumed + duration > length - 1e-9)
                duration = length - consumed;
            if (duration < 0.125 - 1e-9) {
                if (!durations.empty())
                    durations.back() += duration;
                break;
            }
            durations.push_back(duration);
            consumed += duration;
        }
    }
    result.count = int(durations.size());
    if (result.count == 0)
        return result;
    double position = 0;
    for (int i = 0; i < result.count; ++i) {
        result.chords[size_t(i)].start = position;
        result.chords[size_t(i)].duration = durations[size_t(i)];
        position += durations[size_t(i)];
    }
    Random random(settings.seed);
    return searchHarmony(result, settings, random);
}

HarmonyModelInfo harmonyModelInfo() {
    return {harmony_model::sourceEvents, harmony_model::sourceSequences, harmony_model::transitions,
            harmony_model::validationSequences, harmony_model::validationTransitions,
            harmony_model::firstPerplexity, harmony_model::contextPerplexity};
}

Clip voiceHarmony(const Harmony& source, int numerator, int denominator, const VoicingSettings& settings) {
    auto harmony = source;
    sanitise(harmony);
    Clip result;
    result.length = harmony.length;
    result.numerator = std::clamp(numerator, 1, 32);
    result.denominator = denominatorValid(denominator) ? denominator : 4;
    result.grid = 0.125;
    if (!harmony.count)
        return result;
    const int centre = std::clamp(settings.centre, 36, 84), style = std::clamp(settings.style, 0, 2);
    std::vector<std::vector<Voicing>> options;
    options.reserve(size_t(harmony.count));
    for (int i = 0; i < harmony.count; ++i)
        options.push_back(candidates(harmony.chords[size_t(i)], centre, style));
    // Optimise the wrap as well as internal transitions. Six central starting
    // voicings bound the work independently of the length of the progression.
    std::vector<int> anchors(options.front().size());
    std::iota(anchors.begin(), anchors.end(), 0);
    std::stable_sort(anchors.begin(), anchors.end(), [&](int a, int b) {
        return options.front()[size_t(a)].positionCost < options.front()[size_t(b)].positionCost;
    });
    anchors.resize(std::min(size_t(harmony.count == 1 ? 1 : 6), anchors.size()));
    std::vector<int> selected(size_t(harmony.count));
    double best = std::numeric_limits<double>::max();
    for (int anchor : anchors) {
        std::vector<std::vector<double>> costs;
        std::vector<std::vector<int>> previous;
        for (int i = 0; i < harmony.count; ++i) {
            const auto& current = options[size_t(i)];
            costs.emplace_back(current.size(), std::numeric_limits<double>::max());
            previous.emplace_back(current.size(), -1);
            for (size_t j = 0; j < current.size(); ++j) {
                if (i == 0) {
                    if (int(j) == anchor)
                        costs[0][j] = current[j].positionCost;
                    continue;
                }
                for (size_t k = 0; k < options[size_t(i - 1)].size(); ++k) {
                    if (costs[size_t(i - 1)][k] == std::numeric_limits<double>::max())
                        continue;
                    const double cost = costs[size_t(i - 1)][k] +
                                        transition(options[size_t(i - 1)][k], current[j]) + current[j].positionCost;
                    if (cost < costs[size_t(i)][j]) {
                        costs[size_t(i)][j] = cost;
                        previous[size_t(i)][j] = int(k);
                    }
                }
            }
        }
        const auto& last = costs.back();
        for (size_t end = 0; end < last.size(); ++end) {
            const double total = last[end] + 0.8 * transition(options.back()[end], options.front()[size_t(anchor)]);
            if (total >= best)
                continue;
            best = total;
            int index = int(end);
            for (int i = harmony.count - 1; i >= 0; --i) {
                selected[size_t(i)] = index;
                index = previous[size_t(i)][size_t(index)];
            }
        }
    }
    for (int i = 0; i < harmony.count; ++i) {
        const auto& chord = harmony.chords[size_t(i)];
        auto voicing = options[size_t(i)][size_t(selected[size_t(i)])];
        if (chord.bass >= 0) {
            const int bassPc = pc(chord.bass);
            int lowest = 0, bassVoice = -1;
            for (int voice = 0; voice < voicing.count; ++voice) {
                if (voicing.pitches[size_t(voice)] < voicing.pitches[size_t(lowest)])
                    lowest = voice;
                if (pc(voicing.pitches[size_t(voice)]) == bassPc)
                    bassVoice = voice;
            }
            if (bassVoice < 0) {
                int pitch = voicing.pitches[size_t(lowest)];
                while (pitch > 0 && pc(pitch) != bassPc)
                    --pitch;
                voicing.pitches[size_t(lowest)] = pitch;
            } else {
                while (voicing.pitches[size_t(bassVoice)] > voicing.pitches[size_t(lowest)] &&
                       voicing.pitches[size_t(bassVoice)] >= 12)
                    voicing.pitches[size_t(bassVoice)] -= 12;
            }
            std::sort(voicing.pitches.begin(), voicing.pitches.begin() + voicing.count);
        }
        for (int voice = 0; voice < voicing.count; ++voice)
            result.notes[size_t(result.count++)] = {chord.start,
                                                    chord.duration,
                                                    voicing.pitches[size_t(voice)],
                                                    voice + 1 == voicing.count ? 88
                                                    : voice == 0               ? 82
                                                                               : 76,
                                                    false,
                                                    0};
    }
    return result;
}

std::string chordName(const ChordEvent& chord) {
    std::string name = std::string(names[pc(chord.root)]) + suffixes[std::clamp(chord.quality, 0, 16)];
    if (chord.bass >= 0 && pc(chord.bass) != pc(chord.root))
        name += "/" + std::string(names[pc(chord.bass)]);
    return name;
}
std::string voicedChordName(const ChordEvent& chord, const Clip& clip) {
    if (chord.bass >= 0)
        return chordName(chord);
    int bass = 128;
    for (int i = 0; i < std::clamp(clip.count, 0, maxNotes); ++i)
        if (std::abs(clip.notes[size_t(i)].start - chord.start) < 1e-8)
            bass = std::min(bass, clip.notes[size_t(i)].pitch);
    const auto name = chordName(chord);
    return bass < 128 && pc(bass) != pc(chord.root) ? name + "/" + names[pc(bass)] : name;
}
} // namespace composer
