#include "CompositionPanel.h"
#include "ComposerTheme.h"
#include "EditorFormat.h"
#include "PianoRoll.h"
#include "PluginProcessor.h"
#include <algorithm>
#include <cmath>

namespace {
using Processor = GeneticVSTComposerJUCEAudioProcessor;

juce::String words(bool polish, const char* english, const char* translated) {
    return juce::String::fromUTF8(polish ? translated : english);
}

class SectionStrip final : public juce::Component {
  public:
    void setSections(const Processor::CompositionWorkspace& next, int sounding, bool pl) {
        workspace = next;
        playing = sounding;
        polish = pl;
        repaint();
    }
    void paint(juce::Graphics& g) override {
        if (workspace.sections.empty()) {
            g.setColour(palette::muted);
            g.setFont(font(13));
            g.drawText(words(polish, "Create sections from your selected idea to begin.",
                             "Utwórz sekcje z wybranego pomysłu, aby zacząć."),
                       getLocalBounds(), juce::Justification::centredLeft);
            return;
        }
        const float width = float(getWidth()) / float(workspace.sections.size());
        for (size_t i = 0; i < workspace.sections.size(); ++i) {
            const auto& section = workspace.sections[i];
            const juce::Rectangle<float> box(float(i) * width, 0, width - 6, float(getHeight() - 1));
            const bool selected = int(i) == workspace.selected, sounding = int(i) == playing;
            g.setColour(selected ? palette::teal.withAlpha(0.15f) : palette::surface);
            g.fillRoundedRectangle(box, 7);
            g.setColour(sounding ? palette::accent : selected ? palette::teal : palette::line);
            g.drawRoundedRectangle(box.reduced(0.5f), 7, 1);
            g.setColour(selected ? palette::teal : palette::text);
            g.setFont(font(13, true));
            g.drawText(section.name, box.toNearestInt().reduced(9, 4).removeFromTop(20),
                       juce::Justification::centredLeft);
            const double bar = section.melody.numerator * 4.0 / section.melody.denominator;
            g.setColour(palette::muted);
            g.setFont(font(10));
            g.drawText(juce::String(section.melody.length / std::max(0.125, bar), 1) +
                           words(polish, " bars", " taktów"),
                       box.toNearestInt().reduced(9, 4).withTop(24), juce::Justification::centredLeft);
        }
    }
    void mouseDown(const juce::MouseEvent& event) override {
        if (!workspace.sections.empty() && onSelect)
            onSelect(
                juce::jlimit(0, int(workspace.sections.size()) - 1,
                             int(event.position.x / std::max(1, getWidth()) * workspace.sections.size())));
    }
    std::function<void(int)> onSelect;

  private:
    Processor::CompositionWorkspace workspace;
    int playing = -1;
    bool polish = false;
};
} // namespace

struct CompositionPanel::Impl : public juce::Component {
    CompositionPanel& owner;
    Processor& processor;
    ComposerLookAndFeel look;
    PianoRoll roll;
    SectionStrip sectionStrip;
    juce::TextButton ideas, sections, context;
    juce::ComboBox role, preferred, alternative, sectionBars;
    juce::Slider identity, amount, energy, contrast;
    juce::TextButton captureHook, clearHook, develop, answer, vary, finish, explore;
    juce::TextButton hearPreferred, hearAlternative, prefer, moreLike, resetPreferences;
    juce::TextButton createSections, addSection, removeSection, earlierSection, laterSection, applyHarmony;
    juce::TextEditor sectionName, harmonyText;
    juce::ToggleButton editSection;
    juce::TextButton sectionDevelop, sectionAnswer, sectionContrast, sectionFinish;
    juce::TextButton importDrums, clearDrums, importBass, clearBass, configureAi, generateAi, cancel;
    juce::TextButton playIdea, playSection, playAll, stop, exportMidi, back;
    juce::Label summary, hookSummary, preferenceSummary, drumsSummary, bassSummary, aiSummary, rollCaption;
    std::unique_ptr<juce::FileChooser> chooser;
    std::vector<juce::File> dragFiles;
    Processor::CompositionWorkspace state;
    int page = 0, language = -1, shownSlot = -1, shownSection = -1, preferredId = 1;
    uint64_t shownRevision = ~uint64_t{0};
    bool refreshing = false;
    double lastAiCheck = 0;
    juce::String cachedAiAvailability;

