#include "ComposerEngine.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace {
void check(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
bool sameNote(const composer::Note& a, const composer::Note& b) {
    return a.start == b.start && a.duration == b.duration && a.pitch == b.pitch && a.velocity == b.velocity &&
           a.locked == b.locked && a.cents == b.cents;
}
void testRegionSearch() {
    using namespace composer;
    Clip parent;
    parent.length = 8;
    parent.count = 16;
    parent.seed = 42;
    parent.favourite = true;
    for (int i = 0; i < parent.count; ++i)
        parent.notes[i] = {i * 0.5, 0.5, 60 + i % 7, 80 + i, i == 6, i % 2 ? 25 : 0};
    parent.notes[4].duration = 1; // Outside onset sustaining into the selected region.
    parent.notes[6].duration = 1.5; // A protected note inside the region.
    Settings s;
    s.population = 16;
    s.generations = 4;
    s.mutation = 1;
    s.regionOnly = true;
    s.regionStart = 2.25;
    s.regionEnd = 5.5;
    std::atomic<bool> cancelled{false};
    for (int mode = 0; mode < 3; ++mode) {
        s.mode = mode;
        s.microEnabled = mode == 0;
        const auto results = generate(s, &parent, cancelled);
        check(results.size() == slotCount, "scoped search did not complete");
        bool changed = false;
        for (const auto& result : results) {
            check(result.length == parent.length && result.grid == parent.grid && result.seed == parent.seed &&
                      result.favourite == parent.favourite, "scoped search changed phrase metadata");
            if (mode == 2) {
                check(result.count == parent.count, "Melody on rhythm changed the scoped note count");
                for (int i = 0; i < parent.count; ++i)
                    check(result.notes[i].start == parent.notes[i].start &&
                              result.notes[i].duration == parent.notes[i].duration,
                          "Melody on rhythm changed scoped timing");
            }
            for (int i = 0; i < parent.count; ++i) {
                const auto& n = parent.notes[i];
                const bool retained = std::any_of(result.notes.begin(), result.notes.begin() + result.count,
                                                  [&](const Note& m) { return sameNote(n, m); });
                if (n.locked || n.start < s.regionStart || n.start >= s.regionEnd)
                    check(retained, "scoped search changed a protected note field");
                else
                    changed |= !retained;
            }
            for (int i = 0; i < result.count; ++i) {
                const auto& n = result.notes[i];
                if (n.start < s.regionStart || n.start >= s.regionEnd || n.locked)
                    check(std::any_of(parent.notes.begin(), parent.notes.begin() + parent.count,
                                      [&](const Note& m) { return sameNote(n, m); }),
                          "scoped search inserted an event outside the selection");
            }
        }
        check(changed, "scoped search did not optimise any editable notes");
    }
    s.mode = 0;
    s.lockRhythm = true;
    for (const auto& result : generate(s, &parent, cancelled)) {
        check(result.count == parent.count, "Keep rhythm changed the scoped note count");
        for (int i = 0; i < parent.count; ++i)
            check(result.notes[i].start == parent.notes[i].start && result.notes[i].duration == parent.notes[i].duration,
                  "Keep rhythm changed scoped timing");
    }
    s.lockRhythm = false;
    Clip full = parent;
    full.length = 128;
    full.count = maxNotes;
    for (int i = 0; i < full.count; ++i)
        full.notes[i] = {i * 0.25, 0.25, 60 + i % 7, 90, false, i % 2 ? 25 : 0};
    s.regionStart = 32;
    s.regionEnd = 33;
    for (const auto& result : generate(s, &full, cancelled)) {
        check(result.count <= maxNotes, "scoped rhythm mutation overflowed capacity");
        for (int i = 0; i < full.count; ++i)
            if (full.notes[i].start < 32 || full.notes[i].start >= 33)
                check(std::any_of(result.notes.begin(), result.notes.begin() + result.count,
                                  [&](const Note& n) { return sameNote(n, full.notes[i]); }),
                      "capacity pressure dropped a protected event");
    }
    s.regionStart = 2.25;
    s.regionEnd = s.regionStart;
    check(generate(s, &parent, cancelled).empty(), "empty evolution scope accepted");
    s.regionEnd = 5.5;
    cancelled.store(true);
    check(generate(s, &parent, cancelled).empty(), "cancelled scoped search returned a result");
}
} // namespace
void runSearchTests() {
    testRegionSearch();
    using namespace composer;
    std::atomic<bool> cancelled{false};
    Settings s;
    check(searchBudget(s).population == 128 && searchBudget(s).generations == 100,
          "balanced budget must match the original 128 x 100");
    TimeSignature meter{4, 4};
    std::string error;
    for (auto text : {"0/4", "33/4", "4/3", "4/64", "3.5/4", "7/8oops", "7", "-7/8"}) {
        check(!parseMeter(text, meter, error), "invalid meter accepted");
        check(meter.numerator == 4 && meter.denominator == 4, "invalid meter changed prior value");
    }
    s.population = 16;
    s.generations = 2;
    s.bars = 1;
    auto meters = timeSignatures();
    meters.push_back({1, 32});
    meters.push_back({32, 1});
    for (const auto& m : meters) {
        check(parseMeter(std::to_string(m.numerator) + "/" + std::to_string(m.denominator), meter, error),
              "supported meter rejected");
        s.numerator = meter.numerator;
        s.denominator = meter.denominator;
        const auto generated = generate(s, nullptr, cancelled);
        check(generated.size() == slotCount, "supported meter did not generate a full bank");
        for (const auto& c : generated) {
            check(c.numerator == m.numerator && c.denominator == m.denominator,
                  "meter changed during generation");
            check(std::abs(c.length - m.numerator * 4.0 / m.denominator) < 1e-8,
                  "generated phrase length does not match meter");
            check(c.count > 0 && c.count <= maxNotes, "meter produced invalid note count");
            for (int i = 0; i < c.count; ++i)
                check(c.notes[i].start >= 0 && c.notes[i].duration > 0 &&
                          c.notes[i].start + c.notes[i].duration <= c.length + 1e-8,
                      "meter produced invalid note timing");
        }
    }
    s.bars = 2;
    check(generate(s, nullptr, cancelled).empty(), "oversize generated phrase must be rejected");

    // Check each degree, including the second repeat of sequences of 1, 2, 3, 8 and 12 bars.
    s = {};
    s.scale = 0;
    s.low = 0;
    s.high = 127;
    Clip cadence;
    cadence.count = 1;
    cadence.length = 4;
    cadence.notes[0] = {0, 1, 66, 96, false};
    for (size_t i = 1; i < progressionPresets().size(); ++i) {
        s.progression = int(i);
        const auto& degrees = progressionPresets()[i].degrees;
        for (size_t bar = 0; bar < degrees.size() * 2; ++bar) {
            s.timeOffset = bar * 4.0;
            const auto resolved = transformRegion(cadence, 0, 4, RegionAction::Resolve, s);
            check(resolved.notes[0].pitch % 12 == scaleIntervals(0)[size_t(degrees[bar % degrees.size()])],
                  "progression degree or repeat differs from catalog");
        }
    }
    // Statistics prove that the requested work occurs, elitism retains the winner,
    // and crossover can be independently enabled/disabled in every generation mode.
    for (int mode = 0; mode < 3; ++mode) {
        s = {};
        s.mode = mode;
        s.population = 32;
        s.generations = 7;
        s.crossover = 1;
        s.seed = 372;
        double best = -1e9;
        int reports = 0;
        SearchStats last;
        const auto bank = makeInitialBank();
        auto result =
            generate(s, mode == 2 ? &bank[0] : nullptr, cancelled, {}, [&](const SearchStats& stats) {
                check(stats.bestFitness + 1e-9 >= best, "elitism lost the best candidate");
                check(std::isfinite(stats.meanFitness), "nonfinite mean fitness");
                best = stats.bestFitness;
                last = stats;
                ++reports;
            });
        check(result.size() == slotCount && reports == 8 && last.generation == 7,
              "requested generations were skipped");
        check(last.evaluations == 32 * 8 && last.crossovers == 31 * 7,
              "fitness or crossover operation counts do not match budget");
        s.crossover = 0;
        result = generate(s, mode == 2 ? &bank[0] : nullptr, cancelled, {},
                          [&](const SearchStats& stats) { last = stats; });
        check(last.crossovers == 0 && result.size() == slotCount, "zero crossover was ignored");
    }
    s = {};
    s.population = 64;
    s.generations = 2;
    s.crossover = 1;
    s.seed = 9;
    SearchStats islands;
    auto islandResult = generate(s, nullptr, cancelled, {}, [&](const SearchStats& stats) { islands = stats; });
    check(islandResult.size() == slotCount && islands.evaluations == 64 * 3 && islands.crossovers == 60 * 2,
          "four islands did not keep one elite each or the evaluation budget");
    const auto repeated = generate(s, nullptr, cancelled);
    check(similarity(islandResult.front(), repeated.front()) == 1, "island search was not deterministic");
    std::vector<Clip> mates{makeInitialBank()[0]};
    s.population = 16;
    s.generations = 2;
    s.seed = 3;
    const auto evolved = generate(s, &mates.front(), cancelled, {}, {}, &mates);
    const auto evolvedAgain = generate(s, &mates.front(), cancelled, {}, {}, &mates);
    check(evolved.size() == slotCount && similarity(evolved.front(), evolvedAgain.front()) == 1,
          "pinned mates made evolution nondeterministic");
    s.generations = 2000;
    auto stopped = generate(s, nullptr, cancelled, {}, [&](const SearchStats& stats) {
        if (stats.generation == 2)
            cancelled = true;
    });
    check(stopped.empty(), "cancelled custom search returned a partial bank");
}
