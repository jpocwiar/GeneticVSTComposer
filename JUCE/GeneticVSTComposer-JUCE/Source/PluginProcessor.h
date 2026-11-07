#pragma once
#include "ChordGenerator.h"
#include "ComposerEngine.h"
#include "MicrotonalEngine.h"
#include "reverbcore/Fdn.hpp"
#include <JuceHeader.h>
#include <deque>
#include <memory>
#include <mutex>

class GeneticVSTComposerJUCEAudioProcessor : public juce::AudioProcessor, private juce::Timer {
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
    void generate(bool evolve, bool overHarmony = false);
    struct MicrotonalWorkspace {
        microtonal::Settings settings;
        microtonal::Clip clip;
        microtonal::Stats stats;
        microtonal::Bank variations;
        int selected = 0;
        microtonal::ChordSettings chordSettings;
        microtonal::Clip chords;
        microtonal::Stats chordStats;
    };
    MicrotonalWorkspace microtonalSnapshot() const;
    juce::Result setMicrotonalSettings(const microtonal::Settings&);
    void generateMicrotonal(bool evolve);
    void generateMicrotonalChords(bool withMelody = false);
    void generateMicrotonalFromChords();
    juce::Result setMicrotonalChordSettings(const microtonal::ChordSettings&);
    juce::Result revoiceMicrotonalChords();
    void selectMicrotonalVariation(int);
    void pinMicrotonalVariation(int);
    void undoMicrotonal();
    void redoMicrotonal();
    bool canUndoMicrotonal() const;
    bool canRedoMicrotonal() const;
    void setMicrotonalClip(microtonal::Clip);
    void setMicrotonalMode(bool);
    bool isMicrotonalMode() const { return microtonalMode.load(); }
    void startMicrotonalAudition(int source = 0);
    void stopMicrotonalAudition();
    juce::Result exportMicrotonalMidi(const juce::File&, int source = 0) const;
    uint64_t microtonalRevision() const { return microRevision.load(); }
    void evolveRegion(double start, double end);
    struct PlaybackRegion {
        double start = 0, end = 0;
        int slot = -1;
        bool enabled = false;
    };
    void setPlaybackRegion(double start, double end, bool enabled);
    PlaybackRegion playbackRegion() const;
    juce::String searchSummary() const;
    uint64_t searchSerial() const { return searchSerialValue.load(std::memory_order_relaxed); }
    uint64_t statusSerial() const { return statusSerialValue.load(std::memory_order_relaxed); }
    void cancelGeneration() { cancelled.store(true); }
    void setPreview(bool enabled);
    void startAudition();
    void startChordAudition();
    bool isAuditioningChords() const { return chordPreview.load(); }
    bool audibleChords() const { return heardChordPreview.load(); }
    bool audibleChordLayer() const { return heardChordPreview.load() || heardChordLayer.load(); }
    double chordPlayPosition() const { return chordBeat.load(); }
    juce::Result generateChords();
    // Chords fitted to the selected melody, which stays unchanged.
    juce::Result harmonizeMelody();
    juce::Result revoiceChords();
    juce::Result adaptMelodyToChords();
    bool canAdaptMelodyToChords() const;
    composer::Clip chordClipSnapshot() const;
    juce::Result exportChordMidi(const juce::File&) const;
    juce::Result setTimeSignature(const juce::String&);
    composer::TimeSignature timeSignature() const;
    juce::String groupingText() const;
    juce::Result setGroupingText(const juce::String&);
    void finishCapture();
    // Applies capture, evolve-each-loop and host-meter requests; message thread only.
    void serviceMessageThread();
    void setArrangementForm(int form);
    int arrangementFormIndex() const { return arrangementForm; }
    void setProgressionPreset(int index);
    int progressionIndex() const;
    int scaleIndex() const;
    int phraseBarCount() const;
    int chordBarCount() const;
    void setScaleChoice(int engineScale);
    void setPhraseBarCount(int bars);
    void setChordLengthMenu(int id);
    int phraseLengthMenuId() const;
    int chordLengthMenuId() const;
    // Phrase type menu: 1-3 Riff with a half-, one- or two-bar cell, 4 Motif, 5 Melody.
    int phraseMenuId() const;
    void setPhraseMenu(int id);
    // Fitness terms of the displayed phrase under the current settings.
    std::vector<composer::FitnessTerm> fitnessBreakdown() const;
    static juce::StringArray extendedScaleNames();
    composer::SearchBudget configuredSearchBudget() const;
    juce::String searchDescription() const;
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
    VisualState chordVisualState() const noexcept;
    bool isGenerating() const { return busy.load(); }
    float generationProgress() const { return progress.load(); }
    uint64_t documentRevision() const { return revision.load(); }
    juce::String statusText() const;
    void setStatus(const juce::String&);
    void setInterfaceLanguage(int language);
    int interfaceLanguageIndex() const { return interfaceLanguage.load(); }
    void applyPreset(int index);
    juce::Result applyRegion(double start, double end, composer::RegionAction);
    bool microtonesEnabled() const { return cached.microEnabled->load() > 0.5f; }
    int tuningIndex() const { return microtonesEnabled() ? std::max(1, int(cached.tuning->load())) : 0; }
    // MIDI encoding of microtonal pitches: 0 is plain 12-TET (tuning off), 1 pitch bend on
    // channel 1, 2 MPE with one member channel per note.
    int microtonalOutput() const {
        return tuningIndex() > 0 ? 1 + int(cached.microOutput->load()) : 0;
    }
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
    juce::Result exportMelodyAndChords(const juce::File&) const;
    juce::Result exportMidiRange(const juce::File&, double start, double end) const;
    juce::Result importMidi(const juce::File&);
    juce::Result saveBank(const juce::File&);
    juce::Result loadBank(const juce::File&);
    void panic() {
        panicRequested.store(true);
        preview.store(false);
        chordPreview.store(false);
    }
    static juce::StringArray scaleNames();
    static juce::StringArray rootNames();
    static juce::StringArray tuningNames();