    Impl(CompositionPanel& panel, Processor& p) : owner(panel), processor(p) {
        setLookAndFeel(&look);
        setOpaque(true);
        const auto add = [this](juce::Component& component, const char* id) {
            component.setComponentID(id);
            addAndMakeVisible(component);
        };
        add(ideas, "compose-ideas-tab");
        add(sections, "compose-sections-tab");
        add(context, "compose-context-tab");
        ideas.onClick = [this] { showPage(0); };
        sections.onClick = [this] { showPage(1); };
        context.onClick = [this] { showPage(2); };
        for (auto* button : {&ideas, &sections, &context})
            button->getProperties().set("segment", true);

        add(role, "compose-role");
        role.onChange = [this] {
            if (!refreshing && role.getSelectedId() > 0)
                report(processor.setCompositionRole(role.getSelectedId() - 1));
        };
        for (auto* slider : {&identity, &amount, &energy, &contrast}) {
            slider->setRange(0, 1, 0.01);
            slider->setSliderStyle(juce::Slider::LinearHorizontal);
            slider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 22);
            slider->textFromValueFunction = [](double value) { return juce::String(int(value * 100)) + "%"; };
            slider->valueFromTextFunction = [](const juce::String& value) {
                return value.getDoubleValue() / 100;
            };
            slider->updateText();
        }
        add(identity, "compose-identity");
        add(amount, "compose-context-amount");
        add(energy, "compose-section-energy");
        add(contrast, "compose-section-contrast");
        identity.onDragEnd = [this] { report(processor.setMotifIdentity(float(identity.getValue()))); };
        amount.onDragEnd = [this] { report(processor.setContextAmount(float(amount.getValue()))); };
        energy.onDragEnd = contrast.onDragEnd = [this] { commitSection(); };
        identity.onValueChange = [this] {
            if (!refreshing && !identity.isMouseButtonDown())
                report(processor.setMotifIdentity(float(identity.getValue())));
        };
        amount.onValueChange = [this] {
            if (!refreshing && !amount.isMouseButtonDown())
                report(processor.setContextAmount(float(amount.getValue())));
        };
        energy.onValueChange = [this] {
            if (!refreshing && !energy.isMouseButtonDown())
                commitSection();
        };
        contrast.onValueChange = [this] {
            if (!refreshing && !contrast.isMouseButtonDown())
                commitSection();
        };
        add(captureHook, "compose-capture-motif");
        add(clearHook, "compose-clear-motif");
        captureHook.onClick = [this] {
            report(processor.captureMotif(roll.regionStart(), roll.regionEnd()));
        };
        clearHook.onClick = [this] {
            processor.clearMotif();
            refresh();
        };
        add(develop, "compose-develop");
        add(answer, "compose-answer");
        add(vary, "compose-contrast");
        add(finish, "compose-finish");
        add(explore, "compose-explore");
        juce::TextButton* actions[]{&develop, &answer, &vary, &finish};
        for (int i = 0; i < 4; ++i)
            actions[i]->onClick = [this, i] { report(processor.developSelectedMotif(i)); };
        explore.onClick = [this] {
            if (processor.isGenerating())
                processor.cancelGeneration();
            else
                processor.generateCreativeBank();
            refresh();
        };
        explore.setColour(juce::TextButton::buttonColourId, palette::accent);
        explore.setColour(juce::TextButton::textColourOffId, palette::background);
        add(preferred, "compose-preferred-variation");
        add(alternative, "compose-other-variation");
        preferred.setSelectedId(1, juce::dontSendNotification);
        alternative.setSelectedId(2, juce::dontSendNotification);
        preferred.onChange = [this] {
            if (!refreshing && preferred.getSelectedId() > 0) {
                preferredId = preferred.getSelectedId();
                processor.selectClip(preferredId - 1);
                refresh();
            }
        };
        alternative.onChange = [this] {
            if (!refreshing)
                refresh();
        };
        add(hearPreferred, "compose-hear-preferred");
        add(hearAlternative, "compose-hear-other");
        hearPreferred.onClick = [this] { auditionVariation(preferred.getSelectedId()); };
        hearAlternative.onClick = [this] { auditionVariation(alternative.getSelectedId()); };
        add(prefer, "compose-prefer");
        add(moreLike, "compose-more-like");
        add(resetPreferences, "compose-reset-preferences");
        prefer.onClick = [this] {
            report(processor.preferVariation(preferred.getSelectedId() - 1, alternative.getSelectedId() - 1));
        };
        moreLike.onClick = [this] {
            processor.selectClip(preferred.getSelectedId() - 1);
            processor.generateCreativeBank(true);
            refresh();
        };
        resetPreferences.onClick = [this] {
            processor.resetPreferences();
            refresh();
        };
        add(createSections, "compose-create-sections");
        createSections.onClick = [this] {
            report(processor.createComposition());
            showPage(1);
        };
        add(sectionStrip, "compose-section-strip");
        sectionStrip.onSelect = [this](int index) { report(processor.selectCompositionSection(index)); };
        add(sectionName, "compose-section-name");
        sectionName.setSelectAllWhenFocused(true);
        sectionName.onReturnKey = sectionName.onFocusLost = [this] { commitSection(); };
        add(sectionBars, "compose-section-bars");
        for (int i = 1; i <= 16; ++i)
            sectionBars.addItem(juce::String(i), i);
        sectionBars.onChange = [this] {
            if (!refreshing)
                commitSection();
        };
        add(addSection, "compose-add-section");
        add(removeSection, "compose-remove-section");
        add(earlierSection, "compose-section-earlier");
        add(laterSection, "compose-section-later");
        addSection.onClick = [this] { report(processor.addCompositionSection()); };
        removeSection.onClick = [this] { report(processor.removeCompositionSection(state.selected)); };
        earlierSection.onClick = [this] { report(processor.moveCompositionSection(state.selected, -1)); };
        laterSection.onClick = [this] { report(processor.moveCompositionSection(state.selected, 1)); };
        add(harmonyText, "compose-section-harmony");
        harmonyText.setSelectAllWhenFocused(true);
        harmonyText.onReturnKey = [this] { commitHarmony(); };
        add(applyHarmony, "compose-apply-section-harmony");
        applyHarmony.onClick = [this] { commitHarmony(); };
        add(editSection, "compose-edit-section-notes");
        editSection.onClick = [this] { roll.setNoteEditing(editSection.getToggleState()); };
        add(sectionDevelop, "compose-section-develop");
        add(sectionAnswer, "compose-section-answer");
        add(sectionContrast, "compose-section-vary");
        add(sectionFinish, "compose-section-finish");
        juce::TextButton* sectionActions[]{&sectionDevelop, &sectionAnswer, &sectionContrast, &sectionFinish};
        for (int i = 0; i < 4; ++i)
            sectionActions[i]->onClick = [this, i] { report(processor.developCompositionSection(i)); };

