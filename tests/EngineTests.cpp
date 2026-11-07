#include "ComposerEngine.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
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
        require(std::abs(n.cents) <= 50, "microtonal offset out of range");
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
    require(parseHarmony("Dm7b5:4 G9:2 Cmaj9:2 Am6:2 F6:2 Bdim7:2 Cadd9:2", harmony, error),
            "ninth and half-diminished symbols rejected");
    require(harmony.chords[0].quality == 9 && harmony.chords[1].quality == 14 &&
                harmony.chords[2].quality == 15 && harmony.chords[3].quality == 12 &&
                harmony.chords[4].quality == 11 && harmony.chords[5].quality == 10 &&
                harmony.chords[6].quality == 13,
            "new chord qualities were parsed as the wrong type");
    require(parseHarmony(harmonyText(harmony), roundTrip, error) &&
                harmonyText(roundTrip) == harmonyText(harmony),
            "new chord symbols did not round-trip");
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
int pitchClass(int pitch, int root = 0) {
    return ((pitch - root) % 12 + 12) % 12;
}
// Unlocked notes of one pitch class share one form within a phrase.
bool oneFormPerPitchClass(const Clip& clip, int root = 0) {
    std::array<int, 12> form{};
    std::array<bool, 12> known{};
    for (int i = 0; i < clip.count; ++i) {
        const auto& note = clip.notes[i];
        const int pc = pitchClass(note.pitch, root);
        if (note.locked)
            continue;
        if (known[pc] && form[pc] != note.cents)
            return false;
        known[pc] = true;
        form[pc] = note.cents;
    }
    return true;
}
void testMicrotonal() {
    for (int tuning = 0; tuning < tuningCount; ++tuning)
        for (int octave : {-12, 0, 12})
            require(microOffset(tuning, octave) == 0, "every tuning keeps an equal-tempered tonic");
    require(microOffset(2, 4) == -50 && microOffset(2, 11) == -50 && microOffset(2, 16) == -50 &&
                microOffset(2, 3) == 0 && microOffset(3, 2) == -50 && microOffset(5, 3) == 50,
            "maqam and blues forms");
    require(microOffset(1, 3) == 50 && microOffset(1, 4) == -50 && microOffset(1, 7) == 0 &&
                microOffset(6, 7) == 50,
            "neutral inflections and 24-EDO steps");
    require(tuningScale(0, 6) == 6 && tuningScale(1, 6) == 6 && tuningScale(2, 6) == 0 &&
                tuningScale(4, 6) == 9 && scaleIntervals(9).size() == 7,
            "tunings select their own scales");
    require(tuningScale(2, 12) == 12 && tuningScale(0, 15) == 15 && scaleIntervals(10).size() == 7 &&
                scaleIntervals(13).size() == 6 && scaleIntervals(14).size() == 6,
            "extended scales were clamped or replaced by a tuning");
    require(snapTuned(63, 0, 0, 1, 2) == std::make_pair(64, -50), "Eb did not snap to Rast's E half-flat");
    require(snapTuned(63, 50, 0, 1, 2) == std::make_pair(64, -50), "Eb+50 must respell as E half-flat");
    require(snapTuned(64, 0, 0, 1, 2) == std::make_pair(64, 0), "plain form of a degree was not kept");
    // 24-EDO reaches the step above the tonic from C#; the tonic itself has no other form.
    require(snapTuned(60, 30, 0, 0, 6) == std::make_pair(61, -50), "24-EDO nearest quarter tone");
    require(snapTuned(62, -50, 0, 0, 6) == std::make_pair(62, -50), "valid spelling was not kept");
    require(snapTuned(64, -50, 0, 0, 0) == std::make_pair(64, 0), "12-TET snap must clear offsets");
    for (int tuning = 0; tuning < tuningCount; ++tuning)
        for (int root : {0, 5, 11})
            for (int pitch = 0; pitch < 128; ++pitch)
                for (int cents : {-50, -20, 0, 35, 50}) {
                    const auto snapped = snapTuned(pitch, cents, root, 3, tuning);
                    require(snapped.first >= 0 && snapped.first <= 127 && std::abs(snapped.second) <= 50,
                            "tuned snap out of range");
                    require(snapTuned(snapped.first, snapped.second, root, 3, tuning) == snapped,
                            "tuned snap not idempotent");
                }

    Clip offsets;
    offsets.length = 4;
    offsets.count = 4;
    offsets.notes[0] = {0, 1, 60, 90, false, 80};
    offsets.notes[1] = {1, 1, 127, 90, false, 80};
    offsets.notes[2] = {2, 1, 0, 90, false, -80};
    offsets.notes[3] = {3, 1, 64, 90, false, -50};
    sanitise(offsets);
    require(offsets.notes[0].pitch == 61 && offsets.notes[0].cents == -20 && offsets.notes[1].pitch == 127 &&
                offsets.notes[1].cents == 50 && offsets.notes[2].pitch == 0 &&
                offsets.notes[2].cents == -50 && offsets.notes[3].pitch == 64 &&
                offsets.notes[3].cents == -50,
            "cents normalisation");

    std::atomic<bool> cancelled{false};
    Settings s;
    s.quality = 0;
    s.seed = 11;
    s.scale = 0;
    s.tension = 0;
    s.tuning = 2;
    s.microEnabled = true;
    const auto rast = generate(s, nullptr, cancelled);
    for (const auto& clip : rast) {
        validate(clip);
        require(analysePhrase(clip, s).invalidPassingNotes == 0,
                "generated microtones must be short resolving passing notes");
        for (int i = 0; i < clip.count; ++i)
            require(clip.notes[i].cents == 0 || clip.notes[i].cents == microOffset(2, clip.notes[i].pitch),
                    "generated passing note left the selected palette");
    }
    const auto again = generate(s, nullptr, cancelled);
    for (size_t i = 0; i < rast.size(); ++i)
        require(similarity(rast[i], again[i]) == 1, "microtonal seed reproducibility");
    s.microEnabled = false;
    for (const auto& clip : generate(s, nullptr, cancelled))
        for (int i = 0; i < clip.count; ++i)
            require(clip.notes[i].cents == 0, "Microtones OFF must stay equal-tempered");
    s.microEnabled = true;
    s.root = 0;

    auto parent = rast[0];
    parent.notes[0].locked = true;
    parent.notes[0].cents = 25;
    parent.notes[1].locked = true;
    s.tuning = 6;
    s.microtones = 1;
    s.mutation = 0.8f;
    s.lockRhythm = true;
    for (const auto& clip : generate(s, &parent, cancelled)) {
        validate(clip);
        require(clip.count == parent.count, "microtonal evolution changed rhythmic template");
        for (int i = 0; i < clip.count; ++i) {
            require(clip.notes[i].start == parent.notes[i].start, "microtonal evolution changed rhythm");
            if (parent.notes[i].locked)
                require(clip.notes[i].locked && clip.notes[i].pitch == parent.notes[i].pitch &&
                            clip.notes[i].cents == parent.notes[i].cents,
                        "locked microtonal note changed");
        }
    }
    s.lockRhythm = false;

    Clip riff;
    riff.length = 8;
    const int riffPitches[]{60, 62, 64, 65, 67, 69, 71, 72, 64, 71};
    for (int i = 0; i < 10; ++i)
        riff.notes[riff.count++] = {i * 0.75, 0.5, riffPitches[i], 90, i == 2};
    s.tuning = 2;
    s.microtones = 1;
    const auto retuned = retuneRegion(riff, 0, 7, s);
    for (int i = 0; i < riff.count; ++i) {
        const bool inside = !riff.notes[i].locked && riff.notes[i].start < 7;
        require(retuned.notes[i].pitch == riff.notes[i].pitch &&
                    retuned.notes[i].start == riff.notes[i].start,
                "retuning moved a note");
        require(retuned.notes[i].cents == (inside ? microOffset(2, riff.notes[i].pitch) : 0),
                "retuning ignored the range, a lock or Rast's forms");
    }
    s.microtones = 0;
    const auto cleared = retuneRegion(retuned, 0, 8, s);
    for (int i = 0; i < riff.count; ++i)
        require(cleared.notes[i].cents == 0, "clearing left a microtonal offset");
    s.tuning = 6;
    s.microtones = 1;
    for (uint32_t seed = 1; seed < 20; ++seed) {
        s.seed = seed;
        require(oneFormPerPitchClass(retuneRegion(riff, 0, 8, s)), "retuning mixed forms");
    }

    // Region edits keep notes outside the range, and a moved note follows its pitch class.
    auto rastPhrase = retuned;
    rastPhrase.notes[2].locked = false;
    rastPhrase.notes[2].cents = -50;
    s.tuning = 2;
    s.scale = 0;
    for (auto action : {RegionAction::MoreMovement, RegionAction::Answer, RegionAction::Resolve}) {
        const auto changed = transformRegion(rastPhrase, 2, 5, action, s);
        validate(changed);
        for (int i = 0; i < rastPhrase.count; ++i) {
            const auto& note = rastPhrase.notes[i];
            if (note.start < 2 || note.start >= 5) {
                bool kept = false;
                for (int j = 0; j < changed.count; ++j)
                    kept |= sameNote(changed.notes[j], note) && changed.notes[j].cents == note.cents;
                require(kept, "microtonal region edit changed a note outside its range");
            }
        }
        for (int i = 0; i < changed.count; ++i) {
            const auto& note = changed.notes[i];
            if (note.start >= 2 && note.start < 5)
                require(note.cents == 0, "region edit invented unscored microtonal offsets");
        }
    }

    auto plain = rastPhrase;
    for (int i = 0; i < plain.count; ++i)
        plain.notes[i].cents = 0;
    require(similarity(plain, rastPhrase) < 1, "similarity ignored microtonal forms");
    const auto combined = combineParts(plain, rastPhrase, plain);
    for (int i = 0; i < combined.count; ++i)
        require(combined.notes[i].cents == rastPhrase.notes[i].cents, "combination lost pitch offsets");
}
void testMetricGrouping() {
    Settings quarters;
    quarters.numerator = 4;
    quarters.denominator = 4;
    Clip clip;
    clip.numerator = 4;
    clip.denominator = 4;
    clip.length = 4;
    clip.grid = 0.25;
    clip.count = 3;
    clip.notes[0] = {0, 0.5, 60, 100, false, 0};
    clip.notes[1] = {1, 0.5, 62, 90, false, 0};
    clip.notes[2] = {1.5, 0.5, 64, 80, false, 0};
    Settings explicitGroups = quarters;
    explicitGroups.groupCount = 4;
    explicitGroups.groupUnit = 4;
    explicitGroups.groups = {1, 1, 1, 1};
    require(fitnessScore(clip, quarters) == fitnessScore(clip, explicitGroups),
            "4/4 default grouping changed quarter-note accents");
    Settings six;
    six.numerator = 6;
    six.denominator = 8;
    require(metricWeight(1.5, six) >= 2, "6/8 second dotted-quarter pulse is not strong");
    require(metricWeight(1.0, six) < 2, "6/8 beat 1.0 should stay off the dotted pulse");
    Clip parent = clip;
    parent.numerator = 7;
    parent.denominator = 8;
    parent.length = 3.5;
    parent.groupCount = 3;
    parent.groupUnit = 8;
    parent.groups = {2, 2, 3};
    Settings evolved;
    evolved.population = 16;
    evolved.generations = 1;
    evolved.seed = 7;
    std::atomic<bool> cancelled{false};
    const auto bank = generate(evolved, &parent, cancelled);
    require(!bank.empty() && bank[0].numerator == 7 && bank[0].denominator == 8 && bank[0].groupCount == 3 &&
                bank[0].groupUnit == 8 && bank[0].groups[0] == 2 && bank[0].groups[1] == 2 &&
                bank[0].groups[2] == 3,
            "evolve did not keep the parent grouping");
}
void runSearchTests();
void runChordGeneratorTests();
void runMelodyAdaptationTests();
void runMusicalArchitectureTests();
void runFuzzTests();
int main(int argc, char** argv) {
    const std::string suite = argc > 1 ? argv[1] : "all";
    if (suite != "all" && suite != "engine" && suite != "harmony" && suite != "search" &&
        suite != "architecture" && suite != "fuzz") {
        std::cerr << "Unknown suite: " << suite << '\n';
        return 2;
    }
    const auto want = [&](const char* name) { return suite == "all" || suite == name; };
    try {
        if (want("search"))
            runSearchTests();
        if (want("harmony")) {
            runChordGeneratorTests();
            runMelodyAdaptationTests();
            testHarmony();
        }
        if (want("architecture"))
            runMusicalArchitectureTests();
        if (want("fuzz"))
            runFuzzTests();
        if (!want("engine")) {
            std::cout << "Engine suite '" << suite << "' passed.\n";
            return 0;
        }
        testMetricGrouping();
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
        s.bars = 12;
        s.scale = 10;
        s.population = 16;
        s.generations = 1;
        s.seed = 7;
        const auto longPhrase = generate(s, nullptr, cancelled);
        require(!longPhrase.empty() && std::abs(longPhrase.front().length - 48.0) < 1e-6,
                "a 12-bar phrase was clamped to eight bars");
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
        testRegionTransforms();
        testGrooveAndCombine();
        testMicrotonal();
        std::cout
            << "Engine: seeds, scales, diversity, locks, rhythm, meters, cancellation, validation, "
               "custom harmony, region transforms, groove, recombination and microtonal tunings passed.\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
