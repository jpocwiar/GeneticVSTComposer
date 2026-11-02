#include "MicrotonalEngine.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool b, const char* message) {
    if (!b)
        throw std::runtime_error(message);
}
bool same(const microtonal::Clip& a, const microtonal::Clip& b) {
    if (a.count != b.count || a.baseHz != b.baseHz || a.length != b.length ||
        !microtonal::sameTuning(a.tuning, b.tuning))
        return false;
    for (int i = 0; i < a.count; ++i) {
        const auto& x = a.notes[size_t(i)];
        const auto& y = b.notes[size_t(i)];
        if (x.degree != y.degree || x.start != y.start || x.duration != y.duration ||
            x.velocity != y.velocity)
            return false;
    }
    return true;
}
} // namespace
int main() {
    using namespace microtonal;
    try {
        std::atomic<bool> cancelled{false};
        Settings s;
        s.population = 16;
        s.generations = 12;
        s.beats = 8;
        for (int edo : {2, 7, 8, 19, 31, 72}) {
            s.tuning.divisions = edo;
            check(std::abs(cents(s.tuning, 1) - 1200.0 / edo) < 1e-10, "EDO step rounded to semitones");
            check(std::abs(cents(s.tuning, edo) - 1200) < 1e-10, "EDO octave");
            Stats final;
            const auto a = generate(s, nullptr, cancelled, [&](const Stats& stat) { final = stat; });
            const auto b = generate(s, nullptr, cancelled);
            check(valid(a) && a.count > 0 && same(a, b), "EDO validity or seeded determinism");
            check(final.evaluations == uint64_t(s.population + (s.population - 1) * s.generations),
                  "micro search budget is not real");
            check(final.generation == s.generations && final.best.total >= final.initialBest,
                  "micro elitism or generation count");
            check(std::abs(evaluate(a, s).total - final.best.total) < 1e-10,
                  "winner was changed after evaluation");
            const auto evolved = generate(s, &a, cancelled);
            check(valid(evolved) && evaluate(evolved, s).total + 1e-10 >= evaluate(a, s).total,
                  "micro evolution lost the elite");
            check(std::abs(frequency(a, edo) / frequency(a, 0) - 2) < 1e-12, "octave frequency");
        }
        std::string error;
        Tuning ratios;
        check(parseRatios("1/1 9/8 7/6 5/4 4/3 3/2 7/4", ratios, error), "rational tuning parser");
        const auto original = ratioText(ratios);
        for (const char* invalid : {"", "1/1", "0/0 3/2", "1/1 2/1", "1/1 3/2 5/4", "1/1 2/2", "1/1 3/2junk",
                                    "1/1 9999999999999/2"}) {
            check(!parseRatios(invalid, ratios, error), "invalid ratio accepted");
            check(ratioText(ratios) == original, "invalid parsing partially changed the tuning");
        }
        s.tuning = ratios;
        auto c = generate(s, nullptr, cancelled);
        check(valid(c), "ratio search invalid");
        check(std::abs(frequency(c, 5) / frequency(c, 0) - 1.5) < 1e-12, "pure 3/2 mistuned");
        // In a nonuniform scale, one degree from 7/6 to 5/4 is NOT the interval 1/1 to 9/8.
        c.count = 2;
        c.notes[0] = {0, 0.4, 2, 90};
        c.notes[1] = {1, 0.4, 3, 90};
        const double actual = approximateRatio(cents(ratios, 3) - cents(ratios, 2)).affinity;
        const double expected = 0.75 * actual + 0.125 * (approximateRatio(cents(ratios, 2)).affinity +
                                                         approximateRatio(cents(ratios, 3)).affinity);
        check(std::abs(evaluate(c, s).harmonicity - expected) < 1e-10,
              "ratio fitness assumed equally spaced degrees");
        check(std::abs(evaluate(c, s).closure - 0.5 * actual) < 1e-10,
              "ratio loop closure assumed equally spaced degrees");
        const auto fifth = approximateRatio(1200 * std::log2(1.5));
        check(fifth.ratio.numerator == 3 && fifth.ratio.denominator == 2 &&
                  std::abs(fifth.errorCents) < 1e-10,
              "rational approximation");
        check(fifth.affinity > approximateRatio(660).affinity, "simple ratio preference");
        check(std::abs(approximateRatio(1199.999).affinity - approximateRatio(0.001).affinity) < 1e-10,
              "octave affinity is discontinuous at the period boundary");
        Settings invalid = s;
        invalid.baseHz = NAN;
        check(!valid(invalid) && generate(invalid, nullptr, cancelled).count == 0,
              "invalid settings reached search");
        cancelled.store(true);
        check(generate(s, nullptr, cancelled).count == 0, "micro search ignored cancellation");
        cancelled.store(false);
        auto aborted = generate(s, nullptr, cancelled, [&](const Stats& stat) {
            if (stat.generation == 2)
                cancelled.store(true);
        });
        check(aborted.count == 0, "partial cancelled result was committed");
        std::cout << "Microtonal engine: EDO / rational pitches, exact Hz, independent fitness, real GA, "
                     "determinism and cancellation passed.\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
