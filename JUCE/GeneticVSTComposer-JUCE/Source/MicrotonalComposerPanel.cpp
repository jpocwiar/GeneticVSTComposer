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
    juce::ComboBox system, output;
    juce::TextEditor ratios;
    juce::Slider divisions, base, octaves, beats, density, consonance, diversity, movement, mutation,
        population, generations, tempo;
    juce::ToggleButton monitor, follow;
    juce::TextButton generate, evolve, applyRatios, exportFile;
    TransportButton play;
    MidiDragButton drag;
    juce::Label title, subtitle, info, stats, feedback;
    std::unique_ptr<juce::FileChooser> chooser;
    std::vector<juce::File> temporaryFiles;
    bool updating = false;
    int language = -1;
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
            for (int beat = 0; beat <= int(impl.clip.length); ++beat) {
                const float x = area.getX() + float(beat / impl.clip.length) * area.getWidth();
                g.setColour(line.withAlpha(beat % 4 == 0 ? 0.8f : 0.3f));
                g.drawVerticalLine(int(x), area.getY(), area.getBottom());
                if (beat % (impl.clip.length > 32 ? 8 : 4) == 0) {
                    g.setColour(muted);
                    g.drawText(juce::String(beat), int(x), 4, 30, 15, juce::Justification::left);
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
                g.setColour(i == selected ? accent : violet.withAlpha(0.8f));
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
                    repaint();
                    break;
                }
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
        slider(beats, "microtonalBeats", 1, 64, 1);
        slider(tempo, "microtonalTempo", 20, 400, 1);
        slider(density, "microtonalDensity", 0, 1, 0.01);
        slider(consonance, "microtonalConsonance", 0, 1, 0.01);
        slider(diversity, "microtonalDiversity", 0, 1, 0.01);
        slider(movement, "microtonalMovement", 0, 1, 0.01);
        slider(mutation, "microtonalMutation", 0, 1, 0.01);
        slider(population, "microtonalPopulation", 8, 256, 1);
        slider(generations, "microtonalGenerations", 1, 1000, 1);
        base.setNumDecimalPlacesToDisplay(3);
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
                processor.startMicrotonalAudition();
        };
        exportFile.onClick = [this] {
            chooser = std::make_unique<juce::FileChooser>(
                tr("Export microtonal MIDI", "Eksportuj mikrotonowe MIDI"), juce::File(), "*.mid");
            chooser->launchAsync(
                juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
                    juce::FileBrowserComponent::warnAboutOverwriting,
                [this](const juce::FileChooser& chosen) {
                    if (chosen.getResult() == juce::File())
                        return;
                    const auto result =
                        processor.exportMicrotonalMidi(chosen.getResult().withFileExtension("mid"));
                    feedback.setText(result.wasOk() ? tr("MIDI exported with pitch bends.",
                                                         "MIDI wyeksportowane z pitch bendami.")
                                                    : result.getErrorMessage(),
                                     juce::dontSendNotification);
                });
        };
        drag.onDrag = [this] {
            auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                            .getNonexistentChildFile("microtonal", ".mid", false);
            if (processor.exportMicrotonalMidi(file).wasOk()) {
                temporaryFiles.push_back(file);
                juce::DragAndDropContainer::performExternalDragDropOfFiles({file.getFullPathName()}, false,
                                                                           &drag);
            }
        };
        sync();
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
            settings.mpe = output.getSelectedId() == 2;
        }
        const auto result = processor.setMicrotonalSettings(settings);
        if (result.failed())
            feedback.setText(result.getErrorMessage(), juce::dontSendNotification);
        return result.wasOk();
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
    void sync() {
        const auto snapshot = processor.microtonalSnapshot();
        settings = snapshot.settings;
        clip = snapshot.clip;
        if (clip.count == 0) {
            clip.tuning = settings.tuning;
            clip.baseHz = settings.baseHz;
            clip.length = settings.beats;
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
        output.setSelectedId(settings.mpe ? 2 : 1, juce::dontSendNotification);
        monitor.setToggleState(settings.monitor, juce::dontSendNotification);
        follow.setToggleState(settings.followHost, juce::dontSendNotification);
        updating = false;
        revision = processor.microtonalRevision();
        describeNote();
        owner.repaint();
    }
};

MicrotonalComposerPanel::MicrotonalComposerPanel(GeneticVSTComposerJUCEAudioProcessor& p)
    : impl(std::make_unique<Impl>(*this, p)) {}
MicrotonalComposerPanel::~MicrotonalComposerPanel() = default;

