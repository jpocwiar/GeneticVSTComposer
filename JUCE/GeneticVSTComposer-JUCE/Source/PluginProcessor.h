#pragma once
#include "ComposerEngine.h"
#include <JuceHeader.h>
#include <deque>
#include <mutex>
#include <thread>

class GeneticVSTComposerJUCEAudioProcessor : public juce::AudioProcessor {
  public:
    GeneticVSTComposerJUCEAudioProcessor();
    ~GeneticVSTComposerJUCEAudioProcessor() override;
    void prepareToPlay(double, int) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.15; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return ""; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState parameters;
    composer::Bank bankSnapshot() const;
    composer::Clip selectedClip() const;
    composer::Clip displayedClip() const;
    int selectedIndex() const { return selected.load(); }
    int audibleIndex() const { return audibleSlot.load(); }
    int queuedIndex() const {
        return isPlaying() && selected.load() != audibleSlot.load() ? selected.load() : -1;
    }
    void selectClip(int index);
    void setClip(int index, composer::Clip clip);
    void toggleFavourite(int index);
    void undo();
    void redo();
    bool canUndo() const;
    bool canRedo() const;
    void generate(bool evolve);
    void cancelGeneration() { cancelled.store(true); }
    void setPreview(bool enabled);
    bool previewRequested() const { return preview.load(); }
    bool isPlaying() const { return playing.load(); }
    double playPosition() const { return playBeat.load(); }
    double tempo() const { return displayTempo.load(); }
    struct VisualState {
        std::array<float, 128> notes{};
        // Emitted MIDI pitch -> note index in the audible clip, or -1 after note-off.
        std::array<int, 128> sourceNotes;
        float outputPeak = 0;
        uint64_t noteOnSerial = 0;
        VisualState() { sourceNotes.fill(-1); }
    };
    // MIDI activity is independent of the internal monitor; peak includes its audible release tail.
    VisualState visualState() const noexcept;
    bool isGenerating() const { return busy.load(); }
    float generationProgress() const { return progress.load(); }
    uint64_t documentRevision() const { return revision.load(); }
    juce::String statusText() const;
    void setStatus(const juce::String&);
    void applyPreset(int index);
    juce::Result applyRegion(double start, double end, composer::RegionAction);
    juce::Result setCustomHarmony(const juce::String&);
    void clearCustomHarmony();
    juce::String customHarmonyText() const;
    composer::Harmony harmonySnapshot() const;
    juce::Result importHarmonyMidi(const juce::File&);
    juce::Result importGrooveMidi(const juce::File&);
    void useSelectedAsGroove();
    bool hasGrooveReference() const;
    juce::Result applyGroove(float timing, float velocity);
    juce::Result previewCombination(int rhythm, int pitches, int accents);
    juce::Result previewArrangement();
    bool hasDraft() const;
    void applyDraft();
    void discardDraft();
    juce::String draftDescription() const;
    void captureComparison();
    void toggleComparison();
    void clearComparison();
    bool hasComparison() const { return comparisonAvailable.load(); }
    bool isComparing() const { return compareRequested.load(); }
    bool audibleComparison() const { return heardComparison.load(); }
    composer::Clip comparisonClip() const;
    juce::Result exportMidi(const juce::File&) const;
    juce::Result exportMidiRange(const juce::File&, double start, double end) const;
    juce::Result importMidi(const juce::File&);
    juce::Result saveBank(const juce::File&);
    juce::Result loadBank(const juce::File&);
    void panic() {
        panicRequested.store(true);
        preview.store(false);
    }
    static juce::StringArray scaleNames();
    static juce::StringArray rootNames();

  private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameters();
    float param(const char* id) const { return parameters.getRawParameterValue(id)->load(); }
    composer::Settings settings() const;
    void publishLocked();
    void pushHistoryLocked();
    bool restore(const void*, int);
    composer::Note renderedNote(composer::Note, const composer::Clip&, int transpose = 0) const;
    void setParameter(const char*, float);
    juce::Result readMidi(const juce::File&, composer::Clip&, bool& truncated) const;
    juce::Result exportClip(const composer::Clip&, const juce::File&, bool alreadyRendered = false) const;
    mutable std::mutex documentMutex;
    composer::Bank bank;
    composer::Harmony customHarmony;
    composer::Clip grooveReference, comparison, draft;
    bool grooveLoaded = false, draftActive = false;
    int draftSlot = -1;
    juce::String draftLabel;
    struct Document {
        composer::Bank bank;
        composer::Harmony harmony;
        composer::Clip groove;
        bool grooveLoaded = false;
    };
    Document documentLocked() const;
    void restoreDocumentLocked(const Document&);
    std::deque<Document> undoStack, redoStack;
    juce::String status{"Choose a variation, or generate something new."};
    std::atomic<int> selected{0};
    std::atomic<int> audibleSlot{0};
    std::atomic<bool> comparisonAvailable{false}, compareRequested{false}, heardComparison{false};
    std::atomic<bool> customHarmonyEnabled{false};
    std::atomic<uint64_t> revision{1};
    std::atomic<bool> busy{false}, cancelled{false}, preview{false}, playing{false}, panicRequested{false};
    std::atomic<float> progress{0};
    std::atomic<double> playBeat{0}, displayTempo{120};
    std::thread worker;
    juce::Random seedGenerator;
    // The audio thread only tries this lock, retaining the last bank if busy.
    juce::SpinLock exchangeLock;
    composer::Bank pendingBank, audioBank;
    composer::Clip pendingComparison, pendingDraft, audioComparison, audioDraft, activeClip;
    bool pendingHasDraft = false, audioHasDraft = false;
    int pendingDraftSlot = -1, audioDraftSlot = -1;
    uint64_t pendingRevision = 1, audioRevision = 0;
    int audioSelection = -1, nextNote = 0, trigger = -1, transpose = 0;
    bool audioCompare = false, audioDraftActive = false, loopBoundary = false;
    bool wasPlaying = false, wasHostPlaying = false, lastFollow = false;
    double phase = 0, expectedPpq = 0, rate = 44100;
    std::array<bool, 12> heldTriggers{};
    std::array<bool, 128> heldTranspose{};
    struct Voice {
        bool on = false;
        int sourceIndex = -1;
        double remaining = 0, phase = 0, gain = 0, target = 0;
    };
    std::array<Voice, 128> voices{};
    std::array<double, 128> phaseIncrements{};
    // Bounded, lock-free publication once per callback; the UI never touches audio-thread voices.
    static_assert(std::atomic<float>::is_always_lock_free && std::atomic<int>::is_always_lock_free &&
                      std::atomic<uint64_t>::is_always_lock_free,
                  "Visual telemetry requires lock-free atomics");
    std::array<std::atomic<float>, 128> visualNotes;
    std::array<std::atomic<int>, 128> visualSourceNotes;
    std::atomic<float> visualOutputPeak{0};
    std::atomic<uint64_t> visualNoteOnSerial{0};
    uint64_t audioNoteOnSerial = 0;
    void clearVisualState() noexcept;
    void publishVisualState(float peak) noexcept;
    juce::MidiBuffer outputMidi;
    void allOff(int sample, juce::MidiBuffer&);
    void noteOn(const composer::Note&, int sourceIndex, int sample, juce::MidiBuffer&);
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GeneticVSTComposerJUCEAudioProcessor)
};
