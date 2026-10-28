#include "ChordGenerator.h"
#include "ComposerEngine.h"
#include <chrono>
#include <iostream>
#include <set>

// Compile against composer_engine; emits raw per-seed measurements, not a quality score.
int main() {
    std::atomic<bool> cancelled{false};
    std::cout << "type,harmony,seed,notes,notes_per_bar,pitches,short_ratio,repeat_pitch_ratio,ms,"
                 "evaluations,crossovers,improvement\n";
    for (int type = 0; type < 3; ++type)
        for (int harmony = 0; harmony < 2; ++harmony)
            for (int seed = 1; seed <= 12; ++seed) {
                composer::Settings s;
                s.phraseType = type;
                s.seed = seed;
                s.quality = 0;
                if (harmony) {
                    std::string error;
                    composer::parseHarmony("Cm:4 Fm:4 Ab:4 G7:4", s.harmony, error);
                }
                composer::SearchStats stats;
                const auto start = std::chrono::steady_clock::now();
                const auto bank =
                    composer::generate(s, nullptr, cancelled, {}, [&](const auto& value) { stats = value; });
                const double ms =
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                        .count();
                const auto& c = bank.front();
                std::set<int> pitches;
                int shortNotes = 0, repeated = 0;
                for (int i = 0; i < c.count; ++i) {
                    pitches.insert(c.notes[i].pitch);
                    shortNotes += c.notes[i].duration <= .5;
                    repeated += i > 0 && c.notes[i].pitch == c.notes[i - 1].pitch;
                }
                std::cout << type << ',' << harmony << ',' << seed << ',' << c.count << ',' << c.count / 4.0
                          << ',' << pitches.size() << ',' << double(shortNotes) / c.count << ','
                          << double(repeated) / std::max(1, c.count - 1) << ',' << ms << ','
                          << stats.evaluations << ',' << stats.crossovers << ','
                          << stats.bestFitness - stats.initialBest << '\n';
            }
}