void MicrotonalComposerPanel::refreshDisplay() {
    auto& p = *impl;
    if (p.revision != p.processor.microtonalRevision() || p.language != p.processor.interfaceLanguageIndex())
        p.sync();
    const bool busy = p.processor.isGenerating();
    p.generate.setButtonText(busy ? p.tr("Cancel search ", "Anuluj szukanie ") +
                                        juce::String(int(p.processor.generationProgress() * 100)) + "%"
                                  : p.tr("Generate sequence", "Generuj sekwencję"));
    const bool compatible =
        p.clip.count > 0 && microtonal::sameTuning(p.clip.tuning, p.settings.tuning) &&
        p.clip.baseHz == p.settings.baseHz && p.clip.length == p.settings.beats &&
        std::all_of(p.clip.notes.begin(), p.clip.notes.begin() + p.clip.count,
                    [&](const microtonal::Event& n) {
                        return n.degree <= p.settings.tuning.divisions * p.settings.octaves;
                    });
    p.evolve.setEnabled(!busy && compatible);
    p.evolve.setTooltip(p.tr("Evolve the stored sequence in the same tuning, base frequency, length and "
                             "range. Generate starts a new pitch space.",
                             "Ewoluuj zapisaną sekwencję w tym samym stroju, częstotliwości bazowej, "
                             "długości i zakresie. Generuj rozpoczyna nowy system wysokości."));
    p.play.setEnabled(p.clip.count > 0);
    p.exportFile.setEnabled(p.clip.count > 0);
    p.drag.setEnabled(p.clip.count > 0);
    const bool playing = p.processor.previewRequested() || (p.settings.followHost && p.processor.isPlaying());
    p.play.setPlaying(playing);
    p.play.setButtonText(p.tr(playing ? "Stop" : "Play", playing ? "Stop" : "Graj"));
    const auto stat = p.processor.microtonalSnapshot().stats;
    p.stats.setText(p.tr("Generation ", "Pokolenie ") + juce::String(stat.generation) + " / " +
                        juce::String(p.settings.generations) + p.tr("   |   Evaluations ", "   |   Oceny ") +
                        juce::String(juce::int64(stat.evaluations)) + "   |   Fitness " +
                        juce::String(stat.best.total, 3) + "\n" +
                        p.tr("Last score: ratio affinity ", "Ostatnia ocena: zgodność proporcji ") +
                        juce::String(stat.best.harmonicity, 3) +
                        p.tr("   |   Variety ", "   |   Różnorodność ") + juce::String(stat.best.variety, 3) +
                        p.tr("   |   Motion ", "   |   Ruch ") + juce::String(stat.best.movement, 3),
                    juce::dontSendNotification);
    p.roll.repaint();
}

void MicrotonalComposerPanel::resized() {
    auto& p = *impl;
    const int w = getWidth(), left = 270, x = left + 28, rw = w - x - 12;
    p.title.setBounds(12, 0, w - 24, 32);
    p.subtitle.setBounds(12, 34, w - 24, 28);
    p.system.setBounds(12, 92, left - 24, 30);
    p.divisions.setBounds(12, 148, left - 24, 26);
    p.ratios.setBounds(12, 204, left - 24, 30);
    p.applyRatios.setBounds(12, 242, left - 24, 27);
    p.base.setBounds(12, 302, left - 24, 26);
    p.octaves.setBounds(12, 358, 116, 26);
    p.beats.setBounds(142, 358, 116, 26);
    p.population.setBounds(12, 416, left - 24, 26);
    p.generations.setBounds(12, 472, left - 24, 26);
    p.generate.setBounds(12, 516, left - 24, 34);
    p.evolve.setBounds(12, 558, left - 24, 32);
    p.feedback.setBounds(12, 600, left - 24, 52);
    p.roll.setBounds(x, 76, rw, 286);
    p.info.setBounds(x, 366, rw, 28);
    p.play.setBounds(x, 404, 88, 32);
    p.exportFile.setBounds(x + rw - 270, 404, 130, 32);
    p.drag.setBounds(x + rw - 132, 404, 132, 32);
    const int half = (rw - 30) / 2;
    p.consonance.setBounds(x, 470, half, 26);
    p.diversity.setBounds(x + half + 30, 470, half, 26);
    p.movement.setBounds(x, 524, half, 26);
    p.density.setBounds(x + half + 30, 524, half, 26);
    p.mutation.setBounds(x, 578, half, 26);
    p.tempo.setBounds(x + half + 30, 578, half, 26);
    p.output.setBounds(x, 620, 230, 28);
    p.monitor.setBounds(x + 240, 620, 176, 28);
    p.follow.setBounds(x + 424, 620, 160, 28);
    p.stats.setBounds(x, 658, rw, 46);
}

void MicrotonalComposerPanel::paint(juce::Graphics& g) {
    auto& p = *impl;
    g.setColour(panel.withAlpha(0.6f));
    g.fillRoundedRectangle(0, 76, 270, float(getHeight() - 82), 10);
    auto label = [&](const char* en, const char* pl, int x, int y, int width) {
        g.setColour(muted);
        g.setFont(font(11));
        g.drawText(p.tr(en, pl), x, y, width, 20, juce::Justification::left);
    };
    label("Pitch system", "System wysokości", 12, 72, 246);
    label("Equal divisions per octave", "Równe podziały oktawy", 12, 126, 246);
    label("Ratios (1/1 ... < 2/1)", "Proporcje (1/1 ... < 2/1)", 12, 182, 246);
    label("Base frequency / Hz", "Częstotliwość bazowa / Hz", 12, 280, 246);
    label("Octaves", "Oktawy", 12, 336, 116);
    label("Length / beats", "Długość / uderzenia", 142, 336, 116);
    label("Population", "Populacja", 12, 394, 246);
    label("Generations", "Pokolenia", 12, 450, 246);
    const int x = 298, half = (getWidth() - x - 42) / 2;
    label("Simple ratio preference", "Preferencja prostych proporcji", x, 448, half);
    label("Degree diversity", "Różnorodność stopni", x + half + 30, 448, half);
    label("Movement", "Wielkość ruchu", x, 502, half);
    label("Density", "Gęstość", x + half + 30, 502, half);
    label("Mutation", "Mutacja", x, 556, half);
    label("Tempo / BPM", "Tempo / BPM", x + half + 30, 556, half);
    g.setColour(violet);
    g.setFont(font(11));
    const auto& c = p.clip;
    const auto name =
        c.tuning.rational ? p.tr("Ratios", "Proporcje") : juce::String(c.tuning.divisions) + "-EDO";
    g.drawText(name + "  /  " + juce::String(c.baseHz, 3) + " Hz", x + 98, 408, 300, 24,
               juce::Justification::left);
}
