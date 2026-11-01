#include "PluginProcessor.h"
#include "InterfaceText.h"
#include "PluginEditor.h"
#include <cmath>
#include <condition_variable>
#include <tuple>
#include <utility>

using Processor = GeneticVSTComposerJUCEAudioProcessor;

class Processor::SearchThread : public juce::Thread {
  public:
    explicit SearchThread(Processor& ownerIn) : juce::Thread("composer-search"), owner(ownerIn) {
        startThread();
    }
    ~SearchThread() override { shutdown(); }
    void shutdown() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stopping = true;
        }
        wake.notify_all();
        stopThread(15000);
    }
    void submit(SearchJob job) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            pending = std::move(job);
            hasJob = true;
        }
        wake.notify_one();
    }
    void run() override {
        while (!threadShouldExit()) {
            SearchJob job;
            {
                std::unique_lock<std::mutex> lock(mutex);
                wake.wait(lock, [&] { return stopping || hasJob || threadShouldExit(); });
                if (stopping || threadShouldExit())
                    return;
                job = std::move(pending);
                hasJob = false;
            }
            owner.performSearch(std::move(job));
        }
    }

  private:
    Processor& owner;
    std::mutex mutex;
    std::condition_variable wake;
    SearchJob pending;
    bool hasJob = false;
    bool stopping = false;
};

