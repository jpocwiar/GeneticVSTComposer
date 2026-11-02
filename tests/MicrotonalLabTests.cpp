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
    int numerator = 4, denominator = 4;
    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo p;
        p.setPpqPosition(ppq);
        p.setBpm(120);
        p.setIsPlaying(playing);
        p.setTimeSignature(juce::AudioPlayHead::TimeSignature{numerator, denominator});
        return p;
    }
};
void testMicrotonalHarmonyWorkflow() {
    auto p = std::make_unique<Processor>();
    p->prepareToPlay(48000, 128);
    const auto conventional = state(*p);
    auto settings = p->microtonalSnapshot().settings;
    settings.population = 16;
    settings.generations = 8;
    settings.bars = 2;
    settings.numerator = 7;
    settings.denominator = 8;
    settings.division = 16;
    std::string error;
    check(microtonal::setGrouping(settings, "2+2+3", error), "workflow grouping");
    p->setMicrotonalSettings(settings);
    p->setMicrotonalMode(true);
    p->generateMicrotonal(false);
    await(*p);
    auto before = p->microtonalSnapshot();
    for (const auto& c : before.variations)
        check(c.count > 0 && c.length == 7 && c.numerator == 7, "micro bank was not filled");
    p->pinMicrotonalVariation(4);
    const auto pinned = state(*p)
                            .getChildWithName("MicrotonalWorkspace")
                            .getChildWithName("Variations")
                            .getChild(4)
                            .createCopy();
    p->generateMicrotonal(false);
    await(*p);
    check(pinned.isEquivalentTo(
              state(*p).getChildWithName("MicrotonalWorkspace").getChildWithName("Variations").getChild(4)),
          "micro regeneration replaced a pinned variation");
    auto chordSettings = p->microtonalSnapshot().chordSettings;
    chordSettings.rhythm = 2;
    p->setMicrotonalChordSettings(chordSettings);
    const auto melodyBefore =
        state(*p).getChildWithName("MicrotonalWorkspace").getChildWithName("Variations").createCopy();
    p->generateMicrotonalChords();
    await(*p);
    auto snapshot = p->microtonalSnapshot();
    check(snapshot.chords.polyphonic && snapshot.chords.count == 18 && snapshot.chords.length == 7,
          "micro chord worker did not respect meter and grouping");
    check(melodyBefore.isEquivalentTo(
              state(*p).getChildWithName("MicrotonalWorkspace").getChildWithName("Variations")),
          "chord-only generation changed melody variations");
    p->undoMicrotonal();
    check(p->microtonalSnapshot().chords.count == 0, "micro chord undo missing");
    p->redoMicrotonal();
    check(p->microtonalSnapshot().chords.count == 18, "micro chord redo missing");
    chordSettings.bars = 3;
    p->setMicrotonalChordSettings(chordSettings);
    p->generateMicrotonalChords(true);
    await(*p);
    snapshot = p->microtonalSnapshot();
    check(snapshot.chords.length == 10.5 && snapshot.clip.length == 10.5 && snapshot.clip.numerator == 7 &&
              microtonal::sameTuning(snapshot.clip.tuning, snapshot.chords.tuning),
          "New chords + melody did not commit a compatible pair");
    p->generateMicrotonalFromChords();
    await(*p);
    check(p->microtonalSnapshot().clip.length == snapshot.chords.length,
          "melody from micro chords uses the wrong duration");
    const auto saved = state(*p);
    restore(*p, saved);
    check(state(*p).isEquivalentTo(saved), "micro bank/chords/settings state round trip");
    check(saved.getChildWithName("Parameters").isEquivalentTo(conventional.getChildWithName("Parameters")),
          "micro harmony changed conventional parameters");
    const auto pairFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                              .getNonexistentChildFile("micro-pair-test", ".mid", false);
    check(p->exportMicrotonalMidi(pairFile, 2).wasOk(), "combined micro MIDI export");
    {
        juce::MidiFile exported;
        juce::FileInputStream stream(pairFile);
        check(exported.readFrom(stream), "combined micro export unreadable");
        bool meter = false, grouping = false;
        int count = 0;
        for (int i = 0; i < exported.getTrack(0)->getNumEvents(); ++i) {
            const auto m = exported.getTrack(0)->getEventPointer(i)->message;
            if (m.isTimeSignatureMetaEvent()) {
                int numerator = 0, denominator = 0;
                m.getTimeSignatureInfo(numerator, denominator);
                meter |= numerator == 7 && denominator == 8;
            }
            if (m.isTextMetaEvent())
                grouping |= m.getTextFromTextMetaEvent().contains("RHYTHMS_GROUPS:2+2+3@8");
            count += m.isNoteOn();
        }
        check(meter && grouping && count > p->microtonalSnapshot().clip.count,
              "combined export lost meter, grouping or chord voices");
    }
    pairFile.deleteFile();
    for (int i = 0; i < conventional.getNumChildren(); ++i)
        if (conventional.getChild(i).hasType("Clip"))
            check(conventional.getChild(i).isEquivalentTo(saved.getChild(i)),
                  "micro harmony changed conventional melody bank");
    // Near pitches may share a MIDI key; each simultaneous voice still owns its channel.
    auto polyState = saved.createCopy();
    auto chords = polyState.getChildWithName("MicrotonalWorkspace").getChildWithName("Chords");
    chords.removeAllChildren(nullptr);
    chords.setProperty("baseHz", 440.0, nullptr);
    chords.setProperty("length", 4.0, nullptr);
    chords.setProperty("numerator", 4, nullptr);
    chords.setProperty("denominator", 4, nullptr);
    chords.setProperty("groups", "", nullptr);
    chords.setProperty("groupUnit", 4, nullptr);
    chords.addChild(juce::ValueTree("PitchSpace", {{"rational", false}, {"divisions", 72}, {"ratios", ""}}),
                    -1, nullptr);
    for (int degree = 0; degree < 3; ++degree)
        chords.addChild(
            juce::ValueTree("Degree",
                            {{"start", 0.0}, {"duration", 1.0}, {"degree", degree}, {"velocity", 100}}),
            -1, nullptr);
    restore(*p, polyState);
    juce::AudioBuffer<float> audio(2, 128);
    juce::MidiBuffer midi;
    midi.ensureSize(65536);
    p->processBlock(audio, midi);
    p->startMicrotonalAudition(1);
    std::array<int, 17> bends{};
    bends.fill(8192);
    std::array<int, 17> active{};
    int ons = 0, offs = 0;
    for (int block = 0; block < 450; ++block) {
        midi.clear();
        long allocations;
        {
            composertest::AllocationGuard guard;
            p->processBlock(audio, midi);
            allocations = guard.count();
        }
        check(allocations == 0, "polyphonic micro playback allocates");
        for (const auto e : midi) {
            const auto m = e.getMessage();
            const int channel = m.getChannel();
            if (m.isPitchWheel())
                bends[size_t(channel)] = m.getPitchWheelValue();
            if (m.isNoteOn()) {
                check(m.getNoteNumber() == 69 && channel >= 2 && active[size_t(channel)] == 0,
                      "close microtonal voices collided on one MIDI key/channel");
                const double pitch = m.getNoteNumber() + (bends[size_t(channel)] - 8192) * 48.0 / 8192;
                check(std::abs((pitch - 69) * 100 - ons * 1200.0 / 72) < 0.59,
                      "polyphonic micro bend frequency");
                ++active[size_t(channel)];
                ++ons;
            }
            if (m.isNoteOff()) {
                check(active[size_t(channel)] == 1, "micro note-off lost voice ownership");
                --active[size_t(channel)];
                ++offs;
            }
        }
    }
    check(ons == 3 && offs == 3, "polyphonic micro lost close simultaneous voices");
    const auto midiFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                              .getNonexistentChildFile("micro-chord-test", ".mid", false);
    check(p->exportMicrotonalMidi(midiFile, 1).wasOk(), "polyphonic micro chord export failed");
    {
        juce::MidiFile file;
        juce::FileInputStream stream(midiFile);
        check(file.readFrom(stream), "micro chord export unreadable");
        int notes = 0;
        for (int i = 0; i < file.getTrack(0)->getNumEvents(); ++i) {
            const auto m = file.getTrack(0)->getEventPointer(i)->message;
            if (m.isNoteOn()) {
                ++notes;
                check(m.getChannel() >= 2, "poly chord export did not force MPE");
            }
        }
        check(notes == 3, "poly chord export lost voices");
    }
    midiFile.deleteFile();
    // Own host-meter control must not change the conventional meter parameters.
    Head head;
    head.playing = false;
    head.numerator = 5;
    head.denominator = 8;
    p->setPlayHead(&head);
    settings = p->microtonalSnapshot().settings;
    settings.hostMeter = true;
    p->setMicrotonalSettings(settings);
    midi.clear();
    p->processBlock(audio, midi);
    p->serviceMessageThread();
    check(p->microtonalSnapshot().settings.numerator == 5 &&
              p->microtonalSnapshot().settings.denominator == 8,
          "own Host meter did not follow the DAW");
    check(p->parameters.getRawParameterValue("meter")->load() == 0,
          "own Host meter changed conventional meter");
    p->setPlayHead(nullptr);
    settings = p->microtonalSnapshot().settings;
    settings.bars = 0;
    settings.beats = 0.125;
    settings.numerator = 1;
    settings.denominator = 32;
    settings.groupCount = 0;
    settings.groupUnit = 32;
    settings.hostMeter = false;
    settings.evolveEachLoop = true;
    settings.population = 8;
    settings.generations = 1;
    settings.tempo = 400;
    p->setMicrotonalSettings(settings);
    microtonal::Clip shortLoop;
    shortLoop.tuning = settings.tuning;
    shortLoop.baseHz = settings.baseHz;
    shortLoop.length = 0.125;
    shortLoop.numerator = 1;
    shortLoop.denominator = 32;
    shortLoop.grid = 0.125;
    shortLoop.groupUnit = 32;
    shortLoop.count = 1;
    shortLoop.notes[0] = {0, 0.1, 0, 96, false};
    p->setMicrotonalClip(shortLoop);
    p->stopMicrotonalAudition();
    midi.clear();
    p->processBlock(audio, midi);
    p->startMicrotonalAudition();
    for (int i = 0; i < 10; ++i) {
        midi.clear();
        p->processBlock(audio, midi);
    }
    p->serviceMessageThread();
    await(*p);
    check(p->microtonalSnapshot().stats.generation == 1 &&
              p->microtonalSnapshot().stats.rhythmEvaluations > 0,
          "micro evolve-each-loop was not serviced outside the audio thread");
}
} // namespace
void runMicrotonalLabTests(const juce::File& screenshots) {
    testMicrotonalHarmonyWorkflow();
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
    auto laboratory = validState.createCopy();
    auto workspaceV1 = laboratory.getChildWithName("MicrotonalWorkspace");
    workspaceV1.setProperty("version", 1, nullptr);
    for (const char* child : {"Variations", "Chords", "ChordSettings"})
        workspaceV1.removeChild(workspaceV1.getChildWithName(child), nullptr);
    workspaceV1.removeProperty("selected", nullptr);
    restore(*p, laboratory);
    check(p->microtonalSnapshot().selected == 0 &&
              p->microtonalSnapshot().variations[0].count == exact.count &&
              p->microtonalSnapshot().chords.count == 0,
          "version-1 laboratory did not migrate into the variation bank");
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
    dynamic_cast<juce::Slider*>(find(*editor, "microtonalDensity"))
        ->setValue(0.61, juce::sendNotificationSync);
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
            out.setPosition(0); out.truncate();
            juce::PNGImageFormat().writeImageToStream(
                editor->createComponentSnapshot(editor->getLocalBounds()), out);
        }
    }
    p->setInterfaceLanguage(1);
    editor->refreshDisplay();
    if (screenshots != juce::File()) {
        editor->setSize(1180, 900);
        juce::FileOutputStream out(screenshots.getChildFile("microtonal-lab-pl.png"));
        out.setPosition(0); out.truncate();
        juce::PNGImageFormat().writeImageToStream(editor->createComponentSnapshot(editor->getLocalBounds()),
                                                  out);
    }
    check(dynamic_cast<juce::TextButton*>(find(*editor, "generateMicrotonal"))
              ->getButtonText()
              .contains(juce::String::fromUTF8("Generuj")),
          "micro panel ignores Polish language");
    auto* meter = dynamic_cast<juce::ComboBox*>(find(*editor, "microtonalMeter"));
    meter->setText("7/8", juce::sendNotificationSync);
    editor->refreshDisplay();
    dynamic_cast<juce::ComboBox*>(find(*editor, "microtonalBars"))
        ->setSelectedId(3, juce::sendNotificationSync);
    editor->refreshDisplay();
    dynamic_cast<juce::ComboBox*>(find(*editor, "microtonalGrouping"))
        ->setText("3+2+2", juce::sendNotificationSync);
    editor->refreshDisplay();
    check(p->microtonalSnapshot().settings.numerator == 7 &&
              p->microtonalSnapshot().settings.denominator == 8 &&
              p->microtonalSnapshot().settings.bars == 2 && p->microtonalSnapshot().settings.groupCount == 3,
          "micro UI did not commit meter, bars and grouping");
    dynamic_cast<juce::TextButton*>(find(*editor, "microtonalChordsTab"))->onClick();
    editor->refreshDisplay();
    dynamic_cast<juce::TextButton*>(find(*editor, "generateMicrotonalChords"))->onClick();
    await(*p);
    editor->refreshDisplay();
    check(p->microtonalSnapshot().chords.count > 0 && p->microtonalSnapshot().chords.length == 7,
          "micro Chords UI did not generate its own sequence");
    dynamic_cast<juce::TextButton*>(find(*editor, "microtonalMelodyFromChords"))->onClick();
    await(*p);
    editor->refreshDisplay();
    check(p->microtonalSnapshot().clip.length == 7 && p->microtonalSnapshot().clip.tuning.rational,
          "micro UI melody-from-chords action did not use the chord tuning and duration");
    for (const auto size : {juce::Point<int>{1180, 900}, juce::Point<int>{870, 630}}) {
        editor->setSize(size.x, size.y);
        editor->refreshDisplay();
        for (const char* id : {"generateMicrotonalChords", "microtonalChordRhythm", "microtonalPlayTogether",
                               "revoiceMicrotonalChords"}) {
            auto* c = find(*editor, id);
            check(c && c->isVisible() &&
                      editor->getLocalBounds().contains(editor->getLocalArea(c, c->getLocalBounds())),
                  "micro chord controls clipped");
        }
        if (screenshots != juce::File()) {
            juce::FileOutputStream out(screenshots.getChildFile(
                size.x == 1180 ? "microtonal-chords.png" : "microtonal-chords-compact.png"));
            out.setPosition(0); out.truncate();
            juce::PNGImageFormat().writeImageToStream(
                editor->createComponentSnapshot(editor->getLocalBounds()), out);
        }
    }
    editor->showChordComposer();
    check(!p->isMicrotonalMode() && !selector->getParentComponent()->isVisible(),
          "leaving micro tab did not hide it");
    std::cout << "Microtonal lab: isolation, worker, state/migration, precise MIDI/audio, MPE cleanup, no "
                 "audio allocations and embedded UI passed.\n";
}