        add(importDrums, "compose-import-drums");
        add(clearDrums, "compose-clear-drums");
        add(importBass, "compose-import-bass");
        add(clearBass, "compose-clear-bass");
        importDrums.onClick = [this] { chooseFile(0); };
        importBass.onClick = [this] { chooseFile(1); };
        clearDrums.onClick = [this] {
            processor.clearCompositionReference(false);
            refresh();
        };
        clearBass.onClick = [this] {
            processor.clearCompositionReference(true);
            refresh();
        };
        add(configureAi, "compose-configure-ai");
        add(generateAi, "compose-generate-ai");
        add(cancel, "compose-cancel-ai");
        configureAi.onClick = [this] { chooseFile(3); };
        generateAi.onClick = [this] { report(processor.generateAiIdeas()); };
        cancel.onClick = [this] {
            processor.cancelGeneration();
            refresh();
        };
        add(playIdea, "compose-play-idea");
        add(playSection, "compose-play-section");
        add(playAll, "compose-play-all");
        add(stop, "compose-stop");
        add(exportMidi, "compose-export");
        add(back, "compose-back-to-melody");
        playIdea.onClick = [this] {
            processor.stopComposition();
            processor.startAudition();
            refresh();
        };
        playSection.onClick = [this] {
            processor.auditionComposition(false);
            refresh();
        };
        playAll.onClick = [this] {
            processor.auditionComposition(true);
            refresh();
        };
        stop.onClick = [this] {
            processor.stopComposition();
            processor.setPreview(false);
            refresh();
        };
        exportMidi.onClick = [this] { chooseFile(2); };
        back.onClick = [this] {
            if (owner.onShowMelody)
                owner.onShowMelody();
        };
        add(roll, "compose-piano-roll");
        roll.setEditable(true);
        roll.onRegionChange = [this](double start, double end) {
            if (!refreshing && page != 1) {
                const auto clip = processor.selectedClip();
                processor.setPlaybackRegion(start, end, start > 1.0e-8 || end < clip.length - 1.0e-8);
            }
        };
        roll.onEdit = [this](composer::Clip clip) {
            if (page == 1 && !state.sections.empty())
                report(processor.setCompositionSectionClip(state.selected, std::move(clip)));
            else {
                processor.setClip(processor.selectedIndex(), std::move(clip));
                refresh();
            }
        };
        roll.onAudition = [this] {
            if (page == 1)
                processor.auditionComposition(false);
            else {
                processor.stopComposition();
                processor.startAudition();
            }
            refresh();
        };
        roll.onMidiDrag = [this] { dragMidi(); };
        add(summary, "compose-summary");
        add(hookSummary, "compose-motif-summary");
        add(preferenceSummary, "compose-preference-summary");
        add(drumsSummary, "compose-drums-summary");
        add(bassSummary, "compose-bass-summary");
        add(aiSummary, "compose-ai-status");
        add(rollCaption, "compose-roll-caption");
        for (auto* label : {&summary, &hookSummary, &preferenceSummary, &drumsSummary, &bassSummary,
                            &aiSummary, &rollCaption}) {
            label->setFont(font(12));
            label->setColour(juce::Label::textColourId, palette::muted);
            label->setMinimumHorizontalScale(0.85f);
        }
        aiSummary.setJustificationType(juce::Justification::topLeft);
        refresh();
    }
    ~Impl() override {
        chooser.reset();
        for (const auto& file : dragFiles)
            file.deleteFile();
        setLookAndFeel(nullptr);
    }
    bool polish() const { return processor.interfaceLanguageIndex() == 1; }
    juce::String tr(const char* en, const char* pl) const { return words(polish(), en, pl); }
    void report(const juce::Result& result) {
        if (result.failed())
            processor.setStatus(result.getErrorMessage());
        refresh();
    }
    void showPage(int next) {
        page = juce::jlimit(0, 2, next);
        shownRevision = ~uint64_t{0};
        refresh();
        resized();
        repaint();
    }
    void auditionVariation(int id) {
        if (id < 1 || id > composer::slotCount)
            return;
        processor.stopComposition();
        processor.selectClip(id - 1);
        processor.startAudition();
        refresh();
    }
    void commitSection() {
        if (refreshing || state.sections.empty())
            return;
        report(processor.updateCompositionSection(state.selected, sectionName.getText(),
                                                  std::max(1, sectionBars.getSelectedId()),
                                                  float(energy.getValue()), float(contrast.getValue())));
    }
    void commitHarmony() {
        if (!state.sections.empty())
            report(processor.setCompositionSectionHarmony(state.selected, harmonyText.getText()));
    }
    juce::String familyName(int family) const {
        return family == 0   ? tr("development", "rozwinięcie")
               : family == 1 ? tr("response", "odpowiedź")
               : family == 2 ? tr("contrast", "kontrast")
                             : tr("idea", "pomysł");
    }
    void refresh() {
        refreshing = true;
        state = processor.compositionSnapshot();
        const auto revision = processor.documentRevision();
        const int slot = processor.selectedIndex();
        const bool hasSections = !state.sections.empty();
        const int sectionIndex = page == 1 && hasSections ? state.selected : -1;
        const bool changed = shownRevision != revision || shownSlot != slot || shownSection != sectionIndex ||
                             language != processor.interfaceLanguageIndex();
        language = processor.interfaceLanguageIndex();
        const bool busy = processor.isGenerating();
        const auto text = [this](juce::Button& button, const char* en, const char* pl) {
            button.setButtonText(tr(en, pl));
        };
        text(ideas, "Ideas", "Pomysły");
        text(sections, "Sections", "Sekcje");
        text(context, "Context / AI", "Kontekst / AI");
        text(captureHook, "Keep selected motif", "Zachowaj motyw");
        text(clearHook, "Clear motif", "Usuń motyw");
        text(develop, "Develop", "Rozwiń");
        text(answer, "Answer", "Odpowiedz");
        text(vary, "Contrast", "Zmień charakter");
        text(finish, "Finish", "Zakończ");
        text(explore, busy ? "Cancel search" : "Explore 12 ideas",
             busy ? "Anuluj szukanie" : "Odkryj 12 pomysłów");
        text(hearPreferred, "Hear A", "Słuchaj A");
        text(hearAlternative, "Hear B", "Słuchaj B");
        text(prefer, "Prefer A to B", "Wolę A od B");
        text(moreLike, "More like A", "Więcej jak A");
        text(resetPreferences, "Reset role preferences", "Wyzeruj preferencje roli");
        text(createSections, "Build sections from idea", "Utwórz sekcje z pomysłu");
        text(addSection, "+ Section", "+ Sekcja");
        text(removeSection, "Remove", "Usuń");
        text(earlierSection, "Earlier", "Wcześniej");
        text(laterSection, "Later", "Później");
        text(applyHarmony, "Apply chords", "Zastosuj akordy");
        text(editSection, "Edit notes", "Edytuj nuty");
        text(sectionDevelop, "Develop section", "Rozwiń sekcję");
        text(sectionAnswer, "Answer", "Odpowiedz");
        text(sectionContrast, "Contrast", "Zmień charakter");
        text(sectionFinish, "Finish", "Zakończ");
        text(importDrums, "Import drum MIDI", "Importuj MIDI perkusji");
        text(clearDrums, "Clear", "Usuń");
        text(importBass, "Import bass MIDI", "Importuj MIDI basu");
        text(clearBass, "Clear", "Usuń");
        text(configureAi, "AI environment...", "Środowisko AI...");
        text(generateAi, "Generate AI ideas", "Generuj pomysły AI");
        text(cancel, "Cancel", "Anuluj");
        text(playIdea, "Play idea", "Graj pomysł");
        text(playSection, "Play section", "Graj sekcję");
        text(playAll, "Play all", "Graj całość");
        text(stop, "Stop", "Stop");
        text(exportMidi, page == 1 ? "Export composition..." : "Export idea...",
             page == 1 ? "Eksportuj kompozycję..." : "Eksportuj pomysł...");
        text(back, "Melody editor", "Edytor melodii");
        if (!role.getNumItems()) {
            role.addItem(tr("Lead", "Melodia prowadząca"), 1);
            role.addItem(tr("Bass", "Bas"), 2);
            role.addItem(tr("Riff", "Riff"), 3);
            role.addItem(tr("Arpeggio", "Arpeggio"), 4);
        } else {
            role.changeItemText(1, tr("Lead", "Melodia prowadząca"));
            role.changeItemText(2, tr("Bass", "Bas"));
            role.changeItemText(3, "Riff");
            role.changeItemText(4, "Arpeggio");
        }
        role.setSelectedId(juce::jlimit(0, 3, state.role) + 1, juce::dontSendNotification);
        role.setTooltip(tr("Instrument role guides new creative proposals; existing notes stay editable.",
                           "Rola prowadzi nowe kreatywne propozycje; istniejące nuty pozostają edytowalne."));
        applyHarmony.setTooltip(
            tr("Apply this section's chords, then Develop section to fit its melody to them.",
               "Zastosuj akordy tej sekcji, a potem Rozwiń sekcję, aby dopasować do nich melodię."));
        if (!identity.isMouseButtonDown())
            identity.setValue(state.identity, juce::dontSendNotification);
        if (!amount.isMouseButtonDown())
            amount.setValue(state.contextAmount, juce::dontSendNotification);
        const auto bank = processor.bankSnapshot();
        const int currentA = preferred.getSelectedId() > 0 ? preferred.getSelectedId() : preferredId;
        const int currentB = alternative.getSelectedId() > 0 ? alternative.getSelectedId() : 2;
        if (changed && !preferred.isPopupActive() && !alternative.isPopupActive()) {
            preferred.clear(juce::dontSendNotification);
            alternative.clear(juce::dontSendNotification);
            for (int i = 0; i < composer::slotCount; ++i) {
                const auto title =
                    juce::String(i + 1).paddedLeft('0', 2) + " / " + familyName(bank[size_t(i)].family);
                preferred.addItem(title, i + 1);
                alternative.addItem(title, i + 1);
            }
            preferred.setSelectedId(currentA, juce::dontSendNotification);
            alternative.setSelectedId(currentB, juce::dontSendNotification);
            if (page == 1 && hasSections) {
                const auto& section = state.sections[size_t(state.selected)];
                roll.setClip(section.melody, shownSection != state.selected);
                roll.setHarmony(section.harmony);
                if (!sectionName.hasKeyboardFocus(true))
                    sectionName.setText(section.name, false);
                if (!harmonyText.hasKeyboardFocus(true))
                    harmonyText.setText(juce::String(composer::harmonyText(section.harmony)), false);
                const double bar = section.melody.numerator * 4.0 / section.melody.denominator;
                sectionBars.setSelectedId(
                    juce::jlimit(1, 16, int(std::lround(section.melody.length / std::max(0.125, bar)))),
                    juce::dontSendNotification);
                if (!energy.isMouseButtonDown())
                    energy.setValue(section.energy, juce::dontSendNotification);
                if (!contrast.isMouseButtonDown())
                    contrast.setValue(section.contrast, juce::dontSendNotification);
            } else {
                roll.setClip(bank[size_t(slot)], shownSlot != slot || shownSection >= 0);
                roll.setHarmony(processor.harmonySnapshot());
                const auto region = processor.playbackRegion();
                if (region.enabled && region.slot == slot)
                    roll.setRegion(region.start, region.end);
            }
            roll.setMicrotonal(processor.microtonesEnabled());
            shownRevision = revision;
            shownSlot = slot;
            shownSection = page == 1 && hasSections ? state.selected : -1;
        }
        roll.setInterfaceLanguage(language);
        roll.setNoteEditing(page == 1 && hasSections && editSection.getToggleState());
        if (page == 1)
            roll.setTooltip(tr("Section development applies to this whole section. Note locks protect exact "
                               "events. Drag the roll to export only this section's melody.",
                               "Rozwinięcie dotyczy całej tej sekcji. Blokady chronią dokładne nuty. "
                               "Przeciągnięcie piano rolla eksportuje tylko melodię tej sekcji."));
        const bool hearingSection = page == 1 && processor.isCompositionPlaying() &&
                                    processor.audibleCompositionSection() == state.selected;
        const bool hearingIdea = page != 1 && !processor.isCompositionPlaying() && processor.isPlaying() &&
                                 !processor.audibleChords() && !processor.audibleComparison() &&
                                 processor.audibleIndex() == slot;
        const bool hearingDisplayed = hearingSection || hearingIdea;
        roll.setPlayhead(hearingDisplayed ? processor.playPosition() : -1);
        roll.setActivity(hearingDisplayed ? processor.visualState() : Processor::VisualState{},
                         hearingDisplayed, true, 0.05f);
        sectionStrip.setSections(state, processor.audibleCompositionSection(), polish());
        hookSummary.setText(
            state.preserveHook && state.hook.count > 0
                ? tr("Motif note count: ", "Liczba nut motywu: ") + juce::String(state.hook.count) +
                      tr(". Identity preserves recognisable relationships; locks keep exact notes.",
                         ". Tożsamość zachowuje rozpoznawalne relacje; blokady chronią dokładne nuty.")
                : tr("Select a time range on the roll, then keep its motif. Without a range, the whole idea "
                     "is used.",
                     "Zaznacz czas na piano rollu i zachowaj jego motyw. Bez zaznaczenia użyty zostanie cały "
                     "pomysł."),
            juce::dontSendNotification);
        preferenceSummary.setText(tr("Choices learned for this role: ", "Zapisane wybory dla tej roli: ") +
                                      juce::String(state.preference.comparisons),
                                  juce::dontSendNotification);
        drumsSummary.setText(state.drums.count ? juce::String(state.drums.count) +
                                                     tr(" drum events loaded", " zdarzeń perkusji")
                                               : tr("No rhythm reference", "Brak referencji rytmu"),
                             juce::dontSendNotification);
        bassSummary.setText(state.bass.count
                                ? juce::String(state.bass.count) + tr(" bass notes loaded", " nut basu")
                                : tr("No bass reference", "Brak referencji basu"),
                            juce::dontSendNotification);
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (changed || now - lastAiCheck > 1000 || cachedAiAvailability.isEmpty()) {
            cachedAiAvailability = processor.aiAvailability();
            lastAiCheck = now;
        }
        auto aiStatus = cachedAiAvailability;
        if (polish()) {
            if (aiStatus.startsWith("Local AI ready"))
                aiStatus = juce::String::fromUTF8("Lokalne AI gotowe. Generowanie działa offline.");
            else if (aiStatus.startsWith("Local AI runtime selected:"))
                aiStatus = juce::String::fromUTF8("Wybrane lokalne środowisko: ") +
                           aiStatus.fromFirstOccurrenceOf(":", false, false).trim();
            else if (aiStatus.contains("model files are missing"))
                aiStatus = juce::String::fromUTF8(
                    "Brak plików modelu. Przygotuj lokalne środowisko według instrukcji AI.");
            else if (aiStatus.startsWith("Local AI:"))
                aiStatus = juce::String::fromUTF8(
                    "Wybierz przygotowane lokalne środowisko AI. Instrukcja jest w rozdziale 12 pomocy.");
        }
        aiSummary.setText(aiStatus, juce::dontSendNotification);
        aiSummary.setTooltip(cachedAiAvailability);
        summary.setText(juce::String(state.sections.size()) + tr(" sections / ", " sekcji / ") +
                            tr("selected idea ", "wybrany pomysł ") + juce::String(slot + 1),
                        juce::dontSendNotification);
        rollCaption.setText(page == 1 && hasSections
                                ? tr("SECTION / ", "SEKCJA / ") + state.sections[size_t(state.selected)].name
                                : tr("SELECTED IDEA / ", "WYBRANY POMYSŁ / ") +
                                      juce::String(slot + 1).paddedLeft('0', 2),
                            juce::dontSendNotification);
        const auto visible = [](bool show, std::initializer_list<juce::Component*> components) {
            for (auto* component : components)
                component->setVisible(show);
        };
        visible(page == 0, {&role, &identity, &captureHook, &clearHook, &develop, &answer, &vary, &finish,
                            &explore, &preferred, &alternative, &hearPreferred, &hearAlternative, &prefer,
                            &moreLike, &resetPreferences, &hookSummary, &preferenceSummary});
        visible(page == 1,
                {&createSections, &sectionStrip, &sectionName, &sectionBars, &energy, &contrast, &addSection,
                 &removeSection, &earlierSection, &laterSection, &harmonyText, &applyHarmony, &editSection,
                 &sectionDevelop, &sectionAnswer, &sectionContrast, &sectionFinish});
        visible(page == 2, {&importDrums, &clearDrums, &importBass, &clearBass, &amount, &drumsSummary,
                            &bassSummary, &configureAi, &generateAi, &cancel, &aiSummary});
        playIdea.setVisible(page != 1);
        playSection.setVisible(page == 1);
        playAll.setVisible(page == 1);
        for (auto* button :
             {&captureHook, &develop, &answer, &vary, &finish, &moreLike, &prefer, &createSections,
              &addSection, &removeSection, &sectionDevelop, &sectionAnswer, &sectionContrast, &sectionFinish,
              &importDrums, &importBass, &generateAi, &applyHarmony})
            button->setEnabled(!busy);
        for (auto* component :
             std::initializer_list<juce::Component*>{&sectionName, &sectionBars, &energy, &contrast,
                                                     &harmonyText, &editSection, &playSection, &playAll})
            component->setEnabled(hasSections && !busy);
        removeSection.setEnabled(hasSections && !busy);
        addSection.setEnabled(state.sections.size() < 8 && !busy);
        earlierSection.setEnabled(hasSections && !busy && state.selected > 0);
        laterSection.setEnabled(hasSections && !busy && state.selected + 1 < int(state.sections.size()));
        applyHarmony.setEnabled(hasSections && !busy);
        sectionDevelop.setEnabled(hasSections && !busy);
        sectionAnswer.setEnabled(hasSections && !busy);
        sectionContrast.setEnabled(hasSections && !busy);
        sectionFinish.setEnabled(hasSections && !busy);
        prefer.setEnabled(!busy && preferred.getSelectedId() != alternative.getSelectedId());
        clearHook.setEnabled(state.hook.count > 0 && !busy);
        clearDrums.setEnabled(state.drums.count > 0 && !busy);
        clearBass.setEnabled(state.bass.count > 0 && !busy);
        cancel.setEnabled(busy);
        for (int i = 0; i < 3; ++i) {
            auto* button = i == 0 ? &ideas : i == 1 ? &sections : &context;
            button->setColour(juce::TextButton::buttonColourId, page == i ? palette::teal : palette::surface);
            button->setColour(juce::TextButton::textColourOffId,
                              page == i ? palette::background : palette::muted);
        }
        refreshing = false;
        repaint();
    }
    void chooseFile(int action) {
        const bool exporting = action == 2;
        const bool compositionExport = page == 1;
        auto initial = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
        if (action == 3 && juce::File::isAbsolutePath(state.aiPython))
            initial = juce::File(state.aiPython); // A restored path only preselects the chooser.
        chooser = std::make_unique<juce::FileChooser>(
            action == 0   ? tr("Import drum MIDI reference", "Importuj referencję MIDI perkusji")
            : action == 1 ? tr("Import bass MIDI reference", "Importuj referencję MIDI basu")
            : action == 3 ? tr("Choose the Python executable from your local AI environment",
                               "Wybierz interpreter Python z lokalnego środowiska AI")
                          : tr("Export composition MIDI", "Eksportuj MIDI kompozycji"),
            initial, action == 3 ? "*" : "*.mid;*.midi");
        juce::Component::SafePointer<Impl> safe(this);
        chooser->launchAsync(
            exporting
                ? juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting
                : juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [safe, action, compositionExport](const juce::FileChooser& dialog) {
                if (!safe || dialog.getResult() == juce::File())
                    return;
                auto file = dialog.getResult();
                if (action == 3) {
                    safe->processor.setAiPythonExecutable(file);
                    safe->refresh();
                    return;
                }
                if (action == 2) {
                    if (!file.hasFileExtension("mid;midi"))
                        file = file.withFileExtension("mid");
                    safe->report(compositionExport ? safe->processor.exportCompositionMidi(file)
                                                   : safe->processor.exportMidi(file));
                } else
                    safe->report(safe->processor.importCompositionReference(file, action == 1));
            });
    }
    void dragMidi() {
        auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                        .getNonexistentChildFile("Genetic-Composition", ".mid", false);
        const auto result = page == 1 ? processor.exportCompositionSectionMidi(state.selected, file)
                                      : processor.exportMidi(file);
        if (result.failed()) {
            report(result);
            return;
        }
        dragFiles.push_back(file);
        if (!juce::DragAndDropContainer::performExternalDragDropOfFiles({file.getFullPathName()}, false,
                                                                        &roll))
            processor.setStatus(tr("Drag could not start. Use Export instead.",
                                   "Nie udało się rozpocząć przeciągania. Użyj eksportu."));
    }
    void resized() override {
        const int w = getWidth(), h = getHeight();
        summary.setBounds(w - 382, 18, 362, 25);
        ideas.setBounds(20, 57, 142, 31);
        sections.setBounds(170, 57, 142, 31);
        context.setBounds(320, 57, 170, 31);
        createSections.setBounds(w - 264, 57, 244, 31);
        earlierSection.setBounds(500, 57, 78, 31);
        laterSection.setBounds(586, 57, 78, 31);
        role.setBounds(20, 125, 160, 29);
        captureHook.setBounds(192, 125, 188, 29);
        clearHook.setBounds(390, 125, 112, 29);
        identity.setBounds(534, 125, w - 554, 29);
        hookSummary.setBounds(20, 161, w - 40, 28);
        const int actionWidth = (w - 64 - 220) / 4;
        int x = 20;
        for (auto* button : {&develop, &answer, &vary, &finish}) {
            button->setBounds(x, 198, actionWidth, 31);
            x += actionWidth + 6;
        }
        explore.setBounds(w - 240, 198, 220, 31);
        const int choiceWidth = (w - 380) / 2;
        preferred.setBounds(20, 244, choiceWidth, 29);
        hearPreferred.setBounds(28 + choiceWidth, 244, 82, 29);
        alternative.setBounds(122 + choiceWidth, 244, choiceWidth, 29);
        hearAlternative.setBounds(130 + 2 * choiceWidth, 244, 82, 29);
        prefer.setBounds(w - 152, 244, 132, 29);
        moreLike.setBounds(20, 283, 164, 28);
        resetPreferences.setBounds(194, 283, 176, 28);
        preferenceSummary.setBounds(382, 283, w - 402, 28);
        sectionStrip.setBounds(20, 103, w - 40, 48);
        sectionName.setBounds(20, 182, 170, 29);
        sectionBars.setBounds(202, 182, 90, 29);
        energy.setBounds(312, 182, 200, 29);
        contrast.setBounds(532, 182, w - 748, 29);
        addSection.setBounds(w - 196, 182, 82, 29);
        removeSection.setBounds(w - 106, 182, 86, 29);
        harmonyText.setBounds(20, 247, w - 194, 29);
        applyHarmony.setBounds(w - 162, 247, 142, 29);
        x = 20;
        for (auto* button : {&sectionDevelop, &sectionAnswer, &sectionContrast, &sectionFinish}) {
            button->setBounds(x, 289, 148, 28);
            x += 156;
        }
        editSection.setBounds(w - 174, 289, 154, 28);
        const int column = (w - 64) / 3, aiX = 44 + 2 * column;
        drumsSummary.setBounds(20, 133, column, 41);
        bassSummary.setBounds(32 + column, 133, column, 41);
        importDrums.setBounds(20, 185, column - 80, 30);
        clearDrums.setBounds(28 + column - 80, 185, 72, 30);
        importBass.setBounds(32 + column, 185, column - 80, 30);
        clearBass.setBounds(40 + 2 * column - 80, 185, 72, 30);
        amount.setBounds(20, 254, 2 * column + 12, 30);
        aiSummary.setBounds(aiX, 133, column, 68);
        configureAi.setBounds(aiX, 211, column, 29);
        generateAi.setBounds(aiX, 250, column, 31);
        cancel.setBounds(aiX, 290, column, 27);
        rollCaption.setBounds(20, h - 343, w - 40, 22);
        roll.setBounds(20, h - 318, w - 40, 212);
        playIdea.setBounds(20, h - 92, 130, 31);
        playSection.setBounds(20, h - 92, 142, 31);
        playAll.setBounds(170, h - 92, 124, 31);
        stop.setBounds(page == 1 ? 302 : 158, h - 92, 84, 31);
        back.setBounds(w - 416, h - 92, 178, 31);
        exportMidi.setBounds(w - 226, h - 92, 206, 31);
    }
    void paint(juce::Graphics& g) override {
        g.fillAll(palette::background);
        g.setColour(palette::text);
        g.setFont(font(23, true));
        g.drawText(tr("COMPOSE", "KOMPONUJ"), 20, 13, 320, 31, juce::Justification::centredLeft);
        const auto label = [&](const juce::String& text, int x, int y, int width) {
            g.setColour(palette::muted);
            g.setFont(font(11, true));
            g.drawText(text, x, y, width, 20, juce::Justification::centredLeft);
        };
        if (page == 0) {
            label(tr("INSTRUMENT ROLE", "ROLA INSTRUMENTU"), 20, 103, 170);
            label(tr("MOTIF IDENTITY", "TOŻSAMOŚĆ MOTYWU"), 534, 103, getWidth() - 554);
        } else if (page == 1) {
            label(tr("SECTION NAME", "NAZWA SEKCJI"), 20, 160, 170);
            label(tr("BARS", "TAKTY"), 202, 160, 90);
            label(tr("ENERGY", "ENERGIA"), 312, 160, 200);
            label(tr("CONTRAST", "KONTRAST"), 532, 160, 200);
            label(tr("SECTION HARMONY / chord:beats, e.g. Cm:4 Fm:4 G7:4 Cm:4",
                     "HARMONIA SEKCJI / akord:uderzenia, np. Cm:4 Fm:4 G7:4 Cm:4"),
                  20, 224, getWidth() - 40);
        } else {
            const int column = (getWidth() - 64) / 3;
            label(tr("DRUM CONTEXT", "KONTEKST PERKUSJI"), 20, 104, column);
            label(tr("BASS CONTEXT", "KONTEKST BASU"), 32 + column, 104, column);
            label(tr("LOCAL AI / EXPERIMENTAL", "LOKALNE AI / EKSPERYMENTALNE"), 44 + 2 * column, 104,
                  column);
            label(tr("CONTEXT INFLUENCE", "WPŁYW KONTEKSTU"), 20, 229, 2 * column);
        }
        g.setColour(palette::muted);
        g.setFont(font(11));
        g.drawText(tr("Changes, sections and preferences are saved with your project. Drag the roll to "
                      "export its displayed MIDI.",
                      "Zmiany, sekcje i preferencje zapisują się w projekcie. Przeciągnij piano roll, aby "
                      "wyeksportować widoczne MIDI."),
                   20, getHeight() - 45, getWidth() - 40, 25, juce::Justification::centredLeft);
    }
};

CompositionPanel::CompositionPanel(Processor& processor) : impl(std::make_unique<Impl>(*this, processor)) {
    setComponentID("composition-panel");
    setOpaque(true);
    addAndMakeVisible(*impl);
    setSize(940, 660);
    startTimerHz(20);
}
CompositionPanel::~CompositionPanel() {
    stopTimer();
}
void CompositionPanel::refreshDisplay() {
    impl->refresh();
}
void CompositionPanel::showPage(int page) {
    impl->showPage(page);
}
int CompositionPanel::pageIndex() const {
    return impl->page;
}
void CompositionPanel::paint(juce::Graphics& g) {
    g.fillAll(palette::background);
}
void CompositionPanel::resized() {
    const float scale = std::min(1.0f, std::min(getWidth() / 940.0f, getHeight() / 660.0f));
    if (scale <= 0)
        return;
    impl->setTransform(juce::AffineTransform::scale(scale));
    impl->setBounds(0, 0, int(std::ceil(getWidth() / scale)), int(std::ceil(getHeight() / scale)));
}
void CompositionPanel::timerCallback() {
    if (isShowing())
        refreshDisplay();
}