void Processor::cacheAudioParameters() {
    auto take = [this](const char* id) { return parameters.getRawParameterValue(id); };
    cached.swing = take("swing");
    cached.gate = take("gate");
    cached.snap = take("snap");
    cached.root = take("root");
    cached.scale = take("scale");
    cached.microEnabled = take("microEnabled");
    cached.tuning = take("tuning");
    cached.microOutput = take("microOutput");
    cached.tempo = take("tempo");
    cached.follow = take("follow");
    cached.bendRange = take("bendRange");
    cached.monitor = take("monitor");
    cached.volume = take("volume");
    cached.auditionSound = take("auditionSound");
    cached.quantiseSwitch = take("quantiseSwitch");
    cached.auditionChords = take("auditionChords");
    cached.chordMidiOut = take("chordMidiOut");
    cached.humanize = take("humanize");
    cached.extendedScale = take("extendedScale");
    cached.hostMeter = take("hostMeter");
    cached.capturePhrase = take("capturePhrase");
    cached.evolveEachLoop = take("evolveEachLoop");
    cached.auditionReverb = take("auditionReverb");
}
juce::StringArray Processor::rootNames() {
    return {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
}
juce::StringArray Processor::scaleNames() {
    return {"Major",          "Natural minor",    "Dorian",
            "Mixolydian",     "Major pentatonic", "Minor pentatonic",
            "Harmonic minor", "Lydian",           "Chromatic"};
}
juce::StringArray Processor::tuningNames() {
    return {"Off (12-TET)",
            "Quarter-tone inflections",
            "Maqam Rast",
            "Maqam Bayati",
            "Maqam Saba",
            "Neutral blues pentatonic",
            "Quarter tones (24-EDO)"};
}
juce::AudioProcessorValueTreeState::ParameterLayout Processor::createParameters() {
    juce::AudioProcessorValueTreeState::ParameterLayout p;
    auto choice = [&](const char* id, const char* name, juce::StringArray items, int initial) {
        p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{id, 1}, name, items, initial));
    };
    auto number = [&](const char* id, const char* name, float lo, float hi, float initial,
                      float step = 0.01f) {
        p.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{id, 1}, name, juce::NormalisableRange<float>(lo, hi, step), initial));
    };
    auto toggle = [&](const char* id, const char* name, bool initial) {
        p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{id, 1}, name, initial));
    };
    choice("root", "Root", rootNames(), 0);
    choice("scale", "Scale", scaleNames(), 1);
    choice("bars", "Phrase length", {"1 bar", "2 bars", "4 bars", "8 bars"}, 2);
    choice("grid", "Note grid", {"1/8", "1/16", "1/32"}, 1);
    choice("meter", "Time signature", {"4/4", "3/4", "5/4", "7/8", "6/8"}, 0);
    choice("mode", "Generation mode", {"Full melody", "Rhythm only", "Melody on rhythm"}, 0);
    choice("progression", "Harmony",
           {"Scale only", "I - IV - V - I", "I - VI - IV - V", "I - IV - VI - V", "II - V - I - I"}, 0);
    choice("quality", "Search depth", {"Quick", "Balanced", "Deep"}, 1);
    number("density", "Density", 0, 1, 0.5f);
    number("diversity", "Diversity", 0, 1, 0.5f);
    number("syncopation", "Syncopation", 0, 1, 0.25f);
    number("tension", "Jazziness", 0, 1, 0.15f);
    number("space", "Pause amount", 0, 0.85f, 0.15f);
    number("mutation", "Evolution amount", 0, 1, 0.35f);
    number("low", "Lowest note", 24, 96, 48, 1);
    number("high", "Highest note", 36, 108, 84, 1);
    number("swing", "Swing", 0, 0.75f, 0);
    number("gate", "Gate", 0.1f, 1, 0.9f);
    number("tempo", "Preview tempo", 40, 240, 120, 1);
    number("volume", "Monitor level", 0, 1, 0.65f);
    toggle("snap", "Fit to scale", false);
    toggle("lockRhythm", "Keep rhythm", false);
    toggle("follow", "Follow host transport", false);
    toggle("monitor", "Internal sound", true);
    toggle("quantiseSwitch", "Switch at next loop", true);
    choice("tuning", "Microtonal tuning", tuningNames(), 0);
    number("microtones", "Microtones", 0, 1, 1);
    choice("microOutput", "Microtonal MIDI", {"Pitch bend (mono)", "MPE"}, 0);
    number("bendRange", "Pitch-bend range", 1, 48, 2, 1);
    // New choices use separate parameters so existing normalized automation keeps its meaning.
    toggle("meterOverride", "Custom time signature", false);
    number("meterNumerator", "Meter numerator", 1, 32, 4, 1);
    choice("meterDenominator", "Meter denominator", {"1", "2", "4", "8", "16", "32"}, 2);
    juce::StringArray extended{"Use Harmony parameter"};
    const auto& progressions = composer::progressionPresets();
    for (size_t i = 5; i < progressions.size(); ++i)
        extended.add(progressions[i].name);
    choice("extendedProgression", "Extended harmony", extended, 0);
    toggle("customSearch", "Custom search budget", false);
    number("searchPopulation", "Search population", 16, 512, 128, 1);
    number("searchGenerations", "Search generations", 1, 2000, 100, 1);
    number("crossover", "Crossover probability", 0, 1, 0.9f);
    choice("auditionSound", "Audition sound", {"Soft keys", "Sine", "Warm organ"}, 0);
    number("chordOriginality", "Chord originality", 0, 1, 0.35f);
    choice("chordRhythm", "Chord rhythm", {"Two bars", "One bar", "Half bar"}, 1);
    choice("chordVoicing", "Chord voicing", {"Smooth inversions", "Root position", "Open voicings"}, 0);
    number("chordCentre", "Chord register", 36, 72, 60, 1);
    toggle("chordResolve", "Resolve chords to tonic", true);
    toggle("microEnabled", "Microtones enabled", false);
    choice("phraseType", "Phrase type", {"Riff", "Motif", "Melody"}, 0);
    toggle("auditionChords", "Play with chords", false);
    number("dynamics", "Dynamics", 0, 1, 0.5f);
    number("valence", "Valence", 0, 1, 0.5f);
    number("arousal", "Arousal", 0, 1, 0.5f);
    number("weirdness", "Weirdness", 0, 1, 0.1f);
    choice("chordLength", "Chord sequence length", {"Follow melody", "1 bar", "2 bars", "4 bars", "8 bars"},
           3);
    number("humanize", "Humanize", 0, 1, 0);
    choice("extendedBars", "Extended phrase length", {"Use Length", "3 bars", "6 bars", "12 bars", "16 bars"},
           0);
    choice("extendedChordLength", "Extended chord length",
           {"Use chord length", "3 bars", "6 bars", "12 bars", "16 bars"}, 0);
    choice("extendedScale", "Extended scale",
           {"Use Scale", "Phrygian", "Locrian", "Melodic minor", "Whole tone", "Blues", "Phrygian dominant"},
           0);
    choice("chordRhythmShape", "Chord rhythm shape", {"Even", "Long-short", "Short-long"}, 0);
    toggle("chordMidiOut", "Chord MIDI output", false);
    toggle("hostMeter", "Follow host meter", false);
    toggle("capturePhrase", "Capture phrase", false);
    toggle("evolveEachLoop", "Evolve each loop", false);
    number("auditionReverb", "Audition reverb", 0, 1, 0.15f);
    choice("chordCharacter", "Harmonic character", {"Balanced", "Pop", "Jazz", "Modal", "Cinematic"}, 0);
    number("chordColour", "Chord colour", 0, 1, 0.45f);
    number("chordMood", "Chord mood", 0, 1, 0.5f);
    return p;
}
GeneticVSTComposerJUCEAudioProcessor::GeneticVSTComposerJUCEAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "Parameters", createParameters()), bank(composer::makeInitialBank()),
      pendingBank(bank), audioBank(bank),
      captureEvents(std::make_unique<std::array<CaptureEvent, captureCapacity>>()) {
    cacheAudioParameters();
    clearVisualState();
    searchThread = std::make_unique<SearchThread>(*this);
    startTimerHz(30);
}
GeneticVSTComposerJUCEAudioProcessor::~GeneticVSTComposerJUCEAudioProcessor() {
    stopTimer();
    cancelled.store(true);
    searchThread.reset();
}
bool Processor::isBusesLayoutSupported(const BusesLayout& layout) const {
    return layout.getMainOutputChannelSet() == juce::AudioChannelSet::stereo() ||
           layout.getMainOutputChannelSet() == juce::AudioChannelSet::mono();
}
void Processor::prepareToPlay(double sampleRate, int) {
    rate = sampleRate;
    voices = {};
    chordVoices = {};
    chordLayerPlaying = false;
    nextChordNote = 0;
    lastChordPhase = -1;
    accompanimentBeat = 0;
    chordNoteOnSerial = 0;
    visualChordSerial.store(0);
    channelNotes = {};
    channelUse = {};
    channelClock = 0;
    outputMode = 0;
    outputConfigured = false;
    audioNoteOnSerial = 0;
    visualNoteOnSerial.store(0, std::memory_order_relaxed);
    clearVisualState();
    phase = 0;
    nextNote = 0;
    wasPlaying = wasHostPlaying = false;
    trigger = -1;
    transpose = 0;
    heldTriggers = {};
    heldTranspose = {};
    audioSelection = -1;
    audioCompare = audioDraftActive = audioChordPreview = loopBoundary = false;
    heardChordPreview.store(false);
    heardComparison.store(false);
    for (int p = 0; p < 128; ++p)
        phaseIncrements[p] = 440.0 * std::exp2((p - 69.0) / 12.0) / rate;
    outputMidi.ensureSize(65536);
    auditionReverbFx.prepare(rate);
    auditionReverbFx.setDecay(1.6f);
    auditionReverbFx.setSize(0.62f);
    auditionReverbFx.setDamping(6500.0f);
    reverbMix.reset(rate, 0.02);
    reverbMix.setCurrentAndTargetValue(cached.auditionReverb->load());
}
void Processor::releaseResources() {
    playing.store(false);
    preview.store(false);
    chordPreview.store(false);
    heardChordPreview.store(false);
    heardChordLayer.store(false);
    chordBeat.store(-1);
    clearVisualState();
}
Processor::VisualState Processor::visualState() const noexcept {
    VisualState state;
    for (size_t p = 0; p < state.notes.size(); ++p) {
        state.notes[p] = visualNotes[p].load(std::memory_order_relaxed);
        state.sourceNotes[p] = visualSourceNotes[p].load(std::memory_order_relaxed);
    }
    state.outputPeak = visualOutputPeak.load(std::memory_order_relaxed);
    state.noteOnSerial = visualNoteOnSerial.load(std::memory_order_relaxed);
    return state;
}
Processor::VisualState Processor::chordVisualState() const noexcept {
    if (heardChordPreview.load())
        return visualState();
    VisualState state;
    for (size_t i = 0; i < state.notes.size(); ++i) {
        state.notes[i] = visualChordNotes[i].load(std::memory_order_relaxed);
        state.sourceNotes[i] = visualChordSourceNotes[i].load(std::memory_order_relaxed);
    }
    state.noteOnSerial = visualChordSerial.load(std::memory_order_relaxed);
    return state;
}
void Processor::clearVisualState() noexcept {
    for (auto& note : visualChordNotes)
        note.store(0, std::memory_order_relaxed);
    for (auto& index : visualChordSourceNotes)
        index.store(-1, std::memory_order_relaxed);
    heardChordLayer.store(false);
    chordBeat.store(-1);
    for (auto& note : visualNotes)
        note.store(0, std::memory_order_relaxed);
    for (auto& index : visualSourceNotes)
        index.store(-1, std::memory_order_relaxed);
    visualOutputPeak.store(0, std::memory_order_relaxed);
}
void Processor::publishVisualState(float peak) noexcept {
    for (size_t p = 0; p < voices.size(); ++p) {
        visualNotes[p].store(voices[p].on ? float(voices[p].target) : 0.0f, std::memory_order_relaxed);
        visualSourceNotes[p].store(voices[p].on ? voices[p].sourceIndex : -1, std::memory_order_relaxed);
    }
    for (size_t p = 0; p < chordVoices.size(); ++p) {
        visualChordNotes[p].store(chordVoices[p].on ? float(chordVoices[p].target) : 0.0f,
                                  std::memory_order_relaxed);
        visualChordSourceNotes[p].store(chordVoices[p].on ? chordVoices[p].sourceIndex : -1,
                                        std::memory_order_relaxed);
    }
    visualChordSerial.store(chordNoteOnSerial, std::memory_order_relaxed);
    visualOutputPeak.store(peak, std::memory_order_relaxed);
    visualNoteOnSerial.store(audioNoteOnSerial, std::memory_order_relaxed);
}
composer::Bank Processor::bankSnapshot() const {
    std::lock_guard<std::mutex> lock(documentMutex);
    return bank;
}
composer::Clip Processor::selectedClip() const {
    std::lock_guard<std::mutex> lock(documentMutex);
    return bank[selected.load()];
}
composer::Clip Processor::displayedClip() const {
    std::lock_guard<std::mutex> lock(documentMutex);
    return draftActive && draftSlot == selected.load() ? draft : bank[selected.load()];
}
juce::String Processor::statusText() const {
    std::lock_guard<std::mutex> lock(documentMutex);
    return interfaceText(status, interfaceLanguage.load());
}
void Processor::setInterfaceLanguage(int language) {
    const int next = language == 1 ? 1 : 0;
    if (interfaceLanguage.exchange(next) != next)
        statusSerialValue.fetch_add(1, std::memory_order_relaxed);
}
void Processor::assignStatusLocked(const juce::String& text) {
    if (status == text)
        return;
    status = text;
    statusSerialValue.fetch_add(1, std::memory_order_relaxed);
}
void Processor::setStatus(const juce::String& text) {
    std::lock_guard<std::mutex> lock(documentMutex);
    assignStatusLocked(text);
}
void Processor::selectClip(int index) {
    index = juce::jlimit(0, 11, index);
    std::lock_guard<std::mutex> lock(documentMutex);
    const bool changed = selected.exchange(index) != index;
    if (changed && draftActive) {
        draftActive = false;
        assignStatusLocked("Preview discarded after selecting another variation.");
        publishLocked();
    }
    if (!playing.load())
        audibleSlot.store(index);
}
Processor::Document Processor::documentLocked() const {
    return {bank, customHarmony, grooveReference, grooveLoaded, chordClip};
}
void Processor::restoreDocumentLocked(const Document& d) {
    bank = d.bank;
    customHarmony = d.harmony;
    chordClip = d.chords;
    if (chordClip.count == 0 && chordPreview.exchange(false))
        preview.store(false);
    grooveReference = d.groove;
    grooveLoaded = d.grooveLoaded;
    customHarmonyEnabled.store(customHarmony.count > 0);
    draftActive = false;
}
void Processor::pushHistoryLocked() {
    undoStack.push_back(documentLocked());
    if (undoStack.size() > 24)
        undoStack.pop_front();
    redoStack.clear();
}
void Processor::publishLocked() {
    juce::SpinLock::ScopedLockType lock(exchangeLock);
    pendingBank = bank;
    pendingComparison = comparison;
    pendingDraft = draft;
    pendingChords = chordClip;
    pendingHasDraft = draftActive;
    pendingDraftSlot = draftSlot;
    pendingRevision = revision.fetch_add(1) + 1;
}
void Processor::setClip(int index, composer::Clip clip) {
    cancelled.store(true);
    composer::sanitise(clip);
    std::lock_guard<std::mutex> lock(documentMutex);
    if (draftActive) {
        assignStatusLocked("Apply or discard the preview before editing a phrase.");
        return;
    }
    pushHistoryLocked();
    bank[juce::jlimit(0, 11, index)] = clip;
    publishLocked();
}
void Processor::toggleFavourite(int index) {
    std::lock_guard<std::mutex> lock(documentMutex);
    pushHistoryLocked();
    auto& c = bank[juce::jlimit(0, 11, index)];
    c.favourite = !c.favourite;
    publishLocked();
}
bool Processor::canUndo() const {
    std::lock_guard<std::mutex> lock(documentMutex);
    return !undoStack.empty();
}
bool Processor::canRedo() const {
    std::lock_guard<std::mutex> lock(documentMutex);
    return !redoStack.empty();
}
void Processor::undo() {
    cancelled.store(true);
    std::lock_guard<std::mutex> lock(documentMutex);
    if (undoStack.empty())
        return;
    redoStack.push_back(documentLocked());
    restoreDocumentLocked(undoStack.back());
    undoStack.pop_back();
    publishLocked();
}
void Processor::redo() {
    cancelled.store(true);
    std::lock_guard<std::mutex> lock(documentMutex);
    if (redoStack.empty())
        return;
    undoStack.push_back(documentLocked());
    restoreDocumentLocked(redoStack.back());
    redoStack.pop_back();
    publishLocked();
}
void Processor::setPreview(bool enabled) {
    preview.store(enabled);
    if (!enabled)
        chordPreview.store(false);
}
Processor::PlaybackRegion Processor::playbackRegion() const {
    std::lock_guard<std::mutex> lock(documentMutex);
    return pendingRegion;
}
void Processor::setPlaybackRegion(double start, double end, bool enabled) {
    std::lock_guard<std::mutex> lock(documentMutex);
    const int slot = selected.load();
    const double length = draftActive && draftSlot == slot ? draft.length : bank[slot].length;
    if (!std::isfinite(start) || !std::isfinite(end))
        return;
    start = juce::jlimit(0.0, length, start);
    end = juce::jlimit(start, length, end);
    enabled = enabled && end > start;
    if (pendingRegion.start == start && pendingRegion.end == end && pendingRegion.slot == slot &&
        pendingRegion.enabled == enabled)
        return;
    juce::SpinLock::ScopedLockType exchange(exchangeLock);
    pendingRegion = {start, end, slot, enabled};
    ++pendingRegionSerial;
}
void Processor::startAudition() {
    chordPreview.store(false);
    setParameter("follow", 0);
    setParameter("monitor", 1);
    if (param("volume") <= 0)
        setParameter("volume", 0.65f);
    setPreview(true);
    setStatus(
        "Playing the selected phrase. Stop ends playback; With chords adds the internal accompaniment.");
}
void Processor::startChordAudition() {
    if (chordClipSnapshot().count == 0) {
        setStatus("Generate or enter chords before auditioning them.");
        return;
    }
    setParameter("follow", 0);
    setParameter("monitor", 1);
    if (param("volume") <= 0)
        setParameter("volume", 0.65f);
    compareRequested.store(false);
    chordPreview.store(true);
    preview.store(true);
    setStatus("Playing chords. Drag the chord roll to export these exact voicings.");
}
composer::VoicingSettings Processor::voicingSettings() const {
    return {int(param("chordCentre")), int(param("chordVoicing"))};
}
composer::Clip Processor::chordClipSnapshot() const {
    std::lock_guard<std::mutex> lock(documentMutex);
    return chordClip;
}
juce::Result Processor::generateChords() {
    composer::ChordGenerationSettings s;
    s.root = int(param("root"));
    s.scale = scaleIndex();
    s.bars = chordBarCount();
    const auto meter = timeSignature();
    s.numerator = meter.numerator;
    s.denominator = meter.denominator;
    s.originality = param("chordOriginality");
    s.harmonicRhythm = int(param("chordRhythm"));
    s.rhythmShape = int(param("chordRhythmShape"));
    s.character = int(param("chordCharacter"));
    s.colour = param("chordColour");
    s.mood = param("chordMood");
    s.resolve = param("chordResolve") > 0.5f;
    s.seed = uint32_t(seedGenerator.nextInt());
    const auto harmony = composer::generateHarmony(s);
    if (harmony.count == 0)
        return juce::Result::fail("This phrase exceeds 128 beats. Choose fewer bars or a shorter meter.");
    const auto voiced = composer::voiceHarmony(harmony, s.numerator, s.denominator, voicingSettings());
    cancelled.store(true);
    std::lock_guard<std::mutex> lock(documentMutex);
    if (draftActive)
        return juce::Result::fail("Apply or discard the melody preview before changing harmony.");
    pushHistoryLocked();
    customHarmony = harmony;
    chordClip = voiced;
    customHarmonyEnabled.store(true);
    publishLocked();
    assignStatusLocked("New chords are active. Adapt the existing melody or generate a new one.");
    return juce::Result::ok();
}
juce::Result Processor::revoiceChords() {
    const auto voicing = voicingSettings();
    std::lock_guard<std::mutex> lock(documentMutex);
    if (customHarmony.count == 0)
        return juce::Result::fail("Generate or enter chords first.");
    const auto voiced =
        composer::voiceHarmony(customHarmony, chordClip.numerator, chordClip.denominator, voicing);
    pushHistoryLocked();
    chordClip = voiced;
    publishLocked();
    assignStatusLocked("Chord voicings updated. The harmonic sequence and melody notes are unchanged.");
    return juce::Result::ok();
}
bool Processor::canAdaptMelodyToChords() const {
    std::lock_guard<std::mutex> lock(documentMutex);
    if (customHarmony.count == 0 || draftActive)
        return false;
    const int slot = selected.load();
    const auto& clip = bank[size_t(slot)];
    const bool scoped = pendingRegion.enabled && pendingRegion.slot == slot;
    return std::any_of(clip.notes.begin(), clip.notes.begin() + clip.count, [&](const composer::Note& note) {
        return !note.locked && (!scoped || (note.start >= pendingRegion.start && note.start < pendingRegion.end));
    });
}
juce::Result Processor::adaptMelodyToChords() {
    auto s = settings();
    std::lock_guard<std::mutex> lock(documentMutex);
    if (draftActive)
        return juce::Result::fail("Apply or discard the preview before adapting the melody.");
    if (!customHarmony.count)
        return juce::Result::fail("Create or enter chords before adapting the melody.");
    const int slot = selected.load();
    const auto& source = bank[size_t(slot)];
    if (!source.count)
        return juce::Result::fail("Select a melody with notes to adapt.");
    const bool scoped = pendingRegion.enabled && pendingRegion.slot == slot;
    const double start = scoped ? pendingRegion.start : 0.0;
    const double end = scoped ? std::min(pendingRegion.end, source.length) : source.length;
    s.harmony = customHarmony;
    cancelled.store(true);
    const auto adapted = composer::adaptToHarmony(source, start, end, s);
    int changed = 0;
    for (int i = 0; i < source.count; ++i)
        changed += source.notes[size_t(i)].pitch != adapted.notes[size_t(i)].pitch ||
                   source.notes[size_t(i)].cents != adapted.notes[size_t(i)].cents;
    if (!changed) {
        assignStatusLocked("Melody unchanged: its pitches already fit or its notes are protected.");
        return juce::Result::ok();
    }
    pushHistoryLocked();
    bank[size_t(slot)] = adapted;
    compareRequested.store(false);
    publishLocked();
    assignStatusLocked("Melody adapted to chords: " + juce::String(changed) +
                       " notes adjusted. Rhythm and locked notes are preserved. Undo restores the melody." +
                       (param("snap") > 0.5f ? " Fit to scale is on; disable it to hear borrowed chord tones." : ""));
    return juce::Result::ok();
}
composer::TimeSignature Processor::timeSignature() const {
    if (param("meterOverride") > 0.5f)
        return {int(param("meterNumerator")), 1 << int(param("meterDenominator"))};
    return composer::timeSignatures()[size_t(juce::jlimit(0, 4, int(param("meter"))))];
}
juce::Result Processor::setTimeSignature(const juce::String& text) {
    composer::TimeSignature meter{};
    std::string error;
    if (!composer::parseMeter(text.toStdString(), meter, error))
        return juce::Result::fail(juce::String(error));
    const auto& presets = composer::timeSignatures();
    for (int i = 0; i < 5; ++i)
        if (presets[size_t(i)].numerator == meter.numerator &&
            presets[size_t(i)].denominator == meter.denominator) {
            setParameter("meter", float(i));
            setParameter("meterOverride", 0);
            return juce::Result::ok();
        }
    setParameter("meterNumerator", float(meter.numerator));
    setParameter("meterDenominator", float(std::log2(meter.denominator)));
    setParameter("meterOverride", 1);
    return juce::Result::ok();
}
juce::String Processor::groupingText() const {
    std::lock_guard<std::mutex> lock(documentMutex);
    const auto meter = timeSignature();
    std::array<int, 16> stored = groups;
    int count = groupCount, unit = groupUnit > 0 ? groupUnit : meter.denominator;
    if (count <= 0)
        composer::defaultGrouping(meter.numerator, meter.denominator, stored, count, unit);
    auto text = juce::String(composer::groupingText(stored, count));
    if (unit != meter.denominator)
        text += "@" + juce::String(unit);
    return text;
}
juce::Result Processor::setGroupingText(const juce::String& text) {
    const auto meter = timeSignature();
    auto body = text.trim();
    int unit = meter.denominator;
    if (body.containsChar('@')) {
        unit = body.fromLastOccurrenceOf("@", false, false).getIntValue();
        body = body.upToLastOccurrenceOf("@", false, false).trim();
    }
    if (body.isEmpty()) {
        std::lock_guard<std::mutex> lock(documentMutex);
        pushHistoryLocked();
        groupCount = 0;
        groupUnit = meter.denominator;
        auto& clip = bank[size_t(juce::jlimit(0, 11, selected.load()))];
        clip.groupCount = 0;
        clip.groupUnit = meter.denominator;
        publishLocked();
        return juce::Result::ok();
    }
    std::array<int, 16> parsed{};
    int count = 0;
    std::string error;
    if (!composer::parseGrouping(body.toStdString(), parsed, count, error))
        return juce::Result::fail(juce::String(error));
    if (unit < 1 || unit > 32 || (unit & (unit - 1)) != 0)
        return juce::Result::fail("Grouping unit must be 1, 2, 4, 8, 16 or 32.");
    const int barUnits = meter.numerator * unit / meter.denominator;
    int sum = 0;
    for (int i = 0; i < count; ++i)
        sum += parsed[size_t(i)];
    if (meter.numerator * unit % meter.denominator != 0 || sum != barUnits)
        return juce::Result::fail("Grouping must add up to one bar of this meter.");
    std::lock_guard<std::mutex> lock(documentMutex);
    pushHistoryLocked();
    groups = parsed;
    groupCount = count;
    groupUnit = unit;
    auto& clip = bank[size_t(juce::jlimit(0, 11, selected.load()))];
    clip.groups = parsed;
    clip.groupCount = count;
    clip.groupUnit = unit;
    publishLocked();
    return juce::Result::ok();
}
void Processor::setArrangementForm(int form) {
    arrangementForm = juce::jlimit(0, 3, form);
}
void Processor::applyHostMeter() {
    if (cached.hostMeter->load() <= 0.5f)
        return;
    const int numerator = hostNumerator.load();
    const int denominator = hostDenominator.load();
    if (numerator < 1 || denominator < 1)
        return;
    const auto current = timeSignature();
    if (current.numerator == numerator && current.denominator == denominator)
        return;
    setTimeSignature(juce::String(numerator) + "/" + juce::String(denominator));
}
void Processor::finishCapture() {
    const auto end = captureWrite.load(std::memory_order_acquire);
    const auto start = captureOrigin.load(std::memory_order_acquire);
    captureOrigin.store(end, std::memory_order_release);
    if (end == start || captureEvents == nullptr) {
        setStatus("Capture was empty. The phrase bank is unchanged.");
        return;
    }
    if (end - start > uint32_t(captureCapacity)) {
        setStatus("Capture overflowed the phrase buffer. The phrase bank is unchanged.");
        return;
    }
    juce::MidiMessageSequence sequence;
    const double origin = (*captureEvents)[start % captureCapacity].ppq;
    for (uint32_t index = start; index != end; ++index) {
        const auto& event = (*captureEvents)[index % captureCapacity];
        const double beat = std::max(0.0, event.ppq - origin);
        if (event.velocity > 0) {
            auto on = juce::MidiMessage::noteOn(1, event.pitch, juce::uint8(event.velocity));
            on.setTimeStamp(beat * 960.0);
            sequence.addEvent(on);
        } else {
            auto off = juce::MidiMessage::noteOff(1, event.pitch);
            off.setTimeStamp(beat * 960.0);
            sequence.addEvent(off);
        }
    }
    sequence.updateMatchedPairs();
    composer::Clip clip;
    bool truncated = false;
    if (auto result = clipFromSequence(sequence, 960.0, clip, truncated); result.failed()) {
        setStatus(result.getErrorMessage() + " The phrase bank is unchanged.");
        return;
    }
    const auto meter = timeSignature();
    clip.numerator = meter.numerator;
    clip.denominator = meter.denominator;
    {
        std::lock_guard<std::mutex> lock(documentMutex);
        clip.groups = groups;
        clip.groupCount = groupCount;
        clip.groupUnit = groupUnit > 0 ? groupUnit : meter.denominator;
    }
    const double bar = std::max(0.125, clip.numerator * 4.0 / std::max(1, clip.denominator));
    double endTime = 0;
    for (int i = 0; i < clip.count; ++i)
        endTime = std::max(endTime, clip.notes[size_t(i)].start + clip.notes[size_t(i)].duration);
    clip.length = std::clamp(std::ceil(std::max(endTime, bar) / bar - 0.0001) * bar, bar, 128.0);
    setClip(selected.load(), clip);
    setStatus(truncated ? "Captured phrase limited to 512 notes and 128 beats."
                        : "Captured phrase replaced the selected variation.");
}
void Processor::timerCallback() {
    serviceMessageThread();
}
void Processor::serviceMessageThread() {
    const bool capture = cached.capturePhrase->load() > 0.5f;
    if (capture && !captureWasOn)
        captureWasOn = true;
    else if (!capture && captureWasOn) {
        captureWasOn = false;
        finishCapture();
    }
    if (evolveLoopRequested.load(std::memory_order_acquire) && cached.evolveEachLoop->load() > 0.5f &&
        !busy.load()) {
        evolveLoopRequested.store(false, std::memory_order_release);
        const auto region = playbackRegion();
        if (region.enabled && region.slot == selected.load())
            evolveRegion(region.start, region.end);
        else {
            selectBestOnFinish = true;
            generate(true);
        }
    }
    applyHostMeter();
}
int Processor::progressionIndex() const {
    const int extended = int(param("extendedProgression"));
    return extended > 0 ? extended + 4 : int(param("progression"));
}
int Processor::scaleIndex() const {
    const int extended = int(cached.extendedScale->load());
    return extended > 0 ? 9 + juce::jlimit(1, 6, extended) : juce::jlimit(0, 8, int(cached.scale->load()));
}
juce::StringArray Processor::extendedScaleNames() {
    auto names = scaleNames();
    names.add("Phrygian");
    names.add("Locrian");
    names.add("Melodic minor");
    names.add("Whole tone");
    names.add("Blues");
    names.add("Phrygian dominant");
    return names;
}
void Processor::setScaleChoice(int engineScale) {
    if (engineScale >= 10 && engineScale <= 15)
        setParameter("extendedScale", float(engineScale - 9));
    else {
        setParameter("extendedScale", 0);
        setParameter("scale", float(juce::jlimit(0, 8, engineScale)));
    }
}
int Processor::phraseBarCount() const {
    const int extended = int(param("extendedBars"));
    constexpr int extra[]{3, 6, 12, 16};
    if (extended >= 1 && extended <= 4)
        return extra[extended - 1];
    return 1 << juce::jlimit(0, 3, int(param("bars")));
}
int Processor::phraseLengthMenuId() const {
    constexpr int values[]{1, 2, 3, 4, 6, 8, 12, 16};
    const int bars = phraseBarCount();
    for (int i = 0; i < 8; ++i)
        if (values[i] == bars)
            return i + 1;
    return 5;
}
void Processor::setPhraseBarCount(int bars) {
    switch (bars) {
    case 1: setParameter("extendedBars", 0); setParameter("bars", 0); break;
    case 2: setParameter("extendedBars", 0); setParameter("bars", 1); break;
    case 3: setParameter("extendedBars", 1); break;
    case 4: setParameter("extendedBars", 0); setParameter("bars", 2); break;
    case 6: setParameter("extendedBars", 2); break;
    case 8: setParameter("extendedBars", 0); setParameter("bars", 3); break;
    case 12: setParameter("extendedBars", 3); break;
    case 16: setParameter("extendedBars", 4); break;
    default: break;
    }
}
int Processor::chordBarCount() const {
    const int extended = int(param("extendedChordLength"));
    constexpr int extra[]{3, 6, 12, 16};
    if (extended >= 1 && extended <= 4)
        return extra[extended - 1];
    const int length = juce::jlimit(0, 4, int(param("chordLength")));
    return length == 0 ? phraseBarCount() : 1 << (length - 1);
}
int Processor::chordLengthMenuId() const {
    const int extended = int(param("extendedChordLength"));
    constexpr int extendedIds[]{4, 6, 8, 9};
    if (extended >= 1 && extended <= 4)
        return extendedIds[extended - 1];
    constexpr int ids[]{1, 2, 3, 5, 7};
    return ids[juce::jlimit(0, 4, int(param("chordLength")))];
}
void Processor::setChordLengthMenu(int id) {
    switch (id) {
    case 1: setParameter("extendedChordLength", 0); setParameter("chordLength", 0); break;
    case 2: setParameter("extendedChordLength", 0); setParameter("chordLength", 1); break;
    case 3: setParameter("extendedChordLength", 0); setParameter("chordLength", 2); break;
    case 4: setParameter("extendedChordLength", 1); break;
    case 5: setParameter("extendedChordLength", 0); setParameter("chordLength", 3); break;
    case 6: setParameter("extendedChordLength", 2); break;
    case 7: setParameter("extendedChordLength", 0); setParameter("chordLength", 4); break;
    case 8: setParameter("extendedChordLength", 3); break;
    case 9: setParameter("extendedChordLength", 4); break;
    default: break;
    }
}
void Processor::setProgressionPreset(int index) {
    index = juce::jlimit(0, int(composer::progressionPresets().size()) - 1, index);
    if (customHarmonyEnabled.load())
        clearCustomHarmony();
    if (index < 5) {
        setParameter("progression", float(index));
        setParameter("extendedProgression", 0);
    } else
        setParameter("extendedProgression", float(index - 4));
}
composer::SearchBudget Processor::configuredSearchBudget() const {
    composer::Settings s;
    s.quality = int(param("quality"));
    if (param("customSearch") > 0.5f) {
        s.population = int(param("searchPopulation"));
        s.generations = int(param("searchGenerations"));
    }
    return composer::searchBudget(s);
}
juce::String Processor::searchDescription() const {
    std::lock_guard<std::mutex> lock(searchMutex);
    if (lastSearch.population == 0)
        return "Tournament selection / one elite per island / crossover / adaptive mutation. Fitness is an "
               "internal score.";
    return "Generation " + juce::String(lastSearch.generation) + "/" + juce::String(lastSearch.generations) +
           "  |  Population " + juce::String(lastSearch.population) + "  |  Evaluations " +
           juce::String(lastSearch.evaluations) + "  |  Crossovers " + juce::String(lastSearch.crossovers) +
           "  |  Microtonal candidates " + juce::String(lastSearch.microtonalEvaluations) +
           "\nBest fitness " + juce::String(lastSearch.bestFitness, 3) + " (initial " +
           juce::String(lastSearch.initialBest, 3) + ")  |  Mean " + juce::String(lastSearch.meanFitness, 3) +
           "  |  " + juce::String(searchSeconds, 3) + " s";
}
composer::Settings Processor::settings() const {
    composer::Settings s;
    s.root = int(param("root"));
    s.scale = scaleIndex();
    s.bars = phraseBarCount();
    s.division = 8 << int(param("grid"));
    const auto meter = timeSignature();
    s.numerator = meter.numerator;
    s.denominator = meter.denominator;
    s.low = int(param("low"));
    s.high = int(param("high"));
    s.mode = int(param("mode"));
    s.progression = progressionIndex();
    s.quality = int(param("quality"));
    s.density = param("density");
    s.diversity = param("diversity");
    s.syncopation = param("syncopation");
    s.tension = param("tension");
    s.space = param("space");
    s.mutation = param("mutation");
    s.lockRhythm = param("lockRhythm") > 0.5f;
    s.harmony = harmonySnapshot();
    s.tuning = tuningIndex();
    s.microEnabled = microtonesEnabled();
    s.phraseType = int(param("phraseType"));
    s.dynamics = param("dynamics");
    s.valence = param("valence");
    s.arousal = param("arousal");
    s.weirdness = param("weirdness");
    const auto budget = configuredSearchBudget();
    s.population = budget.population;
    s.generations = budget.generations;
    s.crossover = param("crossover");
    {
        std::lock_guard<std::mutex> lock(documentMutex);
        s.groups = groups;
        s.groupCount = groupCount;
        s.groupUnit = groupUnit > 0 ? groupUnit : s.denominator;
    }
    return s;
}
juce::String Processor::searchSummary() const {
    std::lock_guard<std::mutex> lock(searchMutex);
    if (!lastSearch.population)
        return {};
    return juce::String(lastSearch.generation) + "/" + juce::String(lastSearch.generations) + " gen  |  " +
           juce::String(lastSearch.evaluations) + " eval  |  " + juce::String(searchSeconds, 2) + " s";
}
void Processor::generate(bool evolve, bool overHarmony) {
    beginSearch(evolve, overHarmony, false);
}
void Processor::evolveRegion(double start, double end) {
    beginSearch(true, false, true, start, end);
}
void Processor::beginSearch(bool evolve, bool overHarmony, bool regionOnly, double start, double end) {
    if (hasDraft()) {
        setStatus("Apply or discard the preview before generating.");
        return;
    }
    if (busy.exchange(true))
        return;
    cancelled.store(false);
    progress.store(0);
    auto s = settings();
    if (overHarmony) {
        const auto chords = chordClipSnapshot();
        if (s.harmony.count == 0) {
            setStatus("Create or enter a chord sequence first.");
            busy.store(false);
            return;
        }
        s.lengthOverride = s.harmony.length;
        s.numerator = chords.numerator;
        s.denominator = chords.denominator;
        s.mode = 0;
        setParameter("mode", 0);
        setTimeSignature(juce::String(s.numerator) + "/" + juce::String(s.denominator));
        const double bars = s.harmony.length / (s.numerator * 4.0 / s.denominator);
        for (int count : {1, 2, 3, 4, 6, 8, 12, 16})
            if (std::abs(bars - count) < 1e-8)
                setPhraseBarCount(count);
    }
    s.seed = uint32_t(seedGenerator.nextInt());
    composer::Clip parent;
    uint64_t startRevision;
    int parentSlot;
    {
        std::lock_guard<std::mutex> lock(documentMutex);
        parentSlot = selected.load();
        parent = bank[parentSlot];
        startRevision = revision.load();
    }
    if (regionOnly) {
        if (!std::isfinite(start) || !std::isfinite(end) || start < 0 || end > parent.length || end <= start) {
            setStatus("Select a valid time range on the bar ruler first.");
            busy.store(false);
            return;
        }
        if (std::none_of(parent.notes.begin(), parent.notes.begin() + parent.count,
                         [=](const composer::Note& n) { return !n.locked && n.start >= start && n.start < end; })) {
            setStatus("The selected range has no unlocked notes to evolve.");
            busy.store(false);
            return;
        }
        s.regionOnly = true;
        s.regionStart = start;
        s.regionEnd = end;
    }
    const bool useParent = evolve || s.mode == 2;
    if ((!useParent || parent.count == 0) &&
        (s.lengthOverride > 0 ? s.lengthOverride : s.bars * s.numerator * 4.0 / s.denominator) > 128.0) {
        setStatus("This meter and bar count exceed 128 quarter-note beats. Choose fewer bars.");
        busy.store(false);
        return;
    }
    {
        std::lock_guard<std::mutex> lock(searchMutex);
        lastSearch = {};
        lastSearch.population = s.population;
        lastSearch.generations = s.generations;
        searchSeconds = 0;
        searchSerialValue.fetch_add(1, std::memory_order_relaxed);
    }
    setStatus(regionOnly ? "Optimising the selected range..."
                        : evolve ? "Evolving the selected phrase..." : "Searching for new phrases...");
    SearchJob job;
    job.settings = s;
    job.parent = parent;
    job.startRevision = startRevision;
    job.useParent = useParent;
    job.evolve = evolve;
    job.regionSlot = regionOnly ? parentSlot : -1;
    job.selectBest = selectBestOnFinish;
    selectBestOnFinish = false;
    if (evolve)
        for (const auto& clip : bankSnapshot())
            if (clip.favourite && clip.count > 0)
                job.mates.push_back(clip);
    searchThread->submit(std::move(job));
}
void Processor::performSearch(SearchJob job) {
    const auto& s = job.settings;
    try {
        const auto startTime = juce::Time::getMillisecondCounterHiRes();
        auto results = composer::generate(
            s, job.useParent ? &job.parent : nullptr, cancelled, [this](float value) { progress.store(value); },
            [this, startTime](const composer::SearchStats& stats) {
                std::lock_guard<std::mutex> lock(searchMutex);
                lastSearch = stats;
                searchSeconds = (juce::Time::getMillisecondCounterHiRes() - startTime) / 1000.0;
                searchSerialValue.fetch_add(1, std::memory_order_relaxed);
            },
            job.evolve && !job.mates.empty() ? &job.mates : nullptr);
        std::lock_guard<std::mutex> lock(documentMutex);
        if (!results.empty() && !cancelled.load() && revision.load() == job.startRevision) {
            pushHistoryLocked();
            if (job.regionSlot >= 0) {
                bank[size_t(job.regionSlot)] = results.front();
                publishLocked();
                assignStatusLocked("Range optimised. The remaining notes and other variations are unchanged.");
                busy.store(false);
                searchSerialValue.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            int next = 0, bestSlot = -1;
            for (int index = 0; index < composer::slotCount; ++index) {
                if (size_t(next) >= results.size())
                    break;
                if (!bank[size_t(index)].favourite) {
                    if (next == 0)
                        bestSlot = index;
                    bank[size_t(index)] = results[size_t(next++)];
                }
            }
            if (job.selectBest && bestSlot >= 0)
                selected.store(bestSlot);
            publishLocked();
            assignStatusLocked(next > 0 ? "Ready: " + juce::String(s.generations) + " generations x " +
                                              juce::String(s.population) +
                                              " individuals. Audition a phrase; drag its piano roll to your DAW."
                                        : "All variations are pinned. Unpin a slot to generate into it.");
        } else
            assignStatusLocked("Generation cancelled. Your previous phrases are safe.");
    } catch (const std::exception& error) {
        setStatus("Generation failed: " + juce::String(error.what()));
    }
    busy.store(false);
    searchSerialValue.fetch_add(1, std::memory_order_relaxed);
}
void Processor::setParameter(const char* id, float value) {
    auto* p = parameters.getParameter(id);
    p->beginChangeGesture();
    p->setValueNotifyingHost(p->convertTo0to1(value));
    p->endChangeGesture();
}
void Processor::applyPreset(int i) {
    struct Preset {
        float density, diversity, syncopation, tension, space, mutation;
        float dynamics, valence, arousal, weirdness;
        float phraseType, low, high;
    };
    // The first six columns keep the original starting points. The 2.5 controls and phrase type
    // now move with the same names: airy stays open, rhythmic raises energy, chromatic raises
    // tension, and bass stays in a low register.
    constexpr Preset presets[]{
        {0.5f, 0.5f, 0.25f, 0.15f, 0.15f, 0.35f, 0.5f, 0.5f, 0.5f, 0.1f, 0, 48, 84},
        {0.25f, 0.25f, 0.1f, 0.05f, 0.35f, 0.25f, 0.35f, 0.65f, 0.25f, 0.05f, 2, 48, 84},
        {0.8f, 0.65f, 0.55f, 0.15f, 0.08f, 0.45f, 0.55f, 0.45f, 0.85f, 0.25f, 0, 48, 84},
        {0.6f, 0.8f, 0.7f, 0.75f, 0.2f, 0.6f, 0.7f, 0.35f, 0.6f, 0.75f, 1, 48, 84},
        {0.35f, 0.3f, 0.4f, 0.1f, 0.3f, 0.3f, 0.3f, 0.4f, 0.35f, 0.15f, 0, 36, 60}};
    const char* ids[]{"density", "diversity", "syncopation", "tension",   "space",     "mutation",
                      "dynamics", "valence",  "arousal",     "weirdness", "phraseType"};
    i = juce::jlimit(0, 4, i);
    const auto& preset = presets[i];
    const float values[]{preset.density, preset.diversity, preset.syncopation, preset.tension,
                         preset.space,   preset.mutation,  preset.dynamics,    preset.valence,
                         preset.arousal, preset.weirdness, preset.phraseType};
    for (int j = 0; j < 11; ++j)
        setParameter(ids[j], values[j]);
    setParameter("low", preset.low);
    setParameter("high", preset.high);
    setStatus("Starting point loaded. Generate to create a new bank.");
}
composer::Harmony Processor::harmonySnapshot() const {
    std::lock_guard<std::mutex> lock(documentMutex);
    return customHarmony;
}
juce::String Processor::customHarmonyText() const {
    return juce::String(composer::harmonyText(harmonySnapshot()));
}
juce::Result Processor::setCustomHarmony(const juce::String& text) {
    composer::Harmony parsed;
    std::string error;
    if (!composer::parseHarmony(text.toStdString(), parsed, error))
        return juce::Result::fail(juce::String(error));
    const auto meter = timeSignature();
    const auto voiced = composer::voiceHarmony(parsed, meter.numerator, meter.denominator, voicingSettings());
    cancelled.store(true);
    std::lock_guard<std::mutex> lock(documentMutex);
    pushHistoryLocked();
    customHarmony = parsed;
    chordClip = voiced;
    if (voiced.count == 0 && chordPreview.exchange(false))
        preview.store(false);
    customHarmonyEnabled.store(parsed.count > 0);
    publishLocked();
    assignStatusLocked("Custom chords set. Adapt the existing melody or generate a new one.");
    return juce::Result::ok();
}
void Processor::clearCustomHarmony() {
    cancelled.store(true);
    std::lock_guard<std::mutex> lock(documentMutex);
    if (customHarmony.count == 0)
        return;
    pushHistoryLocked();
    customHarmony = {};
    chordClip = {};
    if (chordPreview.exchange(false))
        preview.store(false);
    customHarmonyEnabled.store(false);
    publishLocked();
    assignStatusLocked("Custom harmony cleared. The scale and progression controls apply again.");
}
juce::Result Processor::importHarmonyMidi(const juce::File& file) {
    composer::Clip reference;
    bool truncated = false;
    composer::Harmony marked;
    if (auto result = readMidi(file, reference, truncated, &marked); result.failed())
        return result;
    auto inferred = marked.count > 0 ? marked : composer::inferHarmony(reference);
    if (inferred.count == 0)
        return juce::Result::fail(
            "No chords could be recognised. Import block chords or enter them manually.");
    const auto voiced =
        composer::voiceHarmony(inferred, reference.numerator, reference.denominator, voicingSettings());
    cancelled.store(true);
    std::lock_guard<std::mutex> lock(documentMutex);
    pushHistoryLocked();
    customHarmony = inferred;
    chordClip = voiced;
    customHarmonyEnabled.store(true);
    publishLocked();
    assignStatusLocked(truncated ? "Harmony imported from a truncated reference. Check the chord names and lengths."
                                 : "Chords estimated from MIDI. Check and edit the chord names before generating.");
    return juce::Result::ok();
}
bool Processor::hasGrooveReference() const {
    std::lock_guard<std::mutex> lock(documentMutex);
    return grooveLoaded;
}
juce::Result Processor::importGrooveMidi(const juce::File& file) {
    composer::Clip reference;
    bool truncated = false;
    if (auto result = readMidi(file, reference, truncated); result.failed())
        return result;
    cancelled.store(true);
    std::lock_guard<std::mutex> lock(documentMutex);
    pushHistoryLocked();
    grooveReference = reference;
    grooveLoaded = true;
    if (reference.groupCount > 0) {
        groups = reference.groups;
        groupCount = reference.groupCount;
        groupUnit = reference.groupUnit;
    }
    publishLocked();
    assignStatusLocked(truncated ? "Groove imported; reference limited to 512 notes and 128 beats."
                                 : "Groove reference loaded. Adjust Timing and Velocity, then apply.");
    return juce::Result::ok();
}
void Processor::useSelectedAsGroove() {
    cancelled.store(true);
    std::lock_guard<std::mutex> lock(documentMutex);
    const auto& source = draftActive ? draft : bank[selected.load()];
    if (source.count == 0) {
        assignStatusLocked("Choose a non-empty phrase as the groove reference.");
        return;
    }
    pushHistoryLocked();
    grooveReference = source;
    grooveLoaded = true;
    publishLocked();
    assignStatusLocked("Groove captured. Select a target phrase, then apply timing and velocity.");
}
juce::Result Processor::applyGroove(float timing, float velocity) {
    cancelled.store(true);
    std::lock_guard<std::mutex> lock(documentMutex);
    if (draftActive)
        return juce::Result::fail("Apply or discard the preview first.");
    if (!grooveLoaded)
        return juce::Result::fail("Import a groove or capture a reference phrase first.");
    if (!std::isfinite(timing) || !std::isfinite(velocity))
        return juce::Result::fail("Groove amounts must be finite.");
    auto transformed = composer::applyGroove(bank[selected.load()], grooveReference, timing, velocity);
    pushHistoryLocked();
    bank[selected.load()] = transformed;
    compareRequested.store(false);
    publishLocked();
    assignStatusLocked("Groove applied. Locked notes are preserved; Undo restores the previous performance.");
    return juce::Result::ok();
}
juce::Result Processor::applyRegion(double start, double end, composer::RegionAction action) {
    auto s = settings();
    s.seed = uint32_t(seedGenerator.nextInt());
    cancelled.store(true);
    std::lock_guard<std::mutex> lock(documentMutex);
    if (draftActive)
        return juce::Result::fail("Apply or discard the preview first.");
    const auto& source = bank[selected.load()];
    if (!std::isfinite(start) || !std::isfinite(end) || start < 0 || end <= start ||
        end > source.length + 1e-6)
        return juce::Result::fail("Select a time range inside the phrase using the piano-roll ruler.");
    auto transformed = composer::transformRegion(source, start, end, action, s);
    pushHistoryLocked();
    bank[selected.load()] = transformed;
    compareRequested.store(false);
    publishLocked();
    assignStatusLocked("Selected region edited. Notes outside the range and locked notes are preserved.");
    return juce::Result::ok();
}

bool Processor::hasDraft() const {
    std::lock_guard<std::mutex> lock(documentMutex);
    return draftActive;
}
juce::String Processor::draftDescription() const {
    std::lock_guard<std::mutex> lock(documentMutex);
    return draftActive ? draftLabel : juce::String();
}
juce::Result Processor::previewCombination(int rhythm, int pitches, int accents) {
    if (rhythm < 0 || rhythm >= composer::slotCount || pitches < 0 || pitches >= composer::slotCount ||
        accents < 0 || accents >= composer::slotCount)
        return juce::Result::fail("Select valid source variations.");
    cancelled.store(true);
    std::lock_guard<std::mutex> lock(documentMutex);
    if (bank[rhythm].count == 0 || bank[pitches].count == 0 || bank[accents].count == 0)
        return juce::Result::fail("All three source variations must contain notes.");
    draft = composer::combineParts(bank[rhythm], bank[pitches], bank[accents]);
    draft.favourite = bank[selected.load()].favourite;
    draftSlot = selected.load();
    draftActive = true;
    draftLabel = "Combined phrase: rhythm " + juce::String(rhythm + 1) + ", pitches " +
                 juce::String(pitches + 1) + ", accents " + juce::String(accents + 1);
    compareRequested.store(false);
    publishLocked();
    assignStatusLocked("Preview ready. Play to audition, then Apply or Discard. The saved bank is unchanged.");
    return juce::Result::ok();
}
juce::Result Processor::previewArrangement() {
    auto s = settings();
    s.seed = uint32_t(seedGenerator.nextInt());
    cancelled.store(true);
    std::lock_guard<std::mutex> lock(documentMutex);
    const auto source = bank[selected.load()];
    if (source.count == 0)
        return juce::Result::fail("Choose a non-empty motif first.");
    if (source.length * 4 > 128.0 || source.count > composer::maxNotes / 4)
        return juce::Result::fail(
            "Use a motif of at most 32 beats and 128 notes for a four-part arrangement.");
    std::array<composer::Clip, 4> sections{source, source, source, source};
    const double bar = source.numerator * 4.0 / source.denominator;
    const int form = juce::jlimit(0, 3, arrangementForm);
    const auto lastBar = std::max(0.0, source.length - bar);
    if (form == 1) {
        s.timeOffset = source.length * 3;
        sections[3] = composer::transformRegion(source, lastBar, source.length,
                                                composer::RegionAction::MoreMovement, s);
    } else if (form == 2) {
        s.timeOffset = source.length;
        sections[1] = composer::transformRegion(source, 0, source.length, composer::RegionAction::Answer, s);
        s.seed += 1;
        s.timeOffset = source.length * 3;
        sections[3] =
            composer::transformRegion(source, lastBar, source.length, composer::RegionAction::Resolve, s);
    } else if (form == 3) {
        s.timeOffset = source.length;
        sections[1] = composer::transformRegion(source, 0, source.length, composer::RegionAction::Answer, s);
        s.seed += 1;
        s.timeOffset = source.length * 2;
        sections[2] =
            composer::transformRegion(source, 0, source.length, composer::RegionAction::MoreMovement, s);
        s.seed += 1;
        s.timeOffset = source.length * 3;
        sections[3] = composer::transformRegion(source, 0, source.length, composer::RegionAction::Resolve, s);
    } else {
        // A stays literal; A' develops the ending, B answers the whole motif, A'' resolves it.
        s.timeOffset = source.length;
        sections[1] = composer::transformRegion(source, lastBar, source.length,
                                                composer::RegionAction::MoreMovement, s);
        s.seed += 1;
        s.timeOffset = source.length * 2;
        sections[2] = composer::transformRegion(source, 0, source.length, composer::RegionAction::Answer, s);
        s.seed += 1;
        s.timeOffset = source.length * 3;
        sections[3] = composer::transformRegion(source, lastBar, source.length, composer::RegionAction::Resolve, s);
    }
    composer::Clip result = source;
    result.count = 0;
    result.length = source.length * 4;
    for (int part = 0; part < 4; ++part)
        for (int i = 0; i < sections[part].count; ++i) {
            if (result.count >= composer::maxNotes)
                return juce::Result::fail(
                    "This arrangement exceeds 512 notes. Simplify the source motif first.");
            auto n = sections[part].notes[i];
            n.start += part * source.length;
            result.notes[result.count++] = n;
        }
    composer::sanitise(result);
    draft = result;
    draftSlot = selected.load();
    draftActive = true;
    const char* formName = form == 1   ? "Arrangement AAAB"
                           : form == 2 ? "Arrangement ABAC"
                           : form == 3 ? "Arrangement Call / Response"
                                       : "Arrangement A / A' / B / A''";
    draftLabel = juce::String(formName) + "  |  " + juce::String(result.length / bar, 1) + " bars";
    compareRequested.store(false);
    publishLocked();
    assignStatusLocked(form == 1   ? "AAAB preview ready. The last bar develops. Keep or Discard."
                       : form == 2 ? "ABAC preview ready. The answer and the ending are drafts. Keep or Discard."
                       : form == 3 ? "Call and response preview ready. Keep or Discard."
                                   : "Arrangement preview ready. A stays intact, A' develops, B answers, A'' "
                                     "resolves. Apply or Discard.");
    return juce::Result::ok();
}
void Processor::applyDraft() {
    cancelled.store(true);
    std::lock_guard<std::mutex> lock(documentMutex);
    if (!draftActive)
        return;
    pushHistoryLocked();
    bank[draftSlot] = draft;
    selected.store(draftSlot);
    draftActive = false;
    publishLocked();
    assignStatusLocked("Preview applied to the selected variation. Undo restores the original.");
}
void Processor::discardDraft() {
    cancelled.store(true);
    std::lock_guard<std::mutex> lock(documentMutex);
    if (!draftActive)
        return;
    draftActive = false;
    publishLocked();
    assignStatusLocked("Preview discarded. The bank is unchanged.");
}
void Processor::captureComparison() {
    std::lock_guard<std::mutex> lock(documentMutex);
    comparison = draftActive && draftSlot == selected.load() ? draft : bank[selected.load()];
    comparisonAvailable.store(true);
    compareRequested.store(false);
    publishLocked();
    assignStatusLocked("A captured. Edit or choose a variation for B; switch A/B to compare.");
}
composer::Clip Processor::comparisonClip() const {
    std::lock_guard<std::mutex> lock(documentMutex);
    return comparison;
}
void Processor::toggleComparison() {
    if (!comparisonAvailable.load()) {
        setStatus("Capture A before comparing.");
        return;
    }
    chordPreview.store(false);
    compareRequested.store(!compareRequested.load());
    setStatus(param("quantiseSwitch") > 0.5f && playing.load() ? "A/B change queued for the next loop."
                                                               : "A/B audition changed.");
}
void Processor::clearComparison() {
    compareRequested.store(false);
    comparisonAvailable.store(false);
    std::lock_guard<std::mutex> lock(documentMutex);
    comparison = {};
    publishLocked();
}
juce::AudioProcessorEditor* Processor::createEditor() {
    return new GeneticVSTComposerJUCEAudioProcessorEditor(*this);
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new Processor();
}
