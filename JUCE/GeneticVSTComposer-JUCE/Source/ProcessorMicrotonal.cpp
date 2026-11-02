#include "PlaybackHelpers.h"
#include "PluginProcessor.h"
#include <cmath>

using Processor = GeneticVSTComposerJUCEAudioProcessor;
namespace {
// MIDI is an output encoding only. Pitches remain degrees / double-precision Hz in the document.
std::pair<int, double> midiPitch(const microtonal::Clip& clip, int degree) {
    const double pitch = 69.0 + 12.0 * std::log2(microtonal::frequency(clip, degree) / 440.0);
    const int key = std::clamp(int(std::lround(pitch)), 0, 127);
    return {key, (pitch - key) * 100.0};
}
juce::ValueTree tuningState(const microtonal::Tuning& t) {
    return juce::ValueTree(
        "PitchSpace", {{"rational", t.rational},
                       {"divisions", t.divisions},
                       {"ratios", t.rational ? juce::String(microtonal::ratioText(t)) : juce::String()}});
}
bool readTuning(const juce::ValueTree& state, microtonal::Tuning& t) {
    if (!state.isValid())
        return false;
    t.rational = state.getProperty("rational", false);
    const double divisions = state.getProperty("divisions", 0);
    if (!std::isfinite(divisions) || divisions < 2 || divisions > microtonal::maxDegrees ||
        std::floor(divisions) != divisions)
        return false;
    t.divisions = int(divisions);
    if (t.rational) {
        const int expected = t.divisions;
        std::string error;
        if (!microtonal::parseRatios(state.getProperty("ratios").toString().toStdString(), t, error) ||
            t.divisions != expected)
            return false;
    }
    return microtonal::valid(t);
}
juce::ValueTree microClipState(const microtonal::Clip& c, const char* type) {
    juce::ValueTree clip(type, {{"baseHz", c.baseHz},
                                {"length", c.length},
                                {"seed", juce::int64(c.seed)},
                                {"numerator", c.numerator},
                                {"denominator", c.denominator},
                                {"grid", c.grid},
                                {"groups", juce::String(composer::groupingText(c.groups, c.groupCount))},
                                {"groupUnit", c.groupUnit},
                                {"polyphonic", c.polyphonic},
                                {"favourite", c.favourite}});
    clip.addChild(tuningState(c.tuning), -1, nullptr);
    for (int i = 0; i < c.count; ++i) {
        const auto& n = c.notes[size_t(i)];
        clip.addChild(juce::ValueTree("Degree", {{"start", n.start},
                                                 {"duration", n.duration},
                                                 {"degree", n.degree},
                                                 {"velocity", n.velocity},
                                                 {"locked", n.locked}}),
                      -1, nullptr);
    }
    return clip;
}
bool readMicroClip(const juce::ValueTree& state, microtonal::Clip& c) {
    if (!state.isValid() || !readTuning(state.getChildWithName("PitchSpace"), c.tuning))
        return false;
    c.baseHz = state.getProperty("baseHz", c.baseHz);
    c.length = state.getProperty("length", c.length);
    c.seed = uint32_t(juce::int64(state.getProperty("seed", 0)));
    c.numerator = state.getProperty("numerator", 4);
    c.denominator = state.getProperty("denominator", 4);
    c.grid = state.getProperty("grid", 0.5);
    c.groupUnit = state.getProperty("groupUnit", 4);
    c.polyphonic = state.getProperty("polyphonic", false);
    c.favourite = state.getProperty("favourite", false);
    const auto grouping = state.getProperty("groups").toString();
    if (grouping.isNotEmpty()) {
        std::string error;
        if (!composer::parseGrouping(grouping.toStdString(), c.groups, c.groupCount, error))
            return false;
    }
    for (const auto& child : state) {
        if (child.hasType("PitchSpace"))
            continue;
        if (!child.hasType("Degree") || c.count >= microtonal::maxEvents)
            return false;
        const double degree = child.getProperty("degree", -1), velocity = child.getProperty("velocity", 0);
        if (!std::isfinite(degree) || degree < 0 || degree > microtonal::maxDegrees * 3 ||
            std::floor(degree) != degree || !std::isfinite(velocity) || velocity < 1 || velocity > 127 ||
            std::floor(velocity) != velocity)
            return false;
        c.notes[size_t(c.count++)] = {double(child.getProperty("start", -1)),
                                      double(child.getProperty("duration", -1)), int(degree), int(velocity),
                                      bool(child.getProperty("locked", false))};
    }
    return microtonal::valid(c);
}
} // namespace
Processor::MicrotonalWorkspace Processor::microtonalSnapshot() const {
    std::lock_guard<std::mutex> lock(documentMutex);
    return microWorkspace;
}
void Processor::publishMicrotonalLocked() {
    juce::SpinLock::ScopedLockType lock(exchangeLock);
    pendingMicro.settings = microWorkspace.settings;
    pendingMicro.clip = microtonalPlaybackClipLocked(microPlaybackSource);
    pendingMicroRevision = microRevision.fetch_add(1) + 1;
}
microtonal::Clip Processor::microtonalPlaybackClipLocked(int source) const {
    if (source == 1)
        return microWorkspace.chords;
    if (source == 2) {
        auto melody = microWorkspace.clip;
        for (int i = 0; i < melody.count; ++i)
            melody.notes[size_t(i)] =
                microtonal::performedEvent(microWorkspace.clip, i, microWorkspace.settings);
        return microtonal::combine(melody, microWorkspace.chords);
    }
    return microWorkspace.clip;
}
void Processor::pushMicrotonalHistoryLocked() {
    microUndo.push_back(microWorkspace);
    if (microUndo.size() > 24)
        microUndo.pop_front();
    microRedo.clear();
}
bool Processor::canUndoMicrotonal() const {
    std::lock_guard<std::mutex> lock(documentMutex);
    return !microUndo.empty();
}
bool Processor::canRedoMicrotonal() const {
    std::lock_guard<std::mutex> lock(documentMutex);
    return !microRedo.empty();
}
void Processor::undoMicrotonal() {
    cancelled.store(true);
    std::lock_guard<std::mutex> lock(documentMutex);
    if (microUndo.empty())
        return;
    microRedo.push_back(microWorkspace);
    microWorkspace = microUndo.back();
    microUndo.pop_back();
    publishMicrotonalLocked();
}
void Processor::redoMicrotonal() {
    cancelled.store(true);
    std::lock_guard<std::mutex> lock(documentMutex);
    if (microRedo.empty())
        return;
    microUndo.push_back(microWorkspace);
    microWorkspace = microRedo.back();
    microRedo.pop_back();
    publishMicrotonalLocked();
}
void Processor::selectMicrotonalVariation(int index) {
    std::lock_guard<std::mutex> lock(documentMutex);
    index = std::clamp(index, 0, microtonal::slotCount - 1);
    if (index == microWorkspace.selected)
        return;
    microWorkspace.selected = index;
    microWorkspace.clip = microWorkspace.variations[size_t(index)];
    microWorkspace.stats.best = microtonal::evaluate(microWorkspace.clip, microWorkspace.settings);
    publishMicrotonalLocked();
}
void Processor::pinMicrotonalVariation(int index) {
    std::lock_guard<std::mutex> lock(documentMutex);
    index = std::clamp(index, 0, microtonal::slotCount - 1);
    pushMicrotonalHistoryLocked();
    auto& clip = microWorkspace.variations[size_t(index)];
    clip.favourite = !clip.favourite;
    if (index == microWorkspace.selected)
        microWorkspace.clip = clip;
    publishMicrotonalLocked();
}
void Processor::setMicrotonalClip(microtonal::Clip clip) {
    if (!microtonal::valid(clip) || clip.polyphonic)
        return;
    cancelled.store(true);
    std::lock_guard<std::mutex> lock(documentMutex);
    pushMicrotonalHistoryLocked();
    microWorkspace.clip = clip;
    microWorkspace.variations[size_t(microWorkspace.selected)] = clip;
    microWorkspace.stats.best = microtonal::evaluate(clip, microWorkspace.settings);
    publishMicrotonalLocked();
}
juce::Result Processor::setMicrotonalChordSettings(const microtonal::ChordSettings& settings) {
    if (!microtonal::valid(settings))
        return juce::Result::fail("Invalid microtonal chord settings.");
    if (microSearchActive.load())
        cancelled.store(true);
    std::lock_guard<std::mutex> lock(documentMutex);
    microWorkspace.chordSettings = settings;
    publishMicrotonalLocked();
    return juce::Result::ok();
}
juce::Result Processor::revoiceMicrotonalChords() {
    cancelled.store(true);
    std::lock_guard<std::mutex> lock(documentMutex);
    const auto result = microtonal::revoiceChords(microWorkspace.chords, microWorkspace.settings,
                                                  microWorkspace.chordSettings);
    if (result.count == 0)
        return juce::Result::fail("Generate chords first, or choose a wider register.");
    pushMicrotonalHistoryLocked();
    microWorkspace.chords = result;
    microWorkspace.chordStats.best =
        microtonal::evaluateChords(result, microWorkspace.settings, microWorkspace.chordSettings);
    publishMicrotonalLocked();
    return juce::Result::ok();
}
juce::Result Processor::setMicrotonalSettings(const microtonal::Settings& s) {
    if (!microtonal::valid(s))
        return juce::Result::fail("Invalid microtonal settings.");
    // A new pitch space applies to the next search; the existing clip retains its exact tuning.
    if (microSearchActive.load())
        cancelled.store(true);
    std::lock_guard<std::mutex> lock(documentMutex);
    microWorkspace.settings = s;
    publishMicrotonalLocked();
    return juce::Result::ok();
}
void Processor::setMicrotonalMode(bool enabled) {
    if (microtonalMode.load() == enabled)
        return;
    panic();
    microtonalMode.store(enabled);
}
void Processor::startMicrotonalAudition(int source) {
    setMicrotonalMode(true);
    auto s = microtonalSnapshot().settings;
    if (s.followHost) {
        s.followHost = false;
        setMicrotonalSettings(s);
    }
    {
        std::lock_guard<std::mutex> lock(documentMutex);
        const auto clip = microtonalPlaybackClipLocked(source);
        if (clip.count == 0) {
            assignStatusLocked("Generate compatible microtonal material before playback.");
            return;
        }
        if (microPlaybackSource != source) {
            microPlaybackSource = source;
            publishMicrotonalLocked();
        }
    }
    setPreview(true);
}
void Processor::stopMicrotonalAudition() {
    auto s = microtonalSnapshot().settings;
    if (s.followHost) {
        s.followHost = false;
        setMicrotonalSettings(s);
    }
    panic();
}
void Processor::performMicrotonalSearch(const SearchJob& job) {
    try {
        auto settings = job.microSettings;
        auto chords = job.microHarmony;
        const bool createChords = job.microAction == 1 || job.microAction == 2;
        auto report = [&](const microtonal::Stats& stats, bool harmony) {
            float value = float(stats.generation) / settings.generations;
            if (!harmony && stats.rhythmGenerations > 0)
                value = stats.evaluations == 0
                            ? 0.5f * float(stats.rhythmGeneration) / stats.rhythmGenerations
                            : 0.5f + value * 0.5f;
            progress.store(job.microAction == 2 ? (harmony ? value * 0.5f : 0.5f + value * 0.5f) : value);
            std::lock_guard<std::mutex> lock(documentMutex);
            if (microRevision.load() == job.startRevision) {
                if (harmony)
                    microWorkspace.chordStats = stats;
                else
                    microWorkspace.stats = stats;
            }
        };
        if (createChords)
            chords = microtonal::generateChords(settings, job.microChords, cancelled,
                                                [&](const microtonal::Stats& stat) { report(stat, true); });
        if ((createChords || job.microAction == 3) && chords.count == 0) {
            setStatus(
                "Generate a valid microtonal chord sequence. Use fewer bars or a slower harmonic rhythm.");
            microSearchActive.store(false);
            busy.store(false);
            return;
        }
        if (job.microAction == 2 || job.microAction == 3) {
            settings.tuning = chords.tuning;
            settings.baseHz = chords.baseHz;
            settings.bars = 0;
            settings.beats = chords.length;
            settings.numerator = chords.numerator;
            settings.denominator = chords.denominator;
            settings.groups = chords.groups;
            settings.groupCount = chords.groupCount;
            settings.groupUnit = chords.groupUnit;
            settings.mode = 0;
            const double bars = chords.length / (chords.numerator * 4.0 / chords.denominator);
            for (int count : {1, 2, 3, 4, 6, 8, 12, 16})
                if (std::abs(bars - count) < 1e-8)
                    settings.bars = count;
        }
        std::vector<microtonal::Clip> results;
        microtonal::Clip clip;
        const bool compatible = chords.count > 0 && microtonal::sameTuning(settings.tuning, chords.tuning) &&
                                settings.baseHz == chords.baseHz;
        if (job.microAction != 1)
            clip = microtonal::generate(
                settings, job.evolve || settings.mode == 2 ? &job.microParent : nullptr, cancelled,
                [&](const microtonal::Stats& stat) { report(stat, false); }, &results,
                compatible ? &chords : nullptr);
        std::lock_guard<std::mutex> lock(documentMutex);
        if (!cancelled.load() && (job.microAction == 1 || clip.count > 0) &&
            microRevision.load() == job.startRevision) {
            pushMicrotonalHistoryLocked();
            if (createChords)
                microWorkspace.chords = chords;
            if (job.microAction == 2 || job.microAction == 3)
                microWorkspace.settings = settings;
            if (job.microAction != 1) {
                int next = 0, best = -1;
                for (int i = 0; i < microtonal::slotCount && next < int(results.size()); ++i)
                    if (!microWorkspace.variations[size_t(i)].favourite) {
                        if (best < 0)
                            best = i;
                        microWorkspace.variations[size_t(i)] = results[size_t(next++)];
                    }
                if (best >= 0)
                    microWorkspace.selected = best;
                microWorkspace.clip = microWorkspace.variations[size_t(microWorkspace.selected)];
            }
            assignStatusLocked(createChords ? "Microtonal chords ready." : "Microtonal melody bank ready.");
            publishMicrotonalLocked();
        }
    } catch (const std::exception& e) {
        setStatus("Generation failed: " + juce::String(e.what()));
    }
    microSearchActive.store(false);
    busy.store(false);
}
void Processor::releaseMicroVoice(int index, int sample, juce::MidiBuffer& midi) {
    auto& v = microVoices[size_t(index)];
    if (v.sound.on)
        midi.addEvent(juce::MidiMessage::noteOff(v.sound.channel, v.key), sample);
    v.sound.on = false;
    v.sound.target = 0;
    v.sound.sourceIndex = -1;
}
void Processor::allMicroOff(int sample, juce::MidiBuffer& midi) {
    for (int i = 0; i < 15; ++i)
        releaseMicroVoice(i, sample, midi);
}
void Processor::startMicroVoice(const microtonal::Clip& clip, const microtonal::Event& n, int sourceIndex,
                                int sample, juce::MidiBuffer& midi) {
    int index = 0;
    if (outputMode == 2) {
        index = microChannel;
        for (int i = 0; i < 15; ++i) {
            const int candidate = (microChannel + i) % 15;
            if (!microVoices[size_t(candidate)].sound.on) {
                index = candidate;
                break;
            }
        }
        microChannel = (index + 1) % 15;
    }
    releaseMicroVoice(index, sample, midi);
    auto& v = microVoices[size_t(index)];
    const auto encoded = midiPitch(clip, n.degree);
    v.key = encoded.first;
    v.sound.channel = outputMode == 2 ? index + 2 : 1;
    midi.addEvent(juce::MidiMessage::pitchWheel(
                      v.sound.channel, bendPosition(encoded.second, outputMode == 2 ? mpeBendRange : 2)),
                  sample);
    midi.addEvent(juce::MidiMessage::noteOn(v.sound.channel, v.key, juce::uint8(n.velocity)), sample);
    v.sound.on = true;
    v.sound.sourceIndex = sourceIndex;
    v.sound.remaining = n.duration;
    v.sound.target = n.velocity / 127.0;
    v.sound.increment = microtonal::frequency(clip, n.degree) / rate;
    ++audioNoteOnSerial;
}
void Processor::processMicrotonalBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi) {
    bool changed = false;
    {
        juce::SpinLock::ScopedTryLockType lock(exchangeLock);
        if (lock.isLocked() && pendingMicroRevision != audioMicroRevision) {
            audioMicro = pendingMicro;
            audioMicroRevision = pendingMicroRevision;
            changed = true;
        }
    }
    const bool entering = !audioMicrotonalMode;
    if (entering) {
        allOff(0, outputMidi);
        stopChordLayer(0, outputMidi);
        resetOutput(0, outputMidi);
        voices = {};
        microVoices = {};
        microChannel = 0;
        chordVoices = {};
        channelNotes = {};
        heldTriggers = {};
        heldTranspose = {};
        trigger = -1;
        transpose = 0;
        outputConfigured = false;
        microWasRunning = false;
        audioMicrotonalMode = true;
        clearVisualState();
    }
    const auto& s = audioMicro.settings;
    const auto& clip = audioMicro.clip;
    double bpm = s.tempo, ppq = 0;
    bool hostPlaying = false, hasPpq = false;
    if (s.followHost || s.hostMeter)
        if (auto* head = getPlayHead())
            if (auto position = head->getPosition()) {
                hostPlaying = s.followHost && position->getIsPlaying();
                if (auto signature = position->getTimeSignature()) {
                    hostNumerator.store(signature->numerator);
                    hostDenominator.store(signature->denominator);
                } else {
                    hostNumerator.store(0);
                    hostDenominator.store(0);
                }
                if (auto b = position->getBpm(); s.followHost && b && std::isfinite(*b))
                    bpm = std::clamp(*b, 20.0, 400.0);
                if (auto p = position->getPpqPosition(); p && std::isfinite(*p)) {
                    ppq = *p;
                    hasPpq = true;
                }
            }
    displayTempo.store(bpm);
    const double step = bpm / (60.0 * rate);
    const bool panicNow = panicRequested.exchange(false);
    bool run = !panicNow && clip.count > 0 && (s.followHost ? hostPlaying : preview.load());
    const int mode = s.mpe || clip.polyphonic ? 2 : 1;
    const bool jump = s.followHost && hasPpq && hostPlaying &&
                      (!microWasHostPlaying || std::abs(ppq - microExpectedPpq) > 2 * step);
    if (entering || changed || jump || run != microWasRunning || mode != outputMode || panicNow) {
        allOff(0, outputMidi);
        if (mode != outputMode)
            resetOutput(0, outputMidi);
        outputMode = mode;
        outputBendRange = 2;
        outputConfigured = false;
        microPhase = s.followHost && hasPpq ? std::fmod(std::max(0.0, ppq), clip.length) : 0;
        nextMicroNote = 0;
        microChannel = 0;
        while (nextMicroNote < clip.count &&
               microtonal::performedEvent(clip, nextMicroNote, s).start < microPhase - step * 0.5 &&
               microtonal::performedEvent(clip, nextMicroNote, s).start +
                       microtonal::performedEvent(clip, nextMicroNote, s).duration <=
                   microPhase)
            ++nextMicroNote;
    }
    if (panicNow) {
        for (int channel = 1; channel <= (mode == 2 ? 16 : 1); ++channel) {
            outputMidi.addEvent(juce::MidiMessage::controllerEvent(channel, 64, 0), 0);
            outputMidi.addEvent(juce::MidiMessage::allNotesOff(channel), 0);
        }
        resetOutput(0, outputMidi);
    }
    if (run && !outputConfigured) {
        microtonalSetup(mode, 2, [&](const juce::MidiMessage& m) { outputMidi.addEvent(m, 0); });
        outputConfigured = true;
    }
    auto incoming = midi.cbegin();
    const auto end = midi.cend();
    float peak = 0;
    for (int sample = 0; sample < audio.getNumSamples(); ++sample) {
        while (incoming != end && (*incoming).samplePosition <= sample) {
            const auto m = (*incoming).getMessage();
            if (m.isAllNotesOff() || m.isAllSoundOff()) {
                allOff(sample, outputMidi);
                resetOutput(sample, outputMidi);
                preview.store(false);
                run = false;
            }
            // This workspace has no 12-TET keyboard triggers or semitone transposition.
            ++incoming;
        }
        for (int voice = 0; voice < 15; ++voice)
            if (microVoices[size_t(voice)].sound.on && microVoices[size_t(voice)].sound.remaining <= 1e-9)
                releaseMicroVoice(voice, sample, outputMidi);
        if (run)
            while (nextMicroNote < clip.count &&
                   microtonal::performedEvent(clip, nextMicroNote, s).start <= microPhase + step * 0.5) {
                auto n = microtonal::performedEvent(clip, nextMicroNote, s);
                if (n.start + n.duration > microPhase) {
                    n.duration = std::min(n.duration, n.start + n.duration - microPhase);
                    startMicroVoice(clip, n, nextMicroNote, sample, outputMidi);
                }
                ++nextMicroNote;
            }
        double signal = 0;
        for (auto& voice : microVoices)
            if (voice.sound.on || voice.sound.gain > 0.00005)
                signal += renderVoice(voice.sound, s.monitor, 2, step);
        const float value = float(std::tanh(signal * 0.65));
        for (int channel = 0; channel < audio.getNumChannels(); ++channel)
            audio.setSample(channel, sample, value);
        peak = std::max(peak, std::abs(value));
        if (run) {
            microPhase += step;
            if (microPhase >= clip.length - step * 0.0001) {
                microPhase = std::fmod(microPhase, clip.length);
                nextMicroNote = 0;
                if (s.evolveEachLoop && !clip.polyphonic)
                    microEvolveRequested.store(true);
            }
        }
    }
    microWasRunning = run;
    microWasHostPlaying = hostPlaying;
    microExpectedPpq = ppq + audio.getNumSamples() * step;
    playing.store(run);
    playBeat.store(run ? microPhase : -1);
    heardChordPreview.store(false);
    heardChordLayer.store(false);
    heardComparison.store(false);
    chordBeat.store(-1);
    midi.swapWith(outputMidi);
    publishVisualState(peak);
    for (const auto& voice : microVoices)
        if (voice.sound.on) {
            visualNotes[size_t(voice.key)].store(float(voice.sound.target), std::memory_order_relaxed);
            visualSourceNotes[size_t(voice.key)].store(voice.sound.sourceIndex, std::memory_order_relaxed);
        }
}
juce::Result Processor::exportMicrotonalMidi(const juce::File& file, int source) const {
    MicroPlaybackSnapshot workspace;
    {
        std::lock_guard<std::mutex> lock(documentMutex);
        workspace.settings = microWorkspace.settings;
        workspace.clip = microtonalPlaybackClipLocked(source);
    }
    const auto& clip = workspace.clip;
    if (clip.count == 0)
        return juce::Result::fail("Generate a microtonal sequence first.");
    juce::MidiMessageSequence sequence;
    auto add = [&](juce::MidiMessage m, double beat) {
        m.setTimeStamp(beat * 960);
        sequence.addEvent(m);
    };
    add(juce::MidiMessage::tempoMetaEvent(int(60000000.0 / workspace.settings.tempo)), 0);
    add(juce::MidiMessage::timeSignatureMetaEvent(clip.numerator, clip.denominator), 0);
    std::array<int, 16> groups = clip.groups;
    int count = clip.groupCount, unit = clip.groupUnit;
    if (count == 0)
        composer::defaultGrouping(clip.numerator, clip.denominator, groups, count, unit);
    add(juce::MidiMessage::textMetaEvent(
            1, "RHYTHMS_GROUPS:" + juce::String(composer::groupingText(groups, count)) + "@" +
                   juce::String(unit)),
        0);
    add(juce::MidiMessage::textMetaEvent(1, "MICROTONAL: " +
                                                (clip.tuning.rational
                                                     ? juce::String(microtonal::ratioText(clip.tuning))
                                                     : juce::String(clip.tuning.divisions) + "-EDO") +
                                                " baseHz=" + juce::String(clip.baseHz, 10)),
        0);
    const int mode = workspace.settings.mpe || clip.polyphonic ? 2 : 1;
    microtonalSetup(mode, 2, [&](const juce::MidiMessage& m) { add(m, 0); });
    std::array<double, 15> freeAt{};
    int cursor = 0;
    for (int i = 0; i < clip.count; ++i) {
        const auto n = microtonal::performedEvent(clip, i, workspace.settings);
        const auto encoded = midiPitch(clip, n.degree);
        int member = 0;
        if (mode == 2) {
            bool found = false;
            for (int k = 0; k < 15; ++k) {
                const int candidate = (cursor + k) % 15;
                if (freeAt[size_t(candidate)] <= n.start + 1e-9) {
                    member = candidate;
                    found = true;
                    break;
                }
            }
            if (!found)
                return juce::Result::fail("This sequence needs more than 15 MPE member channels.");
            freeAt[size_t(member)] = n.start + n.duration;
            cursor = (member + 1) % 15;
        }
        const int channel = mode == 2 ? member + 2 : 1;
        add(juce::MidiMessage::pitchWheel(channel,
                                          bendPosition(encoded.second, mode == 2 ? mpeBendRange : 2)),
            n.start);
        add(juce::MidiMessage::noteOn(channel, encoded.first, juce::uint8(n.velocity)), n.start);
        add(juce::MidiMessage::noteOff(channel, encoded.first), n.start + n.duration);
    }
    // Receiver cleanup follows the last note-off, including clips ending exactly at the boundary.
    const double finish = clip.length + 1.0 / 960;
    if (mode == 2) {
        for (int channel = 2; channel <= 16; ++channel)
            add(juce::MidiMessage::pitchWheel(channel, 8192), finish);
        addRpn([&](const juce::MidiMessage& m) { add(m, finish); }, 1, 6, 0);
    } else
        add(juce::MidiMessage::pitchWheel(1, 8192), finish);
    add(juce::MidiMessage::endOfTrack(), finish);
    sequence.updateMatchedPairs();
    return writeMidiFile(file, sequence, nullptr);
}
juce::ValueTree Processor::microtonalStateLocked() const {
    const auto& s = microWorkspace.settings;
    juce::ValueTree root(
        "MicrotonalWorkspace",
        {{"version", 2}, {"active", microtonalMode.load()}, {"selected", microWorkspace.selected}});
    juce::ValueTree settings("Settings", {{"baseHz", s.baseHz},
                                          {"beats", s.beats},
                                          {"tempo", s.tempo},
                                          {"consonance", s.consonance},
                                          {"diversity", s.diversity},
                                          {"motion", s.motion},
                                          {"density", s.density},
                                          {"mutation", s.mutation},
                                          {"octaves", s.octaves},
                                          {"population", s.population},
                                          {"generations", s.generations},
                                          {"seed", juce::int64(s.seed)},
                                          {"monitor", s.monitor},
                                          {"followHost", s.followHost},
                                          {"mpe", s.mpe}});
    settings.setProperty("bars", s.bars, nullptr);
    settings.setProperty("numerator", s.numerator, nullptr);
    settings.setProperty("denominator", s.denominator, nullptr);
    settings.setProperty("division", s.division, nullptr);
    settings.setProperty("groups", juce::String(composer::groupingText(s.groups, s.groupCount)), nullptr);
    settings.setProperty("groupUnit", s.groupUnit, nullptr);
    settings.setProperty("phraseType", s.phraseType, nullptr);
    settings.setProperty("mode", s.mode, nullptr);
    settings.setProperty("lockRhythm", s.lockRhythm, nullptr);
    settings.setProperty("dynamics", s.dynamics, nullptr);
    settings.setProperty("syncopation", s.syncopation, nullptr);
    settings.setProperty("space", s.space, nullptr);
    settings.setProperty("arousal", s.arousal, nullptr);
    settings.setProperty("weirdness", s.weirdness, nullptr);
    settings.setProperty("swing", s.swing, nullptr);
    settings.setProperty("gate", s.gate, nullptr);
    settings.setProperty("humanize", s.humanize, nullptr);
    settings.setProperty("crossover", s.crossover, nullptr);
    settings.setProperty("hostMeter", s.hostMeter, nullptr);
    settings.setProperty("evolveEachLoop", s.evolveEachLoop, nullptr);
    settings.addChild(tuningState(s.tuning), -1, nullptr);
    root.addChild(settings, -1, nullptr);
    root.addChild(microClipState(microWorkspace.clip, "Sequence"), -1, nullptr);
    root.addChild(microClipState(microWorkspace.chords, "Chords"), -1, nullptr);
    juce::ValueTree bank("Variations");
    for (const auto& c : microWorkspace.variations)
        bank.addChild(microClipState(c, "Variation"), -1, nullptr);
    root.addChild(bank, -1, nullptr);
    const auto& cs = microWorkspace.chordSettings;
    root.addChild(juce::ValueTree("ChordSettings", {{"bars", cs.bars},
                                                    {"rhythm", cs.rhythm},
                                                    {"rhythmShape", cs.rhythmShape},
                                                    {"voices", cs.voices},
                                                    {"voicing", cs.voicing},
                                                    {"originality", cs.originality},
                                                    {"colour", cs.colour},
                                                    {"brightness", cs.brightness},
                                                    {"resolve", cs.resolve}}),
                  -1, nullptr);
    return root;
}
bool Processor::readMicrotonalState(const juce::ValueTree& root, MicrotonalWorkspace& workspace) {
    if (!root.isValid()) {
        workspace = {};
        return true;
    } // Pre-workspace sessions.
    const int version = root.getProperty("version", 0);
    if (version != 1 && version != 2)
        return false;
    const auto state = root.getChildWithName("Settings");
    auto& s = workspace.settings;
    if (!state.isValid() || !readTuning(state.getChildWithName("PitchSpace"), s.tuning))
        return false;
    for (const char* id : {"octaves", "population", "generations"}) {
        const double value = state.getProperty(id, 0);
        if (!std::isfinite(value) || value < 1 || value > 2000 || std::floor(value) != value)
            return false;
    }
    s.baseHz = state.getProperty("baseHz", s.baseHz);
    s.beats = state.getProperty("beats", s.beats);
    s.tempo = state.getProperty("tempo", s.tempo);
    s.consonance = state.getProperty("consonance", s.consonance);
    s.diversity = state.getProperty("diversity", s.diversity);
    s.motion = state.getProperty("motion", s.motion);
    s.density = state.getProperty("density", s.density);
    s.mutation = state.getProperty("mutation", s.mutation);
    s.octaves = state.getProperty("octaves", s.octaves);
    s.population = state.getProperty("population", s.population);
    s.generations = state.getProperty("generations", s.generations);
    s.seed = uint32_t(juce::int64(state.getProperty("seed", 1)));
    s.monitor = state.getProperty("monitor", true);
    s.followHost = state.getProperty("followHost", false);
    s.mpe = state.getProperty("mpe", false);
    s.bars = state.getProperty("bars", 0);
    s.numerator = state.getProperty("numerator", 4);
    s.denominator = state.getProperty("denominator", 4);
    s.division = state.getProperty("division", 8);
    s.groupUnit = state.getProperty("groupUnit", 4);
    s.phraseType = state.getProperty("phraseType", 2);
    s.mode = state.getProperty("mode", 0);
    s.lockRhythm = state.getProperty("lockRhythm", false);
    s.dynamics = state.getProperty("dynamics", 0.5);
    s.syncopation = state.getProperty("syncopation", 0.25);
    s.space = state.getProperty("space", 0.15);
    s.arousal = state.getProperty("arousal", 0.5);
    s.weirdness = state.getProperty("weirdness", 0.1);
    s.swing = state.getProperty("swing", 0.0);
    s.gate = state.getProperty("gate", 1.0);
    s.humanize = state.getProperty("humanize", 0.0);
    s.crossover = state.getProperty("crossover", 0.9);
    s.hostMeter = state.getProperty("hostMeter", false);
    s.evolveEachLoop = state.getProperty("evolveEachLoop", false);
    if (state.getProperty("groups").toString().isNotEmpty()) {
        std::string error;
        if (!composer::parseGrouping(state.getProperty("groups").toString().toStdString(), s.groups,
                                     s.groupCount, error))
            return false;
    }
    if (!microtonal::valid(s))
        return false;
    if (!readMicroClip(root.getChildWithName("Sequence"), workspace.clip) || workspace.clip.polyphonic)
        return false;
    if (version >= 2) {
        const int selected = root.getProperty("selected", -1);
        if (selected < 0 || selected >= microtonal::slotCount)
            return false;
        workspace.selected = selected;
        const auto bank = root.getChildWithName("Variations");
        if (bank.getNumChildren() != microtonal::slotCount)
            return false;
        for (int i = 0; i < microtonal::slotCount; ++i)
            if (!readMicroClip(bank.getChild(i), workspace.variations[size_t(i)]) ||
                workspace.variations[size_t(i)].polyphonic)
                return false;
        if (!readMicroClip(root.getChildWithName("Chords"), workspace.chords) ||
            (workspace.chords.count > 0 && !workspace.chords.polyphonic))
            return false;
        const auto saved = root.getChildWithName("ChordSettings");
        if (!saved.isValid())
            return false;
        auto& cs = workspace.chordSettings;
        cs.bars = saved.getProperty("bars", 0);
        cs.rhythm = saved.getProperty("rhythm", 0);
        cs.rhythmShape = saved.getProperty("rhythmShape", 0);
        cs.voices = saved.getProperty("voices", 3);
        cs.voicing = saved.getProperty("voicing", 0);
        cs.originality = saved.getProperty("originality", 0.5);
        cs.colour = saved.getProperty("colour", 0.45);
        cs.brightness = saved.getProperty("brightness", 0.5);
        cs.resolve = saved.getProperty("resolve", true);
        if (!microtonal::valid(cs))
            return false;
    }
    workspace.variations[size_t(workspace.selected)] = workspace.clip;
    workspace.stats.best = microtonal::evaluate(workspace.clip, s);
    workspace.chordStats.best = microtonal::evaluateChords(workspace.chords, s, workspace.chordSettings);
    return true;
}
