#include "InterfaceText.h"
#include "PluginProcessor.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <iterator>
#include <random>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
using RegressionProcessor = GeneticVSTComposerJUCEAudioProcessor;

void check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
void setParameter(RegressionProcessor& processor, const char* id, float value) {
    auto* parameter = processor.parameters.getParameter(id);
    check(parameter != nullptr, "regression parameter missing");
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}
void awaitSearch(RegressionProcessor& processor) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
    while (processor.isGenerating() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    check(!processor.isGenerating(), "search did not finish");
}
bool sameNotes(const composer::Clip& a, const composer::Clip& b) {
    if (a.count != b.count || a.length != b.length)
        return false;
    for (int i = 0; i < a.count; ++i)
        if (a.notes[size_t(i)].start != b.notes[size_t(i)].start ||
            a.notes[size_t(i)].pitch != b.notes[size_t(i)].pitch ||
            a.notes[size_t(i)].duration != b.notes[size_t(i)].duration)
            return false;
    return true;
}
void validate(const composer::Clip& clip, const char* where) {
    check(clip.count >= 0 && clip.count <= composer::maxNotes && clip.length > 0 && clip.length <= 128,
          where);
    for (int i = 0; i < clip.count; ++i) {
        const auto& note = clip.notes[size_t(i)];
        check(note.start >= 0 && note.start < clip.length && note.duration > 0 && note.pitch >= 0 &&
                  note.pitch <= 127 && note.velocity >= 1 && note.velocity <= 127 &&
                  std::abs(note.cents) <= 50 && (i == 0 || clip.notes[size_t(i - 1)].start <= note.start),
              where);
    }
}

void testSearchSurvivesPinsAndCaptures() {
    auto processor = std::make_unique<RegressionProcessor>();
    setParameter(*processor, "customSearch", 1);
    setParameter(*processor, "searchPopulation", 32);
    setParameter(*processor, "searchGenerations", 2000);
    const auto before = processor->bankSnapshot();
    processor->generate(false);
    processor->toggleFavourite(5);
    processor->captureComparison();
    processor->clearComparison();
    check(processor->isGenerating(), "the fixture must pin while the search is running");
    awaitSearch(*processor);
    const auto after = processor->bankSnapshot();
    check(after[5].favourite && sameNotes(after[5], before[5]),
          "a slot pinned during the search was replaced");
    bool replaced = false;
    for (int i = 0; i < composer::slotCount; ++i)
        replaced |= i != 5 && !sameNotes(after[size_t(i)], before[size_t(i)]);
    check(replaced && processor->statusText().startsWith("Ready"),
          "pinning or an A/B capture discarded the finished search");
    // Undo restores the bank as it was when the results arrived, pin included.
    processor->undo();
    check(processor->bankSnapshot()[5].favourite && sameNotes(processor->bankSnapshot()[0], before[0]),
          "undo after a pinned search lost the pin or the previous phrase");

    // With every slot pinned nothing is searched and no empty undo step appears.
    for (int i = 0; i < composer::slotCount; ++i)
        if (!processor->bankSnapshot()[size_t(i)].favourite)
            processor->toggleFavourite(i);
    const auto pinned = processor->bankSnapshot();
    processor->generate(false);
    check(!processor->isGenerating() && processor->statusText().startsWith("All variations are pinned"),
          "a search started with every slot pinned");
    check(sameNotes(processor->bankSnapshot()[0], pinned[0]), "an all-pinned bank changed");
}

