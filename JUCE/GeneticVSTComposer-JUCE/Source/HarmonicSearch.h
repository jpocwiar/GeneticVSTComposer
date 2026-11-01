// Internal implementation, included inside ChordGenerator.cpp's anonymous namespace.
// The empirical prior is intentionally only one term: Bach chorales do not define
// pop, jazz, modal or cinematic style. No source phrase is stored or retrieved.
int triadFamily(int quality) {
    if (quality == 1 || quality == 6 || quality == 12 || quality == 16)
        return 1;
    if (quality == 2 || quality == 9 || quality == 10)
        return 2;
    return 0;
}
double learnedTransition(const Harmony& path, const ChordEvent& chord) {
    if (!path.count)
        return 1.0;
    const auto& previous = path.chords[size_t(path.count - 1)];
    const int family = triadFamily(previous.quality);
    const int outcome = pc(chord.root - previous.root) * 3 + triadFamily(chord.quality);
    const double first = (harmony_model::first[family][outcome] + 0.5) /
                         (harmony_model::firstTotals[family] + 18.0);
    if (path.count < 2)
        return first;
    const int interval = pc(previous.root - path.chords[size_t(path.count - 2)].root);
    const int context = interval * 3 + family;
    return (harmony_model::context[context][outcome] + 6.0 * first) /
           (harmony_model::contextTotals[context] + 6.0);
}
unsigned chordMask(const ChordEvent& chord) {
    unsigned mask = 0;
    for (int interval : intervals[chord.quality])
        if (interval >= 0)
            mask |= 1u << pc(chord.root + interval);
    return mask;
}
int bitCount(unsigned value) {
    int count = 0;
    for (; value; value &= value - 1)
        ++count;
    return count;
}
struct HarmonicCandidate {
    ChordEvent chord;
    double prior = 0;
    unsigned mask = 0;
};
struct HarmonicPlan {
    std::array<int, 64> key{}, mode{}, forcedRoot{}, forcedFamily{};
    int character = 0;
    double originality = 0;
    double colour = 0.45, mood = 0.5;
    bool finalDominant = false;
};
HarmonicPlan planHarmony(const Harmony& timeline, const ChordGenerationSettings& settings, Random& random) {
    HarmonicPlan plan;
    plan.character = std::clamp(settings.character, 0, 4);
    plan.originality = amount(settings.originality);
    plan.colour = amount(settings.colour);
    plan.mood = std::isfinite(settings.mood) ? amount(settings.mood) : 0.5;
    const int key = pc(settings.root), mode = harmonicScale(settings.scale);
    plan.key.fill(key);
    plan.mode.fill(mode);
    plan.forcedRoot.fill(-1);
    plan.forcedFamily.fill(-1);
    const auto& scale = scaleIntervals(mode);
    const bool minor = scale[2] == 3;
    const int count = timeline.count;
    // A local key is sustained for a section, then returns home. It is not a
    // separate random key on every chord. Modal colour usually stays on one centre.
    if (count >= 8 && plan.originality > 0.35 && plan.character != 3 &&
        random.unit() < (plan.originality - 0.25) * (plan.character == 4 ? 1.1 : 0.7)) {
        const int degree = plan.character == 2 ? (random.unit() < 0.5f ? 1 : 4)
                                              : (random.unit() < 0.5f ? 2 : 5);
        const auto destination = degreeChord(scale, degree, key);
        const int localMode = destination.quality == 1 || destination.quality == 2 ? 1 : 0;
        for (int i = std::max(2, count / 3); i < count * 2 / 3; ++i) {
            plan.key[size_t(i)] = destination.root;
            plan.mode[size_t(i)] = localMode;
        }
    }
    // Phrase endpoints constrain a destination, not a canned chord sequence.
    // A question can stop on V or vi; the final answer returns to the tonic.
    const double bar = std::clamp(settings.numerator, 1, 32) * 4.0 /
                       (denominatorValid(settings.denominator) ? settings.denominator : 4);
    const int phraseBars = count >= 12 && random.unit() < 0.35f ? 3 : 4;
    for (int i = 2; i + 2 < count; ++i) {
        const auto& event = timeline.chords[size_t(i)];
        const double end = (event.start + event.duration) / bar;
        if (std::abs(std::remainder(end, double(phraseBars))) < 1e-6 && random.unit() < 0.75f) {
            const auto& localScale = scaleIntervals(plan.mode[size_t(i)]);
            const int degree = plan.character == 3 ? (minor ? 6 : 3)
                               : random.unit() < 0.55f ? 4 : 5;
            plan.forcedRoot[size_t(i)] = pc(plan.key[size_t(i)] + localScale[size_t(degree)]);
        }
    }
    if (count > 0) {
        plan.forcedRoot[0] = key;
        // Pop and jazz may enter on a related function; Balanced remains a clear tonic entry.
        if (count >= 4 && plan.character == 1 && random.unit() < 0.45f)
            plan.forcedRoot[0] = pc(key + scale[5]);
        if (count >= 4 && plan.character == 2 && random.unit() < 0.65f)
            plan.forcedRoot[0] = pc(key + scale[random.unit() < 0.7f ? 1 : 5]);
        if (count >= 4 && plan.character == 4 && random.unit() < 0.4f)
            plan.forcedRoot[0] = pc(key + scale[5]);
        for (int degree = 0; degree < 7; ++degree)
            if (pc(key + scale[size_t(degree)]) == plan.forcedRoot[0])
                plan.forcedFamily[0] = triadFamily(degreeChord(scale, degree, key).quality);
    }
    if (settings.resolve) {
        plan.forcedRoot[size_t(count - 1)] = key;
        plan.forcedFamily[size_t(count - 1)] = minor ? 1 : 0;
        if (count >= 2) {
            int approach = 7;
            if (count > 2) {
                const float choice = random.unit();
                if (plan.character == 3)
                    approach = minor && scale[6] == 10 ? 10 : scale[3];
                else if (plan.character == 4 && choice < 0.45f)
                    approach = scale[3];
                else if (plan.originality > 0.45 && choice < 0.18f)
                    approach = 10; // Backdoor cadence.
                else if (plan.character != 2 && choice < 0.30f)
                    approach = scale[3]; // Plagal answer, preserving the mode's fourth.
            }
            plan.forcedRoot[size_t(count - 2)] = pc(key + approach);
            plan.finalDominant = approach == 7;
            plan.forcedFamily[size_t(count - 2)] = plan.finalDominant ? 0 : -1;
        }
    }
    return plan;
}
std::vector<HarmonicCandidate> harmonicCandidates(const HarmonicPlan& plan, int position) {
    const int key = plan.key[size_t(position)], mode = plan.mode[size_t(position)];
    const auto& scale = scaleIntervals(mode);
    const int character = plan.character;
    const double originality = plan.originality;
    const double colour = plan.colour;
    std::vector<HarmonicCandidate> pool;
    auto add = [&](ChordEvent chord, double prior) {
        const bool suspension = chord.quality == 7 || chord.quality == 8;
        // Colour is an explicit vocabulary limit, even for Jazz and chromatic harmony.
        if ((suspension && colour <= 0.1) || (chord.quality >= 4 && !suspension && colour <= 0.1) ||
            (chord.quality >= 13 && chord.quality <= 15 && colour <= 0.55))
            return;
        if (chord.quality <= 3)
            prior += 0.45 - 1.5 * colour;
        else if (suspension)
            prior += -0.8 + 1.7 * colour;
        else
            prior += -1.4 + 2.8 * colour;
        if (character == 1)
            prior += chord.quality <= 3 ? 0.55 : chord.quality == 13 || suspension ? -0.1 : -0.85;
        else if (character == 2)
            prior += chord.quality <= 3 ? -0.6 : chord.quality >= 13 ? 0.7 : 0.25;
        else if (character == 3)
            prior += suspension ? 1.1 : chord.quality <= 3 ? 0.2 : -1.25;
        else if (character == 4)
            prior += chord.quality == 11 || chord.quality == 12 || chord.quality == 13 ? 0.65 :
                     suspension ? 0.45 : -0.1;
        for (auto& existing : pool)
            if (existing.chord.root == chord.root && existing.chord.quality == chord.quality) {
                existing.prior = std::max(existing.prior, prior);
                return;
            }
        pool.push_back({chord, prior, chordMask(chord)});
    };
    for (int degree = 0; degree < 7; ++degree) {
        auto chord = degreeChord(scale, degree, key);
        add(chord, 0);
        if (colour > 0.1) {
            auto extended = chord;
            extendChord(extended, scale, key);
            if (extended.quality != chord.quality)
                add(extended, -1.2 + 1.7 * colour);
            if (chord.quality <= 1 && colour > 0.2) {
                auto sixth = chord;
                sixth.quality = chord.quality == 0 ? 11 : 12;
                add(sixth, -1.8 + 1.6 * colour + (character == 4 ? 0.3 : 0));
            }
            if (chord.quality == 0 && colour > 0.55) {
                auto ninth = chord;
                ninth.quality = extended.quality == 5 && (character == 2 || colour > 0.8) ? 15 : 13;
                add(ninth, -1.7 + 1.5 * colour + (character == 4 ? 0.4 : 0));
            }
            if (extended.quality == 4 && colour > 0.55) {
                extended.quality = 14;
                add(extended, -1.3 + 1.5 * colour);
            }
        }
        if (colour > 0.1 && chord.quality <= 1) {
            auto suspension = chord;
            unsigned scaleMask = 0;
            for (int interval : scale)
                scaleMask |= 1u << pc(key + interval);
            for (int quality : {7, 8}) {
                suspension.quality = quality;
                if (originality < 0.8 && (chordMask(suspension) & ~scaleMask) != 0)
                    continue;
                add(suspension, -0.9 + colour * 0.7);
            }
        }
    }
    if (originality > 0.25) {
        // Mixture and chromatic mediants belong to a tonal palette, not arbitrary roots.
        const bool minor = scale[2] == 3;
        static constexpr int majorBorrowed[][2]{{3, 0}, {8, 0}, {10, 0}, {5, 1}, {1, 0}};
        static constexpr int minorBorrowed[][2]{{2, 0}, {5, 0}, {1, 0}, {0, 0}, {9, 1}};
        const auto& borrowed = minor ? minorBorrowed : majorBorrowed;
        for (const auto& item : borrowed) {
            ChordEvent chord{0, 4, pc(key + item[0]), item[1]};
            const double prior = -2.4 + 2.3 * originality + (character == 4 ? 0.65 : 0);
            add(chord, prior);
            if (colour > 0.55) {
                chord.quality = item[1] == 1 ? 6 : 5;
                add(chord, prior - 0.3);
            }
        }
    }
    if (originality > 0.45) {
        for (int degree = 0; degree < 7; ++degree) {
            const int target = pc(key + scale[size_t(degree)]);
            add({0, 4, pc(target + 7), colour > 0.1 ? 4 : 0}, -2.0 + originality * 1.5);
            if (character == 2 && colour > 0.6)
                add({0, 4, pc(target + 7), 14}, -1.8 + originality * 1.7);
            if (originality > 0.65) {
                add({0, 4, pc(target - 1), 2}, -2.3 + originality * 1.5);
                add({0, 4, pc(target + 1), colour > 0.1 ? 4 : 0}, -2.6 + originality * 1.7);
            }
        }
    }
    if (originality > 0.7) {
        // At the adventurous end every chromatic root is reachable. This also
        // permits distant triads with Colour at zero, rather than equating novelty
        // with extra chord tones. The transition score controls their placement.
        const double adventure = (originality - 0.7) / 0.3;
        for (int root = 0; root < 12; ++root) {
            const double prior = -3.0 + 2.0 * adventure;
            for (int quality : {0, 1, 2, 3})
                add({0, 4, pc(key + root), quality}, prior - (quality >= 2 ? 0.6 : 0));
            for (int quality : {4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16})
                add({0, 4, pc(key + root), quality}, prior - (quality == 10 || quality == 16 ? 0.6 : 0));
        }
    }
    // Preserve the conventional major dominant in natural/harmonic minor.
    if (plan.finalDominant) {
        add({0, 4, pc(plan.key[0] + 7), 0}, -0.5);
        if (colour > 0.1)
            add({0, 4, pc(plan.key[0] + 7), 4}, -0.8 + colour);
    }
    // The requested ending/entry is always representable, even in a modal scale.
    const int forced = plan.forcedRoot[size_t(position)];
    if (forced >= 0 && plan.forcedFamily[size_t(position)] >= 0)
        add({0, 4, forced, plan.forcedFamily[size_t(position)] == 1 ? 1 : 0}, -0.5 * colour);
    return pool;
}
int harmonicFunction(int root, int key, const std::vector<int>& scale) {
    const int relative = pc(root - key);
    if (relative == 0 || relative == scale[2] || relative == scale[5])
        return 0;
    if (relative == scale[1] || relative == scale[3])
        return 1;
    return 2;
}
double harmonicStepScore(const Harmony& path, const HarmonicCandidate& candidate,
                         const HarmonicPlan& plan, int position, int count) {
    const auto& chord = candidate.chord;
    const int key = plan.key[size_t(position)];
    const auto& scale = scaleIntervals(plan.mode[size_t(position)]);
    unsigned scaleMask = 0;
    for (int interval : scale)
        scaleMask |= 1u << pc(key + interval);
    const double outside = double(bitCount(candidate.mask & ~scaleMask)) / bitCount(candidate.mask);
    double score = candidate.prior - outside * (3.2 - 3.0 * plan.originality);
    // Mood describes brightness, not a promise of a particular listener emotion.
    // Dissonant colour is only available when the independent Colour permits it.
    const double brightness = chord.quality == 1 || chord.quality == 6 || chord.quality == 12 ? -1.0 :
                              chord.quality == 2 || chord.quality == 9 || chord.quality == 10 ? -1.25 :
                              chord.quality == 16 ? -1.35 : chord.quality == 3 ? -0.35 :
                              chord.quality == 7 || chord.quality == 8 ? 0.0 :
                              chord.quality == 11 || chord.quality == 13 ? 1.1 : 0.85;
    score += (plan.mood * 2.0 - 1.0) * brightness * 1.6;
    if (position == 0)
        return score;
    const auto& previous = path.chords[size_t(path.count - 1)];
    const int delta = pc(chord.root - previous.root);
    const double corpusWeight[] = {0.70, 0.38, 0.45, 0.08, 0.20};
    score += corpusWeight[plan.character] * (1.0 - plan.originality * 0.8) *
             std::log(learnedTransition(path, chord));
    // Fifth distance is a useful root-motion measure, independent of voicing.
    // A semitone is five steps away on this circle. High Originality can prefer
    // that remote motion; low Originality softly favours familiar relationships.
    const int fifthSteps = (delta * 7) % 12;
    const int distance = std::min(fifthSteps, 12 - fifthSteps);
    score += distance * (-0.14 + 0.48 * plan.originality * plan.originality);
    const auto common = bitCount(chordMask(previous) & candidate.mask);
    score += common * (plan.character == 3 || plan.character == 4 ? 0.30 : 0.12) *
             (1.0 - plan.originality * 0.65);
    if (delta == 0)
        score -= previous.quality == chord.quality ? 3.8 : 1.1;
    // Functional movement is a soft preference; modal harmony favours shared tones.
    static constexpr double functions[3][3]{{0.0, 0.6, 0.15}, {-0.1, -0.2, 0.9}, {1.0, -0.3, -0.25}};
    const int from = harmonicFunction(previous.root, key, scale);
    const int to = harmonicFunction(chord.root, key, scale);
    score += functions[from][to] * (plan.character == 3 ? 0.1 : plan.character == 1 ? 0.4 : 0.65);
    // Applied dominants and leading-tone approaches earn their colour by resolving.
    if (previous.quality == 4 || previous.quality == 14) {
        if (delta == 5 || delta == 11)
            score += 0.85;
        else if (delta != 0)
            score -= 0.65 * (1.0 - plan.originality * 0.5);
    }
    if ((previous.quality == 2 || previous.quality == 9) && delta == 1)
        score += 0.7;
    if ((previous.quality == 7 || previous.quality == 8) && delta == 0 && chord.quality <= 1)
        score += 1.2;
    if (path.count >= 2 && path.chords[size_t(path.count - 2)].root == chord.root)
        score -= plan.character == 1 || plan.character == 3 ? 0.15 : 0.75;
    int occurrences = 0;
    for (int i = 0; i < path.count; ++i)
        occurrences += path.chords[size_t(i)].root == chord.root;
    score -= std::max(0, occurrences - (count <= 8 ? 1 : count / 5)) * 0.35;
    if (path.count >= 7) {
        bool copiedCell = path.chords[size_t(path.count - 4)].root == chord.root;
        for (int j = 1; j < 4 && copiedCell; ++j)
            copiedCell = path.chords[size_t(path.count - j)].root == path.chords[size_t(path.count - j - 4)].root;
        if (copiedCell)
            score -= plan.character == 1 ? 0.8 : 3.0;
    }
    // Colour builds through each phrase and relaxes at the final answer.
    const double arc = std::sin(3.141592653589793 * double(position) / std::max(1, count - 1));
    const double tension = outside + (chord.quality >= 4 ? 0.25 : 0);
    score -= std::abs(tension - arc * plan.originality * 0.7) * 0.4;
    return score;
}
Harmony searchHarmony(Harmony timeline, const ChordGenerationSettings& settings, Random& random) {
    const auto plan = planHarmony(timeline, settings, random);
    struct Node { Harmony path; double score = 0; };
    struct Expansion { int parent, chord; double score; };
    std::vector<Node> beam(1);
    beam[0].path.length = timeline.length;
    constexpr int beamWidth = 12;
    const int count = timeline.count;
    for (int position = 0; position < count; ++position) {
        const auto pool = harmonicCandidates(plan, position);
        std::vector<Expansion> expanded;
        expanded.reserve(beam.size() * pool.size());
        for (size_t parent = 0; parent < beam.size(); ++parent)
            for (size_t index = 0; index < pool.size(); ++index) {
                const auto& chord = pool[index].chord;
                if (plan.forcedRoot[size_t(position)] >= 0 && chord.root != plan.forcedRoot[size_t(position)])
                    continue;
                if (plan.forcedFamily[size_t(position)] >= 0 &&
                    triadFamily(chord.quality) != plan.forcedFamily[size_t(position)] &&
                    !(position + 1 < count && (chord.quality == 7 || chord.quality == 8)))
                    continue;
                if (position + 2 == count && settings.resolve && plan.finalDominant &&
                    chord.quality != 0 && chord.quality != 4 && chord.quality != 14)
                    continue;
                if (position + 1 == count && !settings.resolve && chord.root == pc(settings.root) && count > 1)
                    continue;
                if (position + 1 == count && settings.resolve && plan.forcedFamily[size_t(position)] == 0 &&
                    chord.quality != 0 && chord.quality != 5 && chord.quality != 11 &&
                    chord.quality != 13 && chord.quality != 15)
                    continue;
                const auto& path = beam[parent].path;
                if (path.count >= 2) {
                    const auto& a = path.chords[size_t(path.count - 1)];
                    const auto& b = path.chords[size_t(path.count - 2)];
                    if (chord.root == a.root && chord.quality == a.quality &&
                        chord.root == b.root && chord.quality == b.quality)
                        continue;
                }
                const double unit = std::clamp(double(random.unit()), 1.0e-7, 1.0 - 1.0e-7);
                const double noise = -std::log(-std::log(unit)) * (0.4 + plan.originality * 0.55);
                expanded.push_back({int(parent), int(index), beam[parent].score + noise +
                    harmonicStepScore(beam[parent].path, pool[index], plan, position, count)});
            }
        if (expanded.empty())
            return {}; // A plan must never silently omit a requested chord position.
        const int keep = std::min(beamWidth, int(expanded.size()));
        std::partial_sort(expanded.begin(), expanded.begin() + keep, expanded.end(),
                          [](const Expansion& a, const Expansion& b) { return a.score > b.score; });
        std::vector<Node> next;
        next.reserve(size_t(keep));
        for (int i = 0; i < keep; ++i) {
            const auto& choice = expanded[size_t(i)];
            auto node = beam[size_t(choice.parent)];
            auto chord = pool[size_t(choice.chord)].chord;
            chord.start = timeline.chords[size_t(position)].start;
            chord.duration = timeline.chords[size_t(position)].duration;
            node.path.chords[size_t(node.path.count++)] = chord;
            node.score = choice.score;
            next.push_back(std::move(node));
        }
        beam = std::move(next);
    }
    return beam[size_t(random.index(std::min(3, int(beam.size()))))].path;
}
