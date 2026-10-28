#include "ComposerEngine.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace {
using namespace composer;
void check(bool condition, const char* text) {
    if (!condition)
        throw std::runtime_error(text);
}
bool same(const Clip& a, const Clip& b) {
    if (a.count != b.count || a.length != b.length)
        return false;
    for (int i = 0; i < a.count; ++i) {
        const auto& x = a.notes[size_t(i)];
        const auto& y = b.notes[size_t(i)];
        if (x.start != y.start || x.duration != y.duration || x.pitch != y.pitch || x.cents != y.cents ||
            x.velocity != y.velocity || x.locked != y.locked)
            return false;
    }
    return true;
}
Clip line(std::initializer_list<int> pitches, double step = 0.25) {
    Clip clip;
    clip.length = pitches.size() * step;
    for (int pitch : pitches) {
        clip.notes[size_t(clip.count)] = {clip.count * step, step, pitch, 90, false, 0};
        ++clip.count;
    }
    return clip;
}
} // namespace

void runMusicalArchitectureTests() {
    using namespace composer;
    check(intervalCost(-150) == intervalCost(150) && intervalCost(50) < intervalCost(200) &&
              intervalCost(200) < intervalCost(700) && intervalCost(1200) < intervalCost(2400) &&
              intervalCost(125) != intervalCost(100),
          "interval objective ignores cents, step preference, sign or octave-sized leaps");
    check(std::isfinite(intervalCost(std::numeric_limits<double>::quiet_NaN())),
          "interval objective must be finite for malformed input");
    Settings settings;
    settings.scale = 0;
    settings.microEnabled = true;
    settings.tuning = 6;
    settings.phraseType = 2;
    auto passing = line({60, 64, 64, 67});
    passing.notes[1].cents = -50;
    auto plain = passing;
    plain.notes[1].cents = 0;
    check(analysePhrase(passing, settings).resolvedPassingNotes == 1 &&
              analysePhrase(passing, settings).invalidPassingNotes == 0,
          "short microtonal approach to an equal-tempered anchor was not recognised");
    check(std::abs(fitnessScore(passing, settings) - fitnessScore(plain, settings)) > 1e-6,
          "cents are not part of the actual fitness objective");
    auto shorterPassing = passing;
    shorterPassing.notes[1].duration = 0.125;
    auto withoutMicroObjective = settings;
    withoutMicroObjective.microEnabled = false;
    const double onDurationPreference =
        fitnessScore(shorterPassing, settings) - fitnessScore(passing, settings);
    const double offDurationPreference =
        fitnessScore(shorterPassing, withoutMicroObjective) - fitnessScore(passing, withoutMicroObjective);
    check(onDurationPreference > offDurationPreference + 0.1,
          "microtonal target counts events instead of their share of sounding time");
    auto hanging = passing;
    hanging.notes[2].cents = 50;
    hanging.notes[3].cents = 50;
    check(analysePhrase(hanging, settings).invalidPassingNotes >= 2 &&
              fitnessScore(hanging, settings) < fitnessScore(passing, settings),
          "unresolved microtonal chains lack a fitness penalty");
    const auto loop = analysePhrase(line({60, 62, 64, 72}), settings);
    check(loop.loopIntervalCents == 1200 && loop.meanIntervalCents == 600,
          "last-to-first melodic interval was omitted");

    std::atomic<bool> cancelled{false};
    // Regressions for the sparse 2.5 search: healthy defaults must make active
    // phrases across seeds, while a deliberately low Density remains useful.
    for (int type = 0; type < 3; ++type) {
        double defaultNotes = 0, sparseNotes = 0;
        for (uint32_t seed = 1; seed <= 6; ++seed) {
            Settings active;
            active.seed = seed;
            active.phraseType = type;
            active.quality = 0;
            SearchStats stats;
            const auto bank = generate(active, nullptr, cancelled, {}, [&](const auto& value) { stats = value; });
            defaultNotes += bank.front().count;
            check(stats.generation == 40 && stats.evaluations == 64 * 41 && stats.crossovers > 0,
                  "denser search skipped its actual generations or crossover");
            active.density = 0;
            sparseNotes += generate(active, nullptr, cancelled).front().count;
        }
        check(defaultNotes / 24 >= 6 && defaultNotes / 24 <= 11,
              "default search collapsed to sparse phrases or indiscriminate grid filling");
        check(defaultNotes > sparseNotes * 1.35, "Density does not meaningfully change onset activity");
    }
    {
        auto rhythmParent = line({60, 62, 64, 65, 67, 65, 64, 62}, .5);
        Settings evolve;
        evolve.phraseType = 2;
        evolve.mutation = .85f;
        evolve.population = 48;
        evolve.generations = 32;
        bool newOnset = false;
        for (const auto& clip : generate(evolve, &rhythmParent, cancelled))
            for (int i = 0; i < clip.count; ++i)
                newOnset |= std::abs(clip.notes[i].start * 2 - std::round(clip.notes[i].start * 2)) > .01;
        check(newOnset, "evolution cannot introduce onsets between the parent's fixed starts");
        evolve.lockRhythm = true;
        for (const auto& clip : generate(evolve, &rhythmParent, cancelled)) {
            check(clip.count == rhythmParent.count, "rhythm operator bypassed Keep rhythm");
            for (int i = 0; i < clip.count; ++i)
                check(clip.notes[i].start == rhythmParent.notes[i].start &&
                          clip.notes[i].duration == rhythmParent.notes[i].duration,
                      "rhythm operator moved protected timing");
        }
    }
    settings.population = 32;
    settings.generations = 16;
    settings.density = 0.8f;
    settings.space = 0.05f;
    bool heardMicrotones = false;
    uint64_t evaluatedMicrotones = 0;
    for (uint32_t seed = 1; seed <= 8; ++seed) {
        settings.seed = seed;
        SearchStats final;
        const auto bank =
            generate(settings, nullptr, cancelled, {}, [&](const SearchStats& s) { final = s; });
        check(bank.size() == slotCount, "microtonal search did not complete");
        evaluatedMicrotones += final.microtonalEvaluations;
        check(std::abs(fitnessScore(bank.front(), settings) - final.bestFitness) < 1e-8,
              "winning chromosome changed after its fitness was measured");
        for (const auto& clip : bank) {
            const auto details = analysePhrase(clip, settings);
            heardMicrotones |= details.microtonalNotes > 0;
            check(details.invalidPassingNotes == 0, "search output violates passing-note constraints");
            check(clip.notes[0].cents == 0 && clip.notes[size_t(clip.count - 1)].cents == 0,
                  "phrase lacks stable beginning/end anchors");
            for (int i = 0; i < clip.count; ++i)
                if (clip.notes[size_t(i)].cents)
                    check(clip.notes[size_t(i)].duration <= std::min(0.5, clip.grid) + 1e-8,
                          "generated microtone is too long");
        }
        if (seed == 1) {
            const auto repeat = generate(settings, nullptr, cancelled);
            for (size_t i = 0; i < bank.size(); ++i)
                check(same(bank[i], repeat[i]), "microtonal genetic search is not reproducible");
            auto ignoredAmount = settings;
            ignoredAmount.microtones = 0;
            const auto withoutLegacyAmount = generate(ignoredAmount, nullptr, cancelled);
            for (size_t i = 0; i < bank.size(); ++i)
                check(same(bank[i], withoutLegacyAmount[i]),
                      "legacy Microtones amount still changes generation");
        }
    }
    check(heardMicrotones && evaluatedMicrotones > 0,
          "microtonal notes were never part of the evaluated population");

    // This is the exact listening-example configuration. Its short motif repeats
    // each passing gene, so valid approaches must not lose full harmonic credit
    // on every copy and disappear from an otherwise feasible Microtones-ON search.
    {
        Settings example;
        std::string error;
        check(parseHarmony("Cm:4 Fm:4 Ab:4 G7:4", example.harmony, error), "motif example harmony setup");
        example.phraseType = 1;
        example.scale = 6;
        example.low = 60;
        example.high = 81;
        example.microEnabled = true;
        example.tuning = 6;
        example.diversity = 0.4f;
        example.arousal = 0.6f;
        example.seed = 41;
        SearchStats last;
        const auto bank =
            generate(example, nullptr, cancelled, {}, [&](const SearchStats& stats) { last = stats; });
        check(!bank.empty() && analysePhrase(bank.front(), example).resolvedPassingNotes > 0 &&
                  analysePhrase(bank.front(), example).invalidPassingNotes == 0,
              "feasible motif example lost every valid microtonal approach");
        check(std::abs(fitnessScore(bank.front(), example) - last.bestFitness) < 1e-8,
              "motif example was modified after its genetic evaluation");
    }

    // Bayati supplies D half-flat over C. It is a short non-chord approach to C/E,
    // so zero Jazziness must constrain its anchors rather than eliminate the gene.
    for (int type = 0; type < 3; ++type) {
        auto passingSettings = settings;
        passingSettings.phraseType = type;
        passingSettings.tuning = 3;
        passingSettings.tension = 0;
        passingSettings.population = 48;
        passingSettings.generations = 32;
        std::string error;
        check(parseHarmony("C:16", passingSettings.harmony, error), "passing harmony setup");
        bool present = false;
        uint64_t evaluated = 0;
        for (uint32_t seed : {11u, 23u, 42u}) {
            passingSettings.seed = seed;
            SearchStats last;
            const auto bank = generate(passingSettings, nullptr, cancelled, {},
                                       [&](const SearchStats& stats) { last = stats; });
            evaluated += last.microtonalEvaluations;
            for (const auto& clip : bank) {
                const auto details = analysePhrase(clip, passingSettings);
                check(details.invalidPassingNotes == 0, "non-chord microtone did not resolve correctly");
                present |= details.resolvedPassingNotes > 0;
                for (int i = 0; i < clip.count; ++i) {
                    const auto& note = clip.notes[size_t(i)];
                    if (note.cents)
                        check(note.pitch % 12 == 2 && note.cents == -50,
                              "non-chord passing mutation left the Bayati palette");
                    else
                        check(note.pitch % 12 == 0 || note.pitch % 12 == 4 || note.pitch % 12 == 7,
                              "zero Jazziness left a non-chord stable anchor");
                }
            }
        }
        check(evaluated > 0 && present,
              "zero Jazziness suppressed all valid Bayati passing notes over a C chord");
    }

    settings.microEnabled = false;
    for (const auto& clip : generate(settings, nullptr, cancelled))
        for (int i = 0; i < clip.count; ++i)
            check(clip.notes[size_t(i)].cents == 0, "disabled generation retained microtonal genes");
    auto parent = line({60, 62, 64, 65, 67, 65, 64, 62});
    parent.notes[1].cents = 50;
    parent.notes[3].cents = -50;
    parent.notes[3].locked = true;
    settings.lockRhythm = true;
    settings.mutation = 0.8f;
    for (const auto& clip : generate(settings, &parent, cancelled)) {
        check(clip.count == parent.count, "evolution changed a protected rhythmic template");
        for (int i = 0; i < clip.count; ++i) {
            check(clip.notes[size_t(i)].start == parent.notes[size_t(i)].start &&
                      clip.notes[size_t(i)].duration == parent.notes[size_t(i)].duration,
                  "passing-note repair changed protected timing");
            if (i == 3)
                check(clip.notes[3].pitch == parent.notes[3].pitch && clip.notes[3].cents == -50,
                      "Microtones OFF overwrote a locked offset");
            else
                check(clip.notes[size_t(i)].cents == 0, "Microtones OFF retained an unlocked parent offset");
        }
    }

    settings = {};
    settings.scale = 0;
    settings.seed = 23;
    settings.population = 48;
    settings.generations = 24;
    settings.space = 0.10f;
    std::array<Clip, 3> examples;
    std::array<PhraseDiagnostics, 3> diagnostics;
    for (int type = 0; type < 3; ++type) {
        settings.phraseType = type;
        examples[size_t(type)] = generate(settings, nullptr, cancelled).front();
        diagnostics[size_t(type)] = analysePhrase(examples[size_t(type)], settings);
    }
    check(diagnostics[0].repetition > 0.90 && diagnostics[0].rhythmicRepetition > 0.90,
          "riff architecture did not repeat its actual note/rhythm cell");
    check(diagnostics[1].motifReturn > 0.70 && diagnostics[1].middleContrast > 0.02,
          "motif architecture lacks its returning theme or contrasting middle");
    check(diagnostics[2].repetition < diagnostics[0].repetition && diagnostics[2].contourArc > 0,
          "melody architecture did not develop a longer contour");
    check(!same(examples[0], examples[1]) && !same(examples[1], examples[2]),
          "phrase types are labels for the same event structure");
    settings.phraseType = 0;
    for (const auto& clip : generate(settings, &examples[0], cancelled))
        check(analysePhrase(clip, settings).repetition > 0.90,
              "unlocked riff evolution lost its repeated cell architecture");

    // Musical controls participate in the objective, not just UI labels or velocity.
    auto regular = line({60, 62, 64, 65, 67, 65, 64, 62});
    auto varied = regular;
    for (int i = 0; i < varied.count; ++i) {
        varied.notes[size_t(i)].duration = i % 2 ? 0.125 : 0.25;
        varied.notes[size_t(i)].pitch += i % 3 ? 0 : 12;
    }
    settings.phraseType = 2;
    settings.dynamics = 0;
    const double calmDynamics = fitnessScore(varied, settings) - fitnessScore(regular, settings);
    settings.dynamics = 1;
    check(fitnessScore(varied, settings) - fitnessScore(regular, settings) > calmDynamics,
          "Dynamics does not prefer greater rhythmic/pitch variation");
    settings = {};
    settings.phraseType = 2;
    auto low = line({48, 50, 52, 53, 55, 53, 52, 50});
    auto high = low;
    for (int i = 0; i < high.count; ++i)
        high.notes[size_t(i)].pitch += 24;
    settings.valence = 0;
    const double lowValence = fitnessScore(high, settings) - fitnessScore(low, settings);
    settings.valence = 1;
    check(fitnessScore(high, settings) - fitnessScore(low, settings) > lowValence,
          "Valence does not influence preferred register");
    settings = {};
    auto sparse = line({60, 62, 64, 62}, 1.0);
    auto active = line({60, 62, 64, 62, 60, 62, 64, 62, 60, 62, 64, 62, 60, 62, 64, 62});
    settings.arousal = 0;
    const double lowArousal = fitnessScore(active, settings) - fitnessScore(sparse, settings);
    settings.arousal = 1;
    check(fitnessScore(active, settings) - fitnessScore(sparse, settings) > lowArousal,
          "Arousal does not influence onset activity");
    auto shifted = sparse;
    for (int i = 0; i < shifted.count; ++i) {
        shifted.notes[size_t(i)].start += 0.25;
        shifted.notes[size_t(i)].duration = 0.75;
    }
    settings.weirdness = 0;
    const double familiar = fitnessScore(shifted, settings) - fitnessScore(sparse, settings);
    settings.weirdness = 1;
    check(fitnessScore(shifted, settings) - fitnessScore(sparse, settings) > familiar,
          "Weirdness does not influence offbeat preference");
}