void testSwingKeepsNotesApart() {
    auto processor = std::make_unique<RegressionProcessor>();
    setParameter(*processor, "swing", 0.5f);
    setParameter(*processor, "gate", 0.9f);
    composer::Clip run;
    run.length = 4;
    run.grid = 0.25;
    for (int i = 0; i < 16; ++i)
        run.notes[size_t(run.count++)] = {i * 0.25, 0.25, 64, 96, false, 0};
    processor->setClip(0, run);
    processor->selectClip(0);
    const auto file = juce::File::createTempFile(".mid");
    check(processor->exportMidi(file).wasOk(), "swing export failed");
    juce::FileInputStream stream(file);
    juce::MidiFile midi;
    check(midi.readFrom(stream), "swing export is not a MIDI file");
    auto sequence = *midi.getTrack(0);
    sequence.updateMatchedPairs();
    double previousEnd = -1;
    int notes = 0;
    for (int i = 0; i < sequence.getNumEvents(); ++i) {
        const auto* event = sequence.getEventPointer(i);
        if (!event->message.isNoteOn() || event->noteOffObject == nullptr)
            continue;
        const double start = event->message.getTimeStamp(),
                     end = event->noteOffObject->message.getTimeStamp();
        check(start + 1e-6 >= previousEnd, "a swung note runs into the next note of the same pitch");
        check(end > start, "swing produced an empty note");
        previousEnd = end;
        ++notes;
    }
    check(notes == 16, "swing export lost notes");
    file.deleteFile();
}

void testScopedSearchSurvivesPinsAndCaptures() {
    auto processor = std::make_unique<RegressionProcessor>();
    for (int i = 0; i < composer::slotCount; ++i)
        processor->toggleFavourite(i);
    processor->selectClip(3);
    setParameter(*processor, "customSearch", 1);
    setParameter(*processor, "searchPopulation", 16);
    setParameter(*processor, "searchGenerations", 1000);
    setParameter(*processor, "mutation", 1);
    const auto before = processor->bankSnapshot();
    processor->evolveRegion(0, 2);
    check(processor->isGenerating(), "explicit range editing was blocked by an all-pinned bank");
    processor->captureComparison();
    processor->toggleFavourite(4);
    processor->toggleFavourite(4);
    awaitSearch(*processor);
    const auto after = processor->bankSnapshot();
    check(processor->statusText().startsWith("Range optimised") && after[3].favourite,
          "pinning or an A/B capture discarded a scoped search or removed its pin");
    for (int i = 0; i < composer::slotCount; ++i)
        if (i != 3)
            check(sameNotes(before[size_t(i)], after[size_t(i)]) && after[size_t(i)].favourite,
                  "scoped search replaced another pinned variation");
    for (int i = 0; i < before[3].count; ++i) {
        const auto& note = before[3].notes[size_t(i)];
        if (note.start >= 2 || note.locked)
            check(std::any_of(after[3].notes.begin(), after[3].notes.begin() + after[3].count,
                              [&](const composer::Note& retained) {
                                  return retained.start == note.start && retained.duration == note.duration &&
                                         retained.pitch == note.pitch && retained.velocity == note.velocity &&
                                         retained.cents == note.cents && retained.locked == note.locked;
                              }),
                  "scoped search changed a protected note after an A/B capture");
    }
    processor->undo();
    check(sameNotes(processor->selectedClip(), before[3]) && processor->selectedClip().favourite,
          "scoped search undo lost the pinned source");
}

