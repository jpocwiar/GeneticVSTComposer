#include "ComposerEngine.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace composer {
namespace {
int pc(int pitch) { return (pitch % 12 + 12) % 12; }
double actual(const Note& n) { return n.pitch + n.cents * 0.01; }
bool contains(const ChordEvent& chord, int pitch) {
    const auto tones = chordPitchClasses(chord);
    return std::find(tones.begin(), tones.end(), pc(pitch)) != tones.end();
}
const ChordEvent& chordAt(const Harmony& harmony, double beat) {
    const double phase = std::fmod(std::max(0.0, beat), harmony.length);
    for (int i = harmony.count - 1; i > 0; --i)
        if (harmony.chords[size_t(i)].start <= phase + 1e-8)
            return harmony.chords[size_t(i)];
    return harmony.chords[0];
}
bool overlaps(const Note& a, const Note& b) {
    return a.start < b.start + b.duration - 1e-8 && b.start < a.start + a.duration - 1e-8;
}
struct Choice {
    int pitch, cents;
    double cost;
    double actual() const { return pitch + cents * 0.01; }
};
double contourCost(const Note& a, const Note& b, const Choice& first, const Choice& second) {
    if (a.pitch != b.pitch && first.pitch == second.pitch && overlaps(a, b))
        return std::numeric_limits<double>::infinity();
    const double before = actual(b) - actual(a), after = second.actual() - first.actual();
    const double error = std::abs(after - before);
    double cost = error * 0.45 + error * error * 0.06;
    if (std::abs(before) > 0.25 && before * after < 0)
        cost += 2.0;
    if (b.start - (a.start + a.duration) > 1.0)
        cost *= 0.2; // A rest separates melodic gestures.
    return cost;
}
// Integrate the actual chord timeline over a held note. A full-cycle contribution
// keeps this bounded even when a long note covers hundreds of short harmony loops.
std::array<double, 12> harmonicCoverage(const Note& note, const Harmony& harmony, double offset) {
    std::array<double, 12> result{};
    auto integrate = [&](double begin, double end, double multiplier) {
        for (int i = 0; i < harmony.count; ++i) {
            const double start = i == 0 ? 0.0 : harmony.chords[size_t(i)].start;
            const double finish = i + 1 < harmony.count ? harmony.chords[size_t(i + 1)].start : harmony.length;
            const double span = std::max(0.0, std::min(end, finish) - std::max(begin, start)) * multiplier;
            for (int tone : chordPitchClasses(harmony.chords[size_t(i)]))
                if (tone >= 0)
                    result[size_t(tone)] += span;
        }
    };
    const double phase = std::fmod(std::max(0.0, note.start + offset), harmony.length);
    const double first = std::min(note.duration, harmony.length - phase);
    integrate(phase, phase + first, 1);
    const double remaining = std::max(0.0, note.duration - first);
    const double cycles = std::floor(remaining / harmony.length);
    if (cycles > 0)
        integrate(0, harmony.length, cycles);
    integrate(0, remaining - cycles * harmony.length, 1);
    return result;
}
} // namespace

