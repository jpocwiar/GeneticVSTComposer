#pragma once
#include "ComposerEngine.h"

namespace composer {
enum class InstrumentRole { Lead = 0, Bass = 1, Riff = 2, Arpeggio = 3 };
enum class DevelopmentAction { Develop = 0, Answer = 1, Contrast = 2, Finish = 3 };

struct PreferenceModel {
    static constexpr int featureCount = 16;
    std::array<double, featureCount> weights{};
    uint32_t comparisons = 0;
};
struct CreativeContext {
    Clip hook, drums, bass;
    bool preserveHook = false;
    float identity = 0.75f, contextAmount = 0.6f;
    InstrumentRole role = InstrumentRole::Lead;
    PreferenceModel preference;
};
struct CreativeScores {
    double identity = 0, gestures = 0, context = 0, role = 0, preference = 0;
};

const char* roleName(InstrumentRole);
const char* familyName(int family);
// Transform a phrase while retaining its length, metadata, protected onsets and exact locks.
// Keep rhythm/Melody on rhythm preserve every start and duration.
Clip developMotif(const Clip&, DevelopmentAction, const Settings&, uint32_t seed);
// Relative pitch/contour, normalized rhythm and duration; invariant to a whole transposition.
double musicalSimilarity(const Clip&, const Clip&);
// Best local correspondence of a hook's relative intervals and rhythm within a phrase.
double motifIdentity(const Clip&, const Clip& hook);
std::vector<double> phraseFeatures(const Clip&, const Settings&);
void learnPreference(PreferenceModel&, const Clip& preferred, const Clip& other, const Settings&);
double preferenceScore(const PreferenceModel&, const Clip&, const Settings&);
CreativeScores evaluateCreative(const Clip&, const Settings&);
} // namespace composer
