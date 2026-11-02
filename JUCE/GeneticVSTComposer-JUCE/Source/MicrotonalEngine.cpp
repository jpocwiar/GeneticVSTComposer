#include "MicrotonalEngine.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>
#include <sstream>
#include <vector>

namespace microtonal {
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
           std::isfinite(s.beats) && s.beats >= 1 && s.beats <= 64 && std::isfinite(s.tempo) &&
           s.tempo >= 20 && s.tempo <= 400 && s.octaves >= 1 && s.octaves <= 3 && s.population >= 8 &&
           s.population <= 256 && s.generations >= 1 && s.generations <= 1000 && unit(s.consonance) &&
           unit(s.diversity) && unit(s.motion) && unit(s.density) && unit(s.mutation);
}
bool valid(const Clip& c) {
    if (!valid(c.tuning) || !std::isfinite(c.baseHz) || c.baseHz < 20 || c.baseHz > 1000 ||
        !std::isfinite(c.length) || c.length < 1 || c.length > 64 || c.count < 0 || c.count > maxEvents)
        return false;
    double end = 0;
    for (int i = 0; i < c.count; ++i) {
        const auto& n = c.notes[size_t(i)];
        if (!std::isfinite(n.start) || !std::isfinite(n.duration) || n.start < end - 1e-9 ||
            n.duration < 0.001 || n.start + n.duration > c.length + 1e-9 || n.degree < 0 ||
            n.degree > c.tuning.divisions * 3 || n.velocity < 1 || n.velocity > 127)
            return false;
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
    std::array<double, maxDegrees * 3 + 1> affinity{};
    std::array<double, maxDegrees * maxDegrees> rationalIntervals{};
    explicit Objective(const Settings& settings) : s(settings) {
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
        return out;
    }
};
} // namespace
Score evaluate(const Clip& c, const Settings& s) {
    if (!valid(c) || !valid(s) || !sameTuning(c.tuning, s.tuning))
        return {};
    return Objective(s).score(c);
}
Clip generate(const Settings& s, const Clip* parent, const std::atomic<bool>& cancelled,
              const std::function<void(const Stats&)>& progress) {
    if (!valid(s) || cancelled.load())
        return {};
    Objective objective(s);
    std::mt19937 random(s.seed);
    auto integer = [&](int high) { return int(random() % uint32_t(high)); };
    auto unit = [&] { return double(random()) / 4294967296.0; };
    const int top = s.tuning.divisions * s.octaves;
    auto mutate = [&](Clip& c, double amount) {
        for (int i = 0; i < c.count; ++i)
            if (unit() < amount) {
                auto& n = c.notes[size_t(i)];
                const int radius = std::max(1, int(s.tuning.divisions * (0.1 + s.motion * 0.65)));
                n.degree = unit() < 0.12 ? integer(top + 1)
                                         : std::clamp(n.degree + integer(radius * 2 + 1) - radius, 0, top);
                n.velocity = 78 + integer(35);
            }
    };
    const bool useParent = parent && valid(*parent) && parent->count > 0 &&
                           sameTuning(parent->tuning, s.tuning) && parent->baseHz == s.baseHz &&
                           parent->length == s.beats &&
                           std::all_of(parent->notes.begin(), parent->notes.begin() + parent->count,
                                       [=](const Event& n) { return n.degree <= top; });
    auto fresh = [&] {
        Clip c;
        c.tuning = s.tuning;
        c.baseHz = s.baseHz;
        c.length = s.beats;
        c.seed = s.seed;
        const int cells = int(s.beats * 2);
        for (int i = 0; i < cells && c.count < maxEvents; ++i)
            if (i == 0 || unit() < 0.15 + 0.85 * s.density)
                c.notes[size_t(c.count++)] = {i * 0.5, 0.42, integer(top + 1), 78 + integer(35)};
        return c;
    };
    struct Individual {
        Clip clip;
        Score score;
    };
    std::vector<Individual> population(size_t(s.population)), next(size_t(s.population));
    Stats stats;
    auto assess = [&](Individual& item) {
        item.score = objective.score(item.clip);
        ++stats.evaluations;
    };
    for (int i = 0; i < s.population; ++i) {
        if (cancelled.load())
            return {};
        auto& item = population[size_t(i)];
        item.clip = useParent ? *parent : fresh();
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
            const int split = integer(child.clip.count);
            for (int j = split; j < child.clip.count; ++j)
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
    return result;
}
} // namespace microtonal
