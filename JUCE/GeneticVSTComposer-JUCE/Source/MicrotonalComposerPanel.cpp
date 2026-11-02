#include "MicrotonalComposerPanel.h"
#include "ComposerTheme.h"
#include "EditorFormat.h"
#include "PluginEditor.h"
#include <cmath>

using namespace palette;

struct MicrotonalComposerPanel::Impl {
    MicrotonalComposerPanel& owner;
    GeneticVSTComposerJUCEAudioProcessor& processor;
    microtonal::Settings settings;
    microtonal::Clip clip;
    GeneticVSTComposerJUCEAudioProcessor::MicrotonalWorkspace workspace;
    bool chordPage = false;
    int controlsPage = 0;
    juce::ComboBox system, output;
    juce::ComboBox bars, meter, grid, grouping, phrase, mode, chordLength, chordRhythm, chordShape, voicing;
    juce::TextEditor ratios;
    juce::Slider divisions, base, octaves, beats, density, consonance, diversity, movement, mutation,
        population, generations, tempo;
    juce::Slider dynamics, syncopation, space, arousal, weirdness, swing, gate, humanize, crossover,
        originality, colour, brightness, chordVoices;
    juce::ToggleButton monitor, follow;
    juce::ToggleButton keepRhythm, editNotes, resolve, hostMeter, eachLoop;
    juce::TextButton generate, evolve, applyRatios, exportFile;
    juce::TextButton melodyTab, chordsTab, characterTab, rhythmTab, searchTab, undo, redo;
    juce::TextButton newChords, fromChords, newPair, revoice, exportPair;
    std::array<juce::TextButton, microtonal::slotCount> variants;
    TransportButton play;
    TransportButton together;
    MidiDragButton drag;
    juce::Label title, subtitle, info, stats, feedback;
    std::unique_ptr<juce::FileChooser> chooser;
    std::vector<juce::File> temporaryFiles;
    bool updating = false;
    int language = -1;
    int controlsLanguage = -1;
    uint64_t revision = 0;

    juce::String tr(const char* en, const char* pl) const {
        return juce::String::fromUTF8(processor.interfaceLanguageIndex() == 1 ? pl : en);
    }

    struct DegreeRoll : juce::Component {
        Impl& impl;
        int selected = 0;
        explicit DegreeRoll(Impl& p) : impl(p) {}
        juce::Rectangle<float> plot() const {
            return getLocalBounds().toFloat().reduced(8).withTrimmedLeft(52).withTrimmedTop(20);
        }
        int top() const {
            int result = impl.clip.tuning.divisions;
            for (int i = 0; i < impl.clip.count; ++i)
                result = std::max(result, impl.clip.notes[size_t(i)].degree);
            return result + 1;
        }
        juce::Rectangle<float> noteBounds(int index) const {
            const auto area = plot();
            const auto& n = impl.clip.notes[size_t(index)];
            const float lane = area.getHeight() / (top() + 1);
            return {area.getX() + float(n.start / impl.clip.length) * area.getWidth(),
                    area.getBottom() - (n.degree + 1) * lane,
                    std::max(3.0f, float(n.duration / impl.clip.length) * area.getWidth()),
                    std::max(2.0f, lane - 1)};
        }
        void paint(juce::Graphics& g) override {
            g.setColour(panel);
            g.fillRoundedRectangle(getLocalBounds().toFloat(), 8);
            const auto area = plot();
            const float lane = area.getHeight() / (top() + 1);
            const int stride = std::max(1, int(std::ceil(12.0f / lane)));
            g.setFont(font(10));
            for (int d = 0; d <= top(); ++d) {
                const float y = area.getBottom() - (d + 1) * lane;
                const bool octave = d % impl.clip.tuning.divisions == 0;
                g.setColour(octave ? violet.withAlpha(0.22f) : line.withAlpha(0.35f));
                g.drawHorizontalLine(int(y), area.getX(), area.getRight());
                if (d % stride == 0 || octave) {
                    g.setColour(octave ? violet : muted);
                    g.drawText(juce::String(d), 6, int(y - 3), 46, 13, juce::Justification::centredRight);
                }
            }
            const double bar = impl.clip.numerator * 4.0 / impl.clip.denominator;
            const int barStride = std::max(1, int(std::ceil(impl.clip.length / bar / 16)));
            for (double beat = 0; beat <= impl.clip.length + 1e-8; beat += 4.0 / impl.clip.denominator) {
                const float x = area.getX() + float(beat / impl.clip.length) * area.getWidth();
                auto metric = impl.settings;
                metric.numerator = impl.clip.numerator;
                metric.denominator = impl.clip.denominator;
                metric.groups = impl.clip.groups;
                metric.groupCount = impl.clip.groupCount;
                metric.groupUnit = impl.clip.groupUnit;
                const int weight = microtonal::metricWeight(beat, metric);
                g.setColour(line.withAlpha(weight == 3 ? 0.9f : weight == 2 ? 0.6f : 0.2f));
                g.drawVerticalLine(int(x), area.getY(), area.getBottom());
                if (weight == 3 && int(std::lround(beat / bar)) % barStride == 0) {
                    g.setColour(muted);
                    g.drawText(juce::String(1 + int(std::lround(beat / bar))), int(x), 4, 30, 15,
                               juce::Justification::left);
                }
            }
            if (impl.clip.count == 0) {
                g.setColour(muted);
                g.setFont(font(15));
                g.drawFittedText(impl.tr("Generate a sequence to explore this pitch space.",
                                         "Wygeneruj sekwencję, aby zbadać ten system wysokości."),
                                 area.toNearestInt().reduced(20), juce::Justification::centred, 2);
            }
            for (int i = 0; i < impl.clip.count; ++i) {
                g.setColour(impl.clip.notes[size_t(i)].locked ? gold
                            : i == selected                   ? accent
                                                              : violet.withAlpha(0.8f));
                g.fillRoundedRectangle(noteBounds(i), 2);
            }
            if (impl.processor.isMicrotonalMode() && impl.processor.isPlaying()) {
                const float x =
                    area.getX() + float(impl.processor.playPosition() / impl.clip.length) * area.getWidth();
                g.setColour(teal);
                g.drawVerticalLine(int(x), area.getY(), area.getBottom());
            }
        }
        void mouseDown(const juce::MouseEvent& e) override {
            for (int i = 0; i < impl.clip.count; ++i)
                if (noteBounds(i).expanded(1, 3).contains(e.position)) {
                    selected = i;
                    impl.describeNote();
                    if (!impl.chordPage && e.mods.isShiftDown()) {
                        impl.clip.notes[size_t(i)].locked = !impl.clip.notes[size_t(i)].locked;
                        impl.processor.setMicrotonalClip(impl.clip);
                    }
                    repaint();
                    break;
                }
        }
        void mouseDoubleClick(const juce::MouseEvent& e) override {
            if (impl.chordPage || !impl.editNotes.getToggleState())
                return;
            auto next = impl.clip;
            const auto area = plot();
            if (!area.contains(e.position))
                return;
            const double start =
                std::floor((e.position.x - area.getX()) / area.getWidth() * next.length / next.grid) *
                next.grid;
            const int degree =
                std::clamp(int((area.getBottom() - e.position.y) / area.getHeight() * (top() + 1)), 0,
                           next.tuning.divisions * 3);
            for (int i = 0; i < next.count; ++i)
                if (noteBounds(i).expanded(1, 2).contains(e.position)) {
                    if (next.notes[size_t(i)].locked)
                        return;
                    for (int j = i; j + 1 < next.count; ++j)
                        next.notes[size_t(j)] = next.notes[size_t(j + 1)];
                    --next.count;
                    impl.processor.setMicrotonalClip(next);
                    return;
                }
            if (next.count >= microtonal::maxEvents)
                return;
            next.notes[size_t(next.count++)] = {start, std::min(next.grid * 0.9, next.length - start), degree,
                                                96, false};
            std::stable_sort(
                next.notes.begin(), next.notes.begin() + next.count,
                [](const microtonal::Event& a, const microtonal::Event& b) { return a.start < b.start; });
            impl.processor.setMicrotonalClip(next);
        }
    } roll{*this};

