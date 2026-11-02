#include "ComposerEngine.h"
#include "MicrotonalEngine.h"
#include <algorithm>
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
            x.velocity != y.velocity || x.locked != y.locked)
            return false;
    }
    return true;
}
void testSharedRhythmAndHarmony() {
    using namespace microtonal;
    std::atomic<bool> cancelled{false};
    Settings s;
    s.population = 16;
    s.generations = 10;
    s.bars = 2;
    s.numerator = 7;
    s.denominator = 8;
    s.division = 16;
    s.phraseType = 0;
    std::string error;
    check(setGrouping(s, "2+2+3", error), "micro grouping rejected");
    const auto previous = s;
    check(!setGrouping(s, "3+3", error) && s.groups == previous.groups,
          "invalid grouping partially changed settings");
    composer::Settings conventional;
    conventional.numerator = 7;
    conventional.denominator = 8;
    conventional.groups = s.groups;
    conventional.groupCount = s.groupCount;
    conventional.groupUnit = s.groupUnit;
    for (double beat = 0; beat < 7; beat += 0.25)
        check(metricWeight(beat, s) == composer::metricWeight(beat, conventional),
              "micro meter diverged from the shared metric rules");
    std::vector<Clip> bank;
    Stats stat;
    auto riff = generate(
        s, nullptr, cancelled, [&](const Stats& v) { stat = v; }, &bank);
    check(valid(riff) && riff.count > 0 && riff.length == 7 && riff.grid == 0.25 && riff.numerator == 7 &&
              riff.denominator == 8 && riff.groupCount == 3 && bank.size() == 12 &&
              stat.rhythmEvaluations > 0,
          "micro generator did not use meter, grid, grouping and rhythm planner");
    for (int i = 0; i < riff.count; ++i)
        if (riff.notes[size_t(i)].start >= 3.5)
            for (int j = 0; j < riff.count; ++j)
                if (std::abs(riff.notes[size_t(j)].start - (riff.notes[size_t(i)].start - 3.5)) < 1e-8)
                    check(riff.notes[size_t(j)].degree == riff.notes[size_t(i)].degree,
                          "micro riff lost repeated pitch structure");
    riff.notes[0].locked = true;
    s.lockRhythm = true;
    s.mutation = 0.9;
    s.numerator = 5;
    s.denominator = 4;
    s.groupCount = 0;
    const auto evolved = generate(s, &riff, cancelled);
    check(evolved.count == riff.count && evolved.length == riff.length && evolved.numerator == 7 &&
              evolved.denominator == 8,
          "evolution did not retain source meter and timing");
    for (int i = 0; i < riff.count; ++i)
        check(evolved.notes[size_t(i)].start == riff.notes[size_t(i)].start &&
                  evolved.notes[size_t(i)].duration == riff.notes[size_t(i)].duration,
              "Keep rhythm changed timing");
    check(evolved.notes[0].locked && evolved.notes[0].degree == riff.notes[0].degree &&
              evolved.notes[0].velocity == riff.notes[0].velocity,
          "locked micro event was changed");
    s.lockRhythm = false;
    s.mode = 2;
    const auto onRhythm = generate(s, &riff, cancelled);
    check(onRhythm.count == riff.count, "melody on rhythm changed note count");
    s = previous;
    s.mode = 1;
    auto rhythm = generate(s, nullptr, cancelled);
    for (int i = 0; i < rhythm.count; ++i)
        check(rhythm.notes[size_t(i)].degree == 0, "rhythm-only generated pitches");
    s = previous;
    s.phraseType = 2;
    ChordSettings cs;
    cs.rhythm = 2;
    for (int edo : {7, 8, 19, 72}) {
        s.tuning.divisions = edo;
        s.seed = uint32_t(edo);
        Stats final;
        const auto chords = generateChords(s, cs, cancelled, [&](const Stats& v) { final = v; });
        const auto again = generateChords(s, cs, cancelled);
        check(valid(chords) && chords.polyphonic && chords.count == 18 && chords.length == 7 &&
                  same(chords, again),
              "micro chord timeline, polyphony or determinism");
        check(final.generation == s.generations && final.best.total >= final.initialBest &&
                  std::abs(evaluateChords(chords, s, cs).total - final.best.total) < 1e-9,
              "chord search reported an unscored result");
        for (int i = 0; i < chords.count; i += cs.voices) {
            check(chords.notes[size_t(i)].duration == (i / cs.voices % 3 == 2 ? 1.5 : 1.0),
                  "chord rhythm ignored grouping");
            for (int a = 0; a < cs.voices; ++a)
                for (int b = a + 1; b < cs.voices; ++b)
                    check(chords.notes[size_t(i + a)].degree % edo !=
                              chords.notes[size_t(i + b)].degree % edo,
                          "micro chord search collapsed to octave doublings");
        }
        cs.voicing = 1;
        const auto voiced = revoiceChords(chords, s, cs);
        check(valid(voiced) && voiced.count == chords.count, "micro revoicing lost events");
        for (int i = 0; i < chords.count; i += cs.voices) {
            std::array<int, 6> old{}, now{};
            for (int v = 0; v < cs.voices; ++v) {
                old[size_t(v)] = chords.notes[size_t(i + v)].degree % edo;
                now[size_t(v)] = voiced.notes[size_t(i + v)].degree % edo;
            }
            std::sort(old.begin(), old.end());
            std::sort(now.begin(), now.end());
            check(old == now, "revoicing changed chord pitch classes");
        }
        cs.voicing = 0;
        const auto melody = generate(s, nullptr, cancelled, {}, nullptr, &chords);
        const auto combined = combine(melody, chords);
        check(valid(combined) && combined.polyphonic && combined.count == melody.count + chords.count,
              "micro melody/chord combination");
    }
    check(parseRatios("1/1 9/8 7/6 5/4 4/3 3/2 7/4", s.tuning, error), "ratio chord test tuning");
    auto justChords = generateChords(s, cs, cancelled);
    check(valid(justChords) && justChords.count > 0, "rational chord generator");
    s.bars = 1;
    s.numerator = 1;
    s.denominator = 32;
    s.groupCount = 0;
    s.groupUnit = 32;
    check(valid(s) && generate(s, nullptr, cancelled).length == 0.125, "short meter cannot generate");
    Clip timing;
    timing.count = 4;
    timing.length = 2;
    timing.grid = 0.25;
    timing.seed = 42;
    for (int i = 0; i < 4; ++i)
        timing.notes[size_t(i)] = {i * 0.25, 0.2, i, 96, false};
    const auto unchanged = timing;
    s.swing = 0.5;
    s.gate = 0.5;
    s.humanize = 0.3;
    for (int i = 0; i < timing.count; ++i) {
        const auto a = performedEvent(timing, i, s), b = performedEvent(timing, i, s);
        check(a.start == b.start && a.duration == b.duration && a.velocity == b.velocity &&
                  a.duration <= 0.1 + 1e-10,
              "micro performance is not deterministic or ignores gate");
        if (i + 1 < timing.count)
            check(a.start + a.duration <= performedEvent(timing, i + 1, s).start + 1e-9,
                  "mono micro performance overlaps bends");
    }
    check(same(timing, unchanged) && performedEvent(timing, 1, s).start != timing.notes[1].start,
          "micro performance rewrote storage or ignored swing/humanize");
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
        cancelled.store(false);
        Settings large;
        large.population = 16;
        large.generations = 300;
        for (bool harmony : {false, true}) {
            Stats final;
            int reports = 0;
            auto observe = [&](const Stats& stats) {
                if (stats.evaluations > 0)
                    check(stats.generation == reports++, "large micro search skipped a generation");
                final = stats;
            };
            const auto result = harmony ? generateChords(large, {}, cancelled, observe)
                                        : generate(large, nullptr, cancelled, observe);
            check(result.count > 0 && reports == 301 && final.generation == 300 && final.evaluations == 4516,
                  "large micro search silently capped the generation/evaluation budget");
            check(harmony || final.rhythmEvaluations == 4816, "large micro rhythm planner capped its budget");
        }
        testSharedRhythmAndHarmony();
        std::cout << "Microtonal engine: EDO / rational pitches, exact Hz, independent fitness, real GA, "
                     "determinism and cancellation passed.\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
