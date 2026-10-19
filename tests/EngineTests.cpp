#include "ComposerEngine.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace composer;
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
void validate(const Clip& c) {
    require(c.count > 0 && c.count <= maxNotes, "invalid note count");
    double previous = -1;
    for (int i = 0; i < c.count; ++i) {
        const auto& n = c.notes[i];
        require(std::isfinite(n.start) && std::isfinite(n.duration), "nonfinite timing");
        require(n.start >= previous && n.start >= 0 && n.start < c.length, "unsorted/out-of-range start");
        require(n.duration > 0 && n.start + n.duration <= c.length + 1e-8, "invalid duration");
        require(n.pitch >= 0 && n.pitch <= 127 && n.velocity > 0 && n.velocity <= 127, "invalid MIDI");
        previous = n.start;
    }
}
bool sameNote(const Note& a, const Note& b) {
    return a.start == b.start && a.duration == b.duration && a.pitch == b.pitch && a.velocity == b.velocity &&
           a.locked == b.locked;
}
bool contains(const Clip& clip, const Note& note) {
    for (int i = 0; i < clip.count; ++i)
        if (sameNote(clip.notes[i], note))
            return true;
    return false;
}
void testHarmony() {
    Harmony harmony;
    std::string error;
    require(parseHarmony("Cm:4 Fm:2 G7:2", harmony, error), "basic chord progression rejected");
    require(error.empty() && harmony.count == 3 && harmony.length == 8.0, "progression length/count");
    require(harmony.chords[0].root == 0 && harmony.chords[0].quality == 1 && harmony.chords[1].start == 4.0 &&
                harmony.chords[2].start == 6.0 && harmony.chords[2].quality == 4,
            "chord names/timeline parsed incorrectly");
    const auto original = harmonyText(harmony);
    for (const auto* malformed : {"H:4", "H#:4", "Cwat:4", "C:", ":4", "C:0", "C:-1", "C:nan", "C:inf",
                                  "C:4foo", "C:4:2", "C:129", "C:100 F:29", "C:0.001", "C"}) {
        require(!parseHarmony(malformed, harmony, error), "malformed chord input accepted");
        require(!error.empty() && harmonyText(harmony) == original, "failed parse changed progression");
    }
    std::string tooMany;
    for (int i = 0; i < 65; ++i)
        tooMany += "C:1 ";
    require(!parseHarmony(tooMany, harmony, error), "too many chords accepted");
    require(parseHarmony("Db:0.5 Ddim:0.5 Eaug:1 Fmaj7:2 F#m7:1 Gsus2:1 Absus4:2", harmony, error),
            "extended chord qualities rejected");
    require(harmony.count == 7 && harmony.chords[0].root == 1 && harmony.chords[1].quality == 2 &&
                harmony.chords[2].quality == 3 && harmony.chords[3].quality == 5 &&
                harmony.chords[4].quality == 6 && harmony.chords[5].quality == 7 &&
                harmony.chords[6].quality == 8,
            "accidental/extended chord quality mismatch");
    Harmony roundTrip;
    require(parseHarmony(harmonyText(harmony), roundTrip, error) &&
                harmonyText(roundTrip) == harmonyText(harmony),
            "harmony text round trip");
    require(parseHarmony(" \t\n", roundTrip, error) && roundTrip.count == 0, "clear harmony");

    Clip chords;
    chords.length = 4.0;
    // Inverted C major, followed mid-bar by F minor seventh.
    for (int pitch : {64, 67, 72})
        chords.notes[chords.count++] = {0.0, 1.5, pitch, 90, false};
    for (int pitch : {53, 56, 60, 63})
        chords.notes[chords.count++] = {1.5, 2.0, pitch, 90, false};
    auto inferred = inferHarmony(chords);
    require(inferred.count == 2 && inferred.chords[0].root == 0 && inferred.chords[0].quality == 0 &&
                inferred.chords[1].root == 5 && inferred.chords[1].quality == 6 &&
                inferred.chords[1].start == 1.5 && inferred.chords[1].duration == 2.5,
            "polyphonic harmony inference or held ending failed");
    require(inferHarmony(makeInitialBank()[0]).count == 0, "monophonic melody invented chords");

    Settings settings;
    settings.quality = 0;
    settings.bars = 1;
    settings.scale = 0;
    settings.tension = 0;
    settings.space = 0;
    require(parseHarmony("Db:1.5 A:0.5", settings.harmony, error), "custom harmony setup");
    std::atomic<bool> cancelled{false};
    const auto generated = generate(settings, nullptr, cancelled);
    bool heardNonDiatonic = false;
    for (const auto& clip : generated) {
        validate(clip);
        bool hasBoundary = false;
        for (int i = 0; i < clip.count; ++i) {
            const auto& note = clip.notes[i];
            const double phase = std::fmod(note.start, 2.0);
            const int root = phase < 1.5 - 1e-8 ? 1 : 9;
            const int interval = (note.pitch - root + 120) % 12;
            require(interval == 0 || interval == 4 || interval == 7,
                    "custom chord ignored at sub-bar boundary");
            hasBoundary |= std::abs(note.start - 1.5) < 1e-8;
            heardNonDiatonic |= snapPitch(note.pitch, 0, 0) != note.pitch;
        }
        require(hasBoundary, "generation skipped custom chord boundary");
    }
    require(heardNonDiatonic, "global scale suppressed non-diatonic custom chords");
}
void testRegionTransforms() {
    Clip section;
    section.length = 4;
    section.notes[section.count++] = {0, 1, 64, 90, false};
    section.notes[section.count++] = {2, 1, 64, 90, false};
    Settings sectionSettings;
    std::string harmonyError;
    require(parseHarmony("C:4 F#:4", sectionSettings.harmony, harmonyError), "section harmony setup");
    const auto firstSection = transformRegion(section, 0, 4, RegionAction::Resolve, sectionSettings);
    sectionSettings.timeOffset = 4;
    const auto secondSection = transformRegion(section, 0, 4, RegionAction::Resolve, sectionSettings);
    require(firstSection.notes[1].pitch % 12 == 0 && secondSection.notes[1].pitch % 12 == 6,
            "region cadence ignored absolute section position");
    for (int i = 0; i < section.count; ++i)
        require(firstSection.notes[i].start == section.notes[i].start &&
                    secondSection.notes[i].start == section.notes[i].start &&
                    firstSection.notes[i].duration == section.notes[i].duration &&
                    secondSection.notes[i].duration == section.notes[i].duration,
                "harmonic time offset shifted local note times");
    Clip clip;
    clip.length = 8;
    clip.notes[clip.count++] = {0, 2, 59, 80, false};
    clip.notes[clip.count++] = {1, 1, 60, 90, false};
    clip.notes[clip.count++] = {2, 1, 64, 91, false};
    clip.notes[clip.count++] = {3, 0.5, 67, 100, true};
    clip.notes[clip.count++] = {3.5, 1, 70, 95, false};
    clip.notes[clip.count++] = {5, 1, 72, 85, false};
    clip.notes[clip.count++] = {7, 0.5, 74, 80, false};
    Settings settings;
    settings.scale = 0;
    for (const auto action :
         {RegionAction::Simplify, RegionAction::MoreMovement, RegionAction::Answer, RegionAction::Resolve}) {
        const auto changed = transformRegion(clip, 1, 5, action, settings);
        validate(changed);
        for (int protectedIndex : {0, 3, 5, 6})
            require(contains(changed, clip.notes[protectedIndex]),
                    "region transform changed protected event");
        if (action == RegionAction::Simplify)
            require(changed.count < clip.count, "simplify did not reduce the selected phrase");
        if (action == RegionAction::MoreMovement) {
            require(changed.count > clip.count, "movement did not add rhythmic detail");
            for (int i = 0; i < changed.count; ++i) {
                const auto& note = changed.notes[i];
                bool existingOnset = false;
                for (int j = 0; j < clip.count; ++j)
                    existingOnset |= note.start == clip.notes[j].start;
                if (!existingOnset) {
                    require(note.start >= 1 && note.start + note.duration <= 5,
                            "added event escaped selected region");
                    for (int protectedIndex : {0, 3, 5, 6}) {
                        const auto& protectedNote = clip.notes[protectedIndex];
                        require(note.start >= protectedNote.start + protectedNote.duration ||
                                    note.start + note.duration <= protectedNote.start,
                                "added event overlapped a protected note");
                    }
                }
            }
        }
        if (action == RegionAction::Answer || action == RegionAction::Resolve) {
            require(changed.count == clip.count, "answer/resolve changed rhythm");
            require(changed.notes[4].pitch % 12 == 0, "phrase did not resolve to tonic");
        }
        const auto repeated = transformRegion(clip, 1, 5, action, settings);
        require(repeated.count == changed.count, "region action not deterministic");
        for (int i = 0; i < repeated.count; ++i)
            require(sameNote(repeated.notes[i], changed.notes[i]), "seeded region action not reproducible");
    }
    const auto empty = transformRegion(clip, 5, 5, RegionAction::MoreMovement, settings);
    for (int i = 0; i < clip.count; ++i)
        require(sameNote(empty.notes[i], clip.notes[i]), "empty selection edited notes");
    for (int i = 0; i < clip.count; ++i)
        clip.notes[i].locked = true;
    const auto locked = transformRegion(clip, 0, 8, RegionAction::Simplify, settings);
    require(locked.count == clip.count, "simplify removed locked notes");
    for (int i = 0; i < clip.count; ++i)
        require(sameNote(locked.notes[i], clip.notes[i]), "locked selection changed");
}
void testGrooveAndCombine() {
    Clip target, reference;
    target.length = 8;
    reference.length = 4;
    for (int i = 0; i < 8; ++i)
        target.notes[target.count++] = {double(i), 0.5, 60 + i, 80, i == 2};
    for (int i = 0; i < 4; ++i)
        reference.notes[reference.count++] = {i + (i % 2 ? 0.10 : 0.0), 0.4, 36, i % 2 ? 60 : 110, false};
    const auto half = applyGroove(target, reference, 0.5f, 0.5f);
    validate(half);
    require(half.count == target.count && half.length == target.length, "groove replaced target rhythm");
    for (int i = 0; i < half.count; ++i) {
        require(half.notes[i].pitch == target.notes[i].pitch &&
                    half.notes[i].duration == target.notes[i].duration,
                "groove changed pitches/durations");
        if (i == 2)
            require(sameNote(half.notes[i], target.notes[i]), "groove changed locked note");
        else {
            require(std::abs(half.notes[i].start - (i + (i % 2 ? 0.05 : 0.0))) < 1e-8,
                    "groove offset interpolation or repeating reference failed");
            require(half.notes[i].velocity == (i % 2 ? 70 : 95), "groove velocity interpolation failed");
        }
    }
    const auto zero = applyGroove(target, reference, 0, 0);
    for (int i = 0; i < target.count; ++i)
        require(sameNote(zero.notes[i], target.notes[i]), "zero groove changed notes");
    const auto accentsOnly = applyGroove(target, reference, 0, 1);
    for (int i = 0; i < target.count; ++i)
        require(accentsOnly.notes[i].start == target.notes[i].start, "velocity-only groove changed timing");
    Clip empty;
    const auto noReference = applyGroove(target, empty, 1, 1);
    for (int i = 0; i < target.count; ++i)
        require(sameNote(noReference.notes[i], target.notes[i]), "empty groove changed notes");

    Clip pitches, accents;
    pitches.length = 4;
    pitches.notes[pitches.count++] = {0, 1, 48, 90, false};
    pitches.notes[pitches.count++] = {2, 1, 72, 90, false};
    accents.length = 16;
    accents.notes[accents.count++] = {0, 1, 36, 45, false};
    accents.notes[accents.count++] = {8, 1, 36, 115, false};
    const auto combined = combineParts(target, pitches, accents);
    validate(combined);
    require(combined.count == target.count && combined.length == target.length, "combine lost target rhythm");
    for (int i = 0; i < combined.count; ++i) {
        if (i == 2) {
            require(sameNote(combined.notes[i], target.notes[i]), "combine changed locked note");
            continue;
        }
        require(combined.notes[i].start == target.notes[i].start &&
                    combined.notes[i].duration == target.notes[i].duration,
                "combine changed timing");
        require(combined.notes[i].pitch == (i <= 2 ? 48 : 72) &&
                    combined.notes[i].velocity == (i <= 2 ? 45 : 115),
                "combine normalized phrase position mismatch");
    }
    const auto noParts = combineParts(target, empty, empty);
    for (int i = 0; i < target.count; ++i)
        require(sameNote(noParts.notes[i], target.notes[i]), "empty combination sources changed target");
}
int main() {
    try {
        std::atomic<bool> cancelled{false};
        Settings s;
        s.quality = 0;
        s.seed = 42;
        auto a = generate(s, nullptr, cancelled), b = generate(s, nullptr, cancelled);
        require(a.size() == 12 && b.size() == 12, "bank size");
        for (size_t i = 0; i < a.size(); ++i) {
            validate(a[i]);
            require(similarity(a[i], b[i]) == 1, "seed reproducibility");
        }
        bool different = false;
        for (size_t i = 1; i < a.size(); ++i)
            different |= similarity(a[0], a[i]) < 0.95;
        require(different, "alternatives lack variety");
        for (int scale = 0; scale < 9; ++scale)
            for (int root = 0; root < 12; ++root)
                for (int p = 0; p < 128; ++p) {
                    int snapped = snapPitch(p, root, scale);
                    require(snapped >= 0 && snapped <= 127, "snap out of bounds");
                    require(snapPitch(snapped, root, scale) == snapped, "snap not idempotent");
                }
        auto parent = makeInitialBank()[0];
        parent.notes[0].locked = true;
        parent.notes[5].locked = true;
        s.mutation = 0.8f;
        s.lockRhythm = true;
        auto evolved = generate(s, &parent, cancelled);
        for (const auto& c : evolved) {
            validate(c);
            require(c.count == parent.count, "rhythm lock changed count");
            for (int i = 0; i < c.count; ++i) {
                require(c.notes[i].start == parent.notes[i].start &&
                            c.notes[i].duration == parent.notes[i].duration,
                        "rhythm lock changed timing");
                if (parent.notes[i].locked)
                    require(c.notes[i].locked && c.notes[i].pitch == parent.notes[i].pitch &&
                                c.notes[i].velocity == parent.notes[i].velocity,
                            "locked note changed");
            }
        }
        s.lockRhythm = false;
        auto unlocked = generate(s, &parent, cancelled);
        for (const auto& c : unlocked)
            for (int lockedIndex : {0, 5}) {
                auto n = parent.notes[lockedIndex];
                bool found = false;
                for (int j = 0; j < c.count; ++j) {
                    auto x = c.notes[j];
                    found |= x.locked && x.start == n.start && x.duration == n.duration &&
                             x.pitch == n.pitch && x.velocity == n.velocity;
                }
                require(found, "locked event lost");
            }
        s.mode = 2;
        auto rhythm = generate(s, &parent, cancelled);
        for (const auto& c : rhythm)
            require(c.count == parent.count, "melody-on-rhythm count");
        s.mode = 0;
        s.numerator = 7;
        s.denominator = 8;
        s.bars = 2;
        auto odd = generate(s, nullptr, cancelled);
        for (auto& c : odd) {
            validate(c);
            require(c.length == 7.0, "7/8 duration");
        }
        s.bars = 1;
        s.numerator = 1;
        s.denominator = 8;
        s.division = 8;
        auto shortClips = generate(s, nullptr, cancelled);
        for (auto& c : shortClips)
            validate(c);
        cancelled = true;
        require(generate(s, nullptr, cancelled).empty(), "cancellation");
        Clip dirty;
        dirty.count = 3;
        dirty.notes[0] = {NAN, 1, 60, 90, false};
        dirty.notes[1] = {0, -1, 60, 90, false};
        dirty.notes[2] = {0, 1, 999, 0, false};
        sanitise(dirty);
        require(dirty.count == 1 && dirty.notes[0].pitch == 127 && dirty.notes[0].velocity == 1,
                "input validation");
        testHarmony();
        testRegionTransforms();
        testGrooveAndCombine();
        std::cout << "Engine: seeds, scales, diversity, locks, rhythm, meters, cancellation, validation, "
                     "custom harmony, region transforms, groove and recombination passed.\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