  private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameters();
    float param(const char* id) const { return parameters.getRawParameterValue(id)->load(); }
    // Audio-thread reads use pointers taken once. APVTS lookup walks a string map.
    struct CachedParameters {
        std::atomic<float>* swing = nullptr;
        std::atomic<float>* gate = nullptr;
        std::atomic<float>* snap = nullptr;
        std::atomic<float>* root = nullptr;
        std::atomic<float>* scale = nullptr;
        std::atomic<float>* microEnabled = nullptr;
        std::atomic<float>* tuning = nullptr;
        std::atomic<float>* microOutput = nullptr;
        std::atomic<float>* tempo = nullptr;
        std::atomic<float>* follow = nullptr;
        std::atomic<float>* bendRange = nullptr;
        std::atomic<float>* monitor = nullptr;
        std::atomic<float>* volume = nullptr;
        std::atomic<float>* auditionSound = nullptr;
        std::atomic<float>* quantiseSwitch = nullptr;
        std::atomic<float>* auditionChords = nullptr;
        std::atomic<float>* chordMidiOut = nullptr;
        std::atomic<float>* humanize = nullptr;
        std::atomic<float>* extendedScale = nullptr;
        std::atomic<float>* hostMeter = nullptr;
        std::atomic<float>* capturePhrase = nullptr;
        std::atomic<float>* evolveEachLoop = nullptr;
        std::atomic<float>* auditionReverb = nullptr;
    };
    CachedParameters cached;
    void cacheAudioParameters();
    struct SearchJob {
        bool independentMicrotonal = false;
        microtonal::Settings microSettings;
        microtonal::Clip microParent;
        microtonal::Clip microHarmony;
        microtonal::ChordSettings microChords;
        int microAction = 0;
        composer::Settings settings;
        composer::Clip parent;
        std::vector<composer::Clip> mates;
        uint64_t startContent = 0;
        uint64_t startRevision = 0; // Independent microtonal workspace revision.
        bool useParent = false;
        bool evolve = false;
        bool selectBest = false;
        int regionSlot = -1;
    };
    void beginSearch(bool evolve, bool overHarmony, bool regionOnly, double start = 0, double end = 0);
    void performSearch(SearchJob);
    void performMicrotonalSearch(const SearchJob&);
    void publishMicrotonalLocked();
    juce::ValueTree microtonalStateLocked() const;
    static bool readMicrotonalState(const juce::ValueTree&, MicrotonalWorkspace&);
    void processMicrotonalBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&);
    void beginMicrotonalSearch(bool evolve, int action);
    void pushMicrotonalHistoryLocked();
    microtonal::Clip microtonalPlaybackClipLocked(int source) const;
    void assignStatusLocked(const juce::String&);
    composer::Settings settings() const;
    void publishLocked();
    // contentChange is false for edits a running search cannot conflict with, such as pinning.
    void pushHistoryLocked(bool contentChange = true);
    bool restore(const void*, int);
    composer::Note renderedNote(composer::Note, const composer::Clip&, int transpose = 0,
                                int index = -1) const;
    void setParameter(const char*, float);
    juce::Result readMidi(const juce::File&, composer::Clip&, bool& truncated,
                          composer::Harmony* markers = nullptr) const;
    juce::Result clipFromSequence(const juce::MidiMessageSequence&, double ticksPerQuarter, composer::Clip&,
                                  bool& truncated) const;
    void addMusicalMeta(juce::MidiMessageSequence&, const composer::Clip&) const;
    void timerCallback() override;
    void applyHostMeter();
    bool selectBestOnFinish = false;
    void appendClipMidi(juce::MidiMessageSequence&, const composer::Clip&, bool alreadyRendered,
                        bool plainMidi) const;
    juce::Result writeMidiFile(const juce::File&, const juce::MidiMessageSequence&,
                               const juce::MidiMessageSequence*) const;
    juce::Result exportClip(const composer::Clip&, const juce::File&, bool alreadyRendered = false,
                            bool plainMidi = false) const;
    composer::VoicingSettings voicingSettings() const;
    mutable std::mutex documentMutex;
    MicrotonalWorkspace microWorkspace;
    std::atomic<bool> microtonalMode{false};
    std::atomic<bool> microSearchActive{false};
    std::atomic<bool> microEvolveRequested{false};
    std::atomic<uint64_t> microRevision{1};
    struct MicroPlaybackSnapshot {
        microtonal::Settings settings;
        microtonal::Clip clip;
    };
    MicroPlaybackSnapshot pendingMicro, audioMicro;
    int microPlaybackSource = 0;
    std::deque<MicrotonalWorkspace> microUndo, microRedo;
    uint64_t pendingMicroRevision = 0, audioMicroRevision = 0;
    bool audioMicrotonalMode = false;
    double microPhase = 0, microExpectedPpq = 0;
    int nextMicroNote = 0;
    bool microWasRunning = false, microWasHostPlaying = false;
    composer::Bank bank;
    composer::Harmony customHarmony;
    composer::Clip grooveReference, comparison, draft, chordClip;
    bool grooveLoaded = false, draftActive = false;
    int draftSlot = -1;
    juce::String draftLabel;
    struct Document {
        composer::Bank bank;
        composer::Harmony harmony;
        composer::Clip groove;
        bool grooveLoaded = false;
        composer::Clip chords;
    };
    Document documentLocked() const;
    void restoreDocumentLocked(const Document&);
    std::deque<Document> undoStack, redoStack;
    juce::String status{"Choose a variation, or generate something new."};
    std::atomic<int> selected{0};
    std::atomic<int> audibleSlot{0};
    std::atomic<bool> comparisonAvailable{false}, compareRequested{false}, heardComparison{false};
    std::atomic<bool> customHarmonyEnabled{false};
    std::atomic<bool> chordPreview{false}, heardChordPreview{false};
    std::atomic<uint64_t> revision{1};
    // Counts edits a finished search must not overwrite. Pinning and A/B captures do not count.
    std::atomic<uint64_t> contentRevision{1};
    std::atomic<bool> busy{false}, cancelled{false}, preview{false}, playing{false}, panicRequested{false};
    std::atomic<float> progress{0};
    mutable std::mutex searchMutex;
    composer::SearchStats lastSearch;
    double searchSeconds = 0;
    std::atomic<uint64_t> searchSerialValue{0};
    std::atomic<uint64_t> statusSerialValue{1};
    std::atomic<int> interfaceLanguage{0};
    std::array<int, 16> groups{};
    int groupCount = 0, groupUnit = 4;
    int arrangementForm = 0;
    std::atomic<int> hostNumerator{0}, hostDenominator{0};
    std::atomic<bool> evolveLoopRequested{false};
    static constexpr int captureCapacity = 65536;
    struct CaptureEvent {
        double ppq = 0;
        int pitch = 0;
        int velocity = 0;
    };
    std::unique_ptr<std::array<CaptureEvent, captureCapacity>> captureEvents;
    std::atomic<uint32_t> captureWrite{0}, captureOrigin{0};
    bool captureWasOn = false;
    bool captureAudioArmed = false;
    reverbcore::Fdn8 auditionReverbFx;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> reverbMix;
    std::atomic<double> playBeat{0}, displayTempo{120};
    class SearchThread;
    friend class SearchThread;
    std::unique_ptr<SearchThread> searchThread;
    juce::Random seedGenerator;
    // The audio thread only tries this lock, retaining the last bank if busy.
    juce::SpinLock exchangeLock;
    PlaybackRegion pendingRegion, audioRequestedRegion, audioRegion;
    uint64_t pendingRegionSerial = 0, audioRegionSerial = 0;
    composer::Bank pendingBank, audioBank;
    composer::Clip pendingComparison, pendingDraft, audioComparison, audioDraft, activeClip;
    composer::Clip pendingChords, audioChords;
    bool pendingHasDraft = false, audioHasDraft = false;
    int pendingDraftSlot = -1, audioDraftSlot = -1;
    uint64_t pendingRevision = 1, audioRevision = 0;
    int audioSelection = -1, nextNote = 0, trigger = -1, transpose = 0;
    bool audioCompare = false, audioDraftActive = false, loopBoundary = false;
    bool audioChordPreview = false;
    bool wasPlaying = false, wasHostPlaying = false, lastFollow = false;
    double phase = 0, expectedPpq = 0, rate = 44100;
    std::array<bool, 12> heldTriggers{};
    std::array<bool, 128> heldTranspose{};
    struct Voice {
        bool on = false;
        int sourceIndex = -1, channel = 1;
        double remaining = 0, phase = 0, gain = 0, target = 0, increment = 0;
    };
    struct MicroVoice {
        Voice sound;
        int key = 0;
    };
    std::array<MicroVoice, 15> microVoices{};
    int microChannel = 0;
    void allMicroOff(int sample, juce::MidiBuffer&);
    void releaseMicroVoice(int index, int sample, juce::MidiBuffer&);
    void startMicroVoice(const microtonal::Clip&, const microtonal::Event&, int sourceIndex, int sample,
                         juce::MidiBuffer&);
    std::array<Voice, 128> voices{};
    // Internal accompaniment has independent voices: melody pitch bend/MPE can never detune it.
    std::array<Voice, 128> chordVoices{};
    // Channel 16 note state for optional chord MIDI. It never shares the melody channel table.
    std::array<bool, 128> chordMidiSounding{};
    bool chordLayerPlaying = false;
    int nextChordNote = 0;
    double lastChordPhase = -1, accompanimentBeat = 0;
    uint64_t chordNoteOnSerial = 0;
    void releaseChordMidi(int sample, juce::MidiBuffer&) noexcept;
    void stopChordLayer(int sample, juce::MidiBuffer&) noexcept;
    void scheduleChordLayer(bool run, bool restart, double beat, double step, int sample,
                            juce::MidiBuffer&) noexcept;
    double renderVoice(Voice&, bool monitor, int sound, double step) noexcept;
    std::array<double, 128> phaseIncrements{};
    // Notes and last use per MIDI channel, so an MPE note takes the longest-idle member channel.
    std::array<int, 17> channelNotes{};
    std::array<uint64_t, 17> channelUse{};
    uint64_t channelClock = 0;
    int outputMode = 0, outputBendRange = 2;
    bool outputConfigured = false;
    void resetOutput(int sample, juce::MidiBuffer&);
    // Bounded, lock-free publication once per callback; the UI never touches audio-thread voices.
    static_assert(std::atomic<float>::is_always_lock_free && std::atomic<int>::is_always_lock_free &&
                      std::atomic<uint64_t>::is_always_lock_free,
                  "Visual telemetry requires lock-free atomics");
    std::array<std::atomic<float>, 128> visualChordNotes;
    std::array<std::atomic<int>, 128> visualChordSourceNotes;
    std::atomic<uint64_t> visualChordSerial{0};
    std::atomic<bool> heardChordLayer{false};
    std::atomic<double> chordBeat{-1};
    std::array<std::atomic<float>, 128> visualNotes;
    std::array<std::atomic<int>, 128> visualSourceNotes;
    std::atomic<float> visualOutputPeak{0};
    std::atomic<uint64_t> visualNoteOnSerial{0};
    uint64_t audioNoteOnSerial = 0;
    void clearVisualState() noexcept;
    void publishVisualState(float peak) noexcept;
    juce::MidiBuffer outputMidi;
    void allOff(int sample, juce::MidiBuffer&);
    void releaseVoice(int pitch, int sample, juce::MidiBuffer&);
    void noteOn(const composer::Note&, int sourceIndex, int sample, juce::MidiBuffer&);
    void noteOnTuned(const composer::Note&, double cents, int sourceIndex, int sample, juce::MidiBuffer&);
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GeneticVSTComposerJUCEAudioProcessor)
};
