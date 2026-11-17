#include "PlaybackHelpers.h"
#include "PluginProcessor.h"
#include <algorithm>
#include <cmath>

using Processor = GeneticVSTComposerJUCEAudioProcessor;

void Processor::rebuildCompositionScheduleLocked(bool allSections) {
    auto schedule = std::make_unique<CompositionSchedule>();
    schedule->loop = !allSections;
    double origin = 0;
    for (int index = 0; index < int(composition.sections.size()); ++index) {
        if (!allSections && index != composition.selected)
            continue;
        const auto& section = composition.sections[size_t(index)];
        const int position = schedule->sections++;
        schedule->sectionIds[size_t(position)] = index;
        schedule->sources[size_t(index)] = section.melody;
        schedule->origins[size_t(index)] = origin;
        schedule->starts[size_t(position)] = origin;
        schedule->ends[size_t(position)] = origin + section.melody.length;
        for (int i = 0; i < section.melody.count; ++i) {
            auto note = section.melody.notes[size_t(i)];
            note.start += origin;
            schedule->melody[size_t(schedule->melodyCount++)] = {note, index, i};
        }
        for (int i = 0; i < section.chords.count; ++i) {
            auto note = section.chords.notes[size_t(i)];
            note.start += origin;
            schedule->chords[size_t(schedule->chordCount++)] = {note, index, i};
        }
        origin += section.melody.length;
    }
    schedule->length = origin;
    const auto order = [](const ScheduledNote& a, const ScheduledNote& b) {
        return a.note.start < b.note.start;
    };
    std::stable_sort(schedule->melody.begin(), schedule->melody.begin() + schedule->melodyCount, order);
    std::stable_sort(schedule->chords.begin(), schedule->chords.begin() + schedule->chordCount, order);
    juce::SpinLock::ScopedLockType lock(exchangeLock);
    *pendingComposition = *schedule;
    ++pendingCompositionRevision;
}
void Processor::auditionComposition(bool allSections) {
    {
        std::lock_guard<std::mutex> lock(documentMutex);
        if (composition.sections.empty()) {
            assignStatusLocked("Create composition sections first.");
            return;
        }
        rebuildCompositionScheduleLocked(allSections);
    }
    setParameter("follow", 0);
    setParameter("monitor", 1);
    if (param("volume") <= 0)
        setParameter("volume", 0.65f);
    microtonalMode.store(false);
    preview.store(false);
    chordPreview.store(false);
    panicRequested.store(false);
    compositionRequested.store(true);
    compositionSourceSelected.store(true);
    setStatus(allSections ? "Playing the complete composition."
                          : "Looping the selected composition section.");
}
void Processor::stopComposition() {
    compositionRequested.store(false);
}

