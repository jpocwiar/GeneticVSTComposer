#include "AllocationGuard.h"
#include "MicrotonalComposerPanel.h"
#include "PluginEditor.h"
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>

namespace {
using Processor = GeneticVSTComposerJUCEAudioProcessor;
void check(bool b, const char* message) {
    if (!b)
        throw std::runtime_error(message);
}
void await(Processor& p) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (p.isGenerating() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    check(!p.isGenerating(), "micro worker timeout");
}
juce::Component* find(juce::Component& parent, const juce::String& id) {
    if (parent.getComponentID() == id)
        return &parent;
    for (auto* c : parent.getChildren())
        if (auto* result = find(*c, id))
            return result;
    return nullptr;
}
juce::ValueTree state(Processor& p) {
    juce::MemoryBlock data;
    p.getStateInformation(data);
    return juce::ValueTree::fromXml(
        *juce::AudioProcessor::getXmlFromBinary(data.getData(), int(data.getSize())));
}
void restore(Processor& p, const juce::ValueTree& tree) {
    juce::MemoryBlock data;
    juce::AudioProcessor::copyXmlToBinary(*tree.createXml(), data);
    p.setStateInformation(data.getData(), int(data.getSize()));
}
struct Head : juce::AudioPlayHead {
    double ppq = 0;
    bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo p;
        p.setPpqPosition(ppq);
        p.setBpm(120);
        p.setIsPlaying(playing);
        return p;
    }
};
} // namespace
void runMicrotonalLabTests(const juce::File& screenshots) {
    auto p = std::make_unique<Processor>();
    p->prepareToPlay(48000, 128);
    check(p->setCustomHarmony("Cm:4 Fm:4").wasOk(), "micro isolation harmony setup");
    const auto before = state(*p);
    const auto revision = p->documentRevision();
    auto s = p->microtonalSnapshot().settings;
    s.population = 16;
    s.generations = 8;
    s.beats = 4;
    s.tuning.divisions = 7;
    check(p->setMicrotonalSettings(s).wasOk(), "micro settings rejected");
    p->setMicrotonalMode(true);
    p->generateMicrotonal(false);
    await(*p);
    const auto generated = p->microtonalSnapshot();
    check(generated.clip.count > 0 && generated.clip.tuning.divisions == 7,
          "independent worker generated no EDO clip");
    p->generateMicrotonal(true);
    await(*p);
    check(p->microtonalSnapshot().stats.best.total + 1e-10 >= microtonal::evaluate(generated.clip, s).total,
          "worker evolution lost parent elite");
    const auto after = state(*p);
    check(before.getChildWithName("Parameters").isEquivalentTo(after.getChildWithName("Parameters")),
          "micro mode changed melody parameters");
    for (int i = 0; i < before.getNumChildren(); ++i)
        if (before.getChild(i).hasType("Clip"))
            check(before.getChild(i).isEquivalentTo(after.getChild(i)), "micro mode changed the melody bank");
    check(before.getChildWithName("ChordVoicing").isEquivalentTo(after.getChildWithName("ChordVoicing")) &&
              before.getProperty("harmony") == after.getProperty("harmony") &&
              revision == p->documentRevision(),
          "micro mode changed harmony or undo document");

    // Inject a known exact 7-EDO phrase into the independent state, not through MIDI-key storage.
    auto saved = after.createCopy();
    auto sequence = saved.getChildWithName("MicrotonalWorkspace").getChildWithName("Sequence");
    for (int i = sequence.getNumChildren() - 1; i >= 0; --i)
        if (sequence.getChild(i).hasType("Degree"))
            sequence.removeChild(i, nullptr);
    for (int i = 0; i < 4; ++i)
        sequence.addChild(
            juce::ValueTree("Degree",
                            {{"start", double(i)}, {"duration", 0.75}, {"degree", i}, {"velocity", 100}}),
            -1, nullptr);
    restore(*p, saved);
    const auto exact = p->microtonalSnapshot().clip;
    check(exact.count == 4 && exact.notes[1].degree == 1 && p->isMicrotonalMode(),
          "micro host-state round trip");
    juce::AudioBuffer<float> audio(2, 128);
    juce::MidiBuffer midi;
    midi.ensureSize(65536);
    // Consume the restore's panic before starting playback.
    p->processBlock(audio, midi);
    midi.clear();
    p->startMicrotonalAudition();
    double signal = 0;
    float previousSample = 0;
    std::vector<double> crossings;
    int noteOns = 0, noteOffs = 0, lastBend = 8192;
    for (int block = 0; block < 740; ++block) {
        midi.clear();
        if (block == 20)
            midi.addEvent(juce::MidiMessage::noteOn(1, 72, juce::uint8(100)), 10);
        long allocations;
        {
            composertest::AllocationGuard guard;
            p->processBlock(audio, midi);
            allocations = guard.count();
        }
        check(allocations == 0, "micro playback allocates on the audio thread");
        for (const auto event : midi) {
            const auto m = event.getMessage();
            if (m.isPitchWheel())
                lastBend = m.getPitchWheelValue();
            if (m.isNoteOff())
                ++noteOffs;
            if (m.isNoteOn()) {
                check(noteOns < 4, "conventional MIDI trigger affected micro playback");
                const double actual =
                    440 * std::exp2((m.getNoteNumber() - 69 + (lastBend - 8192) * 2.0 / 8192) / 12.0);
                const double expected = microtonal::frequency(exact, exact.notes[size_t(noteOns)].degree);
                check(std::abs(1200 * std::log2(actual / expected)) < 0.025,
                      "7-EDO output rounded to semitones or whole cents");
                ++noteOns;
            }
        }
        for (int i = 0; i < 128; ++i) {
            const float value = audio.getSample(0, i);
            const int elapsed = block * 128 + i;
            if (elapsed >= 30000 && elapsed <= 40000 && previousSample < 0 && value >= 0)
                crossings.push_back(elapsed - value / (value - previousSample));
            previousSample = value;
            signal += std::abs(value);
        }
    }
    check(noteOns == 4 && noteOffs == 4 && signal > 1, "micro playback notes, lifetimes or internal sound");
    check(crossings.size() >= 20 &&
              std::abs((crossings.size() - 1) * 48000.0 / (crossings.back() - crossings.front()) -
                       microtonal::frequency(exact, 1)) < 0.02,
          "internal monitor detuned an exact EDO frequency");
    check(p->selectedIndex() == int(before.getProperty("selected")), "micro input switched melody variation");
    const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getNonexistentChildFile("micro-lab-test", ".mid", false);
    for (bool mpe : {false, true}) {
        s = p->microtonalSnapshot().settings;
        s.mpe = mpe;
        p->setMicrotonalSettings(s);
        check(p->exportMicrotonalMidi(file).wasOk(), "micro MIDI export failed");
        juce::MidiFile exported;
        juce::FileInputStream input(file);
        check(exported.readFrom(input) && exported.getNumTracks() == 1, "micro exported MIDI unreadable");
        int ons = 0;
        std::array<int, 17> bends{};
        bends.fill(8192);
        for (int i = 0; i < exported.getTrack(0)->getNumEvents(); ++i) {
            const auto& m = exported.getTrack(0)->getEventPointer(i)->message;
            if (m.isPitchWheel())
                bends[size_t(m.getChannel())] = m.getPitchWheelValue();
            if (m.isNoteOn()) {
                check(m.getChannel() == (mpe ? 2 + ons % 15 : 1), "micro export channel encoding");
                const double pitch =
                    m.getNoteNumber() + (bends[size_t(m.getChannel())] - 8192) * (mpe ? 48.0 : 2.0) / 8192;
                const double frequency = 440 * std::exp2((pitch - 69) / 12);
                check(std::abs(1200 * std::log2(frequency / microtonal::frequency(exact, ons))) <
                          (mpe ? 0.59 : 0.025),
                      "micro export frequency");
                ++ons;
            }
        }
        check(ons == 4, "micro export lost notes");
    }
    file.deleteFile();
    // Live MPE must release before returning to the ordinary, independent melody path.
    p->panic();
    midi.clear();
    p->processBlock(audio, midi);
    p->startMicrotonalAudition();
    midi.clear();
    p->processBlock(audio, midi);
    bool member = false;
    for (const auto e : midi)
        member |= e.getMessage().isNoteOn() && e.getMessage().getChannel() >= 2;
    check(member, "micro MPE did not use member channels");
    p->setMicrotonalMode(false);
    midi.clear();
    p->processBlock(audio, midi);
    bool released = false;
    for (const auto e : midi)
        released |= e.getMessage().isNoteOff() && e.getMessage().getChannel() >= 2;
    check(released, "leaving micro mode left MPE notes sounding");
    p->setPreview(true);
    midi.clear();
    p->processBlock(audio, midi);
    for (const auto e : midi)
        if (e.getMessage().isNoteOn())
            check(e.getMessage().getChannel() == 1, "melody output stayed microtonal");

    restore(*p, saved);
    midi.clear();
    p->processBlock(audio, midi);
    Head head;
    head.playing = false;
    p->setPlayHead(&head);
    s = p->microtonalSnapshot().settings;
    s.followHost = true;
    p->setMicrotonalSettings(s);
    midi.clear();
    p->processBlock(audio, midi);
    head.playing = true;
    head.ppq = 0;
    midi.clear();
    p->processBlock(audio, midi);
    check(p->isPlaying(), "micro Follow DAW did not start");
    head.ppq = 2;
    midi.clear();
    p->processBlock(audio, midi);
    bool chased = false;
    for (const auto e : midi)
        chased |= e.getMessage().isNoteOn();
    check(chased, "micro Follow DAW did not chase a seek to an onset");
    head.ppq = 1.25;
    midi.clear();
    p->processBlock(audio, midi);
    bool sustained = false;
    for (const auto e : midi)
        sustained |= e.getMessage().isNoteOn();
    check(sustained, "micro Follow DAW did not chase a seek inside a sounding note");
    head.playing = false;
    midi.clear();
    p->processBlock(audio, midi);
    check(!p->isPlaying(), "micro Follow DAW did not stop");
    head.playing = true;
    head.ppq = 0;
    midi.clear();
    p->processBlock(audio, midi);
    p->stopMicrotonalAudition();
    for (int i = 0; i < 3; ++i) {
        midi.clear();
        p->processBlock(audio, midi);
    }
    check(!p->isPlaying() && !p->microtonalSnapshot().settings.followHost,
          "micro Stop resumed while the host kept playing");
    p->setPlayHead(nullptr);
    restore(*p, saved);
    const auto validState = state(*p);
    auto bad = validState.createCopy();
    bad.getChildWithName("MicrotonalWorkspace")
        .getChildWithName("Sequence")
        .getChildWithName("Degree")
        .setProperty("degree", 9999, nullptr);
    restore(*p, bad);
    check(state(*p).isEquivalentTo(validState), "invalid micro state partially replaced the document");
    auto legacy = validState.createCopy();
    legacy.removeChild(legacy.getChildWithName("MicrotonalWorkspace"), nullptr);
    restore(*p, legacy);
    check(!p->isMicrotonalMode() && p->microtonalSnapshot().clip.count == 0,
          "legacy session inherited micro workspace");

    // Exercise the embedded third tab, controls, translation and snapshots at both supported sizes.
    restore(*p, saved);
    std::unique_ptr<juce::AudioProcessorEditor> baseEditor(p->createEditor());
    auto* editor = dynamic_cast<GeneticVSTComposerJUCEAudioProcessorEditor*>(baseEditor.get());
    editor->setVisible(true);
    editor->showMicrotonalComposer();
    editor->refreshDisplay();
    auto* selector = dynamic_cast<juce::ComboBox*>(find(*editor, "microtonalSystem"));
    check(selector && selector->isVisible() && selector->getParentComponent()->isVisible(),
          "micro tab did not embed its own panel");
    dynamic_cast<juce::Slider*>(find(*editor, "microtonalDensity"))->setValue(0.61, juce::sendNotificationSync);
    check(p->microtonalSnapshot().settings.baseHz == exact.baseHz,
          "an unrelated UI control rounded the exact base frequency");
    selector->setSelectedId(8, juce::sendNotificationSync);
    editor->refreshDisplay();
    check(p->microtonalSnapshot().settings.tuning.divisions == 8 &&
              p->microtonalSnapshot().clip.tuning.divisions == 7,
          "tuning controls relabelled existing notes");
    auto* ratioEntry = dynamic_cast<juce::TextEditor*>(find(*editor, "microtonalRatios"));
    ratioEntry->setText("1/1 9/8 5/4 4/3 3/2 5/3 15/8");
    dynamic_cast<juce::TextButton*>(find(*editor, "applyMicrotonalRatios"))->onClick();
    check(p->microtonalSnapshot().settings.tuning.rational, "ratio control did not apply independent tuning");
    for (const auto size : {juce::Point<int>{1180, 900}, juce::Point<int>{870, 630}}) {
        editor->setSize(size.x, size.y);
        editor->refreshDisplay();
        for (const char* id :
             {"microtonalSystem", "generateMicrotonal", "playMicrotonal", "microtonalTempo"}) {
            auto* component = find(*editor, id);
            check(component && component->isVisible() &&
                      editor->getLocalBounds().contains(
                          editor->getLocalArea(component, component->getLocalBounds())),
                  "micro controls clipped at supported editor size");
        }
        if (screenshots != juce::File()) {
            screenshots.createDirectory();
            juce::FileOutputStream out(screenshots.getChildFile(
                size.x == 1180 ? "microtonal-lab.png" : "microtonal-lab-compact.png"));
            juce::PNGImageFormat().writeImageToStream(
                editor->createComponentSnapshot(editor->getLocalBounds()), out);
        }
    }
    p->setInterfaceLanguage(1);
    editor->refreshDisplay();
    if (screenshots != juce::File()) {
        editor->setSize(1180, 900);
        juce::FileOutputStream out(screenshots.getChildFile("microtonal-lab-pl.png"));
        juce::PNGImageFormat().writeImageToStream(editor->createComponentSnapshot(editor->getLocalBounds()), out);
    }
    check(dynamic_cast<juce::TextButton*>(find(*editor, "generateMicrotonal"))
              ->getButtonText()
              .contains(juce::String::fromUTF8("Generuj")),
          "micro panel ignores Polish language");
    editor->showChordComposer();
    check(!p->isMicrotonalMode() && !selector->getParentComponent()->isVisible(),
          "leaving micro tab did not hide it");
    std::cout << "Microtonal lab: isolation, worker, state/migration, precise MIDI/audio, MPE cleanup, no "
                 "audio allocations and embedded UI passed.\n";
}
