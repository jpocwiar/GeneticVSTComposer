#include "PluginProcessor.h"
#include "PlaybackHelpers.h"
#include <cmath>

using Processor = GeneticVSTComposerJUCEAudioProcessor;

namespace {
juce::ValueTree clipState(const composer::Clip& c, const char* type) {
    juce::ValueTree clip(type);
    clip.setProperty("length", c.length, nullptr);
    clip.setProperty("grid", c.grid, nullptr);
    clip.setProperty("numerator", c.numerator, nullptr);
    clip.setProperty("denominator", c.denominator, nullptr);
    clip.setProperty("favourite", c.favourite, nullptr);
    clip.setProperty("seed", juce::int64(c.seed), nullptr);
    if (c.groupCount > 0) {
        clip.setProperty("groupCount", c.groupCount, nullptr);
        clip.setProperty("groupUnit", c.groupUnit, nullptr);
        clip.setProperty("groups", juce::String(composer::groupingText(c.groups, c.groupCount)), nullptr);
    }
    for (int i = 0; i < c.count; ++i) {
        const auto& n = c.notes[i];
        juce::ValueTree note("Note");
        note.setProperty("start", n.start, nullptr);
        note.setProperty("duration", n.duration, nullptr);
        note.setProperty("pitch", n.pitch, nullptr);
        note.setProperty("velocity", n.velocity, nullptr);
        note.setProperty("locked", n.locked, nullptr);
        if (n.cents != 0)
            note.setProperty("cents", n.cents, nullptr);
        clip.addChild(note, -1, nullptr);
    }
    return clip;
}
void appendMatchedNotes(const juce::MidiMessageSequence& sequence, double ticksPerQuarter, composer::Clip& clip,
                        bool& truncated, double& endTime, std::array<int, composer::maxNotes>* channels) {
    if (ticksPerQuarter <= 0)
        return;
    for (int i = 0; i < sequence.getNumEvents(); ++i) {
        auto* event = sequence.getEventPointer(i);
        if (!event->message.isNoteOn() || event->noteOffObject == nullptr)
            continue;
        const double start = event->message.getTimeStamp() / ticksPerQuarter;
        const double finish = event->noteOffObject->message.getTimeStamp() / ticksPerQuarter;
        if (start >= 128.0 || clip.count >= composer::maxNotes) {
            truncated = true;
            continue;
        }
        if (channels != nullptr)
            (*channels)[size_t(clip.count)] = event->message.getChannel();
        clip.notes[clip.count++] = {start, finish - start, event->message.getNoteNumber(),
                                    event->message.getVelocity(), false};
        endTime = std::max(endTime, finish);
    }
}
bool readClipState(const juce::ValueTree& child, composer::Clip& c) {
    if (!child.isValid() || child.getNumChildren() > composer::maxNotes)
        return false;
    c = {};
    c.length = child.getProperty("length", 16.0);
    c.grid = child.getProperty("grid", 0.25);
    c.numerator = child.getProperty("numerator", 4);
    c.denominator = child.getProperty("denominator", 4);
    c.favourite = child.getProperty("favourite", false);
    c.seed = uint32_t(juce::int64(child.getProperty("seed", 0)));
    if (child.hasProperty("groups")) {
        std::string error;
        int count = 0;
        if (!composer::parseGrouping(child.getProperty("groups").toString().toStdString(), c.groups, count, error))
            return false;
        c.groupCount = count;
        c.groupUnit = child.getProperty("groupUnit", c.denominator);
    }
    for (const auto& note : child)
        if (note.hasType("Note"))
            c.notes[c.count++] = {
                double(note.getProperty("start", 0.0)),  double(note.getProperty("duration", 0.25)),
                int(note.getProperty("pitch", 60)),      int(note.getProperty("velocity", 96)),
                bool(note.getProperty("locked", false)), int(note.getProperty("cents", 0))};
    composer::sanitise(c);
    return true;
}
// Validate saved inversions against the harmonic timeline before changing any live state.
// Octaves may differ; every pitch class must occur exactly once per chord.
bool validChordVoicing(const composer::Harmony& harmony, const composer::Clip& clip) {
    const auto reference = composer::voiceHarmony(harmony, clip.numerator, clip.denominator, {});
    if (clip.count != reference.count || std::abs(clip.length - harmony.length) > 1e-8)
        return false;
    std::array<bool, composer::maxNotes> matched{};
    for (int i = 0; i < clip.count; ++i) {
        const auto& n = clip.notes[i];
        if (n.cents != 0)
            return false;
        int match = -1;
        for (int j = 0; j < reference.count; ++j) {
            const auto& expected = reference.notes[j];
            if (!matched[j] && n.pitch % 12 == expected.pitch % 12 &&
                std::abs(n.start - expected.start) < 1e-8 &&
                std::abs(n.duration - expected.duration) < 1e-8) {
                match = j;
                break;
            }
        }
        if (match < 0)
            return false;
        matched[match] = true;
    }
    return true;
}
} // namespace
void Processor::getStateInformation(juce::MemoryBlock& dest) {
    juce::ValueTree state("GeneticComposer");
    state.setProperty("version", 6, nullptr);
    state.addChild(parameters.copyState(), -1, nullptr);
    {
        std::lock_guard<std::mutex> lock(documentMutex);
        state.addChild(microtonalStateLocked(), -1, nullptr);
        state.setProperty("selected", selected.load(), nullptr);
        state.setProperty("harmony", juce::String(composer::harmonyText(customHarmony)), nullptr);
        state.setProperty("arrangementForm", arrangementForm, nullptr);
        {
            const auto meter = timeSignature();
            std::array<int, 16> stored = groups;
            int count = groupCount, unit = groupUnit > 0 ? groupUnit : meter.denominator;
            if (count <= 0)
                composer::defaultGrouping(meter.numerator, meter.denominator, stored, count, unit);
            auto text = juce::String(composer::groupingText(stored, count));
            if (unit != meter.denominator)
                text += "@" + juce::String(unit);
            state.setProperty("grouping", text, nullptr);
        }
        for (const auto& c : bank)
            state.addChild(clipState(c, "Clip"), -1, nullptr);
        if (chordClip.count > 0)
            state.addChild(clipState(chordClip, "ChordVoicing"), -1, nullptr);
        if (grooveLoaded)
            state.addChild(clipState(grooveReference, "Groove"), -1, nullptr);
        if (comparisonAvailable.load())
            state.addChild(clipState(comparison, "Comparison"), -1, nullptr);
        // Unapplied previews are deliberately excluded from the saved document.
    }
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, dest);
}
bool Processor::restore(const void* data, int size) {
    if (data == nullptr || size <= 0 || size > 4 * 1024 * 1024)
        return false;
    const auto xml = getXmlFromBinary(data, size);
    if (!xml || !xml->hasTagName("GeneticComposer"))
        return false;
    const auto state = juce::ValueTree::fromXml(*xml);
    const int version = state.getProperty("version", 0);
    if ((version < 2 || version > 6) || !state.getChildWithName("Parameters").isValid())
        return false;
    Document restored;
    MicrotonalWorkspace restoredMicro;
    const auto microState = state.getChildWithName("MicrotonalWorkspace");
    if (!readMicrotonalState(microState, restoredMicro))
        return false;
    int index = 0;
    for (const auto& child : state)
        if (child.hasType("Clip")) {
            if (index >= composer::slotCount || !readClipState(child, restored.bank[index++]))
                return false;
        }
    if (index != composer::slotCount)
        return false;
    composer::Clip restoredComparison;
    bool hasA = false;
    if (version >= 3) {
        const auto harmony = state.getProperty("harmony").toString();
        std::string error;
        if (harmony.isNotEmpty() && !composer::parseHarmony(harmony.toStdString(), restored.harmony, error))
            return false;
        const auto groove = state.getChildWithName("Groove");
        restored.grooveLoaded = groove.isValid();
        if (restored.grooveLoaded && !readClipState(groove, restored.groove))
            return false;
        const auto a = state.getChildWithName("Comparison");
        hasA = a.isValid();
        if (hasA && !readClipState(a, restoredComparison))
            return false;
    }
    if (version >= 4) {
        const auto chords = state.getChildWithName("ChordVoicing");
        if (restored.harmony.count > 0) {
            if (!readClipState(chords, restored.chords) ||
                !validChordVoicing(restored.harmony, restored.chords))
                return false;
        } else if (chords.isValid())
            return false;
    } else if (restored.harmony.count > 0) {
        // Older sessions have chord names but no saved inversions. Reconstruct once, deterministically.
        const int slot = juce::jlimit(0, 11, int(state.getProperty("selected", 0)));
        const auto& clip = restored.bank[slot];
        restored.chords = composer::voiceHarmony(restored.harmony, clip.numerator, clip.denominator, {});
    }
    cancelled.store(true);
    panic();
    // A parameter missing from an older session takes its default, not the current value, so
    // earlier projects open with the microtonal tuning off.
    auto parameterState = state.getChildWithName("Parameters").createCopy();
    if (version < 5 && !parameterState.getChildWithProperty("id", "microEnabled").isValid()) {
        const auto legacyTuning = parameterState.getChildWithProperty("id", "tuning");
        parameterState.appendChild(
            juce::ValueTree("PARAM", {{"id", "microEnabled"},
                                      {"value", float(int(legacyTuning.getProperty("value", 0)) > 0)}}),
            nullptr);
    }
    if (version < 5 && restored.harmony.count > 0) {
        // Old sessions bypassed scale fit silently whenever custom harmony was present.
        // Make that previous audible behavior explicit in the restored switch.
        auto fit = parameterState.getChildWithProperty("id", "snap");
        if (fit.isValid())
            fit.setProperty("value", 0.0f, nullptr);
    }
    if (!parameterState.getChildWithProperty("id", "auditionReverb").isValid())
        parameterState.appendChild(juce::ValueTree("PARAM", {{"id", "auditionReverb"}, {"value", 0.0f}}), nullptr);
    for (auto* parameter : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter);
            ranged && !parameterState.getChildWithProperty("id", ranged->paramID).isValid())
            parameterState.appendChild(juce::ValueTree("PARAM", {{"id", ranged->paramID}}), nullptr);
    // Do not let a damaged session publish NaN or infinity to the audio thread.
    for (auto* parameter : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter)) {
            auto child = parameterState.getChildWithProperty("id", ranged->paramID);
            const auto fallback = ranged->convertFrom0to1(ranged->getDefaultValue());
            if (child.isValid() && !std::isfinite(float(child.getProperty("value", fallback))))
                child.setProperty("value", fallback, nullptr);
        }
    parameters.replaceState(parameterState);
    {
        std::lock_guard<std::mutex> lock(documentMutex);
        restoreDocumentLocked(restored);
        microWorkspace = restoredMicro;
        microUndo.clear();
        microRedo.clear();
        microPlaybackSource = 0;
        microtonalMode.store(bool(microState.getProperty("active", false)));
        publishMicrotonalLocked();
        comparison = restoredComparison;
        comparisonAvailable.store(hasA);
        compareRequested.store(false);
        selected.store(juce::jlimit(0, 11, int(state.getProperty("selected", 0))));
        audibleSlot.store(selected.load());
        arrangementForm = juce::jlimit(0, 3, int(state.getProperty("arrangementForm", 0)));
        groupCount = 0;
        groupUnit = timeSignature().denominator;
        const auto grouping = state.getProperty("grouping").toString();
        if (version >= 6 && grouping.isNotEmpty()) {
            auto body = grouping;
            int unit = groupUnit;
            if (body.containsChar('@')) {
                unit = body.fromLastOccurrenceOf("@", false, false).getIntValue();
                body = body.upToLastOccurrenceOf("@", false, false);
            }
            std::string error;
            int count = 0;
            std::array<int, 16> parsed{};
            if (composer::parseGrouping(body.toStdString(), parsed, count, error)) {
                groups = parsed;
                groupCount = count;
                groupUnit = unit;
            }
        }
        undoStack.clear();
        redoStack.clear();
        contentRevision.fetch_add(1);
        publishLocked();
        assignStatusLocked("Session restored.");
    }
    return true;
}
void Processor::setStateInformation(const void* data, int size) {
    restore(data, size);
}
juce::Result Processor::saveBank(const juce::File& file) {
    juce::MemoryBlock data;
    getStateInformation(data);
    return file.replaceWithData(data.getData(), data.getSize())
               ? juce::Result::ok()
               : juce::Result::fail("Could not save this bank.");
}
juce::Result Processor::loadBank(const juce::File& file) {
    if (file.getSize() > 4 * 1024 * 1024)
        return juce::Result::fail("Bank is too large.");
    juce::MemoryBlock data;
    if (!file.loadFileAsData(data) || !restore(data.getData(), int(data.getSize())))
        return juce::Result::fail("Not a valid Genetic Composer 2 bank.");
    return juce::Result::ok();
}
juce::Result Processor::exportMidi(const juce::File& file) const {
    return exportClip(displayedClip(), file);
}
juce::Result Processor::exportChordMidi(const juce::File& file) const {
    return exportClip(chordClipSnapshot(), file, true, true);
}
juce::Result Processor::exportMelodyAndChords(const juce::File& file) const {
    const auto melody = displayedClip();
    const auto chords = chordClipSnapshot();
    if (melody.count == 0)
        return juce::Result::fail("This variation is empty.");
    if (chords.count == 0)
        return juce::Result::fail("Create a chord sequence before exporting melody and chords.");
    auto track = [&](const composer::Clip& clip, bool alreadyRendered, bool plain) {
        juce::MidiMessageSequence sequence;
        sequence.addEvent(juce::MidiMessage::tempoMetaEvent(int(60000000.0 / tempo())));
        sequence.addEvent(juce::MidiMessage::timeSignatureMetaEvent(clip.numerator, clip.denominator));
        addMusicalMeta(sequence, clip);
        appendClipMidi(sequence, clip, alreadyRendered, plain);
        auto end = juce::MidiMessage::endOfTrack();
        end.setTimeStamp(clip.length * 960);
        sequence.addEvent(end);
        sequence.updateMatchedPairs();
        return sequence;
    };
    const auto melodyTrack = track(melody, false, false);
    const auto chordTrack = track(chords, true, true);
    return writeMidiFile(file, melodyTrack, &chordTrack);
}
juce::Result Processor::exportMidiRange(const juce::File& file, double start, double end) const {
    const auto source = displayedClip();
    if (!std::isfinite(start) || !std::isfinite(end) || start < 0 || end <= start || end > source.length)
        return juce::Result::fail("Select a time range inside the phrase before exporting.");
    composer::Clip result = source;
    result.count = 0;
    result.length = end - start;
    for (int i = 0; i < source.count; ++i) {
        auto n = renderedNote(source.notes[i], source, 0, i);
        const double finish = std::min(end, n.start + n.duration);
        const double begin = std::max(start, n.start);
        if (finish <= begin)
            continue;
        n.start = begin - start;
        n.duration = finish - begin;
        result.notes[result.count++] = n;
    }
    return exportClip(result, file, true);
}
void Processor::appendClipMidi(juce::MidiMessageSequence& sequence, const composer::Clip& clip,
                               bool alreadyRendered, bool plainMidi) const {
    // Microtonal files carry the same setup and bends as live output. Sorted notes let an MPE
    // note take the member channel whose last note ended first. Plain tracks stay on channel 1.
    const int output = plainMidi ? 0 : microtonalOutput();
    const int range = output == 2 ? mpeBendRange : int(param("bendRange"));
    microtonalSetup(output, range, [&](const juce::MidiMessage& m) { sequence.addEvent(m); });
    std::array<double, 17> channelEnd;
    channelEnd.fill(-1.0);
    for (int i = 0; i < clip.count; ++i) {
        const auto n = alreadyRendered ? clip.notes[i] : renderedNote(clip.notes[i], clip, 0, i);
        int channel = 1;
        if (output == 2) {
            channel = 2;
            for (int candidate = 3; candidate <= 16; ++candidate)
                if (channelEnd[size_t(candidate)] < channelEnd[size_t(channel)])
                    channel = candidate;
            channelEnd[size_t(channel)] = n.start + n.duration;
        }
        if (output != 0) {
            auto bend = juce::MidiMessage::pitchWheel(channel, bendPosition(n.cents, range));
            bend.setTimeStamp(n.start * 960);
            sequence.addEvent(bend);
        }
        auto on = juce::MidiMessage::noteOn(channel, n.pitch, juce::uint8(n.velocity));
        on.setTimeStamp(n.start * 960);
        sequence.addEvent(on);
        auto off = juce::MidiMessage::noteOff(channel, n.pitch);
        off.setTimeStamp((n.start + n.duration) * 960);
        sequence.addEvent(off);
    }
}
juce::Result Processor::writeMidiFile(const juce::File& file, const juce::MidiMessageSequence& first,
                                      const juce::MidiMessageSequence* second) const {
    juce::MidiFile midi;
    midi.setTicksPerQuarterNote(960);
    midi.addTrack(first);
    if (second != nullptr)
        midi.addTrack(*second);
    juce::MemoryOutputStream stream;
    if (!midi.writeTo(stream, second != nullptr ? 1 : 0))
        return juce::Result::fail("Could not encode MIDI.");
    return file.replaceWithData(stream.getData(), stream.getDataSize())
               ? juce::Result::ok()
               : juce::Result::fail("Could not write MIDI file.");
}
juce::Result Processor::exportClip(const composer::Clip& clip, const juce::File& file, bool alreadyRendered,
                                   bool plainMidi) const {
    if (clip.count == 0)
        return juce::Result::fail("This variation is empty.");
    juce::MidiMessageSequence sequence;
    sequence.addEvent(juce::MidiMessage::tempoMetaEvent(int(60000000.0 / tempo())));
    sequence.addEvent(juce::MidiMessage::timeSignatureMetaEvent(clip.numerator, clip.denominator));
    addMusicalMeta(sequence, clip);
    appendClipMidi(sequence, clip, alreadyRendered, plainMidi);
    auto end = juce::MidiMessage::endOfTrack();
    end.setTimeStamp(clip.length * 960);
    sequence.addEvent(end);
    sequence.updateMatchedPairs();
    return writeMidiFile(file, sequence, nullptr);
}
void Processor::addMusicalMeta(juce::MidiMessageSequence& sequence, const composer::Clip& clip) const {
    std::array<int, 16> stored{};
    int count = 0;
    int unit = clip.denominator;
    if (clip.groupCount > 0) {
        stored = clip.groups;
        count = clip.groupCount;
        unit = clip.groupUnit > 0 ? clip.groupUnit : clip.denominator;
    } else
        composer::defaultGrouping(clip.numerator, clip.denominator, stored, count, unit);
    std::string text = "RHYTHMS_GROUPS:" + composer::groupingText(stored, count) + "/" +
                       std::to_string(std::max(1, clip.denominator));
    if (unit != clip.denominator)
        text += "@" + std::to_string(unit);
    auto meta = juce::MidiMessage::textMetaEvent(1, text);
    meta.setTimeStamp(0.0);
    sequence.addEvent(meta);
    const auto harmony = harmonySnapshot();
    const auto voiced = chordClipSnapshot();
    for (int i = 0; i < harmony.count; ++i) {
        auto marker = juce::MidiMessage::textMetaEvent(6, composer::voicedChordName(harmony.chords[size_t(i)], voiced));
        marker.setTimeStamp(harmony.chords[size_t(i)].start * 960.0);
        sequence.addEvent(marker);
    }
}
juce::Result Processor::readMidi(const juce::File& file, composer::Clip& clip, bool& truncated,
                                 composer::Harmony* markers) const {
    if (file.getSize() > 4 * 1024 * 1024)
        return juce::Result::fail("Choose a MIDI file smaller than 4 MB.");
    juce::FileInputStream stream(file);
    juce::MidiFile midi;
    if (!stream.openedOk() || !midi.readFrom(stream) || midi.getTimeFormat() <= 0)
        return juce::Result::fail("Choose a standard MIDI file with a musical timebase.");
    clip = {};
    clip.count = 0;
    clip.grid = 4.0 / (8 << int(param("grid")));
    double endTime = 0.0;
    truncated = false;
    std::array<int, composer::maxNotes> channels{};
    struct Marker {
        double start;
        juce::String text;
    };
    std::vector<Marker> markerEvents;
    for (int track = 0; track < midi.getNumTracks(); ++track) {
        auto sequence = *midi.getTrack(track);
        sequence.updateMatchedPairs();
        for (int i = 0; i < sequence.getNumEvents(); ++i) {
            auto* event = sequence.getEventPointer(i);
            if (event->message.isTimeSignatureMetaEvent())
                event->message.getTimeSignatureInfo(clip.numerator, clip.denominator);
            if (event->message.isMetaEvent() && event->message.getMetaEventType() == 1) {
                const auto text = event->message.getTextFromTextMetaEvent();
                if (text.startsWith("RHYTHMS_GROUPS:")) {
                    auto body = text.fromFirstOccurrenceOf("RHYTHMS_GROUPS:", false, false);
                    int unit = clip.denominator;
                    if (body.containsChar('@')) {
                        unit = body.fromLastOccurrenceOf("@", false, false).getIntValue();
                        body = body.upToLastOccurrenceOf("@", false, false);
                    }
                    const auto slash = body.indexOfChar('/');
                    const auto groupsText = slash >= 0 ? body.substring(0, slash) : body;
                    std::string error;
                    int count = 0;
                    std::array<int, 16> parsed{};
                    if (composer::parseGrouping(groupsText.toStdString(), parsed, count, error)) {
                        clip.groups = parsed;
                        clip.groupCount = count;
                        clip.groupUnit = unit > 0 ? unit : clip.denominator;
                    }
                }
            }
            if (markers != nullptr && event->message.isMetaEvent() && event->message.getMetaEventType() == 6)
                markerEvents.push_back({event->message.getTimeStamp() / midi.getTimeFormat(),
                                        event->message.getTextFromTextMetaEvent()});
            if (event->message.isEndOfTrackMetaEvent())
                endTime = std::max(endTime, event->message.getTimeStamp() / midi.getTimeFormat());
        }
        appendMatchedNotes(sequence, midi.getTimeFormat(), clip, truncated, endTime, &channels);
    }
    if (clip.count == 0)
        return juce::Result::fail("No complete notes were found in this MIDI file.");
    if (tuningIndex() > 0) {
        // The bend on a note's channel at its onset becomes its offset. Ranges follow RPN 0, and
        // an MPE zone message sets ±48 on its member channels; otherwise ±2 is assumed.
        juce::MidiMessageSequence merged;
        for (int track = 0; track < midi.getNumTracks(); ++track)
            merged.addSequence(*midi.getTrack(track), 0);
        std::array<double, 17> range;
        range.fill(2.0);
        juce::MidiRPNDetector rpn;
        struct Bend {
            double start, cents;
            int channel;
        };
        std::vector<Bend> bends;
        for (const auto* event : merged) {
            const auto& message = event->message;
            const int channel = message.getChannel();
            if (message.isController())
                if (const auto parsed =
                        rpn.tryParse(channel, message.getControllerNumber(), message.getControllerValue());
                    parsed && !parsed->isNRPN) {
                    const int value = parsed->is14BitValue ? parsed->value >> 7 : parsed->value;
                    if (parsed->parameterNumber == 0)
                        range[size_t(channel)] =
                            value + (parsed->is14BitValue ? (parsed->value & 127) / 100.0 : 0);
                    if (parsed->parameterNumber == 6 && (channel == 1 || channel == 16)) {
                        range[size_t(channel)] = 2;
                        for (int member = 1; member <= std::min(value, 15); ++member)
                            range[size_t(channel == 1 ? 1 + member : 16 - member)] = mpeBendRange;
                    }
                }
            if (message.isPitchWheel())
                bends.push_back(
                    {message.getTimeStamp() / midi.getTimeFormat(),
                     (message.getPitchWheelValue() - 8192) / 8192.0 * range[size_t(channel)] * 100, channel});
        }
        for (int i = 0; i < clip.count; ++i) {
            auto& note = clip.notes[i];
            double cents = 0;
            for (const auto& bend : bends) {
                if (bend.start > note.start + 1e-9)
                    break;
                if (bend.channel == channels[size_t(i)])
                    cents = bend.cents;
            }
            // Nearest key, so a +60 bend on E becomes F-40.
            const int total = note.pitch * 100 + int(std::lround(cents));
            note.pitch = int(std::floor((total + 50) / 100.0));
            note.cents = total - note.pitch * 100;
        }
    }
    const double bar = std::max(0.125, clip.numerator * 4.0 / std::max(1, clip.denominator));
    clip.length = std::clamp(std::ceil(endTime / bar - 0.0001) * bar, bar, 128.0);
    composer::sanitise(clip);
    if (clip.count == 0)
        return juce::Result::fail("No valid note events were found in this MIDI file.");
    if (markers != nullptr && !markerEvents.empty()) {
        *markers = {};
        for (const auto& marker : markerEvents) {
            if (markers->count >= int(markers->chords.size()))
                break;
            std::string error;
            composer::Harmony parsed;
            if (!composer::parseHarmony(marker.text.toStdString() + ":1", parsed, error) || parsed.count <= 0)
                continue;
            auto chord = parsed.chords[0];
            chord.start = marker.start;
            markers->chords[size_t(markers->count++)] = chord;
        }
        markers->length = std::max(clip.length, markers->count ? markers->chords[size_t(markers->count - 1)].start : 0.0);
        for (int i = 0; i < markers->count; ++i) {
            const auto next = i + 1 < markers->count ? markers->chords[size_t(i + 1)].start : markers->length;
            markers->chords[size_t(i)].duration = std::max(0.25, next - markers->chords[size_t(i)].start);
        }
    }
    return juce::Result::ok();
}
juce::Result Processor::clipFromSequence(const juce::MidiMessageSequence& source, double ticksPerQuarter,
                                         composer::Clip& clip, bool& truncated) const {
    if (!(ticksPerQuarter > 0))
        return juce::Result::fail("The captured phrase has no timing.");
    auto sequence = source;
    sequence.updateMatchedPairs();
    const int numerator = clip.numerator > 0 ? clip.numerator : 4;
    const int denominator = clip.denominator > 0 ? clip.denominator : 4;
    const auto storedGroups = clip.groups;
    const int storedCount = clip.groupCount;
    const int storedUnit = clip.groupUnit;
    const double grid = clip.grid > 0 ? clip.grid : 0.25;
    clip = {};
    clip.numerator = numerator;
    clip.denominator = denominator;
    clip.groups = storedGroups;
    clip.groupCount = storedCount;
    clip.groupUnit = storedUnit;
    clip.grid = grid;
    truncated = false;
    double endTime = 0;
    appendMatchedNotes(sequence, ticksPerQuarter, clip, truncated, endTime, nullptr);
    if (clip.count == 0)
        return juce::Result::fail("The captured phrase has no notes.");
    const double bar = std::max(0.125, clip.numerator * 4.0 / std::max(1, clip.denominator));
    clip.length = std::clamp(std::ceil(std::max(endTime, 0.0) / bar - 0.0001) * bar, bar, 128.0);
    composer::sanitise(clip);
    if (clip.count == 0)
        return juce::Result::fail("The captured phrase has no notes.");
    return juce::Result::ok();
}
juce::Result Processor::importMidi(const juce::File& file) {
    if (hasDraft())
        return juce::Result::fail("Apply or discard the preview before importing a melody.");
    composer::Clip clip;
    bool truncated = false;
    if (auto result = readMidi(file, clip, truncated); result.failed())
        return result;
    setClip(selected.load(), clip);
    if (clip.groupCount > 0) {
        std::lock_guard<std::mutex> lock(documentMutex);
        groups = clip.groups;
        groupCount = clip.groupCount;
        groupUnit = clip.groupUnit;
    }
    setStatus(truncated ? "MIDI imported; limited to 512 notes and 128 beats."
                        : "MIDI imported. Evolve it or keep its rhythm for new melodies.");
    return juce::Result::ok();
}