void testPhraseMenuAndHarmonisation() {
    auto processor = std::make_unique<RegressionProcessor>();
    for (int id = 1; id <= 5; ++id) {
        processor->setPhraseMenu(id);
        check(processor->phraseMenuId() == id, "phrase menu did not round-trip through its parameters");
    }
    processor->setPhraseMenu(3);
    juce::MemoryBlock state;
    processor->getStateInformation(state);
    auto restored = std::make_unique<RegressionProcessor>();
    restored->setStateInformation(state.getData(), int(state.getSize()));
    check(restored->phraseMenuId() == 3, "riff cell length was not saved with the session");

    composer::Clip arpeggios;
    arpeggios.length = 16;
    const int outline[4][4]{{60, 64, 67, 64}, {65, 69, 72, 69}, {67, 71, 74, 71}, {72, 67, 64, 60}};
    for (int bar = 0; bar < 4; ++bar)
        for (int beat = 0; beat < 4; ++beat)
            arpeggios.notes[size_t(arpeggios.count++)] = {
                bar * 4.0 + beat, 1, outline[bar][beat], 90, false, 0};
    processor->setClip(2, arpeggios);
    processor->selectClip(2);
    setParameter(*processor, "root", 0);
    setParameter(*processor, "scale", 0);
    setParameter(*processor, "chordOriginality", 0);
    setParameter(*processor, "chordRhythm", 1);
    check(processor->harmonySnapshot().count == 0, "fixture starts without custom harmony");
    check(processor->harmonizeMelody().wasOk(), "harmonising the selected melody failed");
    const auto harmony = processor->harmonySnapshot();
    const int expected[]{0, 5, 7, 0};
    check(harmony.count == 4 && harmony.length == 16, "harmony does not span the selected melody");
    for (int i = 0; i < 4; ++i)
        check(harmony.chords[size_t(i)].root == expected[i], "selected melody was not harmonised as C-F-G-C");
    check(processor->chordClipSnapshot().count >= 12 && sameNotes(processor->selectedClip(), arpeggios),
          "harmonisation must voice the chords and leave the melody unchanged");
    processor->undo();
    check(processor->harmonySnapshot().count == 0, "undo did not remove the fitted chords");
    composer::Clip empty;
    processor->setClip(4, empty);
    processor->selectClip(4);
    check(processor->harmonizeMelody().failed(), "an empty variation cannot be harmonised");

    processor->selectClip(2);
    double sum = 0;
    const auto terms = processor->fitnessBreakdown();
    for (const auto& term : terms)
        sum += term.value;
    check(terms.size() > 10 && std::isfinite(sum), "fitness explanation is empty or not finite");
}

void testPolishPlurals() {
    const auto polish = [](const char* english) { return interfaceText(english, 1); };
    check(polish("32 notes   /   4.0 bars   /   1 locked   /   3 microtonal") ==
              juce::String::fromUTF8("32 nuty   /   4 takty   /   1 zablokowana   /   3 mikrotonowe"),
          "Polish plural forms for 2-4 and 1");
    check(polish("5 notes   /   1.5 bars   /   0 locked") ==
              juce::String::fromUTF8("5 nut   /   1,5 taktu   /   0 zablokowanych"),
          "Polish plural forms for 5, 0 and fractions");
    check(polish("12 positions / 2 distinct chords / 22.0 bars") ==
              juce::String::fromUTF8("12 pozycji / 2 różne akordy / 22 takty"),
          "Polish plural forms for 12 and 22");
    check(
        polish(
            "Ready: 1 generations x 512 individuals. Audition a phrase; drag its piano roll to your DAW.") ==
            juce::String::fromUTF8(
                "Gotowe: 1 pokolenie × 512 osobników. Odsłuchaj frazę i przeciągnij jej piano roll do DAW."),
        "Polish plural forms in the search status");
    check(interfaceText("32 notes   /   4.0 bars", 0) == "32 notes   /   4.0 bars", "English text changed");
}