    void add(juce::Component& c, const char* id) {
        c.setComponentID(id);
        owner.addAndMakeVisible(c);
    }
    void slider(juce::Slider& s, const char* id, double low, double high, double step) {
        add(s, id);
        s.setRange(low, high, step);
        s.setSliderStyle(juce::Slider::LinearHorizontal);
        s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 72, 22);
        s.onValueChange = [this] { commit(); };
    }
    explicit Impl(MicrotonalComposerPanel& o, GeneticVSTComposerJUCEAudioProcessor& p)
        : owner(o), processor(p) {
        add(system, "microtonalSystem");
        add(output, "microtonalOutputMode");
        add(ratios, "microtonalRatios");
        add(applyRatios, "applyMicrotonalRatios");
        add(generate, "generateMicrotonal");
        add(evolve, "evolveMicrotonal");
        add(play, "playMicrotonal");
        add(exportFile, "exportMicrotonal");
        add(drag, "dragMicrotonal");
        add(monitor, "microtonalMonitor");
        add(follow, "microtonalFollow");
        add(roll, "microtonalDegreeRoll");
        for (auto* component : std::initializer_list<juce::Component*>{
                 &bars,        &meter,      &grid,       &grouping,     &phrase,    &mode,      &chordLength,
                 &chordRhythm, &chordShape, &voicing,    &keepRhythm,   &editNotes, &resolve,   &hostMeter,
                 &eachLoop,    &melodyTab,  &chordsTab,  &characterTab, &rhythmTab, &searchTab, &undo,
                 &redo,        &newChords,  &fromChords, &newPair,      &revoice,   &together,  &exportPair})
            owner.addAndMakeVisible(component);
        bars.setComponentID("microtonalBars");
        meter.setComponentID("microtonalMeter");
        grid.setComponentID("microtonalGrid");
        grouping.setComponentID("microtonalGrouping");
        phrase.setComponentID("microtonalPhraseType");
        mode.setComponentID("microtonalMode");
        keepRhythm.setComponentID("microtonalKeepRhythm");
        editNotes.setComponentID("microtonalEditNotes");
        hostMeter.setComponentID("microtonalHostMeter");
        eachLoop.setComponentID("microtonalEachLoop");
        chordsTab.setComponentID("microtonalChordsTab");
        melodyTab.setComponentID("microtonalMelodyTab");
        newChords.setComponentID("generateMicrotonalChords");
        fromChords.setComponentID("microtonalMelodyFromChords");
        newPair.setComponentID("microtonalNewPair");
        revoice.setComponentID("revoiceMicrotonalChords");
        chordLength.setComponentID("microtonalChordLength");
        chordRhythm.setComponentID("microtonalChordRhythm");
        chordShape.setComponentID("microtonalChordShape");
        voicing.setComponentID("microtonalVoicing");
        resolve.setComponentID("microtonalResolve");
        together.setComponentID("microtonalPlayTogether");
        undo.setComponentID("microtonalUndo");
        redo.setComponentID("microtonalRedo");
        for (auto* l : {&title, &subtitle, &info, &stats, &feedback}) {
            owner.addAndMakeVisible(*l);
            l->setColour(juce::Label::textColourId, muted);
            l->setFont(font(12));
        }
        title.setColour(juce::Label::textColourId, text);
        title.setFont(font(23, true));
        feedback.setFont(font(11));
        ratios.setMultiLine(false);
        ratios.setText("1/1 9/8 7/6 5/4 4/3 3/2 7/4");
        ratios.onReturnKey = [this] { apply(); };
        slider(divisions, "microtonalDivisions", 2, 72, 1);
        // Keep exact saved frequencies when another control commits the form. The displayed
        // three decimals must not quantize the underlying base or turn Evolve into a new search.
        slider(base, "microtonalBaseHz", 20, 1000, 0);
        slider(octaves, "microtonalOctaves", 1, 3, 1);
        slider(beats, "microtonalBeats", 0.125, 128, 0);
        slider(tempo, "microtonalTempo", 20, 400, 1);
        slider(density, "microtonalDensity", 0, 1, 0.01);
        slider(consonance, "microtonalConsonance", 0, 1, 0.01);
        slider(diversity, "microtonalDiversity", 0, 1, 0.01);
        slider(movement, "microtonalMovement", 0, 1, 0.01);
        slider(mutation, "microtonalMutation", 0, 1, 0.01);
        slider(population, "microtonalPopulation", 8, 512, 1);
        slider(generations, "microtonalGenerations", 1, 2000, 1);
        slider(dynamics, "microtonalDynamics", 0, 1, 0.01);
        slider(syncopation, "microtonalSyncopation", 0, 1, 0.01);
        slider(space, "microtonalSpace", 0, 1, 0.01);
        slider(arousal, "microtonalArousal", 0, 1, 0.01);
        slider(weirdness, "microtonalWeirdness", 0, 1, 0.01);
        slider(swing, "microtonalSwing", 0, 0.75, 0.01);
        slider(gate, "microtonalGate", 0.1, 1, 0.01);
        slider(humanize, "microtonalHumanize", 0, 1, 0.01);
        slider(crossover, "microtonalCrossover", 0, 1, 0.01);
        slider(originality, "microtonalChordOriginality", 0, 1, 0.01);
        slider(colour, "microtonalChordColour", 0, 1, 0.01);
        slider(brightness, "microtonalChordBrightness", 0, 1, 0.01);
        slider(chordVoices, "microtonalChordVoices", 2, 6, 1);
        for (auto* slider : {&originality, &colour, &brightness, &chordVoices})
            slider->onValueChange = [this] { commitChords(); };
        meter.setEditableText(true);
        grouping.setEditableText(true);
        meter.onChange = [this] {
            if (updating)
                return;
            composer::TimeSignature signature;
            std::string error;
            if (!composer::parseMeter(meter.getText().toStdString(), signature, error)) {
                feedback.setText(juce::String(error), juce::dontSendNotification);
                return;
            }
            settings.numerator = signature.numerator;
            settings.denominator = signature.denominator;
            settings.groupCount = 0;
            settings.groupUnit = signature.denominator;
            commit(false);
            sync();
        };
        grouping.onChange = [this] {
            if (updating)
                return;
            auto next = settings;
            std::string error;
            if (!microtonal::setGrouping(next, grouping.getText().toStdString(), error)) {
                feedback.setText(juce::String(error), juce::dontSendNotification);
                return;
            }
            settings = next;
            commit(false);
            sync();
        };
        for (auto* box : {&bars, &grid, &phrase, &mode})
            box->onChange = [this] { commit(); };
        for (auto* box : {&chordLength, &chordRhythm, &chordShape, &voicing})
            box->onChange = [this] { commitChords(); };
        resolve.onClick = [this] { commitChords(); };
        keepRhythm.onClick = [this] { commit(); };
        hostMeter.onClick = [this] { commit(); };
        eachLoop.onClick = [this] { commit(); };
        editNotes.setTooltip(
            tr("Shift-click to lock a note. With editing on, double-click to add or remove an unlocked note.",
               "Shift+klik blokuje dźwięk. Przy włączonej edycji podwójny klik dodaje lub usuwa odblokowany "
               "dźwięk."));
        undo.onClick = [this] {
            processor.undoMicrotonal();
            sync();
        };
        redo.onClick = [this] {
            processor.redoMicrotonal();
            sync();
        };
        melodyTab.onClick = [this] {
            chordPage = false;
            processor.stopMicrotonalAudition();
            sync();
            owner.resized();
        };
        chordsTab.onClick = [this] {
            chordPage = true;
            processor.stopMicrotonalAudition();
            sync();
            owner.resized();
        };
        characterTab.onClick = [this] {
            controlsPage = 0;
            visibility();
            owner.resized();
        };
        rhythmTab.onClick = [this] {
            controlsPage = 1;
            visibility();
            owner.resized();
        };
        searchTab.onClick = [this] {
            controlsPage = 2;
            visibility();
            owner.resized();
        };
        newChords.onClick = [this] {
            if (processor.isGenerating())
                processor.cancelGeneration();
            else if (commitChords())
                processor.generateMicrotonalChords();
        };
        fromChords.onClick = [this] {
            if (commit())
                processor.generateMicrotonalFromChords();
        };
        newPair.onClick = [this] {
            if (commit() && commitChords())
                processor.generateMicrotonalChords(true);
        };
        revoice.onClick = [this] {
            const auto result = processor.revoiceMicrotonalChords();
            if (result.failed())
                feedback.setText(result.getErrorMessage(), juce::dontSendNotification);
        };
        together.onClick = [this] { processor.startMicrotonalAudition(2); };
        for (int i = 0; i < microtonal::slotCount; ++i) {
            add(variants[size_t(i)], ("microtonalVariation" + juce::String(i)).toRawUTF8());
            variants[size_t(i)].onClick = [this, i] {
                if (juce::ModifierKeys::getCurrentModifiersRealtime().isShiftDown())
                    processor.pinMicrotonalVariation(i);
                else
                    processor.selectMicrotonalVariation(i);
                sync();
            };
        }
        base.setNumDecimalPlacesToDisplay(3);
        beats.setNumDecimalPlacesToDisplay(3);
        tempo.setTextValueSuffix(" BPM");
        system.onChange = [this] {
            if (updating)
                return;
            const int id = system.getSelectedId();
            if (id >= 2 && id <= 72) {
                settings.tuning.rational = false;
                settings.tuning.divisions = id;
            } else if (id == 101) {
                settings.tuning.rational = false;
            } else if (id == 102) {
                apply();
                return;
            }
            commit(false);
            sync();
        };
        output.onChange = [this] { commit(); };
        monitor.onClick = [this] { commit(); };
        follow.onClick = [this] { commit(); };
        applyRatios.onClick = [this] { apply(); };
        generate.onClick = [this] {
            if (processor.isGenerating())
                processor.cancelGeneration();
            else if (commit())
                processor.generateMicrotonal(false);
        };
        evolve.onClick = [this] {
            if (commit())
                processor.generateMicrotonal(true);
        };
        play.onClick = [this] {
            if (processor.previewRequested() || (settings.followHost && processor.isPlaying()))
                processor.stopMicrotonalAudition();
            else
                processor.startMicrotonalAudition(chordPage ? 1 : 0);
        };
        exportFile.onClick = [this] { chooseExport(chordPage ? 1 : 0); };
        exportPair.setComponentID("exportMicrotonalPair");
        exportPair.onClick = [this] { chooseExport(2); };
        drag.onClick = exportFile.onClick;
        drag.onDrag = [this] {
            auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                            .getNonexistentChildFile("microtonal", ".mid", false);
            if (processor.exportMicrotonalMidi(file, chordPage ? 1 : 0).wasOk()) {
                temporaryFiles.push_back(file);
                juce::DragAndDropContainer::performExternalDragDropOfFiles({file.getFullPathName()}, false,
                                                                           &drag);
            }
        };
        sync();
    }
    void chooseExport(int source) {
        chooser = std::make_unique<juce::FileChooser>(
            tr("Export microtonal MIDI", "Eksportuj mikrotonowe MIDI"), juce::File(), "*.mid");
        chooser->launchAsync(
            juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
                juce::FileBrowserComponent::warnAboutOverwriting,
            [this, source](const juce::FileChooser& chosen) {
                if (chosen.getResult() == juce::File())
                    return;
                const auto result =
                    processor.exportMicrotonalMidi(chosen.getResult().withFileExtension("mid"), source);
                feedback.setText(result.wasOk() ? tr("MIDI exported with pitch bends.",
                                                     "MIDI wyeksportowane z pitch bendami.")
                                                : result.getErrorMessage(),
                                 juce::dontSendNotification);
            });
    }
    ~Impl() {
        chooser.reset();
        for (const auto& file : temporaryFiles)
            file.deleteFile();
    }
    bool commit(bool readControls = true) {
        if (updating)
            return true;
        if (readControls) {
            if (!settings.tuning.rational)
                settings.tuning.divisions = int(divisions.getValue());
            settings.baseHz = base.getValue();
            settings.octaves = int(octaves.getValue());
            settings.beats = beats.getValue();
            settings.density = density.getValue();
            settings.consonance = consonance.getValue();
            settings.diversity = diversity.getValue();
            settings.motion = movement.getValue();
            settings.mutation = mutation.getValue();
            settings.population = int(population.getValue());
            settings.generations = int(generations.getValue());
            settings.tempo = tempo.getValue();
            settings.monitor = monitor.getToggleState();
            settings.followHost = follow.getToggleState();
            if (!chordPage)
                settings.mpe = output.getSelectedId() == 2;
            constexpr int lengths[]{0, 1, 2, 3, 4, 6, 8, 12, 16};
            settings.bars = lengths[std::clamp(bars.getSelectedId(), 1, 9) - 1];
            settings.division = 4 << std::clamp(grid.getSelectedId(), 1, 3);
            settings.phraseType = std::clamp(phrase.getSelectedId() - 1, 0, 2);
            settings.mode = std::clamp(mode.getSelectedId() - 1, 0, 2);
            settings.lockRhythm = keepRhythm.getToggleState();
            settings.dynamics = dynamics.getValue();
            settings.syncopation = syncopation.getValue();
            settings.space = space.getValue();
            settings.arousal = arousal.getValue();
            settings.weirdness = weirdness.getValue();
            settings.swing = swing.getValue();
            settings.gate = gate.getValue();
            settings.humanize = humanize.getValue();
            settings.crossover = crossover.getValue();
            settings.hostMeter = hostMeter.getToggleState();
            settings.evolveEachLoop = eachLoop.getToggleState();
        }
        const auto result = processor.setMicrotonalSettings(settings);
        if (result.failed())
            feedback.setText(result.getErrorMessage(), juce::dontSendNotification);
        return result.wasOk();
    }
    bool commitChords() {
        if (updating)
            return true;
        auto cs = workspace.chordSettings;
        constexpr int lengths[]{0, 1, 2, 3, 4, 6, 8, 12, 16};
        cs.bars = lengths[std::clamp(chordLength.getSelectedId(), 1, 9) - 1];
        cs.rhythm = std::clamp(chordRhythm.getSelectedId() - 1, 0, 2);
        cs.rhythmShape = std::clamp(chordShape.getSelectedId() - 1, 0, 2);
        cs.voicing = std::clamp(voicing.getSelectedId() - 1, 0, 2);
        cs.voices = int(chordVoices.getValue());
        cs.originality = originality.getValue();
        cs.colour = colour.getValue();
        cs.brightness = brightness.getValue();
        cs.resolve = resolve.getToggleState();
        const auto result = processor.setMicrotonalChordSettings(cs);
        if (result.wasOk())
            workspace.chordSettings = cs;
        else
            feedback.setText(result.getErrorMessage(), juce::dontSendNotification);
        return result.wasOk();
    }
    void visibility() {
        for (auto* c : std::initializer_list<juce::Component*>{&generate, &evolve, &phrase, &mode, &editNotes,
                                                               &characterTab, &rhythmTab, &searchTab})
            c->setVisible(!chordPage);
        for (auto* c : std::initializer_list<juce::Component*>{&newChords, &fromChords, &newPair, &revoice,
                                                               &chordLength, &chordRhythm, &chordShape,
                                                               &voicing, &chordVoices, &originality, &colour,
                                                               &brightness, &resolve, &together, &exportPair})
            c->setVisible(chordPage);
        for (auto& variant : variants)
            variant.setVisible(!chordPage);
        for (auto* c : {&consonance, &diversity, &movement, &dynamics, &space, &arousal, &weirdness, &density,
                        &mutation})
            c->setVisible(!chordPage && controlsPage == 0);
        for (auto* c : {&syncopation, &swing, &gate, &humanize})
            c->setVisible(!chordPage && controlsPage == 1);
        keepRhythm.setVisible(!chordPage && controlsPage == 1);
        hostMeter.setVisible(!chordPage && controlsPage == 1);
        eachLoop.setVisible(!chordPage && controlsPage == 1);
        for (auto* c : {&population, &generations, &crossover})
            c->setVisible(!chordPage && controlsPage == 2);
        stats.setVisible(chordPage || controlsPage == 2);
        melodyTab.setColour(juce::TextButton::buttonColourId, chordPage ? surface : violet);
        chordsTab.setColour(juce::TextButton::buttonColourId, chordPage ? violet : surface);
        for (int i = 0; i < 3; ++i) {
            auto* tab = i == 0 ? &characterTab : i == 1 ? &rhythmTab : &searchTab;
            tab->setColour(juce::TextButton::buttonColourId, controlsPage == i ? teal : surface);
        }
        owner.repaint();
    }
    void apply() {
        auto t = settings.tuning;
        std::string error;
        if (!microtonal::parseRatios(ratios.getText().toStdString(), t, error)) {
            feedback.setText(
                tr("Invalid ratios: start at 1/1; use increasing fractions below 2/1.",
                   "Błędne proporcje: zacznij od 1/1; ułamki muszą rosnąć i być mniejsze od 2/1."),
                juce::dontSendNotification);
            updating = true;
            system.setSelectedId(settings.tuning.rational ? 102 : settings.tuning.divisions,
                                 juce::dontSendNotification);
            updating = false;
            return;
        }
        settings.tuning = t;
        commit(false);
        feedback.setText({}, juce::dontSendNotification);
        sync();
    }
    void describeNote() {
        if (clip.count == 0) {
            info.setText(tr("Notation: degree / octave division, cents from base, frequency in Hz.",
                            "Zapis: stopień / podział oktawy, centy od podstawy, częstotliwość w Hz."),
                         juce::dontSendNotification);
            return;
        }
        roll.selected = std::clamp(roll.selected, 0, clip.count - 1);
        const int d = clip.notes[size_t(roll.selected)].degree;
        const double centValue = microtonal::cents(clip.tuning, d);
        const auto match = microtonal::approximateRatio(centValue);
        juce::String notation;
        if (clip.tuning.rational) {
            const auto r = clip.tuning.ratios[size_t(d % clip.tuning.divisions)];
            notation = juce::String(r.numerator) + "/" + juce::String(r.denominator) + " x 2^" +
                       juce::String(d / clip.tuning.divisions);
        } else
            notation = juce::String(d) + "\\" + juce::String(clip.tuning.divisions);
        info.setText(notation + "   |   " + juce::String(centValue, 3) + " c   |   " +
                         juce::String(microtonal::frequency(clip, d), 3) + " Hz   |   " +
                         tr("approx. ", "ok. ") + juce::String(match.ratio.numerator) + "/" +
                         juce::String(match.ratio.denominator) + " (" + juce::String(match.errorCents, 2) +
                         " c)",
                     juce::dontSendNotification);
    }
    void syncMore() {
        if (controlsLanguage != language) {
            controlsLanguage = language;
            title.setText(tr("MICROTONAL COMPOSER", "MIKROTONOWY KOMPOZYTOR"), juce::dontSendNotification);
            subtitle.setText(tr("Melody and chords in your tuning. Meter, grouping and rhythm follow the "
                                "conventional generator.",
                                "Melodia i akordy w wybranym stroju. Metrum, grupowanie i rytm działają jak "
                                "w zwykłym generatorze."),
                             juce::dontSendNotification);
            melodyTab.setButtonText(tr("Melody", "Melodia"));
            chordsTab.setButtonText(tr("Chords", "Akordy"));
            characterTab.setButtonText(tr("Character", "Charakter"));
            rhythmTab.setButtonText(tr("Rhythm / playback", "Rytm / odtwarzanie"));
            searchTab.setButtonText(tr("Search", "Wyszukiwanie"));
            keepRhythm.setButtonText(tr("Keep rhythm", "Zachowaj rytm"));
            hostMeter.setButtonText(tr("Host meter", "Metrum hosta"));
            eachLoop.setButtonText(tr("Evolve each loop", "Ewoluuj co pętlę"));
            editNotes.setButtonText(tr("Edit notes", "Edytuj dźwięki"));
            resolve.setButtonText(tr("Resolve to base", "Zakończ na podstawie"));
            undo.setButtonText(tr("Undo", "Cofnij"));
            redo.setButtonText(tr("Redo", "Ponów"));
            newChords.setButtonText(tr("New chords only", "Nowe akordy"));
            fromChords.setButtonText(tr("Melody from chords", "Melodia do akordów"));
            newPair.setButtonText(tr("New chords + melody", "Nowe akordy + melodia"));
            revoice.setButtonText(tr("Revoice", "Zmień przewroty"));
            together.setButtonText(tr("Play together", "Graj razem"));
            exportPair.setButtonText(tr("Melody + chords MIDI", "MIDI melodii + akordów"));
            auto choices = [&](juce::ComboBox& box, const juce::StringArray& entries) {
                box.clear(juce::dontSendNotification);
                box.addItemList(entries, 1);
            };
            choices(bars, {tr("Custom beats", "Własna długość"), "1", "2", "3", "4", "6", "8", "12", "16"});
            choices(chordLength,
                    {tr("Follow melody", "Jak melodia"), "1", "2", "3", "4", "6", "8", "12", "16"});
            choices(grid, {"1/8", "1/16", "1/32"});
            choices(phrase, {"Riff", tr("Motif", "Motyw"), tr("Melody", "Melodia")});
            choices(mode, {tr("Full melody", "Pełna melodia"), tr("Rhythm only", "Tylko rytm"),
                           tr("Melody on rhythm", "Melodia na rytmie")});
            choices(chordRhythm, {tr("One per bar", "Raz na takt"), tr("Twice per bar", "Dwa razy na takt"),
                                  tr("Beat groups", "Grupy uderzeń")});
            choices(chordShape, {tr("Even", "Równo"), tr("Long-short", "Długi-krótki"),
                                 tr("Short-long", "Krótki-długi")});
            choices(voicing, {tr("Close", "Skupione"), tr("Open", "Rozłożone"),
                              tr("Voice leading", "Prowadzenie głosów")});
            meter.clear(juce::dontSendNotification);
            int id = 1;
            for (const auto signature : composer::timeSignatures())
                meter.addItem(juce::String(signature.numerator) + "/" + juce::String(signature.denominator),
                              id++);
        }
        constexpr int lengths[]{0, 1, 2, 3, 4, 6, 8, 12, 16};
        for (int i = 0; i < 9; ++i) {
            if (lengths[i] == settings.bars)
                bars.setSelectedId(i + 1, juce::dontSendNotification);
            if (lengths[i] == workspace.chordSettings.bars)
                chordLength.setSelectedId(i + 1, juce::dontSendNotification);
        }
        meter.setText(juce::String(settings.numerator) + "/" + juce::String(settings.denominator),
                      juce::dontSendNotification);
        grid.setSelectedId(settings.division == 32   ? 3
                           : settings.division == 16 ? 2
                                                     : 1,
                           juce::dontSendNotification);
        grouping.clear(juce::dontSendNotification);
        int groupId = 1;
        for (const auto& text : composer::groupingChoices(settings.numerator, settings.denominator))
            grouping.addItem(juce::String(text), groupId++);
        std::array<int, 16> groups = settings.groups;
        int groupCount = settings.groupCount, groupUnit = settings.groupUnit;
        if (groupCount == 0)
            composer::defaultGrouping(settings.numerator, settings.denominator, groups, groupCount,
                                      groupUnit);
        grouping.setText(juce::String(composer::groupingText(groups, groupCount)),
                         juce::dontSendNotification);
        phrase.setSelectedId(settings.phraseType + 1, juce::dontSendNotification);
        mode.setSelectedId(settings.mode + 1, juce::dontSendNotification);
        keepRhythm.setToggleState(settings.lockRhythm, juce::dontSendNotification);
        hostMeter.setToggleState(settings.hostMeter, juce::dontSendNotification);
        eachLoop.setToggleState(settings.evolveEachLoop, juce::dontSendNotification);
        beats.setEnabled(settings.bars == 0);
        dynamics.setValue(settings.dynamics, juce::dontSendNotification);
        syncopation.setValue(settings.syncopation, juce::dontSendNotification);
        space.setValue(settings.space, juce::dontSendNotification);
        arousal.setValue(settings.arousal, juce::dontSendNotification);
        weirdness.setValue(settings.weirdness, juce::dontSendNotification);
        swing.setValue(settings.swing, juce::dontSendNotification);
        gate.setValue(settings.gate, juce::dontSendNotification);
        humanize.setValue(settings.humanize, juce::dontSendNotification);
        crossover.setValue(settings.crossover, juce::dontSendNotification);
        const auto& cs = workspace.chordSettings;
        chordRhythm.setSelectedId(cs.rhythm + 1, juce::dontSendNotification);
        chordShape.setSelectedId(cs.rhythmShape + 1, juce::dontSendNotification);
        voicing.setSelectedId(cs.voicing + 1, juce::dontSendNotification);
        chordVoices.setValue(cs.voices, juce::dontSendNotification);
        originality.setValue(cs.originality, juce::dontSendNotification);
        colour.setValue(cs.colour, juce::dontSendNotification);
        brightness.setValue(cs.brightness, juce::dontSendNotification);
        resolve.setToggleState(cs.resolve, juce::dontSendNotification);
        for (int i = 0; i < microtonal::slotCount; ++i) {
            auto& button = variants[size_t(i)];
            const auto& c = workspace.variations[size_t(i)];
            button.setButtonText(juce::String(i + 1).paddedLeft('0', 2) + (c.favourite ? " *" : "") +
                                 "  /  " + juce::String(c.count));
            button.setColour(juce::TextButton::buttonColourId,
                             i == workspace.selected ? violet.withAlpha(0.5f) : surface);
            button.setTooltip(tr("Select a variation. Shift-click to pin it against regeneration.",
                                 "Wybierz wariant. Shift+klik przypina go przed ponownym generowaniem."));
        }
    }
    void sync() {
        workspace = processor.microtonalSnapshot();
        settings = workspace.settings;
        clip = chordPage ? workspace.chords : workspace.clip;
        if (clip.count == 0) {
            clip.tuning = settings.tuning;
            clip.baseHz = settings.baseHz;
            clip.length = microtonal::phraseLength(settings);
            clip.numerator = settings.numerator;
            clip.denominator = settings.denominator;
            clip.groups = settings.groups;
            clip.groupCount = settings.groupCount;
            clip.groupUnit = settings.groupUnit;
            clip.grid = 4.0 / settings.division;
            clip.polyphonic = chordPage;
        }
        updating = true;
        if (language != processor.interfaceLanguageIndex()) {
            language = processor.interfaceLanguageIndex();
            title.setText(tr("MICROTONAL LAB", "LABORATORIUM MIKROTONOWE"), juce::dontSendNotification);
            subtitle.setText(tr("Independent evolution in EDO and ratio pitch spaces. Click a note to "
                                "inspect its exact pitch.",
                                "Niezależna ewolucja w EDO i strojach ułamkowych. Kliknij dźwięk, aby "
                                "sprawdzić jego wysokość."),
                             juce::dontSendNotification);
            system.clear(juce::dontSendNotification);
            for (int edo : {7, 8, 9, 10, 13, 17, 19, 22, 24, 31, 53})
                system.addItem(juce::String(edo) + "-EDO", edo);
            system.addItem(tr("Custom EDO", "Własny podział EDO"), 101);
            system.addItem(tr("Just ratios", "Proporcje ułamkowe"), 102);
            output.clear(juce::dontSendNotification);
            output.addItem(tr("Pitch bend / +/-2 semitones", "Pitch bend / +/-2 półtony"), 1);
            output.addItem("MPE / +/-48", 2);
            applyRatios.setButtonText(tr("Apply ratios", "Zastosuj proporcje"));
            evolve.setButtonText(tr("Evolve sequence", "Ewoluuj sekwencję"));
            exportFile.setButtonText(tr("Export MIDI...", "Eksportuj MIDI..."));
            drag.setButtonText(tr("Drag MIDI", "Przeciągnij MIDI"));
            monitor.setButtonText(tr("Internal sound", "Dźwięk wewnętrzny"));
            follow.setButtonText(tr("Follow DAW", "Podążaj za DAW"));
            consonance.setTooltip(tr("Reward proximity to simple frequency ratios. A harmonic-timbre "
                                     "heuristic, not a universal measure of beauty.",
                                     "Premiuje bliskość prostych proporcji częstotliwości. Heurystyka dla "
                                     "barw harmonicznych, nie uniwersalna miara piękna."));
            ratios.setTooltip(
                tr("2-72 increasing fractions from 1/1 to below 2/1. The octave repeats automatically.",
                   "Od 2 do 72 rosnących ułamków od 1/1 do wartości poniżej 2/1. Oktawa powtarza się "
                   "automatycznie."));
            output.setTooltip(
                tr("Match the receiving instrument's pitch bend range. MPE requires an MPE-capable receiver.",
                   "Dopasuj zakres pitch bend w instrumencie docelowym. MPE wymaga instrumentu obsługującego "
                   "MPE."));
        }
        const bool preset = system.indexOfItemId(settings.tuning.divisions) >= 0;
        system.setSelectedId(settings.tuning.rational ? 102
                             : preset                 ? settings.tuning.divisions
                                                      : 101,
                             juce::dontSendNotification);
        divisions.setValue(settings.tuning.divisions, juce::dontSendNotification);
        divisions.setEnabled(!settings.tuning.rational);
        if (settings.tuning.rational && !ratios.hasKeyboardFocus(true))
            ratios.setText(juce::String(microtonal::ratioText(settings.tuning)), false);
        base.setValue(settings.baseHz, juce::dontSendNotification);
        octaves.setValue(settings.octaves, juce::dontSendNotification);
        beats.setValue(settings.beats, juce::dontSendNotification);
        tempo.setValue(settings.tempo, juce::dontSendNotification);
        density.setValue(settings.density, juce::dontSendNotification);
        consonance.setValue(settings.consonance, juce::dontSendNotification);
        diversity.setValue(settings.diversity, juce::dontSendNotification);
        movement.setValue(settings.motion, juce::dontSendNotification);
        mutation.setValue(settings.mutation, juce::dontSendNotification);
        population.setValue(settings.population, juce::dontSendNotification);
        generations.setValue(settings.generations, juce::dontSendNotification);
        syncMore();
        output.setSelectedId(chordPage || settings.mpe ? 2 : 1, juce::dontSendNotification);
        output.setEnabled(!chordPage);
        monitor.setToggleState(settings.monitor, juce::dontSendNotification);
        follow.setToggleState(settings.followHost, juce::dontSendNotification);
        updating = false;
        revision = processor.microtonalRevision();
        describeNote();
        visibility();
        owner.repaint();
    }
};

