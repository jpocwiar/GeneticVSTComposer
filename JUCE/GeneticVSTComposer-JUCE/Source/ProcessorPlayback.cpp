#include "PluginProcessor.h"
#include "PlaybackHelpers.h"
#include <cmath>

using Processor = GeneticVSTComposerJUCEAudioProcessor;

composer::Note Processor::renderedNote(composer::Note n, const composer::Clip& clip, int transposition,
                                       int index) const {
    const double swing = cached.swing->load();
    n.start = swingTime(n.start, clip.grid, swing);
    const float humanize = cached.humanize->load();
    if (humanize > 0.0001f && index >= 0) {
        uint32_t hash = clip.seed * 2246822519u ^ uint32_t(index) * 3266489917u ^ uint32_t(n.pitch) * 668265263u;
        auto unit = [&] {
            hash = hash * 1664525u + 1013904223u;
            return (hash >> 8) / 16777216.0;
        };
        const double shift = (unit() * 2.0 - 1.0) * humanize * clip.grid * 0.45;
        const double previous = index > 0 ? swingTime(clip.notes[index - 1].start, clip.grid, swing) + 0.001 : 0.0;
        const double following = index + 1 < clip.count
                                     ? swingTime(clip.notes[index + 1].start, clip.grid, swing) - 0.001
                                     : clip.length - 0.001;
        n.start = following > previous ? std::clamp(n.start + shift, previous, following) : n.start;
        n.velocity = std::clamp(n.velocity + int(std::lround((unit() * 2.0 - 1.0) * humanize * 24.0)), 1, 127);
    }
    n.start = std::min(n.start, clip.length - 0.001);
    n.duration = std::max(0.001, std::min(n.duration * cached.gate->load(), clip.length - n.start));
    n.pitch = juce::jlimit(0, 127, n.pitch + transposition);
    // With the tuning off, stored offsets are kept but the output is plain 12-TET.
    const int tuning = tuningIndex();
    if (tuning == 0)
        n.cents = 0;
    // Fit to scale is an explicit performance projection, including live transposition.
    // It never invents offsets after the search; optimized passing-note cents are retained.
    if (cached.snap->load() > 0.5f)
        n.pitch = composer::snapPitch(n.pitch, int(cached.root->load()), scaleIndex());

    return n;
}
void Processor::releaseVoice(int pitch, int sample, juce::MidiBuffer& midi) {
    auto& v = voices[size_t(pitch)];
    midi.addEvent(juce::MidiMessage::noteOff(v.channel, pitch), sample);
    channelNotes[size_t(v.channel)] = std::max(0, channelNotes[size_t(v.channel)] - 1);
    channelUse[size_t(v.channel)] = ++channelClock;
    v.on = false;
    v.sourceIndex = -1;
    v.target = 0;
}
void Processor::allOff(int sample, juce::MidiBuffer& midi) {
    for (int p = 0; p < 128; ++p)
        if (voices[p].on)
            releaseVoice(p, sample, midi);
    allMicroOff(sample, midi);
}
// Centres the bends the current output used; leaving MPE also clears its zone, so a receiver
// returns to ordinary channel-1 playback.
void Processor::resetOutput(int sample, juce::MidiBuffer& midi) {
    if (outputMode == 1)
        midi.addEvent(juce::MidiMessage::pitchWheel(1, 8192), sample);
    if (outputMode == 2) {
        for (int channel = 2; channel <= 16; ++channel)
            midi.addEvent(juce::MidiMessage::pitchWheel(channel, 8192), sample);
        addRpn([&](const juce::MidiMessage& m) { midi.addEvent(m, sample); }, 1, 6, 0);
    }
}
void Processor::processBlockBypassed(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi) {
    audio.clear();
    if (audio.getNumSamples() > 0) {
        allOff(0, midi);
        stopChordLayer(0, midi);
    } else
        chordMidiSounding = {};
    voices = {};
    microVoices = {};
    chordVoices = {};
    chordLayerPlaying = false;
    channelNotes = {};
    outputConfigured = false;
    heldTriggers = {};
    heldTranspose = {};
    trigger = -1;
    transpose = 0;
    wasPlaying = false;
    wasHostPlaying = false;
    microWasRunning = microWasHostPlaying = false;
    preview.store(false);
    chordPreview.store(false);
    heardChordPreview.store(false);
    audioChordPreview = false;
    playing.store(false);
    playBeat.store(-1);
    clearVisualState();
}
void Processor::noteOn(const composer::Note& n, int sourceIndex, int sample, juce::MidiBuffer& midi) {
    noteOnTuned(n, n.cents, sourceIndex, sample, midi);
}
void Processor::noteOnTuned(const composer::Note& n, double cents, int sourceIndex, int sample,
                            juce::MidiBuffer& midi) {
    auto& v = voices[n.pitch];
    if (v.on)
        releaseVoice(n.pitch, sample, midi);
    int channel = 1;
    if (outputMode == 2) {
        // An idle member channel with the oldest release tail; shared only when all are busy.
        channel = 2;
        for (int candidate = 3; candidate <= 16; ++candidate)
            if (std::make_pair(channelNotes[size_t(candidate)] > 0, channelUse[size_t(candidate)]) <
                std::make_pair(channelNotes[size_t(channel)] > 0, channelUse[size_t(channel)]))
                channel = candidate;
    }
    ++channelNotes[size_t(channel)];
    channelUse[size_t(channel)] = ++channelClock;
    if (outputMode != 0)
        midi.addEvent(juce::MidiMessage::pitchWheel(
                          channel, bendPosition(cents, outputMode == 2 ? mpeBendRange : outputBendRange)),
                      sample);
    v.on = true;
    v.channel = channel;
    v.sourceIndex = sourceIndex;
    v.remaining = n.duration;
    v.target = n.velocity / 127.0;
    v.increment = phaseIncrements[size_t(n.pitch)] * std::exp2(cents / 1200.0);
    midi.addEvent(juce::MidiMessage::noteOn(channel, n.pitch, juce::uint8(n.velocity)), sample);
    ++audioNoteOnSerial;
}
void Processor::releaseChordMidi(int sample, juce::MidiBuffer& midi) noexcept {
    for (int pitch = 0; pitch < 128; ++pitch)
        if (chordMidiSounding[size_t(pitch)]) {
            midi.addEvent(juce::MidiMessage::noteOff(16, pitch), sample);
            chordMidiSounding[size_t(pitch)] = false;
        }
}
void Processor::stopChordLayer(int sample, juce::MidiBuffer& midi) noexcept {
    for (auto& voice : chordVoices) {
        voice.on = false;
        voice.target = 0;
        voice.sourceIndex = -1;
    }
    releaseChordMidi(sample, midi);
    chordLayerPlaying = false;
    lastChordPhase = -1;
}
void Processor::scheduleChordLayer(bool run, bool restart, double beat, double step, int sample,
                                   juce::MidiBuffer& midi) noexcept {
    const bool sendChordMidi = cached.chordMidiOut->load() > 0.5f && outputMode != 2;
    if (!sendChordMidi)
        releaseChordMidi(sample, midi);
    if (!run || audioChords.count == 0) {
        if (chordLayerPlaying)
            stopChordLayer(sample, midi);
        return;
    }
    const double position = std::fmod(std::max(0.0, beat), audioChords.length);
    const bool chase = restart || !chordLayerPlaying || position + step * 0.5 < lastChordPhase;
    if (chase) {
        stopChordLayer(sample, midi);
        nextChordNote = 0;
    }
    chordLayerPlaying = true;
    for (int pitch = 0; pitch < 128; ++pitch) {
        auto& voice = chordVoices[size_t(pitch)];
        if (voice.on && voice.remaining <= 1e-9) {
            voice.on = false;
            voice.target = 0;
            voice.sourceIndex = -1;
            if (chordMidiSounding[size_t(pitch)]) {
                midi.addEvent(juce::MidiMessage::noteOff(16, pitch), sample);
                chordMidiSounding[size_t(pitch)] = false;
            }
        }
    }
    while (nextChordNote < audioChords.count) {
        const auto& n = audioChords.notes[nextChordNote];
        if (n.start > position + step * 0.5)
            break;
        const double remaining = n.start + n.duration - position;
        if (remaining > step * 0.5 && (chase || n.start >= position - step * 1.5)) {
            auto& voice = chordVoices[size_t(n.pitch)];
            voice.on = true;
            voice.target = n.velocity / 127.0;
            voice.remaining = remaining;
            voice.increment = phaseIncrements[size_t(n.pitch)];
            voice.sourceIndex = nextChordNote;
            ++chordNoteOnSerial;
            if (sendChordMidi) {
                if (chordMidiSounding[size_t(n.pitch)])
                    midi.addEvent(juce::MidiMessage::noteOff(16, n.pitch), sample);
                midi.addEvent(juce::MidiMessage::noteOn(16, n.pitch, juce::uint8(n.velocity)), sample);
                chordMidiSounding[size_t(n.pitch)] = true;
            }
        }
        ++nextChordNote;
    }
    if (sendChordMidi)
        for (int pitch = 0; pitch < 128; ++pitch) {
            const auto& voice = chordVoices[size_t(pitch)];
            if (!voice.on || chordMidiSounding[size_t(pitch)])
                continue;
            const int velocity = voice.sourceIndex >= 0 && voice.sourceIndex < audioChords.count
                                     ? audioChords.notes[voice.sourceIndex].velocity
                                     : juce::jlimit(1, 127, int(std::lround(voice.target * 127.0)));
            midi.addEvent(juce::MidiMessage::noteOn(16, pitch, juce::uint8(velocity)), sample);
            chordMidiSounding[size_t(pitch)] = true;
        }
    lastChordPhase = position;
}
double Processor::renderVoice(Voice& v, bool monitor, int sound, double step) noexcept {
    if (v.on)
        v.remaining -= step;
    const double target = monitor ? v.target : 0.0;
    v.gain += (target - v.gain) * (target > v.gain ? 0.004 : 0.0015);
    if (v.gain <= 0.00005)
        return 0;
    v.phase += v.increment;
    v.phase -= std::floor(v.phase);
    const double angle = juce::MathConstants<double>::twoPi * v.phase;
    const double fundamental = std::sin(angle);
    const double tone = sound == 1 ? fundamental
                        : sound == 2
                            ? (fundamental + 0.28 * std::sin(angle * 2) + 0.12 * std::sin(angle * 3)) / 1.12
                            : fundamental + 0.22 * std::sin(angle * 2.0);
    return v.gain * tone * 0.1;
}
void Processor::processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    audio.clear();
    outputMidi.clear();
    const int samples = audio.getNumSamples();
    if (samples <= 0)
        return;
    if (microtonalMode.load()) {
        processMicrotonalBlock(audio, midi);
        return;
    }
    if (audioMicrotonalMode) {
        allOff(0, outputMidi);
        resetOutput(0, outputMidi);
        voices = {};
        microVoices = {};
        channelNotes = {};
        outputMode = 0;
        outputConfigured = false;
        wasPlaying = false;
        audioSelection = -1;
        audioMicrotonalMode = false;
        microWasRunning = false;
    }
    bool bankChanged = false, chordsChanged = false, regionChanged = false;
    {
        juce::SpinLock::ScopedTryLockType lock(exchangeLock);
        if (lock.isLocked() && pendingRegionSerial != audioRegionSerial) {
            audioRequestedRegion = pendingRegion;
            const bool switchingLater = !audioChordPreview && wasPlaying &&
                                        cached.quantiseSwitch->load() > 0.5f &&
                                        selected.load() != audioSelection;
            if (!switchingLater) {
                regionChanged = (audioRegion.enabled && audioRegion.slot == audioSelection) ||
                                (audioRequestedRegion.enabled && audioRequestedRegion.slot == selected.load());
                audioRegion = audioRequestedRegion;
            }
            audioRegionSerial = pendingRegionSerial;
        }
        if (lock.isLocked() && pendingRevision != audioRevision) {
            const int slot = juce::jlimit(0, 11, audioSelection);
            const bool nextHasDraft = pendingHasDraft && pendingDraftSlot == slot;
            const auto& nextClip = audioChordPreview ? pendingChords
                                   : audioCompare    ? pendingComparison
                                   : nextHasDraft    ? pendingDraft
                                                     : pendingBank[slot];
            const bool switchingLater = !audioChordPreview && wasPlaying &&
                                        cached.quantiseSwitch->load() > 0.5f &&
                                        selected.load() != audioSelection;
            bankChanged = audioSelection >= 0 && !switchingLater && !samePlayback(activeClip, nextClip);
            audioBank = pendingBank;
            audioComparison = pendingComparison;
            audioDraft = pendingDraft;
            chordsChanged = !samePlayback(audioChords, pendingChords);
            audioChords = pendingChords;
            audioHasDraft = pendingHasDraft;
            audioDraftSlot = pendingDraftSlot;
            audioRevision = pendingRevision;
        }
    }
    double bpm = cached.tempo->load(), ppq = 0.0;
    bool hostPlaying = false, hasPosition = false;
    if (auto* head = getPlayHead())
        if (auto position = head->getPosition()) {
            if (auto b = position->getBpm())
                if (std::isfinite(*b) && *b > 0)
                    bpm = *b;
            if (auto p = position->getPpqPosition()) {
                ppq = *p;
                hasPosition = std::isfinite(ppq);
            }
            if (auto signature = position->getTimeSignature()) {
                hostNumerator.store(signature->numerator, std::memory_order_relaxed);
                hostDenominator.store(signature->denominator, std::memory_order_relaxed);
            } else {
                hostNumerator.store(0, std::memory_order_relaxed);
                hostDenominator.store(0, std::memory_order_relaxed);
            }
            hostPlaying = position->getIsPlaying();
        }
    bpm = juce::jlimit(20.0, 400.0, bpm);
    displayTempo.store(bpm);
    const double step = bpm / (60.0 * rate);
    const bool follow = cached.follow->load() > 0.5f;
    const bool hostJump =
        follow && hostPlaying && hasPosition && (!wasHostPlaying || std::abs(ppq - expectedPpq) > step * 2.0);
    // Chord audition is a local preview. Enabling Follow DAW returns to the melody.
    if (follow)
        chordPreview.store(false);
    const int melodyOutput = microtonalOutput(), bendRange = int(cached.bendRange->load());
    if (bendRange != outputBendRange) {
        outputBendRange = bendRange;
        outputConfigured = outputMode != 1 && outputConfigured;
    }
    const bool panicNow = panicRequested.exchange(false);
    if (panicNow) {
        allOff(0, outputMidi);
        stopChordLayer(0, outputMidi);
        for (int channel = 1; channel <= (outputMode == 2 ? 16 : 1); ++channel) {
            outputMidi.addEvent(juce::MidiMessage::controllerEvent(channel, 64, 0), 0);
            outputMidi.addEvent(juce::MidiMessage::allNotesOff(channel), 0);
        }
        heldTriggers = {};
        heldTranspose = {};
        trigger = -1;
        transpose = 0;
    }
    bool restart = bankChanged || hostJump || follow != lastFollow || (regionChanged && !audioChordPreview);
    auto loopBounds = [this] {
        if (!audioChordPreview && audioRegion.enabled && audioRegion.slot == audioSelection) {
            const double end = std::min(activeClip.length, audioRegion.end);
            if (audioRegion.start < end)
                return std::make_pair(audioRegion.start, end);
        }
        return std::make_pair(0.0, activeClip.length);
    };
    auto incoming = midi.cbegin();
    const auto end = midi.cend();
    const bool monitor = cached.monitor->load() > 0.5f;
    const float volume = cached.volume->load();
    const int auditionSound = int(cached.auditionSound->load());
    const double swingAmount = cached.swing->load();
    const bool quantise = cached.quantiseSwitch->load() > 0.5f;
    const bool accompany = cached.auditionChords->load() > 0.5f;
    float blockPeak = 0;
    const bool captureArmed = cached.capturePhrase->load() > 0.5f;
    if (captureArmed && !captureAudioArmed) {
        captureOrigin.store(captureWrite.load(std::memory_order_relaxed), std::memory_order_relaxed);
        captureAudioArmed = true;
    } else if (!captureArmed)
        captureAudioArmed = false;
    for (int sample = 0; sample < samples; ++sample) {
        bool restartLayer = (sample == 0 && chordsChanged) ||
                            (loopBoundary && !audioChordPreview && audioRegion.enabled &&
                             audioRegion.slot == audioSelection);
        while (incoming != end && (*incoming).samplePosition <= sample) {
            const auto message = (*incoming).getMessage();
            ++incoming;
            const int pitch = message.isNoteOnOrOff() ? message.getNoteNumber() : -1;
            if (cached.capturePhrase->load() > 0.5f && message.isNoteOnOrOff() && captureEvents != nullptr) {
                const auto index = captureWrite.load(std::memory_order_relaxed);
                auto& event = (*captureEvents)[index % uint32_t(captureCapacity)];
                const double clock = hasPosition ? ppq : expectedPpq;
                event.ppq = clock + sample * step;
                event.pitch = pitch;
                event.velocity = message.isNoteOn() ? message.getVelocity() : 0;
                captureWrite.store(index + 1, std::memory_order_release);
            } else if (pitch >= 48 && pitch < 60) {
                const int slot = pitch - 48;
                heldTriggers[slot] = message.isNoteOn();
                if (message.isNoteOn()) {
                    trigger = slot;
                    selected.store(slot);
                    compareRequested.store(false);
                    chordPreview.store(false);
                    restart = true;
                } else if (trigger == slot) {
                    trigger = -1;
                    for (int i = 11; i >= 0; --i)
                        if (heldTriggers[i]) {
                            trigger = i;
                            selected.store(i);
                            restart = true;
                            break;
                        }
                }
            } else if (pitch >= 60) {
                heldTranspose[pitch] = message.isNoteOn();
                if (message.isNoteOn())
                    transpose = pitch - 60;
                else {
                    transpose = 0;
                    for (int i = 127; i >= 60; --i)
                        if (heldTranspose[i]) {
                            transpose = i - 60;
                            break;
                        }
                }
            } else if (message.isAllNotesOff() || message.isAllSoundOff()) {
                allOff(sample, outputMidi);
                stopChordLayer(sample, outputMidi);
                trigger = -1;
                heldTriggers = {};
                heldTranspose = {};
                transpose = 0;
                preview.store(false);
                chordPreview.store(false);
                outputMidi.addEvent(message, sample);
            } else {
                outputMidi.addEvent(message, sample);
            }
        }
        const int slot = selected.load();
        const bool requestedA = compareRequested.load() && comparisonAvailable.load();
        const bool requestedDraft = audioHasDraft && audioDraftSlot == slot;
        const bool wantsChords = chordPreview.load() && !follow;
        const bool previewOn = preview.load();
        // A UI request may arrive after the bank exchange or while its lock is busy.
        // Wait for the first published chord clip rather than briefly playing the melody.
        const bool run = !panicNow && (!wantsChords || audioChords.count > 0) &&
                         (follow ? hostPlaying : (previewOn || trigger >= 0));
        const bool requestedChords = wantsChords && previewOn && audioChords.count > 0;
        const bool sourceChanged = requestedChords != audioChordPreview;
        const int requestedOutput = requestedChords ? 0 : melodyOutput;
        if (requestedOutput != outputMode) {
            // Sounding notes end on the channels on which they began, before resetting bends/MPE.
            allOff(sample, outputMidi);
            releaseChordMidi(sample, outputMidi);
            resetOutput(sample, outputMidi);
            outputMode = requestedOutput;
            outputConfigured = false;
        }
        const bool requestChanged =
            !requestedChords &&
            (slot != audioSelection || requestedA != audioCompare || requestedDraft != audioDraftActive);
        const bool boundarySwitch = loopBoundary && requestChanged;
        if (restart || sourceChanged || run != wasPlaying || audioSelection < 0 ||
            (requestChanged && (!quantise || !wasPlaying || loopBoundary))) {
            allOff(sample, outputMidi);
            restartLayer = true;
            audioSelection = slot;
            audioRegion = audioRequestedRegion;
            audioChordPreview = requestedChords;
            audioCompare = requestedA;
            audioDraftActive = requestedDraft;
            audibleSlot.store(slot);
            heardComparison.store(!audioChordPreview && requestedA);
            activeClip = audioChordPreview ? audioChords
                         : audioCompare    ? audioComparison
                         : requestedDraft  ? audioDraft
                                           : audioBank[slot];
            const auto bounds = loopBounds();
            phase = bounds.first + (!boundarySwitch && follow && hasPosition
                                        ? std::fmod(std::max(0.0, ppq + sample * step),
                                                    bounds.second - bounds.first)
                                        : 0.0);
            const bool scoped = !audioChordPreview && audioRegion.enabled && audioRegion.slot == audioSelection &&
                                audioRegion.start < bounds.second;
            accompanimentBeat = scoped ? phase
                                : follow && hasPosition ? std::max(0.0, ppq + sample * step) : 0.0;
            nextNote = 0;
            while (nextNote < activeClip.count &&
                   (audioChordPreview
                        ? activeClip.notes[nextNote].start
                        : std::max(bounds.first,
                                   renderedNote(activeClip.notes[nextNote], activeClip, 0, nextNote).start)) <
                          phase - step * 0.5)
                ++nextNote;
            restart = false;
        }
        loopBoundary = false;
        const auto& clip = activeClip;
        const auto bounds = loopBounds();
        const double loopStart = bounds.first, loopEnd = bounds.second;
        const bool regionLoop = !audioChordPreview && audioRegion.enabled && audioRegion.slot == audioSelection &&
                                audioRegion.start < loopEnd;
        wasPlaying = run;
        heardChordPreview.store(run && audioChordPreview);
        for (int p = 0; p < 128; ++p)
            if (voices[p].on && voices[p].remaining <= 1.0e-9)
                releaseVoice(p, sample, outputMidi);
        // Each start of playback repeats the setup, in case the receiver was reset meanwhile.
        if (!run)
            outputConfigured = false;
        else if (outputMode != 0 && !outputConfigured) {
            microtonalSetup(outputMode, outputBendRange,
                            [&](const juce::MidiMessage& m) { outputMidi.addEvent(m, sample); });
            outputConfigured = true;
        }
        if (run) {
            while (nextNote < clip.count) {
                const auto& raw = clip.notes[nextNote];
                if (regionLoop && (raw.start < loopStart || raw.start >= loopEnd)) {
                    ++nextNote;
                    continue;
                }
                auto n = audioChordPreview ? raw : renderedNote(raw, clip, transpose, nextNote);
                if (regionLoop) {
                    n.start = juce::jlimit(loopStart, loopEnd - 1.0e-9, n.start);
                    n.duration = std::min(n.duration, loopEnd - n.start);
                }
                const double start =
                    regionLoop ? n.start : audioChordPreview
                        ? raw.start
                        : std::min(clip.length - 0.001, swingTime(raw.start, clip.grid, swingAmount));
                if (start > phase + step * 0.5)
                    break;
                if (n.start >= phase - step * 1.5)
                    noteOn(n, nextNote, sample, outputMidi);
                ++nextNote;
            }
        }
        scheduleChordLayer(run && monitor && accompany && !audioChordPreview, restartLayer, accompanimentBeat,
                           step, sample, outputMidi);
        double signal = 0.0;
        for (int p = 0; p < 128; ++p) {
            // A released voice below the tail threshold no longer changes the mix.
            if (voices[p].on || voices[p].gain > 0.00005)
                signal += renderVoice(voices[p], monitor, auditionSound, step);
            if (chordVoices[p].on || chordVoices[p].gain > 0.00005)
                signal += renderVoice(chordVoices[p], monitor, auditionSound, step) * 0.65;
        }
        reverbMix.setTargetValue(cached.auditionReverb->load());
        const float mix = reverbMix.getNextValue();
        float leftOut = 0.0f, rightOut = 0.0f;
        if (mix <= 0.0f) {
            leftOut = rightOut = float(std::tanh(signal * volume));
        } else {
            const float dry = float(signal * volume);
            float wetLeft = 0.0f, wetRight = 0.0f;
            auditionReverbFx.process(dry, dry, wetLeft, wetRight);
            leftOut = float(std::tanh(dry * (1.0f - mix) + wetLeft * mix));
            rightOut = float(std::tanh(dry * (1.0f - mix) + wetRight * mix));
        }
        if (audio.getNumChannels() > 0)
            audio.setSample(0, sample, leftOut);
        for (int ch = 1; ch < audio.getNumChannels(); ++ch)
            audio.setSample(ch, sample, audio.getNumChannels() == 2 ? rightOut : leftOut);
        if (audio.getNumChannels() > 0)
            blockPeak = std::max(blockPeak, std::abs(leftOut));
        if (audio.getNumChannels() > 1)
            blockPeak = std::max(blockPeak, std::abs(rightOut));
        if (run) {
            accompanimentBeat += step;
            phase += step;
            if (phase >= loopEnd - step * 0.0001) {
                phase = loopStart + std::fmod(std::max(0.0, phase - loopEnd), loopEnd - loopStart);
                if (regionLoop) {
                    allOff(sample, outputMidi);
                    accompanimentBeat = phase;
                }
                nextNote = 0;
                loopBoundary = true;
                if (cached.evolveEachLoop->load() > 0.5f)
                    evolveLoopRequested.store(true, std::memory_order_release);
            }
        }
    }
    heardChordLayer.store(chordLayerPlaying);
    chordBeat.store(wasPlaying && (audioChordPreview || chordLayerPlaying)
                        ? std::fmod(audioChordPreview ? phase : accompanimentBeat, audioChords.length)
                        : -1.0);
    playing.store(wasPlaying);
    playBeat.store(wasPlaying ? phase : -1.0);
    expectedPpq = ppq + samples * step;
    wasHostPlaying = hostPlaying;
    lastFollow = follow;
    midi.swapWith(outputMidi);
    publishVisualState(blockPeak);
}
