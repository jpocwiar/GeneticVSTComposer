#include "CompositionTools.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <random>

namespace composer {
namespace {
double unit(double value, double fallback = 0.5) {
    return std::isfinite(value) ? std::clamp(value, 0.0, 1.0) : fallback;
}
double actual(const Note& note) {
    return note.pitch + note.cents * 0.01;
}
int pc(int value) {
    return (value % 12 + 12) % 12;
}
bool editable(const Note& note, const Settings& settings) {
    return !note.locked &&
           (!settings.regionOnly || (note.start >= settings.regionStart && note.start < settings.regionEnd));
}
Settings meterFor(const Clip& clip, const Settings& settings) {
    auto meter = settings;
    meter.numerator = clip.numerator;
    meter.denominator = clip.denominator;
    if (clip.groupCount > 0) {
        meter.groups = clip.groups;
        meter.groupCount = clip.groupCount;
        meter.groupUnit = clip.groupUnit;
    }
    return meter;
}
const ChordEvent* chordAt(const Harmony& harmony, double time) {
    if (harmony.count <= 0 || !(harmony.length > 0) || !std::isfinite(time))
        return nullptr;
    time = std::fmod(std::max(0.0, time), harmony.length);
    const ChordEvent* result = nullptr;
    for (int i = 0; i < std::clamp(harmony.count, 0, 64); ++i)
        if (harmony.chords[size_t(i)].start <= time + 1e-8)
            result = &harmony.chords[size_t(i)];
    return result;
}
bool chordTone(int pitch, const ChordEvent& chord) {
    const auto tones = chordPitchClasses(chord);
    return std::find(tones.begin(), tones.end(), pc(pitch)) != tones.end();
}
bool harmonicTarget(const Settings& settings, double time, ChordEvent& result) {
    time += settings.timeOffset;
    if (const auto* chord = chordAt(settings.harmony, time)) {
        result = *chord;
        return true;
    }
    const auto& presets = progressionPresets();
    const int progression = std::clamp(settings.progression, 0, int(presets.size()) - 1);
    const auto& degrees = presets[size_t(progression)].degrees;
    if (degrees.empty())
        return false;
    const double bar = std::max(0.125, settings.numerator * 4.0 / std::max(1, settings.denominator));
    const int degree = degrees[size_t(std::max(0.0, std::floor(time / bar))) % degrees.size()];
    const auto& scale = scaleIntervals(harmonicScale(settings.scale));
    result.root = pc(settings.root + scale[size_t(degree)]);
    const int third = pc(scale[size_t((degree + 2) % 7)] - scale[size_t(degree)]);
    const int fifth = pc(scale[size_t((degree + 4) % 7)] - scale[size_t(degree)]);
    result.quality = third == 3 ? fifth == 6 ? 2 : 1 : fifth == 8 ? 3 : 0;
    return true;
}
bool stable(int pitch, double time, const Settings& settings) {
    ChordEvent chord;
    if (harmonicTarget(settings, time, chord))
        return chordTone(pitch, chord);
    return snapPitch(pitch, settings.root, settings.scale) == pitch;
}
int fitted(int desired, double time, const Settings& settings, bool cadence = false) {
    const int low = std::clamp(settings.low, 0, 126), high = std::clamp(settings.high, low + 1, 127);
    desired = std::clamp(desired, low, high);
    ChordEvent chord;
    const bool hasChord = harmonicTarget(settings, time, chord);
    if (hasChord && (cadence || settings.tension == 0 || metricWeight(time, settings) >= 2)) {
        for (int distance = 0; distance < 128; ++distance) {
            if (desired - distance >= low && chordTone(desired - distance, chord))
                return desired - distance;
            if (desired + distance <= high && chordTone(desired + distance, chord))
                return desired + distance;
        }
    }
    return snapPitch(desired, settings.root, settings.scale, low, high);
}
double medianInterval(const Clip& clip, int begin = 0, int length = maxNotes) {
    std::array<double, maxNotes> intervals{};
    int count = 0;
    const int end = std::min(std::clamp(clip.count, 0, maxNotes), begin + length);
    for (int i = begin + 1; i < end; ++i) {
        const double duration = clip.notes[size_t(i)].start - clip.notes[size_t(i - 1)].start;
        if (duration > 1e-8)
            intervals[size_t(count++)] = duration;
    }
    if (!count)
        return std::max(0.03125, clip.grid);
    std::nth_element(intervals.begin(), intervals.begin() + count / 2, intervals.begin() + count);
    return std::max(0.03125, intervals[size_t(count / 2)]);
}
double gestureQuality(const Clip& clip, const Settings& settings) {
    if (clip.count < 2)
        return 0;
    double result = 0, considered = 0;
    for (int i = 0; i + 1 < clip.count; ++i) {
        const auto& note = clip.notes[size_t(i)];
        const auto& next = clip.notes[size_t(i + 1)];
        const double step = actual(next) - actual(note);
        if (note.cents != 0 || !stable(note.pitch, note.start, settings)) {
            ++considered;
            const bool resolves = next.cents == 0 && stable(next.pitch, next.start, settings) &&
                                  std::abs(step) <= 2.5 && next.start <= note.start + note.duration + 0.5;
            const bool passing = note.duration <= 0.5 && metricWeight(note.start, settings) < 2;
            const bool suspension = note.duration <= 2.0 && next.start <= note.start + 2.0;
            result += resolves && (passing || suspension) ? 1.0 : -0.6;
        }
        if (i > 0) {
            const double previous = actual(note) - actual(clip.notes[size_t(i - 1)]);
            if (std::abs(previous) >= 5.0) {
                ++considered;
                result += previous * step < 0 && std::abs(step) <= 4.0 ? 1.0 : -0.5;
            }
        }
        if (const auto* current = chordAt(settings.harmony, note.start)) {
            const auto* following = chordAt(settings.harmony, note.start + note.duration + 1e-4);
            if (following && following != current && !chordTone(note.pitch, *current) &&
                chordTone(note.pitch, *following) && note.duration <= 1.0) {
                result += 0.6;
                ++considered;
            }
        }
    }
    return considered > 0 ? std::clamp(result / considered, -1.0, 1.0) : 0;
}
double onsetStrength(const Clip& reference, double time, double window) {
    if (reference.count <= 0 || !(reference.length > 0))
        return 0;
    const double local = std::fmod(std::max(0.0, time), reference.length);
    double strength = 0;
    for (int i = 0; i < std::clamp(reference.count, 0, maxNotes); ++i) {
        const auto& note = reference.notes[size_t(i)];
        double distance = std::abs(local - note.start);
        distance = std::min(distance, std::abs(reference.length - distance));
        if (distance < window)
            strength = std::max(strength, (1.0 - distance / window) * note.velocity / 127.0);
    }
    return strength;
}
double contextQuality(const Clip& clip, const CreativeContext& context) {
    if (!clip.count)
        return 0;
    double score = 0;
    if (context.drums.count > 0) {
        double alignment = 0, velocity = 0;
        for (int i = 0; i < clip.count; ++i) {
            const auto& note = clip.notes[size_t(i)];
            const double hit = onsetStrength(context.drums, note.start, std::max(0.04, clip.grid * 0.6));
            alignment += hit;
            velocity += hit * (note.velocity - 64.0) / 63.0;
        }
        const double target = context.role == InstrumentRole::Bass       ? 0.65
                              : context.role == InstrumentRole::Riff     ? 0.50
                              : context.role == InstrumentRole::Arpeggio ? 0.35
                                                                         : 0.20;
        score += 0.7 - 1.5 * std::abs(alignment / clip.count - target) + 0.3 * velocity / clip.count;
    }
    if (context.bass.count > 0 && context.bass.length > 0) {
        double fit = 0, considered = 0;
        for (int i = 0; i < clip.count; ++i) {
            const auto& note = clip.notes[size_t(i)];
            const double time = std::fmod(note.start, context.bass.length);
            for (int j = 0; j < std::clamp(context.bass.count, 0, maxNotes); ++j) {
                const auto& bass = context.bass.notes[size_t(j)];
                if (time < bass.start || time >= bass.start + bass.duration)
                    continue;
                ++considered;
                const int interval = pc(note.pitch - bass.pitch);
                fit += interval == 0 || interval == 3 || interval == 4 || interval == 7 || interval == 9 ? 0.6
                       : interval == 1 || interval == 6 || interval == 11 ? -0.6
                                                                          : 0.0;
                if (context.role != InstrumentRole::Bass && note.pitch < 60 &&
                    std::abs(actual(note) - actual(bass)) < 3.0)
                    fit -= 0.9;
                break;
            }
        }
        if (considered)
            score += fit / considered;
    }
    return std::clamp(score, -2.0, 2.0) * unit(context.contextAmount, 0.6);
}
void retainProtected(Clip& result, const Clip& source, const Settings& settings) {
    if (settings.lockRhythm || settings.mode == 2) {
        // Every protected event keeps its original index and timing in this path.
        result.count = source.count;
        for (int i = 0; i < source.count; ++i) {
            if (!editable(source.notes[size_t(i)], settings))
                result.notes[size_t(i)] = source.notes[size_t(i)];
            else {
                result.notes[size_t(i)].start = source.notes[size_t(i)].start;
                result.notes[size_t(i)].duration = source.notes[size_t(i)].duration;
            }
        }
        return;
    }
    Clip merged = source;
    merged.family = result.family;
    merged.count = 0;
    for (int i = 0; i < source.count; ++i)
        if (!editable(source.notes[size_t(i)], settings))
            merged.notes[size_t(merged.count++)] = source.notes[size_t(i)];
    for (int i = 0; i < result.count && merged.count < maxNotes; ++i) {
        auto note = result.notes[size_t(i)];
        if (!editable(note, settings))
            continue;
        bool collision = false;
        for (int j = 0; j < merged.count; ++j) {
            const auto& fixed = merged.notes[size_t(j)];
            collision |= !editable(fixed, settings) && fixed.pitch == note.pitch &&
                         note.start < fixed.start + fixed.duration &&
                         fixed.start < note.start + note.duration;
        }
        if (!collision)
            merged.notes[size_t(merged.count++)] = note;
    }
    std::stable_sort(merged.notes.begin(), merged.notes.begin() + merged.count,
                     [](const Note& a, const Note& b) { return a.start < b.start; });
    result = merged;
}
} // namespace

const char* roleName(InstrumentRole role) {
    switch (role) {
    case InstrumentRole::Bass:
        return "Bass";
    case InstrumentRole::Riff:
        return "Riff";
    case InstrumentRole::Arpeggio:
        return "Arpeggio";
    default:
        return "Lead";
    }
}
const char* familyName(int family) {
    return family == 0 ? "Development" : family == 1 ? "Response" : family == 2 ? "Contrast" : "Idea";
}

Clip developMotif(const Clip& input, DevelopmentAction action, const Settings& settings, uint32_t seed) {
    auto source = input;
    sanitise(source);
    auto result = source;
    if (!source.count)
        return result;
    std::mt19937 random(seed ? seed : 1u);
    const int operation = int(random() % 5), direction = random() % 2 ? 1 : -1;
    const auto meter = meterFor(source, settings);
    const bool timing = !settings.lockRhythm && settings.mode != 2;
    const int protectedHook =
        settings.creative && settings.creative->preserveHook
            ? std::min(std::clamp(settings.creative->hook.count, 0, maxNotes), std::max(1, source.count / 3))
            : 0;
    const int anchor = source.notes[0].pitch;
    for (int i = 0; i < source.count; ++i) {
        const auto& original = source.notes[size_t(i)];
        auto& note = result.notes[size_t(i)];
        if (!editable(original, settings) || i < protectedHook)
            continue;
        const double phase = original.start / source.length;
        int desired = original.pitch;
        if (action == DevelopmentAction::Develop) {
            if (operation == 0 && phase >= 0.5)
                desired += direction * (phase < 0.75 ? 2 : 4);
            else if (operation == 1 && i % 3 == 2)
                desired = source.notes[size_t(i - 1)].pitch + direction * 2;
            else if (operation == 2 && phase >= 0.5)
                desired = anchor + direction * 5 + (original.pitch - anchor);
            else if (operation == 3 && i % 2)
                desired += direction * 2;
            if (timing && operation >= 3 && phase >= 0.5) {
                const double shift = operation == 3 ? source.grid : -source.grid;
                const double earliest = i > 0 ? source.notes[size_t(i - 1)].start + source.grid : 0;
                const double latest = i + 1 < source.count ? source.notes[size_t(i + 1)].start - source.grid
                                                           : source.length - source.grid;
                if (latest >= earliest)
                    note.start = std::clamp(original.start + shift, earliest, latest);
            }
        } else if (action == DevelopmentAction::Answer) {
            if (phase >= 0.35)
                desired = anchor + direction * (2 + operation) - (original.pitch - anchor);
            if (timing && phase >= 0.75)
                note.duration = std::min(source.length - note.start, original.duration * 1.5);
        } else if (action == DevelopmentAction::Contrast) {
            desired = anchor + direction * (5 + operation) - (original.pitch - anchor);
            if (timing && phase >= 0.25 && i % 2 == 0)
                note.duration = std::max(0.03125, original.duration * 0.5);
        } else if (phase >= 0.65) {
            desired = anchor + int(std::lround((1.0 - phase) * (original.pitch - anchor)));
            if (timing)
                note.duration = std::min(source.length - note.start, original.duration * 1.4);
        }
        if (settings.mode == 1)
            desired = source.notes[0].pitch;
        const bool changed = desired != original.pitch || note.start != original.start;
        note.pitch = settings.mode == 1 || (!changed && settings.tension > 0)
                         ? desired
                         : fitted(desired, note.start, meter,
                                  action == DevelopmentAction::Finish && i == source.count - 1);
        note.cents = !changed && settings.microEnabled && isValidMicrotonalPassing(source, i, settings)
                         ? original.cents
                         : 0;
        const int accent = metricWeight(note.start, meter);
        note.velocity = std::clamp(original.velocity + (accent - 1) * 3 +
                                       (action == DevelopmentAction::Contrast && i % 2 ? 9 : 0),
                                   1, 127);
        if (timing) {
            const double next = i + 1 < source.count ? source.notes[size_t(i + 1)].start : source.length;
            note.duration = std::max(0.03125, std::min(note.duration, std::max(0.03125, next - note.start)));
            if (settings.regionOnly)
                note.duration = std::min(note.duration, settings.regionEnd - note.start);
        }
    }
    if (timing && settings.creative && settings.creative->drums.count > 0 &&
        settings.creative->drums.length > 0) {
        const auto& context = *settings.creative;
        const auto& drums = context.drums;
        const bool anchor = context.role == InstrumentRole::Bass || context.role == InstrumentRole::Riff;
        for (int i = protectedHook; i < source.count; ++i) {
            auto& note = result.notes[size_t(i)];
            if (!editable(source.notes[size_t(i)], settings) ||
                (random() / double(std::mt19937::max())) > unit(context.contextAmount, 0.6))
                continue;
            double best = 0.5, destination = note.start;
            const double origin = std::floor(note.start / drums.length) * drums.length;
            for (int j = 0; j < std::clamp(drums.count, 0, maxNotes); ++j)
                for (int cycle : {-1, 0, 1}) {
                    const double candidate = origin + cycle * drums.length + drums.notes[size_t(j)].start +
                                             (anchor ? 0.0 : source.grid);
                    const double distance = std::abs(candidate - note.start);
                    if (distance < best && candidate >= 0 && candidate < source.length) {
                        best = distance;
                        destination = candidate;
                    }
                }
            const double earliest = i > 0 ? result.notes[size_t(i - 1)].start + 0.03125 : 0;
            const double latest =
                i + 1 < source.count ? source.notes[size_t(i + 1)].start - 0.03125 : source.length - 0.03125;
            if (destination >= earliest && destination <= latest &&
                (!settings.regionOnly ||
                 (destination >= settings.regionStart && destination < settings.regionEnd))) {
                note.start = destination;
                note.duration = std::max(0.03125, std::min(note.duration, latest + 0.03125 - note.start));
                note.velocity = std::clamp(note.velocity + (anchor ? 6 : -3), 1, 127);
            }
        }
    }
    if (settings.creative && settings.creative->role == InstrumentRole::Bass && settings.mode != 1) {
        double centre = 0;
        for (int i = 0; i < source.count; ++i)
            centre += source.notes[size_t(i)].pitch / double(source.count);
        const double target = settings.low + (settings.high - settings.low) * 0.22;
        const int shift = int(std::lround((target - centre) / 12.0)) * 12;
        const int low = std::clamp(settings.low, 0, 126), high = std::clamp(settings.high, low + 1, 127);
        for (int i = 0; i < result.count; ++i)
            if (editable(source.notes[size_t(i)], settings))
                result.notes[size_t(i)].pitch = std::clamp(result.notes[size_t(i)].pitch + shift, low, high);
    }
    // Fragmentation/augmentation alter note-count and rests only in unconstrained material.
    const bool protectedEvents = std::any_of(source.notes.begin(), source.notes.begin() + source.count,
                                             [&](const Note& note) { return !editable(note, settings); });
    if (timing && !protectedEvents && action == DevelopmentAction::Develop && operation == 4) {
        Clip augmented = result;
        augmented.count = 0;
        const double begin = source.length * 0.5;
        for (int i = 0; i < result.count; ++i) {
            auto note = result.notes[size_t(i)];
            if (note.start >= begin) {
                note.start = begin + (note.start - begin) * 2.0;
                note.duration *= 2.0;
            }
            if (note.start < source.length) {
                note.duration = std::min(note.duration, source.length - note.start);
                augmented.notes[size_t(augmented.count++)] = note;
            }
        }
        result = augmented;
    } else if (timing && !protectedEvents && action == DevelopmentAction::Contrast && operation == 0) {
        Clip diminished = result;
        diminished.count = 0;
        for (int repeat = 0; repeat < 2; ++repeat)
            for (int i = 0; i < result.count && diminished.count < maxNotes; ++i) {
                auto note = result.notes[size_t(i)];
                note.start = note.start * 0.5 + repeat * source.length * 0.5;
                note.duration *= 0.5;
                if (repeat)
                    note.pitch = fitted(note.pitch + direction * 2, note.start, meter);
                diminished.notes[size_t(diminished.count++)] = note;
            }
        result = diminished;
    } else if (timing && !protectedEvents && action == DevelopmentAction::Contrast && operation % 2) {
        Clip fragment = result;
        fragment.count = 0;
        const double span = std::max(source.grid * 2, source.length * 0.5);
        for (int i = 0; i < result.count && fragment.count < maxNotes; ++i)
            if (result.notes[size_t(i)].start < span)
                fragment.notes[size_t(fragment.count++)] = result.notes[size_t(i)];
        const int core = fragment.count;
        for (int i = 0; i < core && fragment.count < maxNotes; ++i) {
            auto note = fragment.notes[size_t(i)];
            note.start += span;
            note.pitch = fitted(note.pitch + direction * 3, note.start, meter);
            if (note.start < fragment.length)
                fragment.notes[size_t(fragment.count++)] = note;
        }
        if (fragment.count)
            result = fragment;
    } else if (timing && !protectedEvents && action == DevelopmentAction::Develop && operation == 1) {
        for (int i = protectedHook + 1; i < result.count && result.count < maxNotes; ++i) {
            if (result.notes[size_t(i)].start < source.length * 0.5 ||
                result.notes[size_t(i)].duration < source.grid * 2)
                continue;
            auto note = result.notes[size_t(i)];
            result.notes[size_t(i)].duration *= 0.5;
            note.start += result.notes[size_t(i)].duration;
            note.duration *= 0.5;
            note.pitch = fitted(note.pitch + direction * 2, note.start, meter);
            result.notes[size_t(result.count++)] = note;
            break;
        }
    }
    if (settings.mode == 1)
        for (int i = 0; i < result.count; ++i)
            if (editable(result.notes[size_t(i)], settings)) {
                result.notes[size_t(i)].pitch = source.notes[0].pitch;
                result.notes[size_t(i)].cents = 0;
            }
    result.family = action == DevelopmentAction::Answer ? 1 : action == DevelopmentAction::Contrast ? 2 : 0;
    sanitise(result);
    retainProtected(result, source, settings);
    return result;
}

double musicalSimilarity(const Clip& a, const Clip& b) {
    const int na = std::min(32, std::clamp(a.count, 0, maxNotes));
    const int nb = std::min(32, std::clamp(b.count, 0, maxNotes));
    if (!na || !nb || !(a.length > 0) || !(b.length > 0))
        return 0;
    std::array<double, 33> previous{}, current{};
    previous.fill(1e9);
    previous[0] = 0;
    const auto at = [](const Clip& clip, int index, int count) -> const Note& {
        return clip
            .notes[size_t(count > 1 ? index * (std::clamp(clip.count, 0, maxNotes) - 1) / (count - 1) : 0)];
    };
    for (int i = 1; i <= na; ++i) {
        current.fill(1e9);
        const auto& x = at(a, i - 1, na);
        const double pitchA = actual(x) - actual(a.notes[0]);
        for (int j = 1; j <= nb; ++j) {
            const auto& y = at(b, j - 1, nb);
            const double pitchB = actual(y) - actual(b.notes[0]);
            const double pitchCost = std::min(1.0, std::abs(pitchA - pitchB) / 12.0);
            const double onsetCost = std::min(1.0, std::abs(x.start / a.length - y.start / b.length) * 4.0);
            const double durationCost =
                std::min(1.0, std::abs(x.duration / a.length - y.duration / b.length) * 8.0);
            const double cost = 0.50 * pitchCost + 0.32 * onsetCost + 0.18 * durationCost;
            current[size_t(j)] =
                cost + std::min({previous[size_t(j)], current[size_t(j - 1)], previous[size_t(j - 1)]});
        }
        previous = current;
    }
    const double countDifference = std::abs(na - nb) / double(std::max(na, nb));
    return std::clamp(1.0 - previous[size_t(nb)] / std::max(na, nb) - 0.12 * countDifference, 0.0, 1.0);
}

double motifIdentity(const Clip& phrase, const Clip& hook) {
    const int total = std::clamp(phrase.count, 0, maxNotes),
              count = std::min(12, std::clamp(hook.count, 0, maxNotes));
    if (!total || !count)
        return 0;
    if (count == 1)
        return 1;
    const double hookPulse = medianInterval(hook, 0, count);
    double best = 0;
    const int width = std::min(count, total);
    for (int begin = 0; begin + width <= total; ++begin) {
        const double pulse = medianInterval(phrase, begin, width);
        double similarity = 0;
        for (int i = 1; i < width; ++i) {
            const auto& a = phrase.notes[size_t(begin + i)];
            const auto& beforeA = phrase.notes[size_t(begin + i - 1)];
            const auto& b = hook.notes[size_t(i)];
            const auto& beforeB = hook.notes[size_t(i - 1)];
            const double pitch = std::max(
                0.0, 1.0 - std::abs((actual(a) - actual(beforeA)) - (actual(b) - actual(beforeB))) / 7.0);
            const double duration = std::max(0.0, 1.0 - std::abs((a.start - beforeA.start) / pulse -
                                                                 (b.start - beforeB.start) / hookPulse) /
                                                            2.0);
            similarity += pitch * 0.65 + duration * 0.35;
        }
        best = std::max(best, similarity / std::max(1, width - 1) * std::min(1.0, width / double(count)));
    }
    return std::clamp(best, 0.0, 1.0);
}

std::vector<double> phraseFeatures(const Clip& source, const Settings& settings) {
    auto clip = source;
    sanitise(clip);
    std::vector<double> features(PreferenceModel::featureCount, 0.0);
    if (!clip.count)
        return features;
    const auto musical = analysePhrase(clip, settings);
    const auto meter = meterFor(clip, settings);
    double occupied = 0, micro = 0, harmonic = 0, harmonicWeight = 0;
    int minVelocity = 127, maxVelocity = 0;
    for (int i = 0; i < clip.count; ++i) {
        const auto& note = clip.notes[size_t(i)];
        occupied += note.duration;
        if (note.cents)
            micro += note.duration;
        const double weight = note.duration * (1.0 + metricWeight(note.start, meter) * 0.25);
        harmonicWeight += weight;
        harmonic += note.cents == 0 && stable(note.pitch, note.start, settings) ? weight : 0;
        minVelocity = std::min(minVelocity, note.velocity);
        maxVelocity = std::max(maxVelocity, note.velocity);
    }
    features[0] = unit(clip.count / clip.length / 8.0);
    features[1] = unit(occupied / clip.length);
    features[2] = unit(musical.offbeatRatio);
    features[3] = unit(musical.rhythmVariation / 2.0);
    features[4] = unit(musical.pitchSpan / 36.0);
    features[5] = unit((musical.meanPitch - settings.low) / std::max(1, settings.high - settings.low));
    features[6] = unit(musical.upwardRatio);
    features[7] = unit(musical.repetition);
    features[8] = unit(musical.smallStepRatio);
    features[9] = unit(musical.meanIntervalCents / 1200.0);
    features[10] = unit((maxVelocity - minVelocity) / 96.0);
    features[11] = unit(1.0 - std::min(1.0, occupied / clip.length));
    features[12] = occupied > 0 ? unit(micro / occupied) : 0;
    features[13] = harmonicWeight > 0 ? unit(harmonic / harmonicWeight) : 0;
    features[14] = (gestureQuality(clip, meter) + 1.0) * 0.5;
    features[15] = settings.creative && settings.creative->hook.count > 0
                       ? motifIdentity(clip, settings.creative->hook)
                       : unit(musical.motifReturn);
    return features;
}
void learnPreference(PreferenceModel& model, const Clip& preferred, const Clip& other,
                     const Settings& settings) {
    const auto a = phraseFeatures(preferred, settings), b = phraseFeatures(other, settings);
    double prediction = 0, magnitude = 0;
    for (int i = 0; i < PreferenceModel::featureCount; ++i) {
        if (!std::isfinite(model.weights[size_t(i)]))
            model.weights[size_t(i)] = 0;
        prediction += model.weights[size_t(i)] * (a[size_t(i)] - b[size_t(i)]);
        magnitude += std::abs(a[size_t(i)] - b[size_t(i)]);
    }
    if (magnitude < 1e-10)
        return;
    const double probability = 1.0 / (1.0 + std::exp(-std::clamp(prediction, -30.0, 30.0)));
    const double rate = 0.35 / std::sqrt(1.0 + std::min(model.comparisons, 1000000u) * 0.02);
    for (int i = 0; i < PreferenceModel::featureCount; ++i)
        model.weights[size_t(i)] = std::clamp(model.weights[size_t(i)] * 0.999 +
                                                  rate * (1.0 - probability) * (a[size_t(i)] - b[size_t(i)]),
                                              -4.0, 4.0);
    model.comparisons = std::min(model.comparisons, 999999u) + 1;
}
double preferenceScore(const PreferenceModel& model, const Clip& clip, const Settings& settings) {
    if (!model.comparisons)
        return 0;
    const auto features = phraseFeatures(clip, settings);
    double score = 0;
    for (int i = 0; i < PreferenceModel::featureCount; ++i)
        if (std::isfinite(model.weights[size_t(i)]))
            score += std::clamp(model.weights[size_t(i)], -4.0, 4.0) * (features[size_t(i)] - 0.5);
    return std::tanh(score * 0.5) * std::min(1.0, model.comparisons / 12.0);
}
CreativeScores evaluateCreative(const Clip& clip, const Settings& settings) {
    CreativeScores result;
    if (!settings.creative || clip.count <= 0)
        return result;
    const auto& context = *settings.creative;
    const auto meter = meterFor(clip, settings);
    const auto musical = analysePhrase(clip, meter);
    if (context.preserveHook && context.hook.count > 0) {
        double identity;
        if (settings.mode == 1) {
            auto rhythm = clip, hook = context.hook;
            for (int i = 0; i < std::clamp(rhythm.count, 0, maxNotes); ++i)
                rhythm.notes[size_t(i)].pitch = rhythm.notes[size_t(i)].cents = 0;
            for (int i = 0; i < std::clamp(hook.count, 0, maxNotes); ++i)
                hook.notes[size_t(i)].pitch = hook.notes[size_t(i)].cents = 0;
            identity = motifIdentity(rhythm, hook);
        } else
            identity = motifIdentity(clip, context.hook);
        const double strength = unit(context.identity, 0.75);
        const double target = clip.family == 2   ? 0.25 + strength * 0.30
                              : clip.family == 1 ? 0.45 + strength * 0.35
                                                 : 0.65 + strength * 0.35;
        result.identity = -(2.0 + strength * 4.0) * std::abs(identity - target);
    }
    result.gestures = gestureQuality(clip, meter) * 0.9;
    result.context = contextQuality(clip, context);
    result.preference = 1.25 * preferenceScore(context.preference, clip, settings);
    if (settings.mode == 1) {
        result.gestures = 0;
        return result;
    }
    const double span = std::max(1, settings.high - settings.low);
    const double position = (musical.meanPitch - settings.low) / span;
    const double density = clip.count / std::max(0.125, clip.length);
    switch (context.role) {
    case InstrumentRole::Bass:
        result.role = -1.4 * std::abs(position - 0.22) - 0.6 * std::abs(musical.pitchSpan - 9) / 12.0 -
                      0.3 * std::max(0.0, density - 2.5);
        break;
    case InstrumentRole::Riff:
        result.role = 0.7 * musical.rhythmicRepetition - 0.6 * std::abs(musical.pitchSpan - 7) / 12.0;
        break;
    case InstrumentRole::Arpeggio: {
        double chordFit = 0;
        for (int i = 0; i < clip.count; ++i)
            chordFit +=
                stable(clip.notes[size_t(i)].pitch, clip.notes[size_t(i)].start, settings) ? 1.0 : 0.0;
        result.role =
            0.8 * chordFit / clip.count + 0.2 * std::min(1.0, density / 2.0) - 0.25 * musical.smallStepRatio;
        break;
    }
    default:
        result.role = -0.6 * std::abs(position - 0.55) + 0.35 * musical.smallStepRatio;
        break;
    }
    return result;
}
} // namespace composer