void Processor::processCompositionBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi) {
    bool changed = false;
    {
        juce::SpinLock::ScopedTryLockType lock(exchangeLock);
        if (lock.isLocked() && pendingCompositionRevision != audioCompositionRevision) {
            *audioComposition = *pendingComposition;
            audioCompositionRevision = pendingCompositionRevision;
            changed = true;
        }
    }
    const bool panicNow = panicRequested.exchange(false);
    if (!audioCompositionMode || changed || panicNow || (!compositionRequested.load() && wasPlaying)) {
        allOff(0, outputMidi);
        stopChordLayer(0, outputMidi);
        resetOutput(0, outputMidi);
        voices = {};
        microVoices = {};
        chordVoices = {};
        channelNotes = {};
        outputConfigured = false;
        outputMode = microtonalOutput();
        outputBendRange = outputMode == 2 ? mpeBendRange : int(cached.bendRange->load());
        compositionPhase = 0;
        nextCompositionNote = nextCompositionChord = 0;
        wasPlaying = false;
        audioMicrotonalMode = false;
        audioSelection = -1;
        microWasRunning = false;
        heardChordPreview.store(false);
        heardComparison.store(false);
    }
    if (panicNow)
        compositionRequested.store(false);
    audioCompositionMode = compositionSourceSelected.load();
    if (!audioCompositionMode || !compositionRequested.load() || audioComposition->length <= 0) {
        compositionPlaying.store(false);
        heardCompositionSection.store(-1);
        playing.store(false);
        playBeat.store(-1);
        clearVisualState();
        wasPlaying = false;
        midi.swapWith(outputMidi);
        return;
    }
    for (const auto metadata : midi)
        outputMidi.addEvent(metadata.data, metadata.numBytes, metadata.samplePosition);
    const int desiredOutput = microtonalOutput();
    const int desiredRange = desiredOutput == 2 ? mpeBendRange : int(cached.bendRange->load());
    if (outputMode != desiredOutput || outputBendRange != desiredRange) {
        allOff(0, outputMidi);
        releaseChordMidi(0, outputMidi);
        resetOutput(0, outputMidi);
        outputMode = desiredOutput;
        outputBendRange = desiredRange;
        outputConfigured = false;
    }
    const auto projected = [&](const ScheduledNote& event) {
        const auto& source = audioComposition->sources[size_t(event.section)];
        auto note = renderedNote(source.notes[size_t(event.sourceIndex)], source, 0, event.sourceIndex);
        note.start += audioComposition->origins[size_t(event.section)];
        return note;
    };
    const bool follow = cached.follow->load() > 0.5f;
    bool hostPlaying = false, hasPosition = false;
    double bpm = cached.tempo->load(), ppq = 0;
    if (auto* head = getPlayHead())
        if (auto position = head->getPosition()) {
            hostPlaying = position->getIsPlaying();
            if (auto tempo = position->getBpm(); tempo && std::isfinite(*tempo) && *tempo > 0)
                bpm = *tempo;
            if (auto beat = position->getPpqPosition(); beat && std::isfinite(*beat)) {
                ppq = *beat;
                hasPosition = true;
            }
        }
    bpm = std::clamp(bpm, 20.0, 400.0);
    displayTempo.store(bpm);
    const double step = bpm / (60.0 * rate);
    if (follow && hasPosition && (!wasPlaying || std::abs(ppq - expectedPpq) > step * 2)) {
        allOff(0, outputMidi);
        stopChordLayer(0, outputMidi);
        compositionPhase = audioComposition->loop ? std::fmod(std::max(0.0, ppq), audioComposition->length)
                                                  : std::clamp(ppq, 0.0, audioComposition->length);
        nextCompositionNote = nextCompositionChord = 0;
        if (hostPlaying && outputMode != 0 && !outputConfigured) {
            microtonalSetup(outputMode, outputBendRange,
                            [&](const juce::MidiMessage& event) { outputMidi.addEvent(event, 0); });
            outputConfigured = true;
        }
        while (nextCompositionNote < audioComposition->melodyCount) {
            const auto& event = audioComposition->melody[size_t(nextCompositionNote)];
            auto note = projected(event);
            if (note.start >= compositionPhase - step)
                break;
            const double remaining = note.start + note.duration - compositionPhase;
            if (remaining > 0 && hostPlaying) {
                note.duration = remaining;
                noteOn(note, event.sourceIndex, 0, outputMidi);
            }
            ++nextCompositionNote;
        }
    }
    const bool monitor = cached.monitor->load() > 0.5f;
    const double volume = cached.volume->load();
    const int sound = int(cached.auditionSound->load());
    const bool sendChords = cached.chordMidiOut->load() > 0.5f && outputMode != 2;
    if (!sendChords)
        releaseChordMidi(0, outputMidi);
    float peak = 0;
    for (int sample = 0; sample < audio.getNumSamples(); ++sample) {
        const bool run = compositionRequested.load() && (!follow || hostPlaying) &&
                         (audioComposition->loop || compositionPhase < audioComposition->length);
        if (!run && wasPlaying) {
            allOff(sample, outputMidi);
            stopChordLayer(sample, outputMidi);
        }
        if (run && !outputConfigured && outputMode != 0) {
            microtonalSetup(outputMode, outputBendRange,
                            [&](const juce::MidiMessage& event) { outputMidi.addEvent(event, sample); });
            outputConfigured = true;
        }
        for (int key = 0; key < 128; ++key) {
            if (voices[size_t(key)].on && voices[size_t(key)].remaining <= 1e-9)
                releaseVoice(key, sample, outputMidi);
            auto& chord = chordVoices[size_t(key)];
            if (chord.on && chord.remaining <= 1e-9) {
                chord.on = false;
                chord.target = 0;
                if (chordMidiSounding[size_t(key)]) {
                    outputMidi.addEvent(juce::MidiMessage::noteOff(16, key), sample);
                    chordMidiSounding[size_t(key)] = false;
                }
            }
        }
        if (run) {
            while (nextCompositionNote < audioComposition->melodyCount) {
                const auto& event = audioComposition->melody[size_t(nextCompositionNote)];
                const auto note = projected(event);
                if (note.start > compositionPhase + step * 0.5)
                    break;
                if (note.start >= compositionPhase - step * 1.5)
                    noteOn(note, event.sourceIndex, sample, outputMidi);
                ++nextCompositionNote;
            }
            while (nextCompositionChord < audioComposition->chordCount) {
                const auto& event = audioComposition->chords[size_t(nextCompositionChord)];
                if (event.note.start > compositionPhase + step * 0.5)
                    break;
                const auto& note = event.note;
                const double remaining = note.start + note.duration - compositionPhase;
                if (remaining > 0) {
                    auto& voice = chordVoices[size_t(note.pitch)];
                    if (chordMidiSounding[size_t(note.pitch)])
                        outputMidi.addEvent(juce::MidiMessage::noteOff(16, note.pitch), sample);
                    voice.on = true;
                    voice.remaining = remaining;
                    voice.target = note.velocity / 127.0;
                    voice.increment = phaseIncrements[size_t(note.pitch)];
                    voice.sourceIndex = event.sourceIndex;
                    ++chordNoteOnSerial;
                    if (sendChords) {
                        outputMidi.addEvent(
                            juce::MidiMessage::noteOn(16, note.pitch, juce::uint8(note.velocity)), sample);
                        chordMidiSounding[size_t(note.pitch)] = true;
                    }
                }
                ++nextCompositionChord;
            }
        }
        double signal = 0;
        for (int key = 0; key < 128; ++key) {
            if (voices[size_t(key)].on || voices[size_t(key)].gain > 0.00005)
                signal += renderVoice(voices[size_t(key)], monitor, sound, step);
            if (chordVoices[size_t(key)].on || chordVoices[size_t(key)].gain > 0.00005)
                signal += renderVoice(chordVoices[size_t(key)], monitor, sound, step) * 0.65;
        }
        reverbMix.setTargetValue(cached.auditionReverb->load());
        const float mix = reverbMix.getNextValue(), dry = float(signal * volume);
        float wetLeft = 0, wetRight = 0;
        if (mix > 0)
            auditionReverbFx.process(dry, dry, wetLeft, wetRight);
        const float left = float(std::tanh(dry * (1 - mix) + wetLeft * mix));
        const float right = float(std::tanh(dry * (1 - mix) + wetRight * mix));
        for (int channel = 0; channel < audio.getNumChannels(); ++channel)
            audio.setSample(channel, sample, channel == 1 ? right : left);
        peak = std::max({peak, std::abs(left), std::abs(right)});
        if (run) {
            compositionPhase += step;
            if (compositionPhase >= audioComposition->length - step * 0.0001) {
                allOff(sample, outputMidi);
                stopChordLayer(sample, outputMidi);
                if (audioComposition->loop) {
                    compositionPhase = std::fmod(compositionPhase, audioComposition->length);
                    nextCompositionNote = nextCompositionChord = 0;
                } else {
                    compositionPhase = audioComposition->length;
                    if (!follow)
                        compositionRequested.store(false);
                }
            }
        }
        wasPlaying = run;
    }
    int heard = -1;
    double localBeat = -1;
    if (compositionRequested.load() && wasPlaying)
        for (int i = 0; i < audioComposition->sections; ++i)
            if (compositionPhase >= audioComposition->starts[size_t(i)] &&
                compositionPhase < audioComposition->ends[size_t(i)]) {
                heard = audioComposition->sectionIds[size_t(i)];
                localBeat = compositionPhase - audioComposition->starts[size_t(i)];
                break;
            }
    compositionPlaying.store(heard >= 0);
    playing.store(heard >= 0);
    heardCompositionSection.store(heard);
    playBeat.store(localBeat);
    heardChordLayer.store(heard >= 0 && audioComposition->chordCount > 0);
    expectedPpq = ppq + audio.getNumSamples() * step;
    midi.swapWith(outputMidi);
    publishVisualState(peak);
}
juce::Result Processor::exportCompositionSectionMidi(int index, const juce::File& file) const {
    const auto workspace = compositionSnapshot();
    if (index < 0 || index >= int(workspace.sections.size()))
        return juce::Result::fail("Select a section first.");
    return exportClip(workspace.sections[size_t(index)].melody, file);
}
juce::Result Processor::exportCompositionMidi(const juce::File& file) const {
    const auto workspace = compositionSnapshot();
    if (workspace.sections.empty())
        return juce::Result::fail("Create sections before exporting a composition.");
    juce::MidiMessageSequence melody, chords;
    melody.addEvent(juce::MidiMessage::tempoMetaEvent(int(60000000.0 / tempo())));
    double origin = 0;
    for (const auto& section : workspace.sections) {
        auto meter =
            juce::MidiMessage::timeSignatureMetaEvent(section.melody.numerator, section.melody.denominator);
        meter.setTimeStamp(origin * 960);
        melody.addEvent(meter);
        auto name = juce::MidiMessage::textMetaEvent(6, section.name);
        name.setTimeStamp(origin * 960);
        melody.addEvent(name);
        juce::MidiMessageSequence phrase, harmony;
        addMusicalMeta(phrase, section.melody);
        appendClipMidi(phrase, section.melody, false, false);
        appendClipMidi(harmony, section.chords, true, true);
        for (int i = 0; i < section.harmony.count; ++i) {
            const auto& chord = section.harmony.chords[size_t(i)];
            auto marker = juce::MidiMessage::textMetaEvent(6, juce::String(composer::chordName(chord)));
            marker.setTimeStamp(chord.start * 960);
            harmony.addEvent(marker);
        }
        melody.addSequence(phrase, origin * 960);
        chords.addSequence(harmony, origin * 960);
        origin += section.melody.length;
    }
    auto end = juce::MidiMessage::endOfTrack();
    end.setTimeStamp(origin * 960);
    melody.addEvent(end);
    chords.addEvent(end);
    melody.updateMatchedPairs();
    chords.updateMatchedPairs();
    return writeMidiFile(file, melody, &chords);
}
