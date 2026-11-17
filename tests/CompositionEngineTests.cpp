#include "CompositionTools.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <stdexcept>

namespace {
using namespace composer;
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
Clip motif() {
    Clip clip;
    clip.length = 8;
    clip.count = 12;
    clip.seed = 123;
    constexpr int pitches[]{60, 64, 67, 65, 62, 64, 67, 72, 70, 67, 65, 60};
    for (int i = 0; i < clip.count; ++i)
        clip.notes[size_t(i)] = {i * 0.625, i % 3 == 0 ? 0.5 : 0.375, pitches[i], 80 + i, false, 0};
    return clip;
}
bool sameNote(const Note& a, const Note& b) {
    return a.start == b.start && a.duration == b.duration && a.pitch == b.pitch && a.cents == b.cents &&
           a.velocity == b.velocity && a.locked == b.locked;
}
bool sameClip(const Clip& a, const Clip& b) {
    if (a.count != b.count || a.length != b.length || a.family != b.family)
        return false;
    for (int i = 0; i < a.count; ++i)
        if (!sameNote(a.notes[size_t(i)], b.notes[size_t(i)]))
            return false;
    return true;
}
void testDevelopmentAndProtection() {
    auto source = motif();
    source.favourite = true;
    source.notes[4].locked = true;
    source.notes[4].cents = 25;
    Settings settings;
    settings.regionOnly = true;
    settings.regionStart = 2;
    settings.regionEnd = 6;
    auto context = std::make_shared<CreativeContext>();
    context->hook = source;
    context->hook.count = 3;
    context->hook.length = 2;
    context->preserveHook = true;
    settings.creative = context;
    bool different = false;
    for (auto action : {DevelopmentAction::Develop, DevelopmentAction::Answer, DevelopmentAction::Contrast,
                        DevelopmentAction::Finish}) {
        const auto changed = developMotif(source, action, settings, 41);
        check(sameClip(changed, developMotif(source, action, settings, 41)),
              "motif development is not deterministic");
        check(changed.length == source.length && changed.seed == source.seed && changed.favourite,
              "development changed phrase metadata");
        for (int i = 0; i < source.count; ++i) {
            const auto& note = source.notes[size_t(i)];
            if (note.locked || note.start < 2 || note.start >= 6)
                check(std::any_of(changed.notes.begin(), changed.notes.begin() + changed.count,
                                  [&](const Note& n) { return sameNote(note, n); }),
                      "development changed a protected note");
        }
        different |= musicalSimilarity(source, changed) < 0.999;
        settings.lockRhythm = true;
        const auto timed = developMotif(source, action, settings, 7);
        check(timed.count == source.count, "development changed a locked rhythm's note count");
        for (int i = 0; i < source.count; ++i)
            check(timed.notes[size_t(i)].start == source.notes[size_t(i)].start &&
                      timed.notes[size_t(i)].duration == source.notes[size_t(i)].duration,
                  "development changed locked timing");
        settings.lockRhythm = false;
    }
    check(different, "development actions did not develop the phrase");
    settings.regionOnly = false;
    source.notes[4].locked = false;
    bool augmented = false, diminished = false;
    for (uint32_t seed = 1; seed <= 20; ++seed) {
        augmented |= developMotif(source, DevelopmentAction::Develop, settings, seed).count < source.count;
        diminished |= developMotif(source, DevelopmentAction::Contrast, settings, seed).count > source.count;
    }
    check(augmented && diminished, "development never produced rhythmic augmentation or diminution");
}
void testRelativeIdentityAndPreferences() {
    const auto original = motif();
    auto shifted = original;
    for (int i = 0; i < shifted.count; ++i)
        shifted.notes[size_t(i)].pitch += 7;
    check(std::abs(musicalSimilarity(original, shifted) - 1.0) < 1e-12,
          "musical similarity did not recognize a transposed motif");
    check(std::abs(motifIdentity(shifted, original) - 1.0) < 1e-12,
          "hook identity did not recognize relative intervals");
    auto irregular = original;
    for (int i = 0; i < irregular.count; ++i)
        irregular.notes[size_t(i)].start = i * 0.5 + (i % 3 == 1 ? 0.125 : 0);
    auto augmented = irregular;
    augmented.length *= 2;
    for (int i = 0; i < augmented.count; ++i) {
        augmented.notes[size_t(i)].start *= 2;
        augmented.notes[size_t(i)].duration *= 2;
        augmented.notes[size_t(i)].pitch += 5;
    }
    check(std::abs(motifIdentity(augmented, irregular) - 1.0) < 1e-12,
          "hook identity lost an irregular rhythm after augmentation and transposition");
    auto unrelated = original;
    for (int i = 0; i < unrelated.count; ++i) {
        unrelated.notes[size_t(i)].pitch = 48 + (i * 11) % 36;
        unrelated.notes[size_t(i)].start = i * 0.5;
        unrelated.notes[size_t(i)].duration = 0.125;
    }
    check(musicalSimilarity(original, unrelated) < 0.85,
          "an unrelated rhythm and contour were classified as the same idea");
    Settings settings;
    PreferenceModel model;
    check(preferenceScore(model, original, settings) == 0, "an untrained preference model changed ranking");
    for (int i = 0; i < 80; ++i)
        learnPreference(model, original, unrelated, settings);
    check(model.comparisons == 80 &&
              preferenceScore(model, original, settings) > preferenceScore(model, unrelated, settings),
          "pairwise preference learning failed to favor chosen material");
    for (double value : model.weights)
        check(std::isfinite(value) && std::abs(value) <= 4, "preference learning produced invalid weights");
    const auto count = model.comparisons;
    learnPreference(model, original, original, settings);
    check(model.comparisons == count, "identical material added a meaningless preference comparison");
}
void testContextAndGestures() {
    Settings settings;
    auto context = std::make_shared<CreativeContext>();
    context->role = InstrumentRole::Bass;
    context->contextAmount = 1;
    context->drums.length = 4;
    context->drums.count = 4;
    Clip onPulse;
    onPulse.length = 4;
    onPulse.count = 4;
    for (int i = 0; i < 4; ++i) {
        context->drums.notes[size_t(i)] = {double(i), 0.1, 36, 110, false, 0};
        onPulse.notes[size_t(i)] = {double(i), 0.5, 48, 100, false, 0};
    }
    auto between = onPulse;
    for (int i = 0; i < 4; ++i)
        between.notes[size_t(i)].start += 0.25;
    settings.creative = context;
    check(evaluateCreative(onPulse, settings).context > evaluateCreative(between, settings).context,
          "bass rhythm did not respond to the drum reference");
    context->drums.count = 0;
    context->bass.length = 4;
    context->bass.count = 1;
    context->bass.notes[0] = {0, 4, 48, 100, false, 0};
    context->role = InstrumentRole::Lead;
    auto clearRegister = onPulse;
    for (int i = 0; i < clearRegister.count; ++i)
        clearRegister.notes[size_t(i)].pitch += 24;
    check(evaluateCreative(clearRegister, settings).context > evaluateCreative(onPulse, settings).context,
          "melody context did not distinguish a low-register bass collision");
    Clip recovery;
    recovery.length = 4;
    recovery.count = 3;
    recovery.notes[0] = {0, 0.5, 60, 90, false, 0};
    recovery.notes[1] = {0.5, 0.5, 72, 90, false, 0};
    recovery.notes[2] = {1, 0.5, 71, 90, false, 0};
    auto continuingLeap = recovery;
    continuingLeap.notes[2].pitch = 84;
    check(evaluateCreative(recovery, settings).gestures > evaluateCreative(continuingLeap, settings).gestures,
          "musical gestures did not favor recovery after a large leap");
}
void testCreativeSearch() {
    Settings settings;
    settings.population = 24;
    settings.generations = 8;
    settings.bars = 2;
    settings.seed = 12;
    auto context = std::make_shared<CreativeContext>();
    context->hook = motif();
    context->hook.count = 4;
    context->hook.length = 2.5;
    context->preserveHook = true;
    settings.creative = context;
    std::atomic<bool> cancelled{false};
    SearchStats last;
    const auto a = generate(settings, nullptr, cancelled, {}, [&](const SearchStats& s) { last = s; });
    const auto b = generate(settings, nullptr, cancelled);
    check(a.size() == 12 && b.size() == 12, "creative search did not fill its three families");
    double best = -1e100;
    for (int i = 0; i < 12; ++i) {
        check(a[size_t(i)].family == i / 4, "creative bank family was lost during search");
        check(sameClip(a[size_t(i)], b[size_t(i)]), "creative search is not deterministic");
        double sum = 0;
        for (const auto& term : fitnessBreakdown(a[size_t(i)], settings))
            sum += term.value;
        check(std::abs(sum - fitnessScore(a[size_t(i)], settings)) < 1e-9,
              "creative score explanation no longer adds up");
        best = std::max(best, fitnessScore(a[size_t(i)], settings));
    }
    check(std::abs(best - last.bestFitness) < 1e-9, "creative bank contains no evaluated global winner");
    check(last.evaluations == 24 * 9, "creative search silently changed the evaluation budget");
    context->hook.notes[1].cents = 25;
    settings.mode = 1;
    for (const auto& clip : generate(settings, nullptr, cancelled))
        for (int i = 0; i < clip.count; ++i)
            check(clip.notes[size_t(i)].pitch == clip.notes[0].pitch && clip.notes[size_t(i)].cents == 0,
                  "creative rhythm-only generation inherited hook pitches or cents");
    settings.mode = 0;
    context->hook.notes[1].cents = 0;
    const auto parent = motif();
    settings.lockRhythm = true;
    for (const auto& clip : generate(settings, &parent, cancelled)) {
        check(clip.count == parent.count, "creative evolution lost a rhythmic template");
        for (int i = 0; i < parent.count; ++i)
            check(clip.notes[size_t(i)].start == parent.notes[size_t(i)].start &&
                      clip.notes[size_t(i)].duration == parent.notes[size_t(i)].duration,
                  "creative evolution changed protected timing");
    }
    settings.lockRhythm = false;
    settings.creative = std::make_shared<CreativeContext>();
    auto bassContext = std::make_shared<CreativeContext>();
    bassContext->role = InstrumentRole::Bass;
    auto bassSettings = settings;
    bassSettings.creative = bassContext;
    const auto leads = generate(settings, nullptr, cancelled);
    const auto basses = generate(bassSettings, nullptr, cancelled);
    double leadPitch = 0, bassPitch = 0;
    for (const auto& clip : leads)
        leadPitch += analysePhrase(clip, settings).meanPitch;
    for (const auto& clip : basses)
        bassPitch += analysePhrase(clip, bassSettings).meanPitch;
    check(bassPitch < leadPitch, "bass and lead roles produced the same register behavior");
    cancelled.store(true);
    check(generate(settings, nullptr, cancelled).empty(), "cancelled creative search returned results");
}
} // namespace

void runCompositionEngineTests() {
    testDevelopmentAndProtection();
    testRelativeIdentityAndPreferences();
    testContextAndGestures();
    testCreativeSearch();
}