Clip adaptToHarmony(const Clip& source, double start, double end, const Settings& settings) {
    auto result = source;
    auto s = settings;
    sanitise(s.harmony);
    if (source.count <= 0 || source.count > maxNotes || s.harmony.count == 0 ||
        !std::isfinite(source.length) || !std::isfinite(start) || !std::isfinite(end) ||
        start < 0 || end <= start || end > source.length + 1e-8)
        return result;
    for (int i = 0; i < source.count; ++i) {
        const auto& note = source.notes[size_t(i)];
        if (!std::isfinite(note.start) || !std::isfinite(note.duration) || note.start < 0 ||
            note.duration <= 0 || note.pitch < 0 || note.pitch > 127 ||
            note.start >= source.length || note.duration > source.length - note.start + 1e-8 ||
            note.cents < -50 || note.cents > 50 ||
            (i > 0 && source.notes[size_t(i - 1)].start > note.start))
            return result;
    }
    s.timeOffset = std::isfinite(s.timeOffset) ? std::max(0.0, s.timeOffset) : 0.0;
    s.numerator = source.numerator;
    s.denominator = source.denominator;
    s.groups = source.groups;
    s.groupCount = source.groupCount;
    s.groupUnit = source.groupUnit;
    std::array<bool, maxNotes> editable{}, ornament{};
    bool any = false;
    for (int i = 0; i < source.count; ++i) {
        const auto& note = source.notes[size_t(i)];
        editable[size_t(i)] = !note.locked && note.start >= start && note.start < end;
        any |= editable[size_t(i)];
        if (!editable[size_t(i)] || i == 0 || i + 1 == source.count)
            continue;
        const auto& before = source.notes[size_t(i - 1)];
        const auto& after = source.notes[size_t(i + 1)];
        const bool shortWeak = note.duration <= 0.5 + 1e-8 && metricWeight(note.start, s) < 2;
        const bool steps = std::abs(actual(note) - actual(before)) <= 2.5 &&
                           std::abs(actual(after) - actual(note)) <= 2.5;
        const bool separate = before.start < note.start && note.start + note.duration <= after.start + 1e-8 &&
                              after.start - (note.start + note.duration) <= 0.5 + 1e-8;
        ornament[size_t(i)] = separate && ((shortWeak && steps) ||
            (s.microEnabled && isValidMicrotonalPassing(source, i, s)));
    }
    if (!any)
        return result;
    auto conflictsWithProtected = [&](int index, int pitch) {
        const auto& note = source.notes[size_t(index)];
        for (int j = 0; j < source.count; ++j)
            if (j != index && !editable[size_t(j)] && pitch == source.notes[size_t(j)].pitch &&
                pitch != note.pitch && overlaps(note, source.notes[size_t(j)]))
                return true;
        return false;
    };
    const auto& scale = scaleIntervals(s.scale);
    std::vector<std::vector<Choice>> choices(size_t(source.count));
    for (int i = 0; i < source.count; ++i) {
        const auto& note = source.notes[size_t(i)];
        auto& options = choices[size_t(i)];
        if (!editable[size_t(i)]) {
            options.push_back({note.pitch, note.cents, 0});
            continue;
        }
        const auto& chord = chordAt(s.harmony, note.start + s.timeOffset);
        const auto coverage = harmonicCoverage(note, s.harmony, s.timeOffset);
        auto probe = source;
        for (int pitch = std::max(0, note.pitch - 12); pitch <= std::min(127, note.pitch + 12); ++pitch) {
            const bool chordTone = contains(chord, pitch);
            const bool scaleTone = std::find(scale.begin(), scale.end(), pc(pitch - s.root)) != scale.end();
            if (conflictsWithProtected(i, pitch))
                continue;
            auto add = [&](int cents) {
                const double shift = pitch + cents * 0.01 - actual(note);
                double cost = std::abs(shift) * 0.7 + shift * shift * 0.12;
                if (pitch != note.pitch || cents != note.cents)
                    cost += 0.6;
                if (!ornament[size_t(i)])
                    cost += (1.0 - std::min(1.0, coverage[size_t(pc(pitch))] / note.duration)) *
                            18.0 * std::min(1.0, note.duration);
                else if (!chordTone)
                    cost += 0.7;
                options.push_back({pitch, cents, cost});
            };
            if (chordTone || (ornament[size_t(i)] && scaleTone))
                add(0);
            if (ornament[size_t(i)] && s.microEnabled && note.cents) {
                probe.notes[size_t(i)].pitch = pitch;
                if (isValidMicrotonalPassing(probe, i, s))
                    add(note.cents);
            }
        }
        // A dense locked voicing can occupy every nearby compatible key. Keep
        // that event intact rather than changing timing or any protected voice.
        if (options.empty())
            options.push_back({note.pitch, note.cents, 0});
    }
    std::vector<std::vector<double>> costs(size_t(source.count));
    std::vector<std::vector<int>> previous(size_t(source.count));
    for (int i = 0; i < source.count; ++i) {
        costs[size_t(i)].resize(choices[size_t(i)].size());
        previous[size_t(i)].resize(choices[size_t(i)].size());
    }
    std::vector<int> selected(size_t(source.count));
    double best = std::numeric_limits<double>::infinity();
    const bool loop = start == 0 && end >= source.length && source.count > 1;
    for (size_t anchor = 0; anchor < choices.front().size(); ++anchor) {
        for (auto& row : costs)
            std::fill(row.begin(), row.end(), std::numeric_limits<double>::infinity());
        costs[0][anchor] = choices[0][anchor].cost;
        for (int i = 1; i < source.count; ++i)
            for (size_t next = 0; next < choices[size_t(i)].size(); ++next)
                for (size_t prior = 0; prior < choices[size_t(i - 1)].size(); ++prior) {
                    const double cost = costs[size_t(i - 1)][prior] + choices[size_t(i)][next].cost +
                        contourCost(source.notes[size_t(i - 1)], source.notes[size_t(i)],
                                    choices[size_t(i - 1)][prior], choices[size_t(i)][next]);
                    if (cost < costs[size_t(i)][next]) {
                        costs[size_t(i)][next] = cost;
                        previous[size_t(i)][next] = int(prior);
                    }
                }
        for (size_t last = 0; last < choices.back().size(); ++last) {
            const double total = costs.back()[last] + (loop ? contourCost(source.notes[size_t(source.count - 1)],
                source.notes[0], choices.back()[last], choices.front()[anchor]) * 0.4 : 0);
            if (total >= best)
                continue;
            best = total;
            int index = int(last);
            for (int i = source.count - 1; i >= 0; --i) {
                selected[size_t(i)] = index;
                if (i > 0)
                    index = previous[size_t(i)][size_t(index)];
            }
        }
    }
    if (!std::isfinite(best))
        return result;
    for (int i = 0; i < source.count; ++i) {
        const auto& choice = choices[size_t(i)][size_t(selected[size_t(i)])];
        result.notes[size_t(i)].pitch = choice.pitch;
        result.notes[size_t(i)].cents = choice.cents;
    }
    auto collision = [&](int index, int pitch) {
        for (int j = 0; j < result.count; ++j)
            if (j != index && pitch == result.notes[size_t(j)].pitch &&
                source.notes[size_t(index)].pitch != source.notes[size_t(j)].pitch &&
                overlaps(result.notes[size_t(index)], result.notes[size_t(j)]))
                return true;
        return false;
    };
    // Imported melodies can contain several voices. The contour DP compares
    // neighbours; repair any newly introduced collision with a longer-held voice.
    for (int i = 0; i < result.count; ++i) {
        auto& note = result.notes[size_t(i)];
        if (!editable[size_t(i)] || !collision(i, note.pitch))
            continue;
        double bestCost = std::numeric_limits<double>::infinity();
        for (const auto& choice : choices[size_t(i)])
            if (!choice.cents && contains(chordAt(s.harmony, note.start + s.timeOffset), choice.pitch) &&
                !collision(i, choice.pitch) && choice.cost < bestCost) {
                bestCost = choice.cost;
                note.pitch = choice.pitch;
                note.cents = 0;
            }
    }
    // Preserve ornaments only when they still connect the adapted neighbours.
    for (int i = result.count - 1; i >= 0; --i) {
        auto& note = result.notes[size_t(i)];
        if (!editable[size_t(i)])
            continue;
        bool keep = true;
        if (note.cents)
            keep = s.microEnabled && isValidMicrotonalPassing(result, i, s);
        else if (!contains(chordAt(s.harmony, note.start + s.timeOffset), note.pitch)) {
            keep = ornament[size_t(i)] && i > 0 && i + 1 < result.count &&
                   std::abs(actual(note) - actual(result.notes[size_t(i - 1)])) <= 2.5 &&
                   std::abs(actual(result.notes[size_t(i + 1)]) - actual(note)) <= 2.5 &&
                   contains(chordAt(s.harmony, result.notes[size_t(i + 1)].start + s.timeOffset),
                            result.notes[size_t(i + 1)].pitch);
        }
        if (keep)
            continue;
        double nearest = std::numeric_limits<double>::infinity();
        for (const auto& choice : choices[size_t(i)]) {
            if (choice.cents || !contains(chordAt(s.harmony, note.start + s.timeOffset), choice.pitch) ||
                collision(i, choice.pitch))
                continue;
            double cost = choice.cost;
            if (i > 0)
                cost += contourCost(source.notes[size_t(i - 1)], source.notes[size_t(i)],
                                    {result.notes[size_t(i - 1)].pitch, result.notes[size_t(i - 1)].cents, 0}, choice);
            if (i + 1 < result.count)
                cost += contourCost(source.notes[size_t(i)], source.notes[size_t(i + 1)], choice,
                                    {result.notes[size_t(i + 1)].pitch, result.notes[size_t(i + 1)].cents, 0});
            if (cost < nearest) {
                nearest = cost;
                note.pitch = choice.pitch;
                note.cents = 0;
            }
        }
    }
    return result;
}
} // namespace composer
