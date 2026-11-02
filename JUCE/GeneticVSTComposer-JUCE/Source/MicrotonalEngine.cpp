#include "MicrotonalEngine.h"
#include "ComposerEngine.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>
#include <sstream>
#include <vector>

namespace microtonal {
namespace {
bool validMeter(int numerator, int denominator, const std::array<int, 16>& groups, int count, int unit) {
    if (numerator < 1 || numerator > 32 || denominator < 1 || denominator > 32 ||
        (denominator & (denominator - 1)) != 0 || count < 0 || count > 16 || unit < 1 || unit > 32 ||
        (unit & (unit - 1)) != 0)
        return false;
    if (count == 0)
        return true;
    int sum = 0;
    for (int i = 0; i < count; ++i) {
        if (groups[size_t(i)] < 1 || groups[size_t(i)] > 32)
            return false;
        sum += groups[size_t(i)];
    }
    return numerator * unit == sum * denominator;
}
composer::Settings rhythmSettings(const Settings& s) {
    composer::Settings r;
    r.numerator = s.numerator;
    r.denominator = s.denominator;
    r.groups = s.groups;
    r.groupCount = s.groupCount;
    r.groupUnit = s.groupUnit;
    r.bars = s.bars > 0 ? s.bars : 1;
    r.lengthOverride = phraseLength(s);
    r.division = s.division;
    r.phraseType = s.phraseType;
    r.mode = 1;
    r.lockRhythm = s.lockRhythm || s.mode == 2;
    r.density = float(s.density);
    r.diversity = float(s.diversity);
    r.dynamics = float(s.dynamics);
    r.syncopation = float(s.syncopation);
    r.space = float(s.space);
    r.arousal = float(s.arousal);
    r.weirdness = float(s.weirdness);
    r.mutation = float(s.mutation);
    r.crossover = float(s.crossover);
    r.seed = s.seed;
    r.root = 0;
    r.scale = 8;
    r.progression = 0;
    r.microEnabled = false;
    // Use the actual conventional rhythmic search as the template planner. Pitch evolution below
    // has its own objective and budget, with rhythm-planner evaluations reported separately.
    r.population = s.population;
    r.generations = s.generations;
    return r;
}
void copyMeter(Clip& c, const Settings& s) {
    c.numerator = s.numerator;
    c.denominator = s.denominator;
    c.grid = 4.0 / s.division;
    c.groups = s.groups;
    c.groupCount = s.groupCount;
    c.groupUnit = s.groupUnit;
}
} // namespace
double phraseLength(const Settings& s) {
    return s.bars > 0 ? s.bars * s.numerator * 4.0 / std::max(1, s.denominator) : s.beats;
}
int metricWeight(double beat, const Settings& s) {
    return composer::metricWeight(beat, rhythmSettings(s));
}
bool setGrouping(Settings& s, const std::string& text, std::string& error) {
    auto next = s;
    if (text.empty()) {
        next.groupCount = 0;
        next.groupUnit = s.denominator;
    } else {
        if (!composer::parseGrouping(text, next.groups, next.groupCount, error))
            return false;
        next.groupUnit = s.denominator;
    }
    if (!validMeter(next.numerator, next.denominator, next.groups, next.groupCount, next.groupUnit)) {
        error = "Grouping must fill exactly one bar.";
        return false;
    }
    s = next;
    error.clear();
    return true;
}
bool valid(const Tuning& t) {
    if (t.divisions < 2 || t.divisions > maxDegrees)
        return false;
    if (!t.rational)
        return true;
    double previous = 0;
    for (int i = 0; i < t.divisions; ++i) {
        const auto r = t.ratios[size_t(i)];
        if (r.numerator < 1 || r.denominator < 1 || r.numerator > 4096 || r.denominator > 4096)
            return false;
        const double value = double(r.numerator) / r.denominator;
        if (value < 1 || value >= 2 || value <= previous || (i == 0 && value != 1))
            return false;
        previous = value;
    }
    return true;
}
bool valid(const Settings& s) {
    auto unit = [](double v) { return std::isfinite(v) && v >= 0 && v <= 1; };
    return valid(s.tuning) && std::isfinite(s.baseHz) && s.baseHz >= 20 && s.baseHz <= 1000 &&
           std::isfinite(s.beats) && s.beats >= 0.125 && s.beats <= 128 && s.bars >= 0 && s.bars <= 16 &&
           phraseLength(s) >= 0.125 && phraseLength(s) <= 128 &&
           validMeter(s.numerator, s.denominator, s.groups, s.groupCount, s.groupUnit) &&
           (s.division == 8 || s.division == 16 || s.division == 32) && s.phraseType >= 0 &&
           s.phraseType <= 2 && s.mode >= 0 && s.mode <= 2 && std::isfinite(s.tempo) && s.tempo >= 20 &&
           s.tempo <= 400 && s.octaves >= 1 && s.octaves <= 3 && s.population >= 8 && s.population <= 512 &&
           s.generations >= 1 && s.generations <= 2000 && unit(s.consonance) && unit(s.diversity) &&
           unit(s.motion) && unit(s.density) && unit(s.mutation) && unit(s.dynamics) && unit(s.syncopation) &&
           unit(s.space) && unit(s.arousal) && unit(s.weirdness) && unit(s.crossover) &&
           std::isfinite(s.swing) && s.swing >= 0 && s.swing <= 0.75 && std::isfinite(s.gate) &&
           s.gate >= 0.1 && s.gate <= 1 && unit(s.humanize);
}
bool valid(const ChordSettings& s) {
    auto unit = [](double v) { return std::isfinite(v) && v >= 0 && v <= 1; };
    return s.bars >= 0 && s.bars <= 16 && s.rhythm >= 0 && s.rhythm <= 2 && s.rhythmShape >= 0 &&
           s.rhythmShape <= 2 && s.voices >= 2 && s.voices <= 6 && s.voicing >= 0 && s.voicing <= 2 &&
           unit(s.originality) && unit(s.colour) && unit(s.brightness);
}
bool valid(const Clip& c) {
    if (!valid(c.tuning) || !std::isfinite(c.baseHz) || c.baseHz < 20 || c.baseHz > 1000 ||
        !std::isfinite(c.length) || c.length < 0.125 || c.length > 128 || c.count < 0 ||
        c.count > maxEvents || !std::isfinite(c.grid) || c.grid < 0.001 || c.grid > 4 ||
        !validMeter(c.numerator, c.denominator, c.groups, c.groupCount, c.groupUnit))
        return false;
    double end = 0;
    for (int i = 0; i < c.count; ++i) {
        const auto& n = c.notes[size_t(i)];
        if (!std::isfinite(n.start) || !std::isfinite(n.duration) || n.start < 0 ||
            (i > 0 && n.start < c.notes[size_t(i - 1)].start) || (!c.polyphonic && n.start < end - 1e-9) ||
            n.duration < 0.001 || n.start + n.duration > c.length + 1e-9 || n.degree < 0 ||
            n.degree > c.tuning.divisions * 3 || n.velocity < 1 || n.velocity > 127)
            return false;
        if (c.polyphonic) {
            int active = 1;
            for (int j = 0; j < i; ++j)
                active += c.notes[size_t(j)].start + c.notes[size_t(j)].duration > n.start + 1e-9;
            if (active > 15)
                return false;
        }
        end = n.start + n.duration;
    }
    return true;
}
bool sameTuning(const Tuning& a, const Tuning& b) {
    if (a.rational != b.rational || a.divisions != b.divisions)
        return false;
    if (a.rational)
        for (int i = 0; i < a.divisions; ++i)
            if (a.ratios[size_t(i)].numerator != b.ratios[size_t(i)].numerator ||
                a.ratios[size_t(i)].denominator != b.ratios[size_t(i)].denominator)
                return false;
    return true;
}
bool parseRatios(const std::string& text, Tuning& result, std::string& error) {
    Tuning t;
    t.rational = true;
    t.divisions = 0;
    std::istringstream input(text);
    std::string token;
    while (input >> token) {
        std::istringstream pair(token);
        Ratio r;
        char slash = 0;
        if (!(pair >> r.numerator >> slash >> r.denominator) || slash != '/' ||
            pair.peek() != std::char_traits<char>::eof() || r.numerator < 1 || r.denominator < 1 ||
            r.numerator > 4096 || r.denominator > 4096 || t.divisions >= maxDegrees) {
            error = "Use fractions such as 1/1 9/8 5/4 4/3 3/2 5/3 15/8.";
            return false;
        }
        const int divisor = std::gcd(r.numerator, r.denominator);
        r.numerator /= divisor;
        r.denominator /= divisor;
        t.ratios[size_t(t.divisions++)] = r;
    }
    if (!valid(t)) {
        error = "Enter 2-72 increasing ratios, starting at 1/1 and below 2/1 (the octave repeats).";
        return false;
    }
    result = t;
    error.clear();
    return true;
}
std::string ratioText(const Tuning& t) {
    std::ostringstream out;
    for (int i = 0; i < t.divisions; ++i) {
        if (i)
            out << ' ';
        out << t.ratios[size_t(i)].numerator << '/' << t.ratios[size_t(i)].denominator;
    }
    return out.str();
}
double cents(const Tuning& t, int degree) {
    if (!valid(t))
        return 0;
    if (!t.rational)
        return degree * 1200.0 / t.divisions;
    const int octave = int(std::floor(double(degree) / t.divisions));
    const int index = degree - octave * t.divisions;
    const auto r = t.ratios[size_t(index)];
    return octave * 1200.0 + 1200.0 * std::log2(double(r.numerator) / r.denominator);
}
double frequency(const Clip& c, int degree) {
    return c.baseHz * std::exp2(cents(c.tuning, degree) / 1200.0);
}
RatioMatch approximateRatio(double interval) {
    RatioMatch best;
    if (!std::isfinite(interval))
        return best;
    const double folded = std::fmod(std::abs(interval), 1200.0);
    best.affinity = -1;
    // Tenney-height-like complexity log2(p*q), with a 35-cent Gaussian tolerance.
    // Include 2/1 so near-octave intervals are treated continuously at the boundary.
    for (int q = 1; q <= 16; ++q)
        for (int p = q; p <= q * 2; ++p) {
            if (std::gcd(p, q) != 1)
                continue;
            const double error = std::remainder(folded - 1200.0 * std::log2(double(p) / q), 1200.0);
            const double simplicity = 1.0 / (1.0 + std::log2(double(p) * q));
            const double affinity = simplicity * std::exp(-0.5 * std::pow(error / 35.0, 2));
            if (affinity > best.affinity)
                best = {{p, q}, error, simplicity, affinity};
        }
    return best;
}
namespace {
struct Objective {
    const Settings& s;
    const Clip* harmony = nullptr;
    composer::Settings rhythm;
    std::array<double, maxDegrees * 3 + 1> affinity{};
    std::array<double, maxDegrees * maxDegrees> rationalIntervals{};
    explicit Objective(const Settings& settings, const Clip* chords = nullptr)
        : s(settings), harmony(chords), rhythm(rhythmSettings(settings)) {
        for (int d = 0; d <= s.tuning.divisions * 3; ++d)
            affinity[size_t(d)] = approximateRatio(cents(s.tuning, d)).affinity;
        if (s.tuning.rational)
            for (int a = 0; a < s.tuning.divisions; ++a)
                for (int b = 0; b < s.tuning.divisions; ++b)
                    rationalIntervals[size_t(a * maxDegrees + b)] =
                        approximateRatio(std::fmod(cents(s.tuning, b) - cents(s.tuning, a) + 1200.0, 1200.0))
                            .affinity;
    }
    double intervalAffinity(const Clip& c, int a, int b) const {
        return c.tuning.rational
                   ? rationalIntervals[size_t((std::min(a, b) % c.tuning.divisions) * maxDegrees +
                                              std::max(a, b) % c.tuning.divisions)]
                   : affinity[size_t(std::abs(b - a))];
    }
    Score score(const Clip& c) const {
        Score out;
        if (c.count == 0)
            return out;
        std::array<bool, maxDegrees> used{};
        int unique = 0, repeats = 0;
        double motion = 0, harmony = 0;
        for (int i = 0; i < c.count; ++i) {
            const int d = c.notes[size_t(i)].degree;
            const int next = c.notes[size_t((i + 1) % c.count)].degree;
            const double distance = std::abs(cents(c.tuning, next) - cents(c.tuning, d));
            harmony += 0.75 * intervalAffinity(c, d, next) + 0.25 * affinity[size_t(d)];
            if (this->harmony && this->harmony->count > 0) {
                const auto& h = *this->harmony;
                const double time = std::fmod(c.notes[size_t(i)].start, h.length);
                auto upper = std::upper_bound(h.notes.begin(), h.notes.begin() + h.count, time + 1e-8,
                                              [](double t, const Event& n) { return t < n.start; });
                if (upper != h.notes.begin()) {
                    const double start = (upper - 1)->start;
                    double sum = 0;
                    int tones = 0;
                    for (auto n = upper; n != h.notes.begin();) {
                        --n;
                        if (std::abs(n->start - start) > 1e-8)
                            break;
                        if (time < n->start + n->duration) {
                            sum += intervalAffinity(c, d, n->degree);
                            ++tones;
                        }
                    }
                    if (tones)
                        harmony += sum / tones;
                }
            }
            motion += std::min(2.0, distance / 1200.0);
            repeats += d == next;
            if (!used[size_t(d % c.tuning.divisions)]) {
                used[size_t(d % c.tuning.divisions)] = true;
                ++unique;
            }
        }
        const double targetVariety = 2 + s.diversity * (std::min(c.tuning.divisions, c.count) - 2);
        out.harmonicity = harmony / c.count;
        out.variety = std::exp(-std::abs(unique - targetVariety) / std::max(1.0, targetVariety));
        out.movement = std::exp(-4.0 * std::abs(motion / c.count - (0.04 + 0.65 * s.motion)));
        const int first = c.notes[0].degree, last = c.notes[size_t(c.count - 1)].degree;
        out.closure =
            0.5 * intervalAffinity(c, first, last) + 0.5 * (last % c.tuning.divisions == 0 ? 1.0 : 0.0);
        out.total = 60 * s.consonance * out.harmonicity + 30 * out.variety + 25 * out.movement +
                    10 * out.closure - 22.0 * repeats / c.count;
        double accents = 0;
        for (int i = 0; i < c.count; ++i) {
            const int weight = composer::metricWeight(c.notes[size_t(i)].start, rhythm);
            accents += weight > 0 ? 1.0 - s.syncopation * 0.7 : s.syncopation;
        }
        out.total += 8.0 * accents / c.count;
        return out;
    }
};
} // namespace
Score evaluate(const Clip& c, const Settings& s, const Clip* harmony) {
    if (!valid(c) || !valid(s) || !sameTuning(c.tuning, s.tuning))
        return {};
    if (harmony &&
        (!valid(*harmony) || !sameTuning(c.tuning, harmony->tuning) || c.baseHz != harmony->baseHz))
        return {};
    auto effective = s;
    effective.numerator = c.numerator;
    effective.denominator = c.denominator;
    effective.groups = c.groups;
    effective.groupCount = c.groupCount;
    effective.groupUnit = c.groupUnit;
    return Objective(effective, harmony).score(c);
}
Clip generate(const Settings& s, const Clip* parent, const std::atomic<bool>& cancelled,
              const std::function<void(const Stats&)>& progress, std::vector<Clip>* alternatives,
              const Clip* harmony) {
    if (!valid(s) || cancelled.load())
        return {};
    if (alternatives)
        alternatives->clear();
    if (harmony && (!valid(*harmony) || !harmony->polyphonic || !sameTuning(s.tuning, harmony->tuning) ||
                    s.baseHz != harmony->baseHz))
        return {};
    const bool useParent = parent && valid(*parent) && !parent->polyphonic && parent->count > 0 &&
                           sameTuning(parent->tuning, s.tuning) && parent->baseHz == s.baseHz;
    if (s.mode == 2 && !useParent)
        return {};
    Settings effective = s;
    if (useParent) {
        effective.bars = 0;
        effective.beats = parent->length;
        effective.numerator = parent->numerator;
        effective.denominator = parent->denominator;
        effective.division = int(std::lround(4.0 / parent->grid));
        effective.groups = parent->groups;
        effective.groupCount = parent->groupCount;
        effective.groupUnit = parent->groupUnit;
    }
    Objective objective(effective, harmony);
    std::mt19937 random(s.seed);
    auto integer = [&](int high) { return int(random() % uint32_t(high)); };
    auto unit = [&] { return double(random()) / 4294967296.0; };
    const int top = s.tuning.divisions * s.octaves;
    auto architecture = [&](Clip& c) {
        if (s.phraseType >= 2 || s.lockRhythm || s.mode == 2)
            return;
        const double bar = c.numerator * 4.0 / c.denominator;
        const double cell = std::min(c.length, bar * (s.phraseType == 1 ? 2 : 1));
        for (int i = 0; i < c.count; ++i) {
            auto& n = c.notes[size_t(i)];
            if (n.locked || n.start < cell)
                continue;
            if (s.phraseType == 1 && n.start >= cell && n.start < c.length - cell)
                continue;
            const double local = std::fmod(n.start, cell);
            for (int j = 0; j < i && c.notes[size_t(j)].start < cell; ++j)
                if (std::abs(c.notes[size_t(j)].start - local) < 1e-7) {
                    n.degree = c.notes[size_t(j)].degree;
                    break;
                }
        }
    };
    auto mutate = [&](Clip& c, double amount) {
        for (int i = 0; i < c.count; ++i)
            if (!c.notes[size_t(i)].locked && unit() < amount) {
                auto& n = c.notes[size_t(i)];
                const int radius = std::max(1, int(s.tuning.divisions * (0.1 + s.motion * 0.65)));
                n.degree = s.mode == 1     ? 0
                           : unit() < 0.12 ? integer(top + 1)
                                           : std::clamp(n.degree + integer(radius * 2 + 1) - radius, 0, top);
                n.velocity = 78 + integer(35);
            }
        architecture(c);
    };
    composer::Clip source;
    if (useParent) {
        source.length = parent->length;
        source.grid = parent->grid;
        source.numerator = parent->numerator;
        source.denominator = parent->denominator;
        source.groups = parent->groups;
        source.groupCount = parent->groupCount;
        source.groupUnit = parent->groupUnit;
        source.count = parent->count;
        for (int i = 0; i < parent->count; ++i) {
            const auto& n = parent->notes[size_t(i)];
            source.notes[size_t(i)] = {n.start, n.duration, 60, n.velocity, n.locked, 0};
        }
    }
    uint64_t rhythmEvaluations = 0;
    int rhythmGeneration = 0, rhythmGenerations = 0;
    auto rhythmic = composer::generate(rhythmSettings(effective), useParent ? &source : nullptr, cancelled,
                                       {}, [&](const composer::SearchStats& stat) {
                                           rhythmEvaluations = stat.evaluations;
                                           rhythmGeneration = stat.generation;
                                           rhythmGenerations = stat.generations;
                                           if (progress) {
                                               Stats running;
                                               running.rhythmEvaluations = rhythmEvaluations;
                                               running.rhythmGeneration = rhythmGeneration;
                                               running.rhythmGenerations = rhythmGenerations;
                                               progress(running);
                                           }
                                       });
    if (cancelled.load() || rhythmic.empty())
        return {};
    auto fresh = [&](int index) {
        Clip c;
        c.tuning = s.tuning;
        c.baseHz = s.baseHz;
        c.length = phraseLength(effective);
        c.seed = s.seed;
        copyMeter(c, effective);
        const auto& r = rhythmic[size_t(index) % rhythmic.size()];
        c.count = r.count;
        for (int i = 0; i < r.count; ++i) {
            const auto& n = r.notes[size_t(i)];
            c.notes[size_t(i)] = {n.start, n.duration, s.mode == 1 ? 0 : integer(top + 1), n.velocity,
                                  n.locked};
            if (useParent)
                for (int j = 0; j < parent->count; ++j)
                    if (std::abs(parent->notes[size_t(j)].start - n.start) < 1e-8 &&
                        (n.locked || s.lockRhythm || s.mode == 2)) {
                        c.notes[size_t(i)] = parent->notes[size_t(j)];
                        break;
                    }
        }
        architecture(c);
        return c;
    };
    struct Individual {
        Clip clip;
        Score score;
    };
    std::vector<Individual> population(size_t(s.population)), next(size_t(s.population));
    Stats stats;
    stats.rhythmEvaluations = rhythmEvaluations;
    stats.rhythmGeneration = rhythmGeneration;
    stats.rhythmGenerations = rhythmGenerations;
    auto assess = [&](Individual& item) {
        if (s.mode == 1)
            for (int i = 0; i < item.clip.count; ++i)
                if (!item.clip.notes[size_t(i)].locked)
                    item.clip.notes[size_t(i)].degree = 0;
        item.score = objective.score(item.clip);
        ++stats.evaluations;
    };
    for (int i = 0; i < s.population; ++i) {
        if (cancelled.load())
            return {};
        auto& item = population[size_t(i)];
        item.clip = useParent && (i == 0 || s.lockRhythm || s.mode == 2) ? *parent : fresh(i);
        if (useParent && i > 0)
            mutate(item.clip, s.mutation);
        assess(item);
    }
    auto sort = [&] {
        std::stable_sort(population.begin(), population.end(), [](const Individual& a, const Individual& b) {
            return a.score.total > b.score.total;
        });
    };
    sort();
    stats.initialBest = population[0].score.total;
    stats.best = population[0].score;
    if (progress)
        progress(stats);
    for (int g = 0; g < s.generations; ++g) {
        next[0] = population[0];
        auto tournament = [&]() -> const Clip& {
            int best = integer(s.population);
            for (int j = 0; j < 2; ++j) {
                const int candidate = integer(s.population);
                if (population[size_t(candidate)].score.total > population[size_t(best)].score.total)
                    best = candidate;
            }
            return population[size_t(best)].clip;
        };
        for (int i = 1; i < s.population; ++i) {
            if (cancelled.load())
                return {};
            auto& child = next[size_t(i)];
            child.clip = tournament();
            const auto& mate = tournament();
            const int split = integer(std::max(1, child.clip.count));
            if (unit() < s.crossover && mate.count > 0)
                for (int j = split; j < child.clip.count; ++j)
                    if (!child.clip.notes[size_t(j)].locked && s.mode != 1)
                        child.clip.notes[size_t(j)].degree = mate.notes[size_t(j % mate.count)].degree;
            mutate(child.clip, s.mutation);
            assess(child);
        }
        population.swap(next);
        sort();
        stats.generation = g + 1;
        stats.best = population[0].score;
        if (progress)
            progress(stats);
    }
    if (cancelled.load())
        return {};
    auto result = population[0].clip;
    result.seed = s.seed;
    if (alternatives)
        for (int i = 0; i < std::min(slotCount, s.population); ++i) {
            auto c = population[size_t(i)].clip;
            c.seed = s.seed;
            alternatives->push_back(std::move(c));
        }
    return result;
}

namespace {
struct ChordObjective {
    const Settings& s;
    const ChordSettings& cs;
    Objective intervals;
    ChordObjective(const Settings& settings, const ChordSettings& chords)
        : s(settings), cs(chords), intervals(settings) {}
    Score score(const Clip& c) const {
        Score result;
        if (c.count == 0)
            return result;
        int groups = 0, pairs = 0, changes = 0, previous = -1, previousCount = 0;
        double affinity = 0, movement = 0, registerCost = 0, closePairs = 0, spanCost = 0;
        std::array<bool, maxDegrees> classes{};
        for (int begin = 0; begin < c.count;) {
            int end = begin + 1;
            while (end < c.count && c.notes[size_t(end)].start == c.notes[size_t(begin)].start)
                ++end;
            const int tones = end - begin;
            double mean = 0;
            for (int i = begin; i < end; ++i) {
                const int d = c.notes[size_t(i)].degree;
                classes[size_t(d % c.tuning.divisions)] = true;
                mean += cents(c.tuning, d);
                for (int j = i + 1; j < end; ++j) {
                    affinity += intervals.intervalAffinity(c, d, c.notes[size_t(j)].degree);
                    const double distance = cents(c.tuning, c.notes[size_t(j)].degree) - cents(c.tuning, d);
                    closePairs += std::max(0.0, (90.0 - distance) / 90.0);
                    ++pairs;
                }
                if (previous >= 0) {
                    const int old = c.notes[size_t(previous + std::min(i - begin, previousCount - 1))].degree;
                    movement += std::abs(cents(c.tuning, d) - cents(c.tuning, old)) / 1200.0;
                }
            }
            registerCost += std::abs(mean / tones / 1200.0 - s.octaves * (0.25 + 0.5 * cs.brightness));
            const double span = (cents(c.tuning, c.notes[size_t(end - 1)].degree) -
                                 cents(c.tuning, c.notes[size_t(begin)].degree)) /
                                1200.0;
            const double spanTarget = cs.voicing == 0   ? 0.45 + 0.4 * cs.colour
                                      : cs.voicing == 1 ? 1.0 + 0.8 * cs.colour
                                                        : 0.65 + 0.6 * cs.colour;
            spanCost += std::abs(span - spanTarget);
            if (previous >= 0) {
                bool changed = previousCount != tones;
                for (int i = 0; i < std::min(previousCount, tones); ++i)
                    changed |= c.notes[size_t(previous + i)].degree != c.notes[size_t(begin + i)].degree;
                changes += changed;
            }
            previous = begin;
            previousCount = tones;
            ++groups;
            begin = end;
        }
        const int unique = int(std::count(classes.begin(), classes.end(), true));
        result.harmonicity = affinity / std::max(1, pairs);
        result.variety =
            std::exp(-std::abs(double(changes) / std::max(1, groups - 1) - (0.2 + 0.8 * cs.originality))) *
            std::min(1.0, double(unique) / std::min(c.tuning.divisions, 2 + cs.voices));
        const double movementTarget = 0.02 + (cs.voicing == 2 ? 0.15 : 0.6) * cs.originality;
        result.movement =
            std::exp(-3 * std::abs(movement / std::max(1, c.count - previousCount) - movementTarget));
        const int last = c.notes[size_t(previous)].degree;
        result.closure = cs.resolve ? (last % c.tuning.divisions == 0 ? 1.0 : 0.0)
                                    : intervals.intervalAffinity(c, c.notes[0].degree, last);
        result.total = (50 + 50 * s.consonance) * result.harmonicity + 30 * result.variety +
                       24 * result.movement + 15 * result.closure - 15 * registerCost / groups -
                       (20 - 12 * cs.colour) * closePairs / std::max(1, pairs) - 12 * spanCost / groups;
        return result;
    }
};
} // namespace
Score evaluateChords(const Clip& c, const Settings& s, const ChordSettings& cs) {
    if (!valid(c) || !c.polyphonic || !valid(s) || !valid(cs) || !sameTuning(c.tuning, s.tuning))
        return {};
    return ChordObjective(s, cs).score(c);
}
Clip generateChords(const Settings& s, const ChordSettings& cs, const std::atomic<bool>& cancelled,
                    const std::function<void(const Stats&)>& progress) {
    if (!valid(s) || !valid(cs) || cancelled.load())
        return {};
    Clip base;
    base.tuning = s.tuning;
    base.baseHz = s.baseHz;
    base.seed = s.seed;
    base.polyphonic = true;
    base.length = cs.bars > 0 ? cs.bars * s.numerator * 4.0 / s.denominator : phraseLength(s);
    if (base.length < 0.125 || base.length > 128)
        return {};
    copyMeter(base, s);
    const int top = s.tuning.divisions * s.octaves;
    const int voices = std::min(cs.voices, top + 1);
    const double bar = s.numerator * 4.0 / s.denominator;
    std::vector<std::pair<double, double>> cells;
    std::array<int, 16> groups = s.groups;
    int groupCount = s.groupCount, unit = s.groupUnit;
    if (groupCount == 0)
        composer::defaultGrouping(s.numerator, s.denominator, groups, groupCount, unit);
    for (double start = 0; start < base.length - 1e-8;) {
        double duration = cs.rhythm == 0 ? bar
                          : cs.rhythm == 1
                              ? bar * 0.5
                              : groups[cells.size() % size_t(std::max(1, groupCount))] * 4.0 / unit;
        if (cs.rhythmShape != 0 && cs.rhythm != 2)
            duration *= ((cells.size() % 2 == 0) == (cs.rhythmShape == 1)) ? 1.5 : 0.5;
        duration = std::min(duration, base.length - start);
        if (cells.size() >= 64 || (cells.size() + 1) * size_t(voices) > maxEvents)
            return {};
        cells.push_back({start, duration});
        start += duration;
    }
    for (const auto& cell : cells)
        for (int v = 0; v < voices; ++v)
            base.notes[size_t(base.count++)] = {cell.first, cell.second, v, 92, false};
    std::mt19937 random(s.seed);
    auto integer = [&](int count) { return int(random() % uint32_t(std::max(1, count))); };
    auto unitRandom = [&] { return double(random()) / 4294967296.0; };
    auto repairChord = [&](Clip& c, int begin) {
        std::array<bool, maxDegrees * 3 + 1> used{};
        std::array<bool, maxDegrees> classes{};
        for (int v = 0; v < voices; ++v) {
            auto& n = c.notes[size_t(begin + v)];
            n.degree = std::clamp(n.degree, 0, top);
            while (used[size_t(n.degree)] ||
                   (voices <= s.tuning.divisions && classes[size_t(n.degree % s.tuning.divisions)]))
                n.degree = (n.degree + 1) % (top + 1);
            used[size_t(n.degree)] = true;
            classes[size_t(n.degree % s.tuning.divisions)] = true;
        }
        if (cs.resolve && begin + voices == c.count && !used[0]) {
            int target = begin;
            for (int v = 0; v < voices; ++v)
                if (c.notes[size_t(begin + v)].degree % s.tuning.divisions == 0)
                    target = begin + v;
            c.notes[size_t(target)].degree = 0;
        }
        std::sort(c.notes.begin() + begin, c.notes.begin() + begin + voices,
                  [](const Event& a, const Event& b) { return a.degree < b.degree; });
    };
    ChordObjective objective(s, cs);
    struct Individual {
        Clip clip;
        Score score;
    };
    std::vector<Individual> population(size_t(s.population)), next(size_t(s.population));
    Stats stats;
    auto assess = [&](Individual& c) {
        c.score = objective.score(c.clip);
        ++stats.evaluations;
    };
    for (auto& individual : population) {
        if (cancelled.load())
            return {};
        individual.clip = base;
        for (int begin = 0; begin < base.count; begin += voices) {
            const int root = integer(std::max(1, top - voices + 2));
            for (int v = 0; v < voices; ++v) {
                const int span =
                    cs.voicing == 0 ? std::min(top - root + 1, s.tuning.divisions + 1) : top - root + 1;
                individual.clip.notes[size_t(begin + v)].degree = root + integer(span);
            }
            repairChord(individual.clip, begin);
        }
        assess(individual);
    }
    auto sort = [&] {
        std::stable_sort(population.begin(), population.end(), [](const Individual& a, const Individual& b) {
            return a.score.total > b.score.total;
        });
    };
    sort();
    stats.initialBest = population[0].score.total;
    stats.best = population[0].score;
    if (progress)
        progress(stats);
    for (int generation = 0; generation < s.generations; ++generation) {
        next[0] = population[0];
        auto parent = [&]() -> const Clip& {
            int best = integer(s.population);
            for (int k = 0; k < 2; ++k) {
                const int candidate = integer(s.population);
                if (population[size_t(candidate)].score.total > population[size_t(best)].score.total)
                    best = candidate;
            }
            return population[size_t(best)].clip;
        };
        for (int i = 1; i < s.population; ++i) {
            if (cancelled.load())
                return {};
            auto& child = next[size_t(i)];
            child.clip = parent();
            const auto& mate = parent();
            const int split = integer(int(cells.size())) * voices;
            for (int n = 0; n < child.clip.count; ++n) {
                if (n >= split && unitRandom() < s.crossover)
                    child.clip.notes[size_t(n)].degree = mate.notes[size_t(n)].degree;
                if (unitRandom() < s.mutation) {
                    const int radius = std::max(1, int(s.tuning.divisions * (0.1 + cs.originality * 0.7)));
                    child.clip.notes[size_t(n)].degree += integer(radius * 2 + 1) - radius;
                }
            }
            for (int begin = 0; begin < child.clip.count; begin += voices)
                repairChord(child.clip, begin);
            assess(child);
        }
        population.swap(next);
        sort();
        stats.generation = generation + 1;
        stats.best = population[0].score;
        if (progress)
            progress(stats);
    }
    return cancelled.load() ? Clip{} : population[0].clip;
}
Clip revoiceChords(const Clip& original, const Settings& s, const ChordSettings& cs) {
    if (!valid(original) || !original.polyphonic || !valid(s) || !valid(cs))
        return {};
    Clip result = original;
    const int n = result.tuning.divisions, top = n * s.octaves;
    std::array<double, 6> previous{};
    bool hasPrevious = false;
    for (int begin = 0; begin < result.count;) {
        int end = begin + 1;
        while (end < result.count && result.notes[size_t(end)].start == result.notes[size_t(begin)].start)
            ++end;
        std::array<bool, maxDegrees * 3 + 1> used{};
        for (int v = 0; v < end - begin; ++v) {
            const int pitchClass = original.notes[size_t(begin + v)].degree % n;
            int best = original.notes[size_t(begin + v)].degree;
            double cost = 1e9;
            const double centre = cs.voicing == 1 ? top * (v % 2 == 0 ? 0.3 : 0.8)
                                  : cs.voicing == 2 && hasPrevious && v < 6
                                      ? previous[size_t(v)]
                                      : top * (0.25 + 0.5 * cs.brightness);
            for (int degree = pitchClass; degree <= top; degree += n)
                if (!used[size_t(degree)] && std::abs(degree - centre) < cost) {
                    best = degree;
                    cost = std::abs(degree - centre);
                }
            if (cost == 1e9)
                return {};
            result.notes[size_t(begin + v)].degree = best;
            used[size_t(best)] = true;
        }
        std::sort(result.notes.begin() + begin, result.notes.begin() + end,
                  [](const Event& a, const Event& b) { return a.degree < b.degree; });
        for (int v = 0; v < std::min(6, end - begin); ++v)
            previous[size_t(v)] = result.notes[size_t(begin + v)].degree;
        hasPrevious = true;
        begin = end;
    }
    return valid(result) ? result : Clip{};
}
Clip combine(const Clip& melody, const Clip& chords) {
    if (!valid(melody) || !valid(chords) || !sameTuning(melody.tuning, chords.tuning) ||
        melody.baseHz != chords.baseHz)
        return {};
    Clip result = melody;
    result.count = 0;
    result.polyphonic = true;
    result.length = std::max(melody.length, chords.length);
    for (const auto* source : {&melody, &chords})
        for (double offset = 0; offset < result.length - 1e-8; offset += source->length)
            for (int i = 0; i < source->count; ++i) {
                auto n = source->notes[size_t(i)];
                n.start += offset;
                if (n.start >= result.length)
                    continue;
                if (result.count >= maxEvents)
                    return {};
                n.duration = std::min(n.duration, result.length - n.start);
                result.notes[size_t(result.count++)] = n;
            }
    std::stable_sort(result.notes.begin(), result.notes.begin() + result.count,
                     [](const Event& a, const Event& b) {
                         return a.start < b.start || (a.start == b.start && a.degree < b.degree);
                     });
    // Unison melody/chord voices may intentionally overlap; the playback voices own independent channels.
    return result;
}
Event performedEvent(const Clip& clip, int index, const Settings& s) {
    auto n = clip.notes[size_t(index)];
    if (clip.polyphonic)
        return n;
    auto swing = [&](double beat) {
        const double pair = std::floor(beat / (2 * clip.grid)) * (2 * clip.grid), offset = beat - pair;
        return pair + (offset <= clip.grid
                           ? offset * (1 + s.swing)
                           : clip.grid * (1 + s.swing) + (offset - clip.grid) * (1 - s.swing));
    };
    auto onset = [&](int i) {
        const double original = swing(clip.notes[size_t(i)].start);
        if (s.humanize <= 0)
            return std::min(clip.length - 0.001, original);
        uint32_t hash = (clip.seed ^ uint32_t(i) * 3266489917u) * 1664525u + 1013904223u;
        const double shift = (double(hash >> 8) / 16777216.0 * 2 - 1) * clip.grid * s.humanize * 0.2;
        const double low = i > 0 ? (swing(clip.notes[size_t(i - 1)].start) + original) * 0.5 + 1e-7 : 0;
        const double high = i + 1 < clip.count
                                ? (original + swing(clip.notes[size_t(i + 1)].start)) * 0.5 - 1e-7
                                : clip.length - 0.001;
        return std::clamp(original + shift, low, std::max(low, high));
    };
    n.start = onset(index);
    if (s.humanize > 0) {
        const uint32_t hash = (clip.seed ^ uint32_t(index) * 3266489917u) * 1664525u + 1013904223u;
        n.velocity = std::clamp(n.velocity + int((int(hash % 25) - 12) * s.humanize), 1, 127);
    }
    const double end = index + 1 < clip.count ? onset(index + 1) : clip.length;
    n.duration = std::max(0.001, std::min(n.duration * s.gate, end - n.start));
    return n;
}
} // namespace microtonal