MicrotonalComposerPanel::MicrotonalComposerPanel(GeneticVSTComposerJUCEAudioProcessor& p)
    : impl(std::make_unique<Impl>(*this, p)) {}
MicrotonalComposerPanel::~MicrotonalComposerPanel() = default;

void MicrotonalComposerPanel::togglePlayback() {
    auto& p = *impl;
    if (p.processor.previewRequested() || (p.settings.followHost && p.processor.isPlaying()))
        p.processor.stopMicrotonalAudition();
    else
        p.processor.startMicrotonalAudition(p.chordPage ? 1 : 0);
}
void MicrotonalComposerPanel::refreshDisplay() {
    auto& p = *impl;
    if (p.revision != p.processor.microtonalRevision() || p.language != p.processor.interfaceLanguageIndex())
        p.sync();
    const bool busy = p.processor.isGenerating();
    p.generate.setButtonText(busy ? p.tr("Cancel search ", "Anuluj szukanie ") +
                                        juce::String(int(p.processor.generationProgress() * 100)) + "%"
                                  : p.tr("Generate melody bank", "Generuj bank melodii"));
    p.newChords.setButtonText(busy ? p.tr("Cancel search", "Anuluj szukanie")
                                   : p.tr("New chords only", "Nowe akordy"));
    const bool compatible =
        p.workspace.clip.count > 0 && microtonal::sameTuning(p.workspace.clip.tuning, p.settings.tuning) &&
        p.workspace.clip.baseHz == p.settings.baseHz &&
        std::all_of(p.workspace.clip.notes.begin(), p.workspace.clip.notes.begin() + p.workspace.clip.count,
                    [&](const microtonal::Event& n) {
                        return n.locked || n.degree <= p.settings.tuning.divisions * p.settings.octaves;
                    });
    p.evolve.setEnabled(!busy && compatible);
    p.evolve.setTooltip(
        p.tr("Evolve the selected variation. Keep rhythm protects timing; locked notes always survive.",
             "Ewoluuj wybrany wariant. Zachowaj rytm chroni czasy, a zablokowane dźwięki zawsze pozostają."));
    p.play.setEnabled(p.clip.count > 0);
    p.exportFile.setEnabled(p.clip.count > 0);
    p.drag.setEnabled(p.clip.count > 0);
    p.fromChords.setEnabled(!busy && p.workspace.chords.count > 0);
    p.newPair.setEnabled(!busy);
    p.revoice.setEnabled(!busy && p.workspace.chords.count > 0);
    p.undo.setEnabled(p.processor.canUndoMicrotonal());
    p.redo.setEnabled(p.processor.canRedoMicrotonal());
    p.together.setEnabled(p.workspace.clip.count > 0 && p.workspace.chords.count > 0 &&
                          microtonal::sameTuning(p.workspace.clip.tuning, p.workspace.chords.tuning) &&
                          p.workspace.clip.baseHz == p.workspace.chords.baseHz);
    p.exportPair.setEnabled(p.together.isEnabled());
    const bool playing = p.processor.previewRequested() || (p.settings.followHost && p.processor.isPlaying());
    p.play.setPlaying(playing);
    p.play.setButtonText(p.tr(playing ? "Stop" : "Play", playing ? "Stop" : "Graj"));
    const auto snapshot = p.processor.microtonalSnapshot();
    const auto& stat = p.chordPage ? snapshot.chordStats : snapshot.stats;
    p.stats.setText(
        p.tr("Generation ", "Pokolenie ") + juce::String(stat.generation) + " / " +
            juce::String(p.settings.generations) + p.tr("   |   Evaluations ", "   |   Oceny ") +
            juce::String(juce::int64(stat.evaluations)) + "   |   Fitness " +
            juce::String(stat.best.total, 3) + "\n" + p.tr("Ratio affinity ", "Zgodność proporcji ") +
            juce::String(stat.best.harmonicity, 3) + p.tr("   |   Variety ", "   |   Różnorodność ") +
            juce::String(stat.best.variety, 3) + p.tr("   |   Rhythm planner ", "   |   Planowanie rytmu ") +
            juce::String(juce::int64(stat.rhythmEvaluations)),
        juce::dontSendNotification);
    p.roll.repaint();
}
void MicrotonalComposerPanel::resized() {
    auto& p = *impl;
    const int w = getWidth(), x = 298, rw = w - x - 12;
    p.title.setBounds(12, 0, w - 270, 32);
    p.subtitle.setBounds(12, 34, w - 24, 28);
    p.melodyTab.setBounds(w - 234, 4, 106, 28);
    p.chordsTab.setBounds(w - 120, 4, 108, 28);
    p.system.setBounds(12, 92, 246, 28);
    p.divisions.setBounds(12, 146, 246, 24);
    p.base.setBounds(12, 200, 246, 24);
    p.ratios.setBounds(12, 254, 246, 28);
    p.applyRatios.setBounds(12, 288, 246, 26);
    p.bars.setBounds(12, 346, 116, 28);
    p.meter.setBounds(142, 346, 116, 28);
    p.grid.setBounds(12, 402, 96, 28);
    p.grouping.setBounds(120, 402, 138, 28);
    p.phrase.setBounds(12, 458, 116, 28);
    p.mode.setBounds(142, 458, 116, 28);
    p.octaves.setBounds(12, 514, 116, 24);
    p.beats.setBounds(142, 514, 116, 24);
    p.generate.setBounds(12, 558, 246, 34);
    p.evolve.setBounds(12, 600, 246, 32);
    p.undo.setBounds(12, 650, 116, 28);
    p.redo.setBounds(142, 650, 116, 28);
    p.feedback.setBounds(12, 682, 246, 54);
    p.roll.setBounds(x, 76, rw, 228);
    p.info.setBounds(x, 308, rw, 28);
    p.play.setBounds(x, 344, 88, 32);
    p.together.setBounds(x + 96, 344, 154, 32);
    p.exportFile.setBounds(x + rw - 270, 344, 130, 32);
    p.exportPair.setBounds(x + 258, 344, std::max(100, rw - 536), 32);
    p.drag.setBounds(x + rw - 132, 344, 132, 32);
    const int tab = (rw - 16) / 3;
    p.characterTab.setBounds(x, 392, tab, 28);
    p.rhythmTab.setBounds(x + tab + 8, 392, tab, 28);
    p.searchTab.setBounds(x + 2 * (tab + 8), 392, tab, 28);
    const int cw = (rw - 32) / 3;
    juce::Slider* character[]{&p.consonance, &p.diversity, &p.movement, &p.dynamics, &p.space,
                              &p.arousal,    &p.weirdness, &p.density,  &p.mutation};
    for (int i = 0; i < 9; ++i)
        character[i]->setBounds(x + (i % 3) * (cw + 16), 450 + (i / 3) * 52, cw, 24);
    juce::Slider* rhythm[]{&p.syncopation, &p.swing, &p.gate, &p.humanize};
    for (int i = 0; i < 4; ++i)
        rhythm[i]->setBounds(x + (i % 3) * (cw + 16), 450 + (i / 3) * 52, cw, 24);
    p.keepRhythm.setBounds(x + cw + 16, 502, cw, 24);
    p.hostMeter.setBounds(x + (cw + 16) * 2, 502, cw, 24);
    p.eachLoop.setBounds(x, 554, cw + 60, 24);
    p.population.setBounds(x, 450, cw, 24);
    p.generations.setBounds(x + cw + 16, 450, cw, 24);
    p.crossover.setBounds(x + (cw + 16) * 2, 450, cw, 24);
    p.stats.setBounds(x, p.chordPage ? 620 : 488, rw, 56);
    p.editNotes.setBounds(x + rw - 180, 586, 180, 24);
    const int vw = (rw - 5 * 8) / 6;
    for (int i = 0; i < microtonal::slotCount; ++i)
        p.variants[size_t(i)].setBounds(x + (i % 6) * (vw + 8), 616 + (i / 6) * 44, vw, 38);
    const int button = (rw - 24) / 4;
    juce::TextButton* actions[]{&p.newChords, &p.fromChords, &p.newPair, &p.revoice};
    for (int i = 0; i < 4; ++i)
        actions[i]->setBounds(x + i * (button + 8), 392, button, 32);
    const int cc = (rw - 24) / 4;
    p.chordLength.setBounds(x, 458, cc, 28);
    p.chordRhythm.setBounds(x + cc + 8, 458, cc, 28);
    p.chordShape.setBounds(x + (cc + 8) * 2, 458, cc, 28);
    p.voicing.setBounds(x + (cc + 8) * 3, 458, cc, 28);
    p.chordVoices.setBounds(x, 530, cc, 24);
    p.originality.setBounds(x + cc + 8, 530, cc, 24);
    p.colour.setBounds(x + (cc + 8) * 2, 530, cc, 24);
    p.brightness.setBounds(x + (cc + 8) * 3, 530, cc, 24);
    p.resolve.setBounds(x, 574, 240, 28);
    p.output.setBounds(x, 716, 224, 28);
    p.monitor.setBounds(x + 234, 716, 160, 28);
    p.follow.setBounds(x + 402, 716, 146, 28);
    p.tempo.setBounds(x + rw - 142, 716, 142, 26);
}
void MicrotonalComposerPanel::paint(juce::Graphics& g) {
    auto& p = *impl;
    g.setColour(panel.withAlpha(0.6f));
    g.fillRoundedRectangle(0, 76, 270, float(getHeight() - 76), 10);
    auto label = [&](const char* en, const char* pl, int x, int y, int width) {
        g.setColour(muted);
        g.setFont(font(11));
        g.drawText(p.tr(en, pl), x, y, width, 20, juce::Justification::left);
    };
    label("Pitch system", "System wysokości", 12, 72, 246);
    label("Equal divisions per octave", "Równe podziały oktawy", 12, 124, 246);
    label("Base frequency / Hz", "Częstotliwość bazowa / Hz", 12, 178, 246);
    label("Ratios (1/1 ... < 2/1)", "Proporcje (1/1 ... < 2/1)", 12, 232, 246);
    label("Length / bars", "Długość / takty", 12, 324, 116);
    label("Meter", "Metrum", 142, 324, 116);
    label("Grid", "Siatka", 12, 380, 96);
    label("Grouping", "Grupowanie", 120, 380, 138);
    if (!p.chordPage) {
        label("Phrase type", "Typ frazy", 12, 436, 116);
        label("Source mode", "Tryb źródła", 142, 436, 116);
    }
    label("Octaves", "Oktawy", 12, 492, 116);
    label("Custom beats", "Własne uderzenia", 142, 492, 116);
    const int x = 298, rw = getWidth() - x - 12, cw = (rw - 32) / 3;
    if (!p.chordPage) {
        if (p.controlsPage == 0) {
            const char* en[]{"Ratio preference", "Diversity", "Movement", "Dynamics", "Pause amount",
                             "Arousal",          "Weirdness", "Density",  "Evolution"};
            const char* pl[]{"Proste proporcje", "Różnorodność", "Wielkość ruchu", "Dynamika", "Ilość pauz",
                             "Pobudzenie",       "Niezwykłość",  "Gęstość",        "Ewolucja"};
            for (int i = 0; i < 9; ++i)
                label(en[i], pl[i], x + (i % 3) * (cw + 16), 428 + (i / 3) * 52, cw);
        } else if (p.controlsPage == 1) {
            const char* en[]{"Syncopation", "Swing", "Gate", "Humanize"};
            const char* pl[]{"Synkopa", "Swing", "Gate", "Humanizacja"};
            for (int i = 0; i < 4; ++i)
                label(en[i], pl[i], x + (i % 3) * (cw + 16), 428 + (i / 3) * 52, cw);
        } else {
            label("Population", "Populacja", x, 428, cw);
            label("Generations", "Pokolenia", x + cw + 16, 428, cw);
            label("Crossover", "Krzyżowanie", x + (cw + 16) * 2, 428, cw);
        }
        label("VARIATIONS / Shift-click to pin", "WARIANTY / Shift+klik przypina", x, 586, rw - 190);
        g.setColour(violet);
        g.setFont(font(11));
        const auto name = p.clip.tuning.rational ? p.tr("Ratios", "Proporcje")
                                                 : juce::String(p.clip.tuning.divisions) + "-EDO";
        g.drawText(name + " / " + juce::String(p.clip.numerator) + "/" + juce::String(p.clip.denominator) +
                       " / " + juce::String(p.clip.length, 2) + p.tr(" beats", " uderzeń"),
                   x + 98, 348, rw - 380, 24, juce::Justification::left);
    } else {
        const int cc = (rw - 24) / 4;
        const char* en[]{"Chord length / bars", "Harmonic rhythm", "Rhythm shape", "Voicing", "Voices",
                         "Originality",         "Colour",          "Brightness"};
        const char* pl[]{"Długość akordów / takty",
                         "Rytm harmoniczny",
                         "Kształt rytmu",
                         "Przewroty",
                         "Głosy",
                         "Oryginalność",
                         "Kolor",
                         "Jasność"};
        for (int i = 0; i < 8; ++i)
            label(en[i], pl[i], x + (i % 4) * (cc + 8), i < 4 ? 436 : 508, cc);
        label("Polyphonic MIDI uses one MPE member channel per voice (+/-48 semitones).",
              "Polifoniczne MIDI używa osobnego kanału MPE dla każdego głosu (+/-48 półtonów).", x, 684, rw);
    }
}
