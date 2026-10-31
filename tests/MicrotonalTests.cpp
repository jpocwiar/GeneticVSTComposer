#include "PluginProcessor.h"
#include <chrono>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {
using MicroProcessor = GeneticVSTComposerJUCEAudioProcessor;

void check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

void setParameter(MicroProcessor& processor, const char* id, float value) {
    auto* parameter = processor.parameters.getParameter(id);
    check(parameter != nullptr, "microtonal parameter missing");
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

struct Event {
    int sample;
    juce::MidiMessage message;
};

// One 4/4 bar of quarter notes at 120 BPM and 48 kHz: C4, E4 half-flat, G4, B4 half-flat.
composer::Clip rastRiff() {
    composer::Clip clip;
    clip.length = 4;
    clip.count = 4;
    const int pitches[]{60, 64, 67, 71}, cents[]{0, -50, 0, -50};
    for (int i = 0; i < 4; ++i)
        clip.notes[i] = {double(i), 1, pitches[i], 100, false, cents[i]};
    return clip;
}

std::unique_ptr<MicroProcessor> player(int tuning, int output, const composer::Clip& clip) {
    auto processor = std::make_unique<MicroProcessor>();
    processor->prepareToPlay(48000, 128);
    setParameter(*processor, "follow", 0);
    setParameter(*processor, "tempo", 120);
    setParameter(*processor, "snap", 0);
    setParameter(*processor, "swing", 0);
    setParameter(*processor, "gate", 1);
    setParameter(*processor, "monitor", 0);
    setParameter(*processor, "tuning", float(tuning));
    setParameter(*processor, "microEnabled", tuning > 0 ? 1.0f : 0.0f);
    setParameter(*processor, "microOutput", float(output));
    setParameter(*processor, "bendRange", 2);
    processor->setClip(0, clip);
    processor->selectClip(0);
    return processor;
}

std::vector<Event> render(MicroProcessor& processor, int blocks, int& elapsed,
                          std::vector<float>* signal = nullptr) {
    std::vector<Event> events;
    for (int block = 0; block < blocks; ++block) {
        juce::AudioBuffer<float> audio(2, 128);
        juce::MidiBuffer midi;
        processor.processBlock(audio, midi);
        for (const auto metadata : midi)
            events.push_back({elapsed + metadata.samplePosition, metadata.getMessage()});
        if (signal)
            for (int i = 0; i < 128; ++i)
                signal->push_back(audio.getSample(0, i));
        elapsed += 128;
    }
    return events;
}

bool isController(const juce::MidiMessage& m, int channel, int number, int value) {
    return m.isController() && m.getChannel() == channel && m.getControllerNumber() == number &&
           m.getControllerValue() == value;
}

bool hasRpn(const std::vector<Event>& events, int channel, int number, int value) {
    for (size_t i = 0; i + 2 < events.size(); ++i)
        if (isController(events[i].message, channel, 101, 0) &&
            isController(events[i + 1].message, channel, 100, number) &&
            isController(events[i + 2].message, channel, 6, value))
            return true;
    return false;
}

// Each note-on must directly follow a bend on its own channel with the expected position.
void checkBends(const std::vector<Event>& events, const std::vector<int>& positions, const char* message) {
    size_t note = 0;
    for (size_t i = 0; i < events.size(); ++i) {
        const auto& m = events[i].message;
        if (!m.isNoteOn())
            continue;
        check(note < positions.size() && i > 0, message);
        const auto& bend = events[i - 1];
        check(bend.message.isPitchWheel() && bend.message.getChannel() == m.getChannel() &&
                  bend.sample == events[i].sample && bend.message.getPitchWheelValue() == positions[note],
              message);
        ++note;
    }
    check(note == positions.size(), message);
}

void testPlainOutputUnchanged() {
    auto processor = player(0, 0, rastRiff());
    processor->setPreview(true);
    int elapsed = 0;
    const auto events = render(*processor, 740, elapsed);
    int ons = 0;
    for (const auto& event : events) {
        check(!event.message.isPitchWheel() && !event.message.isController(),
              "12-TET output must not send bends or RPN setup");
        check(event.message.getChannel() == 1, "12-TET output must stay on channel 1");
        if (event.message.isNoteOn())
            check(event.message.getNoteNumber() == rastRiff().notes[ons++].pitch,
                  "12-TET output changed keys");
    }
    check(ons == 4, "12-TET riff note count");
}

void testPitchBendOutput() {
    auto processor = player(2, 0, rastRiff());
    processor->setPreview(true);
    int elapsed = 0;
    const auto events = render(*processor, 740, elapsed);
    check(hasRpn(events, 1, 0, 2) && events.front().sample == 0 && events.front().message.isController(),
          "pitch-bend output must set the bend range before the first note");
    // -50 cents at ±2 semitones is a quarter of the downward range.
    checkBends(events, {8192, 6144, 8192, 6144}, "pitch-bend output lost a note's bend");
    for (const auto& event : events)
        check(event.message.getChannel() == 1, "pitch-bend output left channel 1");

    // The bend range follows the parameter, and a new start of playback repeats the setup.
    setParameter(*processor, "bendRange", 12);
    processor->setPreview(false);
    render(*processor, 1, elapsed);
    processor->setPreview(true);
    const auto again = render(*processor, 200, elapsed);
    check(hasRpn(again, 1, 0, 12), "changed bend range was not sent");
    checkBends(again, {8192, 8192 - 341}, "bend position ignored the bend range");
}

void testMpeOutput() {
    auto processor = player(2, 1, rastRiff());
    processor->setPreview(true);
    int elapsed = 0;
    const auto events = render(*processor, 740, elapsed);
    check(hasRpn(events, 1, 6, 15) && hasRpn(events, 2, 0, 48) && hasRpn(events, 1, 0, 2),
          "MPE output must declare a 15-channel lower zone at ±48");
    checkBends(events, {8192, 8107, 8192, 8107}, "MPE output lost a note's bend");
    std::vector<int> channels;
    for (const auto& event : events) {
        const auto& m = event.message;
        if (m.isNoteOn())
            channels.push_back(m.getChannel());
        if (m.isNoteOff()) {
            bool matched = false;
            for (const auto& on : events)
                matched |= on.message.isNoteOn() && on.sample <= event.sample &&
                           on.message.getNoteNumber() == m.getNoteNumber() &&
                           on.message.getChannel() == m.getChannel();
            check(matched, "MPE note-off left its note's channel");
        }
    }
    check(channels == std::vector<int>{2, 3, 4, 5}, "MPE notes must rotate through idle member channels");

    // A long overlapping chord needs one member channel per note.
    composer::Clip chord;
    chord.length = 4;
    chord.count = 3;
    for (int i = 0; i < 3; ++i)
        chord.notes[i] = {0, 4, 60 + i * 4, 100, false, i == 1 ? -50 : 0};
    processor->setClip(0, chord);
    const auto chordEvents = render(*processor, 30, elapsed);
    std::vector<int> chordChannels;
    for (const auto& event : chordEvents)
        if (event.message.isNoteOn())
            chordChannels.push_back(event.message.getChannel());
    check(chordChannels.size() == 3 && chordChannels[0] != chordChannels[1] &&
              chordChannels[1] != chordChannels[2] && chordChannels[0] != chordChannels[2],
          "overlapping MPE notes shared a channel");

    // Panic releases every member channel; switching the tuning off clears the zone.
    processor->panic();
    const auto panicEvents = render(*processor, 1, elapsed);
    int offs = 0, allOffs = 0;
    for (const auto& event : panicEvents) {
        offs += event.message.isNoteOff() && event.message.getChannel() > 1;
        allOffs += event.message.isAllNotesOff();
    }
    check(offs == 3 && allOffs == 16, "MPE panic must release notes on their member channels");
    processor->setPreview(true);
    render(*processor, 10, elapsed);
    setParameter(*processor, "microEnabled", 0);
    const auto offEvents = render(*processor, 1, elapsed);
    int memberOffs = 0, centred = 0;
    for (const auto& event : offEvents) {
        memberOffs += event.message.isNoteOff() && event.message.getChannel() > 1;
        centred += event.message.isPitchWheel() && event.message.getPitchWheelValue() == 8192;
    }
    check(memberOffs == 3 && centred == 15 && hasRpn(offEvents, 1, 6, 0),
          "leaving MPE must end notes on member channels, centre bends and clear the zone");
    processor->setPreview(false);
    render(*processor, 1, elapsed);
    processor->setPreview(true);
    const auto plain = render(*processor, 10, elapsed);
    int plainOns = 0;
    for (const auto& event : plain) {
        check(event.message.getChannel() == 1 && !event.message.isPitchWheel(),
              "12-TET output after MPE must return to plain channel 1");
        plainOns += event.message.isNoteOn();
    }
    check(plainOns == 3, "chord missing after leaving MPE");
}

// Rising zero crossings of a long note; the monitor waveform crosses zero twice per cycle.
int crossings(int cents) {
    composer::Clip clip;
    clip.length = 4;
    clip.count = 1;
    clip.notes[0] = {0, 4, 64, 100, false, cents};
    auto processor = player(2, 0, clip);
    setParameter(*processor, "monitor", 1);
    processor->setPreview(true);
    int elapsed = 0;
    std::vector<float> signal;
    render(*processor, 450, elapsed, &signal);
    int count = 0;
    for (size_t i = 9600; i < 57600; ++i)
        count += signal[i - 1] < 0 && signal[i] >= 0;
    return count;
}

void testInternalSound() {
    const int plain = crossings(0), lowered = crossings(-50);
    check(std::abs(plain - 329.63) <= 2, "internal sound plays E4 off pitch");
    check(std::abs(double(lowered) / plain - std::exp2(-50 / 1200.0)) < 0.01,
          "internal sound ignored a quarter-tone offset");
}

composer::Clip importedAfterExport(int output, bool importWithTuning, bool snap = false,
                                   const composer::Clip& clip = rastRiff()) {
    auto exporter = player(2, output, clip);
    setParameter(*exporter, "snap", snap ? 1.0f : 0.0f);
    setParameter(*exporter, "scale", 0);
    const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getNonexistentChildFile("genetic-microtonal", ".mid", false);
    check(exporter->exportMidi(file).wasOk(), "microtonal export failed");
    auto importer = player(importWithTuning ? 2 : 0, 0, {});
    check(importer->importMidi(file).wasOk(), "microtonal import failed");
    file.deleteFile();
    return importer->selectedClip();
}

void testExportImport() {
    for (int output : {0, 1}) {
        const auto restored = importedAfterExport(output, true);
        const auto original = rastRiff();
        check(restored.count == original.count, "microtonal round trip count");
        for (int i = 0; i < original.count; ++i)
            check(restored.notes[i].pitch == original.notes[i].pitch &&
                      restored.notes[i].cents == original.notes[i].cents,
                  "microtonal export/import must restore keys and offsets");
    }
    const auto ignored = importedAfterExport(1, false);
    for (int i = 0; i < ignored.count; ++i)
        check(ignored.notes[i].cents == 0 && ignored.notes[i].pitch == rastRiff().notes[i].pitch,
              "import with the tuning off must ignore bends");
    // Fit to scale projects the MIDI key onto the selected scale and preserves optimized cents.
    composer::Clip flat;
    flat.length = 4;
    flat.count = 1;
    flat.notes[0] = {0, 1, 61, 100, false, -37};
    const auto snapped = importedAfterExport(0, true, true, flat);
    check(snapped.count == 1 && snapped.notes[0].pitch == 60 && snapped.notes[0].cents == -37,
          "Fit to scale must preserve existing cents while snapping to the selected scale");
    flat.notes[0].cents = 0;
    const auto untuned = importedAfterExport(0, true, true, flat);
    check(untuned.count == 1 && untuned.notes[0].pitch == 60 && untuned.notes[0].cents == 0,
          "Fit to scale must not invent microtonal offsets after generation");
}

void checkSameBank(const composer::Bank& expected, const composer::Bank& actual) {
    for (int slot = 0; slot < composer::slotCount; ++slot) {
        const auto& a = expected[slot];
        const auto& b = actual[slot];
        check(a.count == b.count && a.length == b.length && a.grid == b.grid &&
                  a.numerator == b.numerator && a.denominator == b.denominator &&
                  a.seed == b.seed && a.favourite == b.favourite,
              "microtonal toggle changed clip metadata");
        for (int i = 0; i < a.count; ++i) {
            const auto& x = a.notes[i];
            const auto& y = b.notes[i];
            check(x.start == y.start && x.duration == y.duration && x.pitch == y.pitch &&
                      x.velocity == y.velocity && x.locked == y.locked && x.cents == y.cents,
                  "microtonal toggle rewrote a saved note");
        }
    }
}

void restoreTree(MicroProcessor& processor, const juce::ValueTree& tree) {
    const auto xml = tree.createXml();
    juce::MemoryBlock state;
    juce::AudioProcessor::copyXmlToBinary(*xml, state);
    processor.setStateInformation(state.getData(), int(state.getSize()));
}

void testStateAndToggle() {
    auto processor = player(2, 1, rastRiff());
    setParameter(*processor, "phraseType", 2);
    setParameter(*processor, "auditionChords", 1);
    juce::MemoryBlock state;
    processor->getStateInformation(state);
    MicroProcessor restored;
    restored.setStateInformation(state.getData(), int(state.getSize()));
    check(restored.tuningIndex() == 2 && restored.microtonalOutput() == 2, "tuning state round trip");
    check(restored.microtonesEnabled() &&
              restored.parameters.getRawParameterValue("phraseType")->load() == 2 &&
              restored.parameters.getRawParameterValue("auditionChords")->load() == 1,
          "new phrase, microtonal and accompaniment parameters did not round trip");
    for (int i = 0; i < 4; ++i)
        check(restored.selectedClip().notes[i].cents == rastRiff().notes[i].cents, "cents state round trip");

    const auto bank = processor->bankSnapshot();
    const auto revision = processor->documentRevision();
    setParameter(*processor, "microEnabled", 0);
    check(processor->tuningIndex() == 0 && processor->microtonalOutput() == 0,
          "Microtones OFF must override a retained tuning selection");
    checkSameBank(bank, processor->bankSnapshot());
    setParameter(*processor, "microEnabled", 1);
    check(processor->tuningIndex() == 2 && processor->microtonalOutput() == 2,
          "Microtones ON did not restore the selected tuning and output");
    checkSameBank(bank, processor->bankSnapshot());
    check(processor->documentRevision() == revision, "microtonal toggle created a document edit");
    setParameter(*processor, "tuning", 0);
    check(processor->tuningIndex() == 1 && processor->microtonalOutput() == 2,
          "Microtones ON with legacy tuning Off must select the first active palette");

    const auto xml = juce::AudioProcessor::getXmlFromBinary(state.getData(), int(state.getSize()));
    check(xml && xml->getIntAttribute("version") == 6, "new state must use schema version 6");
    const auto saved = juce::ValueTree::fromXml(*xml);
    for (int version : {2, 3, 4})
        for (int tuning : {0, 2}) {
            auto legacy = saved.createCopy();
            legacy.setProperty("version", version, nullptr);
            auto params = legacy.getChildWithName("Parameters");
            for (const char* id : {"microEnabled", "phraseType", "auditionChords"})
                params.removeChild(params.getChildWithProperty("id", id), nullptr);
            params.getChildWithProperty("id", "tuning").setProperty("value", float(tuning), nullptr);
            restoreTree(restored, legacy);
            check(restored.microtonesEnabled() == (tuning > 0) && restored.tuningIndex() == tuning &&
                      restored.parameters.getRawParameterValue("phraseType")->load() == 0 &&
                      restored.parameters.getRawParameterValue("auditionChords")->load() == 0,
                  "legacy state did not migrate tuning to Microtones or reset new parameters");
            checkSameBank(bank, restored.bankSnapshot());
        }

    // An explicit OFF in a new session wins over a retained nonzero tuning.
    auto disabled = saved.createCopy();
    disabled.getChildWithName("Parameters")
        .getChildWithProperty("id", "microEnabled")
        .setProperty("value", 0.0f, nullptr);
    for (int version : {4, 5}) {
        disabled.setProperty("version", version, nullptr);
        restoreTree(restored, disabled);
        check(!restored.microtonesEnabled() && restored.tuningIndex() == 0 &&
                  restored.microtonalOutput() == 0,
              "state migration ignored explicit Microtones OFF in favour of a retained tuning");
        checkSameBank(bank, restored.bankSnapshot());
    }

    auto preMicro = saved.createCopy();
    preMicro.setProperty("version", 2, nullptr);
    auto params = preMicro.getChildWithName("Parameters");
    for (const char* id : {"microEnabled", "tuning", "microtones", "microOutput", "bendRange"})
        params.removeChild(params.getChildWithProperty("id", id), nullptr);
    restoreTree(restored, preMicro);
    check(!restored.microtonesEnabled() && restored.microtonalOutput() == 0,
          "sessions predating microtones must open with microtones disabled");
}

void testGenerationWithMicrotonesOff() {
    auto processor = player(2, 1, rastRiff());
    setParameter(*processor, "microEnabled", 0);
    setParameter(*processor, "customSearch", 1);
    setParameter(*processor, "searchPopulation", 24);
    setParameter(*processor, "searchGenerations", 4);
    setParameter(*processor, "bars", 0);
    setParameter(*processor, "phraseType", 0);
    processor->generate(false);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (processor->isGenerating() && std::chrono::steady_clock::now() < deadline)
        juce::Thread::sleep(1);
    check(!processor->isGenerating(), "microtones-off generation timed out");
    for (const auto& clip : processor->bankSnapshot()) {
        check(clip.count > 0, "microtones-off generation produced an empty phrase");
        for (int i = 0; i < clip.count; ++i)
            check(clip.notes[i].cents == 0,
                  "Microtones OFF generated offsets from a retained nonzero tuning");
    }
}
} // namespace

void runMicrotonalTests() {
    testPlainOutputUnchanged();
    testPitchBendOutput();
    testMpeOutput();
    testInternalSound();
    testExportImport();
    testStateAndToggle();
    testGenerationWithMicrotonesOff();
}
