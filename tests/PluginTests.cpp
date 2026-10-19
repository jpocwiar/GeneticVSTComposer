#include "PluginEditor.h"
#include "PluginProcessor.h"
#include <chrono>
#include <iostream>
#include <stdexcept>
using Processor = GeneticVSTComposerJUCEAudioProcessor;
void require(bool b, const char* m) {
    if (!b)
        throw std::runtime_error(m);
}
void parameter(Processor& p, const char* id, float v) {
    auto* x = p.parameters.getParameter(id);
    x->setValueNotifyingHost(x->convertTo0to1(v));
}
struct PlayHead : juce::AudioPlayHead {
    double beat = 0, bpm = 120;
    bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo p;
        p.setPpqPosition(beat);
        p.setBpm(bpm);
        p.setIsPlaying(playing);
        return p;
    }
};
void runWorkflowTests();
void runVisualTelemetryTests();
int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI init;
    try {
        auto p = std::make_unique<Processor>();
        p->prepareToPlay(48000, 128);
        auto c = p->selectedClip();
        c.notes[0].locked = true;
        c.favourite = true;
        p->setClip(3, c);
        p->selectClip(3);
        parameter(*p, "swing", 0.3f);
        juce::MemoryBlock state;
        p->getStateInformation(state);
        auto restored = std::make_unique<Processor>();
        restored->setStateInformation(state.getData(), int(state.getSize()));
        auto r = restored->selectedClip();
        require(restored->selectedIndex() == 3 && r.notes[0].locked && r.favourite && r.count == c.count,
                "state round trip");
        require(std::abs(restored->parameters.getRawParameterValue("swing")->load() - 0.3f) < 0.001,
                "parameter restore");
        p->undo();
        require(!p->bankSnapshot()[3].favourite, "undo");
        p->redo();
        require(p->bankSnapshot()[3].favourite, "redo");
        p->setStateInformation("bad", 3);
        require(p->selectedClip().count == c.count, "invalid state corrupts bank");
        const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                              .getNonexistentChildFile("genetic-test", ".mid", false);
        require(p->exportMidi(file).wasOk(), "MIDI export");
        require(restored->importMidi(file).wasOk(), "MIDI import");
        require(restored->selectedClip().count == c.count, "MIDI round trip count");
        file.deleteFile();
        // A note lasting longer than one callback must emit its note-off in the proper block.
        composer::Clip longNote;
        longNote.length = 4;
        longNote.count = 1;
        longNote.notes[0] = {0, 1, 60, 100, false};
        p->setClip(0, longNote);
        p->selectClip(0);
        parameter(*p, "swing", 0);
        parameter(*p, "gate", 1);
        parameter(*p, "snap", 0);
        p->setPreview(true);
        int noteOns = 0, noteOffs = 0, offAt = -1;
        double energy = 0;
        for (int block = 0; block < 200; ++block) {
            juce::AudioBuffer<float> audio(2, 128);
            juce::MidiBuffer midi;
            p->processBlock(audio, midi);
            energy += audio.getRMSLevel(0, 0, 128);
            for (auto metadata : midi) {
                require(metadata.samplePosition >= 0 && metadata.samplePosition < 128,
                        "out of block MIDI timestamp");
                auto m = metadata.getMessage();
                if (m.isNoteOn())
                    ++noteOns;
                if (m.isNoteOff()) {
                    ++noteOffs;
                    offAt = block * 128 + metadata.samplePosition;
                }
            }
        }
        require(noteOns == 1 && noteOffs == 1 && std::abs(offAt - 24000) <= 1,
                "cross-block note-off scheduling");
        require(energy > 0, "internal audition silent");
        p->setPreview(false);
        {
            juce::AudioBuffer<float> audio(2, 128);
            juce::MidiBuffer midi;
            p->processBlock(audio, midi);
        }
        require(!p->isPlaying(), "preview stop");
        // A trigger beginning and ending inside the same callback must still sound.
        {
            juce::AudioBuffer<float> audio(2, 128);
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, 48, juce::uint8(100)), 10);
            midi.addEvent(juce::MidiMessage::noteOff(1, 48), 90);
            p->processBlock(audio, midi);
            bool on = false, off = false;
            for (auto e : midi) {
                on |= e.getMessage().isNoteOn() && e.samplePosition == 10;
                off |= e.getMessage().isNoteOff() && e.samplePosition == 90;
            }
            require(on && off, "sample accurate trigger");
        }
        PlayHead head;
        p->setPlayHead(&head);
        parameter(*p, "follow", 1);
        {
            juce::AudioBuffer<float> a(2, 128);
            juce::MidiBuffer m;
            p->processBlock(a, m);
            require(p->isPlaying(), "host start");
        }
        head.playing = false;
        {
            juce::AudioBuffer<float> a(2, 128);
            juce::MidiBuffer m;
            p->processBlock(a, m);
            bool off = false;
            for (auto e : m)
                off |= e.getMessage().isNoteOff();
            require(off && !p->isPlaying(), "host stop flush");
        }
        p->setPlayHead(nullptr);
        // Releasing an older held trigger must not stop the current one.
        parameter(*p, "follow", 0);
        {
            juce::AudioBuffer<float> audio(2, 128);
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, 48, juce::uint8(100)), 0);
            midi.addEvent(juce::MidiMessage::noteOn(1, 49, juce::uint8(100)), 20);
            midi.addEvent(juce::MidiMessage::noteOff(1, 48), 40);
            midi.addEvent(juce::MidiMessage::noteOff(1, 49), 100);
            p->processBlock(audio, midi);
            bool stoppedEarly = false, stoppedFinally = false;
            for (auto event : midi)
                if (event.getMessage().isNoteOff()) {
                    stoppedEarly |= event.samplePosition == 40;
                    stoppedFinally |= event.samplePosition == 100;
                }
            require(!stoppedEarly && stoppedFinally, "overlapping trigger ownership");
        }
        // Bypass and panic release active notes, including after transposition.
        p->selectClip(0);
        p->setPreview(true);
        {
            juce::AudioBuffer<float> audio(2, 128);
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, 67, juce::uint8(100)), 0);
            p->processBlock(audio, midi);
            midi.clear();
            p->processBlockBypassed(audio, midi);
            bool off = false;
            for (auto event : midi)
                off |= event.getMessage().isNoteOff() && event.getMessage().getNoteNumber() == 67;
            require(off && !p->isPlaying(), "bypass must release the emitted pitch");
        }
        // PPQ seeks must flush the previous voice and begin at the new position.
        longNote.count = 2;
        longNote.notes[1] = {1, 0.5, 64, 100, false};
        p->setClip(0, longNote);
        head.playing = true;
        head.beat = 0;
        p->setPlayHead(&head);
        parameter(*p, "follow", 1);
        {
            juce::AudioBuffer<float> audio(2, 128);
            juce::MidiBuffer midi;
            p->processBlock(audio, midi);
            head.beat = 1;
            midi.clear();
            p->processBlock(audio, midi);
            bool oldOff = false, newOn = false;
            for (auto event : midi) {
                oldOff |= event.getMessage().isNoteOff() && event.getMessage().getNoteNumber() == 60;
                newOn |= event.getMessage().isNoteOn() && event.getMessage().getNoteNumber() == 64 &&
                         event.samplePosition == 0;
            }
            require(oldOff && newOn, "host seek resynchronisation");
            p->panic();
            midi.clear();
            p->processBlock(audio, midi);
            require(!p->isPlaying(), "panic transport");
        }
        p->setPlayHead(nullptr);
        parameter(*p, "follow", 0);
        // Worker completion preserves pinned slots; cancellation cannot overwrite edits.
        parameter(*p, "quality", 0);
        const auto pinned = p->bankSnapshot()[3];
        auto awaitWorker = [&] {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (p->isGenerating() && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            require(!p->isGenerating(), "generation timeout");
        };
        p->generate(false);
        awaitWorker();
        require(p->bankSnapshot()[3].favourite && composer::similarity(pinned, p->bankSnapshot()[3]) == 1.0,
                "worker replaced a pinned phrase");
        p->generate(true);
        auto edited = p->selectedClip();
        edited.notes[0].pitch = 47;
        p->setClip(p->selectedIndex(), edited);
        awaitWorker();
        require(p->selectedClip().notes[0].pitch == 47, "stale worker overwrote a newer edit");
        runWorkflowTests();
        runVisualTelemetryTests();
        if (argc > 1) {
            auto demo = std::make_unique<Processor>();
            std::unique_ptr<juce::AudioProcessorEditor> editor(demo->createEditor());
            editor->setVisible(true);
            auto picture = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.5f);
            juce::FileOutputStream stream{juce::File(argv[1])};
            require(stream.openedOk(), "snapshot output");
            stream.setPosition(0);
            stream.truncate();
            juce::PNGImageFormat png;
            png.writeImageToStream(picture, stream);
            editor->setSize(870, 630);
            const auto compact = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
            juce::FileOutputStream small{juce::File(argv[1]).getSiblingFile("ui-v2-compact.png")};
            small.setPosition(0);
            small.truncate();
            png.writeImageToStream(compact, small);
            editor->setSize(1180, 900);
            auto* workflowEditor = dynamic_cast<GeneticVSTComposerJUCEAudioProcessorEditor*>(editor.get());
            require(workflowEditor != nullptr, "workflow editor type");
            auto snapshot = [&](const juce::String& name) {
                auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
                juce::FileOutputStream output{juce::File(argv[1]).getSiblingFile(name)};
                require(output.openedOk(), "workflow snapshot output");
                output.setPosition(0);
                output.truncate();
                png.writeImageToStream(image, output);
            };
            demo->setCustomHarmony("Cm:4 Fm:2 G7:2");
            demo->captureComparison();
            for (int page = 0; page < 5; ++page) {
                workflowEditor->showWorkflowPage(page);
                snapshot("ui-workflow-" + juce::String(page) + ".png");
            }
            demo->previewArrangement();
            workflowEditor->showWorkflowPage(3);
            snapshot("ui-arrangement.png");
            workflowEditor->showGuide();
            snapshot("ui-guide-pl.png");
            editor->setSize(870, 630);
            snapshot("ui-guide-compact.png");
            if (argc > 2) {
                auto animated = std::make_unique<Processor>();
                animated->prepareToPlay(48000, 800);
                std::unique_ptr<juce::AudioProcessorEditor> animatedBase(animated->createEditor());
                auto* animatedEditor =
                    dynamic_cast<GeneticVSTComposerJUCEAudioProcessorEditor*>(animatedBase.get());
                require(animatedEditor != nullptr, "animated editor type");
                animatedEditor->setSize(1180, 900);
                animatedEditor->setVisible(true);
                auto directory = juce::File(argv[2]);
                require(directory.createDirectory().wasOk(), "animation directory");
                animated->setPreview(true);
                juce::AudioBuffer<float> audio(2, 800);
                juce::MidiBuffer midi;
                for (int frame = 0; frame < 240; ++frame) {
                    midi.clear();
                    animated->processBlock(audio, midi);
                    animatedEditor->refreshDisplay();
                    if (frame % 2 == 0) {
                        auto picture = animatedEditor->createComponentSnapshot(
                            animatedEditor->getLocalBounds(), true, 0.8f);
                        juce::FileOutputStream output(directory.getChildFile(
                            "frame-" + juce::String(frame / 2).paddedLeft('0', 3) + ".png"));
                        output.setPosition(0);
                        output.truncate();
                        png.writeImageToStream(picture, output);
                    }
                    juce::Thread::sleep(16);
                }
                animated->setPreview(false);
            }
        }
        std::cout << "Plugin: session restore, undo/redo, MIDI round trip, note scheduling, audition, "
                     "triggers and host transport passed.\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
