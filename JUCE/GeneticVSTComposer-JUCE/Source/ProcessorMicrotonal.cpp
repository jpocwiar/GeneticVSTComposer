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
} // namespace
Processor::MicrotonalWorkspace Processor::microtonalSnapshot() const {
    std::lock_guard<std::mutex> lock(documentMutex);
    return microWorkspace;
}
void Processor::publishMicrotonalLocked() {
    juce::SpinLock::ScopedLockType lock(exchangeLock);
    pendingMicro = microWorkspace;
    pendingMicroRevision = microRevision.fetch_add(1) + 1;
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
void Processor::startMicrotonalAudition() {
    setMicrotonalMode(true);
    auto s = microtonalSnapshot().settings;
    if (s.followHost) {
        s.followHost = false;
        setMicrotonalSettings(s);
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
        auto clip =
            microtonal::generate(job.microSettings, job.evolve ? &job.microParent : nullptr, cancelled,
                                 [this, &job](const microtonal::Stats& stats) {
                                     progress.store(float(stats.generation) / job.microSettings.generations);
                                     std::lock_guard<std::mutex> lock(documentMutex);
                                     if (microRevision.load() == job.startRevision)
                                         microWorkspace.stats = stats;
                                 });
        std::lock_guard<std::mutex> lock(documentMutex);
        if (!cancelled.load() && clip.count > 0 && microRevision.load() == job.startRevision) {
            microWorkspace.clip = clip;
            publishMicrotonalLocked();
        }
    } catch (const std::exception& e) {
        setStatus("Generation failed: " + juce::String(e.what()));
    }
    microSearchActive.store(false);
    busy.store(false);
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
    if (s.followHost)
        if (auto* head = getPlayHead())
            if (auto position = head->getPosition()) {
                hostPlaying = position->getIsPlaying();
                if (auto b = position->getBpm(); b && std::isfinite(*b))
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
    const int mode = s.mpe ? 2 : 1;
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
        while (nextMicroNote < clip.count &&
               clip.notes[size_t(nextMicroNote)].start < microPhase - step * 0.5 &&
               clip.notes[size_t(nextMicroNote)].start + clip.notes[size_t(nextMicroNote)].duration <=
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
        for (int pitch = 0; pitch < 128; ++pitch)
            if (voices[size_t(pitch)].on && voices[size_t(pitch)].remaining <= 1e-9)
                releaseVoice(pitch, sample, outputMidi);
        if (run)
            while (nextMicroNote < clip.count &&
                   clip.notes[size_t(nextMicroNote)].start <= microPhase + step * 0.5) {
                const auto& n = clip.notes[size_t(nextMicroNote)];
                if (n.start + n.duration > microPhase) {
                    const auto encoded = midiPitch(clip, n.degree);
                    composer::Note transport{
                        n.start,       std::min(n.duration, n.start + n.duration - microPhase),
                        encoded.first, n.velocity,
                        false,         0};
                    noteOnTuned(transport, encoded.second, nextMicroNote, sample, outputMidi);
                }
                ++nextMicroNote;
            }
        double signal = 0;
        for (auto& voice : voices)
            if (voice.on || voice.gain > 0.00005)
                signal += renderVoice(voice, s.monitor, 2, step);
        const float value = float(std::tanh(signal * 0.65));
        for (int channel = 0; channel < audio.getNumChannels(); ++channel)
            audio.setSample(channel, sample, value);
        peak = std::max(peak, std::abs(value));
        if (run) {
            microPhase += step;
            if (microPhase >= clip.length - step * 0.0001) {
                microPhase = std::fmod(microPhase, clip.length);
                nextMicroNote = 0;
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
}
juce::Result Processor::exportMicrotonalMidi(const juce::File& file) const {
    const auto workspace = microtonalSnapshot();
    const auto& clip = workspace.clip;
    if (clip.count == 0)
        return juce::Result::fail("Generate a microtonal sequence first.");
    juce::MidiMessageSequence sequence;
    auto add = [&](juce::MidiMessage m, double beat) {
        m.setTimeStamp(beat * 960);
        sequence.addEvent(m);
    };
    add(juce::MidiMessage::tempoMetaEvent(int(60000000.0 / workspace.settings.tempo)), 0);
    add(juce::MidiMessage::textMetaEvent(1, "MICROTONAL: " +
                                                (clip.tuning.rational
                                                     ? juce::String(microtonal::ratioText(clip.tuning))
                                                     : juce::String(clip.tuning.divisions) + "-EDO") +
                                                " baseHz=" + juce::String(clip.baseHz, 10)),
        0);
    const int mode = workspace.settings.mpe ? 2 : 1;
    microtonalSetup(mode, 2, [&](const juce::MidiMessage& m) { add(m, 0); });
    for (int i = 0; i < clip.count; ++i) {
        const auto& n = clip.notes[size_t(i)];
        const auto encoded = midiPitch(clip, n.degree);
        const int channel = mode == 2 ? 2 + i % 15 : 1;
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
    juce::ValueTree root("MicrotonalWorkspace", {{"version", 1}, {"active", microtonalMode.load()}});
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
    settings.addChild(tuningState(s.tuning), -1, nullptr);
    root.addChild(settings, -1, nullptr);
    const auto& c = microWorkspace.clip;
    juce::ValueTree clip("Sequence",
                         {{"baseHz", c.baseHz}, {"length", c.length}, {"seed", juce::int64(c.seed)}});
    clip.addChild(tuningState(c.tuning), -1, nullptr);
    for (int i = 0; i < c.count; ++i) {
        const auto& n = c.notes[size_t(i)];
        clip.addChild(juce::ValueTree("Degree", {{"start", n.start},
                                                 {"duration", n.duration},
                                                 {"degree", n.degree},
                                                 {"velocity", n.velocity}}),
                      -1, nullptr);
    }
    root.addChild(clip, -1, nullptr);
    return root;
}
bool Processor::readMicrotonalState(const juce::ValueTree& root, MicrotonalWorkspace& workspace) {
    if (!root.isValid()) {
        workspace = {};
        return true;
    } // Pre-workspace sessions.
    if (int(root.getProperty("version", 0)) != 1)
        return false;
    const auto state = root.getChildWithName("Settings");
    auto& s = workspace.settings;
    if (!state.isValid() || !readTuning(state.getChildWithName("PitchSpace"), s.tuning))
        return false;
    for (const char* id : {"octaves", "population", "generations"}) {
        const double value = state.getProperty(id, 0);
        if (!std::isfinite(value) || value < 1 || value > 1000 || std::floor(value) != value)
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
    if (!microtonal::valid(s))
        return false;
    const auto clip = root.getChildWithName("Sequence");
    auto& c = workspace.clip;
    if (!clip.isValid() || !readTuning(clip.getChildWithName("PitchSpace"), c.tuning))
        return false;
    c.baseHz = clip.getProperty("baseHz", c.baseHz);
    c.length = clip.getProperty("length", c.length);
    c.seed = uint32_t(juce::int64(clip.getProperty("seed", 0)));
    for (const auto& child : clip) {
        if (child.hasType("PitchSpace"))
            continue;
        if (!child.hasType("Degree") || c.count >= microtonal::maxEvents)
            return false;
        const double degree = child.getProperty("degree", -1), velocity = child.getProperty("velocity", 0);
        if (!std::isfinite(degree) || degree < 0 || degree > microtonal::maxDegrees * 3 ||
            std::floor(degree) != degree || !std::isfinite(velocity) || velocity < 1 || velocity > 127 ||
            std::floor(velocity) != velocity)
            return false;
        c.notes[size_t(c.count++)] = {
            double(child.getProperty("start", -1)), double(child.getProperty("duration", -1)),
            int(child.getProperty("degree", -1)), int(child.getProperty("velocity", 0))};
    }
    if (!microtonal::valid(c))
        return false;
    workspace.stats.best = microtonal::evaluate(c, s);
    return true;
}