// Saved sessions and .genetic banks come from outside. Damaged or hostile state must be rejected or
// sanitised without crashing, and the processor must keep playing valid material.
void testStateFuzz() {
    auto source = std::make_unique<RegressionProcessor>();
    check(source->setCustomHarmony("Cm:4 Fm:2 G7:2").wasOk(), "fuzz fixture harmony");
    source->useSelectedAsGroove();
    source->captureComparison();
    juce::MemoryBlock valid;
    source->getStateInformation(valid);
    const auto xml = juce::AudioProcessor::getXmlFromBinary(valid.getData(), int(valid.getSize()));
    check(xml != nullptr, "fuzz fixture state");
    auto target = std::make_unique<RegressionProcessor>();
    target->prepareToPlay(48000, 256);
    juce::AudioBuffer<float> audio(2, 256);
    juce::MidiBuffer midi;
    std::mt19937 random(2026);
    const auto pick = [&](int count) { return std::uniform_int_distribution<int>(0, count - 1)(random); };
    static const char* values[]{"-1", "0",      "1e308", "-1e308", "nan",    "inf", "999999", "abc",
                                "",   "0.0001", "128",   "513",    "-50000", "4.5", "1/0",    "\xc3\xb3"};
    for (int round = 0; round < 240; ++round) {
        juce::MemoryBlock data;
        if (round % 3 == 0) {
            // Damaged bytes, as from a truncated or corrupted file.
            data = valid;
            auto* bytes = static_cast<uint8_t*>(data.getData());
            for (int flips = 1 + pick(8); flips > 0; --flips)
                bytes[size_t(pick(int(data.getSize())))] ^= uint8_t(1 + pick(255));
            if (pick(4) == 0)
                data.setSize(size_t(pick(int(data.getSize()))));
        } else {
            // Well-formed XML with hostile values, missing parts or too many notes.
            auto mutated = std::make_unique<juce::XmlElement>(*xml);
            std::vector<juce::XmlElement*> elements;
            std::function<void(juce::XmlElement&)> collect = [&](juce::XmlElement& element) {
                elements.push_back(&element);
                for (auto* child : element.getChildIterator())
                    collect(*child);
            };
            for (int edits = 1 + pick(6); edits > 0; --edits) {
                // Collected again each time: a removed element must not be edited afterwards.
                elements.clear();
                collect(*mutated);
                auto* element = elements[size_t(pick(int(elements.size())))];
                if (element->getNumAttributes() > 0 && pick(5) != 0)
                    element->setAttribute(element->getAttributeName(pick(element->getNumAttributes())),
                                          juce::String::fromUTF8(values[pick(int(std::size(values)))]));
                else if (element != mutated.get() && element->getNumChildElements() > 0 && pick(2) == 0)
                    element->removeChildElement(
                        element->getChildElement(pick(element->getNumChildElements())), true);
                else if (element->hasTagName("Clip"))
                    for (int extra = 0; extra < 520; ++extra)
                        element->createNewChildElement("Note")->setAttribute("start", extra * 0.1);
            }
            juce::AudioProcessor::copyXmlToBinary(*mutated, data);
        }
        target->setStateInformation(data.getData(), int(data.getSize()));
        for (const auto& clip : target->bankSnapshot())
            validate(clip, "restored bank contains an invalid phrase");
        validate(target->chordClipSnapshot(), "restored chords contain an invalid note");
        target->setPreview(true);
        for (int block = 0; block < 4; ++block) {
            midi.clear();
            target->processBlock(audio, midi);
        }
        for (int channel = 0; channel < audio.getNumChannels(); ++channel)
            for (int sample = 0; sample < audio.getNumSamples(); ++sample)
                check(std::isfinite(audio.getSample(channel, sample)),
                      "restored state produced invalid audio");
    }
    // Every parameter value replaced by NaN, an infinity or an extreme number.
    for (const char* hostile : {"nan", "inf", "-inf", "1e308", "-1e308"}) {
        auto mutated = std::make_unique<juce::XmlElement>(*xml);
        auto* parameters = mutated->getChildByName("Parameters");
        check(parameters != nullptr, "fuzz fixture parameters");
        for (auto* parameter : parameters->getChildIterator())
            parameter->setAttribute("value", hostile);
        juce::MemoryBlock data;
        juce::AudioProcessor::copyXmlToBinary(*mutated, data);
        target->setStateInformation(data.getData(), int(data.getSize()));
        for (auto* parameter : target->getParameters())
            check(std::isfinite(parameter->getValue()) && parameter->getValue() >= 0 &&
                      parameter->getValue() <= 1,
                  "a restored parameter is not a finite normalised value");
        target->setPreview(true);
        for (int block = 0; block < 8; ++block) {
            midi.clear();
            target->processBlock(audio, midi);
            for (int channel = 0; channel < audio.getNumChannels(); ++channel)
                for (int sample = 0; sample < audio.getNumSamples(); ++sample)
                    check(std::isfinite(audio.getSample(channel, sample)),
                          "hostile parameter values produced invalid audio");
        }
    }
    target->releaseResources();
}
} // namespace

void runProcessorRegressionTests() {
    testPolishPlurals();
    testSwingKeepsNotesApart();
    testPhraseMenuAndHarmonisation();
    testSearchSurvivesPinsAndCaptures();
    testScopedSearchSurvivesPinsAndCaptures();
    testStateFuzz();
}
