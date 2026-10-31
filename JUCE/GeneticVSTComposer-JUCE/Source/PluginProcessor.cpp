#include "PluginProcessor.h"
#include "InterfaceText.h"
#include "PluginEditor.h"
#include <cmath>
#include <condition_variable>
#include <tuple>
#include <utility>

using Processor = GeneticVSTComposerJUCEAudioProcessor;
namespace {
double swingTime(double time, double grid, double amount) {
    const double pair = std::floor(time / (2.0 * grid)) * (2.0 * grid);
    const double offset = time - pair;
    return pair + (offset <= grid ? offset * (1.0 + amount)
                                  : grid * (1.0 + amount) + (offset - grid) * (1.0 - amount));
}
bool samePlayback(const composer::Clip& a, const composer::Clip& b) {
    if (a.length != b.length || a.grid != b.grid || a.count != b.count)
        return false;
    for (int i = 0; i < a.count; ++i) {
        const auto& x = a.notes[i];
        const auto& y = b.notes[i];
        if (x.start != y.start || x.duration != y.duration || x.pitch != y.pitch ||
            x.velocity != y.velocity || x.cents != y.cents)
            return false;
    }
    return true;
}
constexpr int mpeBendRange = 48;
// 14-bit pitch-wheel position for an offset in cents at a bend range in semitones.
int bendPosition(int cents, int range) {
    return juce::jlimit(0, 16383, 8192 + int(std::lround(cents * 8192.0 / (range * 100.0))));
}
template <typename Add> void addRpn(Add&& add, int channel, int number, int value) {
    add(juce::MidiMessage::controllerEvent(channel, 101, number >> 7));
    add(juce::MidiMessage::controllerEvent(channel, 100, number & 127));
    add(juce::MidiMessage::controllerEvent(channel, 6, value));
}
// Sent before microtonal notes: channel 1's bend range, or for MPE a lower zone of 15 member
// channels at the standard per-note range, with ±2 on the master channel.
template <typename Add> void microtonalSetup(int output, int range, Add&& add) {
    if (output == 1)
        addRpn(add, 1, 0, range);
    if (output == 2) {
        addRpn(add, 1, 6, 15);
        addRpn(add, 2, 0, mpeBendRange);
        addRpn(add, 1, 0, 2);
    }
}
} // namespace

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
    return p;
}
GeneticVSTComposerJUCEAudioProcessor::GeneticVSTComposerJUCEAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "Parameters", createParameters()), bank(composer::makeInitialBank()),
      pendingBank(bank), audioBank(bank) {
    cacheAudioParameters();
    clearVisualState();
    searchThread = std::make_unique<SearchThread>(*this);
}
GeneticVSTComposerJUCEAudioProcessor::~GeneticVSTComposerJUCEAudioProcessor() {
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
    assignStatusLocked("New chords are active. Generate or evolve a melody to follow them; existing variations are "
                       "retained.");
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
    const auto parent = selectedClip();
    const auto startRevision = revision.load();
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
    setStatus(evolve ? "Evolving the selected phrase..." : "Searching for new phrases...");
    SearchJob job;
    job.settings = s;
    job.parent = parent;
    job.startRevision = startRevision;
    job.useParent = useParent;
    job.evolve = evolve;
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
            int next = 0;
            for (auto& clip : bank)
                if (!clip.favourite)
                    clip = results[size_t(next++)];
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
    assignStatusLocked("Custom chords set. Generate or evolve to use them; playback scale snap is bypassed.");
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
    if (auto result = readMidi(file, reference, truncated); result.failed())
        return result;
    auto inferred = composer::inferHarmony(reference);
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
    // A stays literal; A' develops the ending, B answers the whole motif, A'' resolves it.
    s.timeOffset = source.length;
    sections[1] = composer::transformRegion(source, std::max(0.0, source.length - bar), source.length,
                                            composer::RegionAction::MoreMovement, s);
    s.seed += 1;
    s.timeOffset = source.length * 2;
    sections[2] = composer::transformRegion(source, 0, source.length, composer::RegionAction::Answer, s);
    s.seed += 1;
    s.timeOffset = source.length * 3;
    sections[3] = composer::transformRegion(source, std::max(0.0, source.length - bar), source.length,
                                            composer::RegionAction::Resolve, s);
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
    draftLabel = "Arrangement A / A' / B / A''  |  " + juce::String(result.length / bar, 1) + " bars";
    compareRequested.store(false);
    publishLocked();
    assignStatusLocked(
        "Arrangement preview ready. A stays intact, A' develops, B answers, A'' resolves. Apply or Discard.");
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
composer::Note Processor::renderedNote(composer::Note n, const composer::Clip& clip, int transposition,
                                       int index) const {
    const double swing = cached.swing->load();
    n.start = swingTime(n.start, clip.grid, swing);
    const float humanize = cached.humanize->load();
    if (humanize > 0.0001f && index >= 0) {
        uint32_t hash = clip.seed * 2246822519u ^ uint32_t(index) * 3266489917u ^ uint32_t(n.pitch) * 668265263u;
        auto unit = [&] {
            hash = hash * 1664525u + 1013904223u;
            return (hash >> 8) / 16777216.0;
        };
        const double shift = (unit() * 2.0 - 1.0) * humanize * clip.grid * 0.45;
        const double previous = index > 0 ? swingTime(clip.notes[index - 1].start, clip.grid, swing) + 0.001 : 0.0;
        const double following = index + 1 < clip.count
                                     ? swingTime(clip.notes[index + 1].start, clip.grid, swing) - 0.001
                                     : clip.length - 0.001;
        n.start = following > previous ? std::clamp(n.start + shift, previous, following) : n.start;
        n.velocity = std::clamp(n.velocity + int(std::lround((unit() * 2.0 - 1.0) * humanize * 24.0)), 1, 127);
    }
    n.start = std::min(n.start, clip.length - 0.001);
    n.duration = std::max(0.001, std::min(n.duration * cached.gate->load(), clip.length - n.start));
    n.pitch = juce::jlimit(0, 127, n.pitch + transposition);
    // With the tuning off, stored offsets are kept but the output is plain 12-TET.
    const int tuning = tuningIndex();
    if (tuning == 0)
        n.cents = 0;
    // Fit to scale is an explicit performance projection, including live transposition.
    // It never invents offsets after the search; optimized passing-note cents are retained.
    if (cached.snap->load() > 0.5f)
        n.pitch = composer::snapPitch(n.pitch, int(cached.root->load()), scaleIndex());

    return n;
}
void Processor::releaseVoice(int pitch, int sample, juce::MidiBuffer& midi) {
    auto& v = voices[size_t(pitch)];
    midi.addEvent(juce::MidiMessage::noteOff(v.channel, pitch), sample);
    channelNotes[size_t(v.channel)] = std::max(0, channelNotes[size_t(v.channel)] - 1);
    channelUse[size_t(v.channel)] = ++channelClock;
    v.on = false;
    v.sourceIndex = -1;
    v.target = 0;
}
void Processor::allOff(int sample, juce::MidiBuffer& midi) {
    for (int p = 0; p < 128; ++p)
        if (voices[p].on)
            releaseVoice(p, sample, midi);
}
// Centres the bends the current output used; leaving MPE also clears its zone, so a receiver
// returns to ordinary channel-1 playback.
void Processor::resetOutput(int sample, juce::MidiBuffer& midi) {
    if (outputMode == 1)
        midi.addEvent(juce::MidiMessage::pitchWheel(1, 8192), sample);
    if (outputMode == 2) {
        for (int channel = 2; channel <= 16; ++channel)
            midi.addEvent(juce::MidiMessage::pitchWheel(channel, 8192), sample);
        addRpn([&](const juce::MidiMessage& m) { midi.addEvent(m, sample); }, 1, 6, 0);
    }
}
void Processor::processBlockBypassed(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi) {
    audio.clear();
    if (audio.getNumSamples() > 0) {
        allOff(0, midi);
        stopChordLayer(0, midi);
    } else
        chordMidiSounding = {};
    voices = {};
    chordVoices = {};
    chordLayerPlaying = false;
    channelNotes = {};
    outputConfigured = false;
    heldTriggers = {};
    heldTranspose = {};
    trigger = -1;
    transpose = 0;
    wasPlaying = false;
    wasHostPlaying = false;
    preview.store(false);
    chordPreview.store(false);
    heardChordPreview.store(false);
    audioChordPreview = false;
    playing.store(false);
    playBeat.store(-1);
    clearVisualState();
}
void Processor::noteOn(const composer::Note& n, int sourceIndex, int sample, juce::MidiBuffer& midi) {
    auto& v = voices[n.pitch];
    if (v.on)
        releaseVoice(n.pitch, sample, midi);
    int channel = 1;
    if (outputMode == 2) {
        // An idle member channel with the oldest release tail; shared only when all are busy.
        channel = 2;
        for (int candidate = 3; candidate <= 16; ++candidate)
            if (std::make_pair(channelNotes[size_t(candidate)] > 0, channelUse[size_t(candidate)]) <
                std::make_pair(channelNotes[size_t(channel)] > 0, channelUse[size_t(channel)]))
                channel = candidate;
    }
    ++channelNotes[size_t(channel)];
    channelUse[size_t(channel)] = ++channelClock;
    if (outputMode != 0)
        midi.addEvent(juce::MidiMessage::pitchWheel(
                          channel, bendPosition(n.cents, outputMode == 2 ? mpeBendRange : outputBendRange)),
                      sample);
    v.on = true;
    v.channel = channel;
    v.sourceIndex = sourceIndex;
    v.remaining = n.duration;
    v.target = n.velocity / 127.0;
    v.increment = phaseIncrements[size_t(n.pitch)] * std::exp2(n.cents / 1200.0);
    midi.addEvent(juce::MidiMessage::noteOn(channel, n.pitch, juce::uint8(n.velocity)), sample);
    ++audioNoteOnSerial;
}
void Processor::releaseChordMidi(int sample, juce::MidiBuffer& midi) noexcept {
    for (int pitch = 0; pitch < 128; ++pitch)
        if (chordMidiSounding[size_t(pitch)]) {
            midi.addEvent(juce::MidiMessage::noteOff(16, pitch), sample);
            chordMidiSounding[size_t(pitch)] = false;
        }
}
void Processor::stopChordLayer(int sample, juce::MidiBuffer& midi) noexcept {
    for (auto& voice : chordVoices) {
        voice.on = false;
        voice.target = 0;
        voice.sourceIndex = -1;
    }
    releaseChordMidi(sample, midi);
    chordLayerPlaying = false;
    lastChordPhase = -1;
}
void Processor::scheduleChordLayer(bool run, bool restart, double beat, double step, int sample,
                                   juce::MidiBuffer& midi) noexcept {
    const bool sendChordMidi = cached.chordMidiOut->load() > 0.5f && outputMode != 2;
    if (!sendChordMidi)
        releaseChordMidi(sample, midi);
    if (!run || audioChords.count == 0) {
        if (chordLayerPlaying)
            stopChordLayer(sample, midi);
        return;
    }
    const double position = std::fmod(std::max(0.0, beat), audioChords.length);
    const bool chase = restart || !chordLayerPlaying || position + step * 0.5 < lastChordPhase;
    if (chase) {
        stopChordLayer(sample, midi);
        nextChordNote = 0;
    }
    chordLayerPlaying = true;
    for (int pitch = 0; pitch < 128; ++pitch) {
        auto& voice = chordVoices[size_t(pitch)];
        if (voice.on && voice.remaining <= 1e-9) {
            voice.on = false;
            voice.target = 0;
            voice.sourceIndex = -1;
            if (chordMidiSounding[size_t(pitch)]) {
                midi.addEvent(juce::MidiMessage::noteOff(16, pitch), sample);
                chordMidiSounding[size_t(pitch)] = false;
            }
        }
    }
    while (nextChordNote < audioChords.count) {
        const auto& n = audioChords.notes[nextChordNote];
        if (n.start > position + step * 0.5)
            break;
        const double remaining = n.start + n.duration - position;
        if (remaining > step * 0.5 && (chase || n.start >= position - step * 1.5)) {
            auto& voice = chordVoices[size_t(n.pitch)];
            voice.on = true;
            voice.target = n.velocity / 127.0;
            voice.remaining = remaining;
            voice.increment = phaseIncrements[size_t(n.pitch)];
            voice.sourceIndex = nextChordNote;
            ++chordNoteOnSerial;
            if (sendChordMidi) {
                if (chordMidiSounding[size_t(n.pitch)])
                    midi.addEvent(juce::MidiMessage::noteOff(16, n.pitch), sample);
                midi.addEvent(juce::MidiMessage::noteOn(16, n.pitch, juce::uint8(n.velocity)), sample);
                chordMidiSounding[size_t(n.pitch)] = true;
            }
        }
        ++nextChordNote;
    }
    if (sendChordMidi)
        for (int pitch = 0; pitch < 128; ++pitch) {
            const auto& voice = chordVoices[size_t(pitch)];
            if (!voice.on || chordMidiSounding[size_t(pitch)])
                continue;
            const int velocity = voice.sourceIndex >= 0 && voice.sourceIndex < audioChords.count
                                     ? audioChords.notes[voice.sourceIndex].velocity
                                     : juce::jlimit(1, 127, int(std::lround(voice.target * 127.0)));
            midi.addEvent(juce::MidiMessage::noteOn(16, pitch, juce::uint8(velocity)), sample);
            chordMidiSounding[size_t(pitch)] = true;
        }
    lastChordPhase = position;
}
double Processor::renderVoice(Voice& v, bool monitor, int sound, double step) noexcept {
    if (v.on)
        v.remaining -= step;
    const double target = monitor ? v.target : 0.0;
    v.gain += (target - v.gain) * (target > v.gain ? 0.004 : 0.0015);
    if (v.gain <= 0.00005)
        return 0;
    v.phase += v.increment;
    v.phase -= std::floor(v.phase);
    const double angle = juce::MathConstants<double>::twoPi * v.phase;
    const double fundamental = std::sin(angle);
    const double tone = sound == 1 ? fundamental
                        : sound == 2
                            ? (fundamental + 0.28 * std::sin(angle * 2) + 0.12 * std::sin(angle * 3)) / 1.12
                            : fundamental + 0.22 * std::sin(angle * 2.0);
    return v.gain * tone * 0.1;
}
void Processor::processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    audio.clear();
    outputMidi.clear();
    const int samples = audio.getNumSamples();
    if (samples <= 0)
        return;
    bool bankChanged = false, chordsChanged = false;
    {
        juce::SpinLock::ScopedTryLockType lock(exchangeLock);
        if (lock.isLocked() && pendingRevision != audioRevision) {
            const int slot = juce::jlimit(0, 11, audioSelection);
            const bool nextHasDraft = pendingHasDraft && pendingDraftSlot == slot;
            const auto& nextClip = audioChordPreview ? pendingChords
                                   : audioCompare    ? pendingComparison
                                   : nextHasDraft    ? pendingDraft
                                                     : pendingBank[slot];
            const bool switchingLater = !audioChordPreview && wasPlaying &&
                                        cached.quantiseSwitch->load() > 0.5f &&
                                        selected.load() != audioSelection;
            bankChanged = audioSelection >= 0 && !switchingLater && !samePlayback(activeClip, nextClip);
            audioBank = pendingBank;
            audioComparison = pendingComparison;
            audioDraft = pendingDraft;
            chordsChanged = !samePlayback(audioChords, pendingChords);
            audioChords = pendingChords;
            audioHasDraft = pendingHasDraft;
            audioDraftSlot = pendingDraftSlot;
            audioRevision = pendingRevision;
        }
    }
    double bpm = cached.tempo->load(), ppq = 0.0;
    bool hostPlaying = false, hasPosition = false;
    if (auto* head = getPlayHead())
        if (auto position = head->getPosition()) {
            if (auto b = position->getBpm())
                if (std::isfinite(*b) && *b > 0)
                    bpm = *b;
            if (auto p = position->getPpqPosition()) {
                ppq = *p;
                hasPosition = std::isfinite(ppq);
            }
            hostPlaying = position->getIsPlaying();
        }
    bpm = juce::jlimit(20.0, 400.0, bpm);
    displayTempo.store(bpm);
    const double step = bpm / (60.0 * rate);
    const bool follow = cached.follow->load() > 0.5f;
    const bool hostJump =
        follow && hostPlaying && hasPosition && (!wasHostPlaying || std::abs(ppq - expectedPpq) > step * 2.0);
    // Chord audition is a local preview. Enabling Follow DAW returns to the melody.
    if (follow)
        chordPreview.store(false);
    const int melodyOutput = microtonalOutput(), bendRange = int(cached.bendRange->load());
    if (bendRange != outputBendRange) {
        outputBendRange = bendRange;
        outputConfigured = outputMode != 1 && outputConfigured;
    }
    const bool panicNow = panicRequested.exchange(false);
    if (panicNow) {
        allOff(0, outputMidi);
        stopChordLayer(0, outputMidi);
        for (int channel = 1; channel <= (outputMode == 2 ? 16 : 1); ++channel) {
            outputMidi.addEvent(juce::MidiMessage::controllerEvent(channel, 64, 0), 0);
            outputMidi.addEvent(juce::MidiMessage::allNotesOff(channel), 0);
        }
        heldTriggers = {};
        heldTranspose = {};
        trigger = -1;
        transpose = 0;
    }
    bool restart = bankChanged || hostJump || follow != lastFollow;
    auto incoming = midi.cbegin();
    const auto end = midi.cend();
    const bool monitor = cached.monitor->load() > 0.5f;
    const float volume = cached.volume->load();
    const int auditionSound = int(cached.auditionSound->load());
    const double swingAmount = cached.swing->load();
    const bool quantise = cached.quantiseSwitch->load() > 0.5f;
    const bool accompany = cached.auditionChords->load() > 0.5f;
    float blockPeak = 0;
    for (int sample = 0; sample < samples; ++sample) {
        bool restartLayer = sample == 0 && chordsChanged;
        while (incoming != end && (*incoming).samplePosition <= sample) {
            const auto message = (*incoming).getMessage();
            ++incoming;
            const int pitch = message.isNoteOnOrOff() ? message.getNoteNumber() : -1;
            if (pitch >= 48 && pitch < 60) {
                const int slot = pitch - 48;
                heldTriggers[slot] = message.isNoteOn();
                if (message.isNoteOn()) {
                    trigger = slot;
                    selected.store(slot);
                    compareRequested.store(false);
                    chordPreview.store(false);
                    restart = true;
                } else if (trigger == slot) {
                    trigger = -1;
                    for (int i = 11; i >= 0; --i)
                        if (heldTriggers[i]) {
                            trigger = i;
                            selected.store(i);
                            restart = true;
                            break;
                        }
                }
            } else if (pitch >= 60) {
                heldTranspose[pitch] = message.isNoteOn();
                if (message.isNoteOn())
                    transpose = pitch - 60;
                else {
                    transpose = 0;
                    for (int i = 127; i >= 60; --i)
                        if (heldTranspose[i]) {
                            transpose = i - 60;
                            break;
                        }
                }
            } else if (message.isAllNotesOff() || message.isAllSoundOff()) {
                allOff(sample, outputMidi);
                stopChordLayer(sample, outputMidi);
                trigger = -1;
                heldTriggers = {};
                heldTranspose = {};
                transpose = 0;
                preview.store(false);
                chordPreview.store(false);
                outputMidi.addEvent(message, sample);
            } else {
                outputMidi.addEvent(message, sample);
            }
        }
        const int slot = selected.load();
        const bool requestedA = compareRequested.load() && comparisonAvailable.load();
        const bool requestedDraft = audioHasDraft && audioDraftSlot == slot;
        const bool wantsChords = chordPreview.load() && !follow;
        const bool previewOn = preview.load();
        // A UI request may arrive after the bank exchange or while its lock is busy.
        // Wait for the first published chord clip rather than briefly playing the melody.
        const bool run = !panicNow && (!wantsChords || audioChords.count > 0) &&
                         (follow ? hostPlaying : (previewOn || trigger >= 0));
        const bool requestedChords = wantsChords && previewOn && audioChords.count > 0;
        const bool sourceChanged = requestedChords != audioChordPreview;
        const int requestedOutput = requestedChords ? 0 : melodyOutput;
        if (requestedOutput != outputMode) {
            // Sounding notes end on the channels on which they began, before resetting bends/MPE.
            allOff(sample, outputMidi);
            releaseChordMidi(sample, outputMidi);
            resetOutput(sample, outputMidi);
            outputMode = requestedOutput;
            outputConfigured = false;
        }
        const bool requestChanged =
            !requestedChords &&
            (slot != audioSelection || requestedA != audioCompare || requestedDraft != audioDraftActive);
        const bool boundarySwitch = loopBoundary && requestChanged;
        if (restart || sourceChanged || run != wasPlaying || audioSelection < 0 ||
            (requestChanged && (!quantise || !wasPlaying || loopBoundary))) {
            allOff(sample, outputMidi);
            restartLayer = true;
            audioSelection = slot;
            audioChordPreview = requestedChords;
            audioCompare = requestedA;
            audioDraftActive = requestedDraft;
            audibleSlot.store(slot);
            heardComparison.store(!audioChordPreview && requestedA);
            activeClip = audioChordPreview ? audioChords
                         : audioCompare    ? audioComparison
                         : requestedDraft  ? audioDraft
                                           : audioBank[slot];
            phase = boundarySwitch          ? 0.0
                    : follow && hasPosition ? std::fmod(std::max(0.0, ppq + sample * step), activeClip.length)
                                            : 0.0;
            accompanimentBeat = follow && hasPosition ? std::max(0.0, ppq + sample * step) : 0.0;
            nextNote = 0;
            while (nextNote < activeClip.count &&
                   (audioChordPreview
                        ? activeClip.notes[nextNote].start
                        : renderedNote(activeClip.notes[nextNote], activeClip, 0, nextNote).start) <
                          phase - step * 0.5)
                ++nextNote;
            restart = false;
        }
        loopBoundary = false;
        const auto& clip = activeClip;
        wasPlaying = run;
        heardChordPreview.store(run && audioChordPreview);
        for (int p = 0; p < 128; ++p)
            if (voices[p].on && voices[p].remaining <= 1.0e-9)
                releaseVoice(p, sample, outputMidi);
        // Each start of playback repeats the setup, in case the receiver was reset meanwhile.
        if (!run)
            outputConfigured = false;
        else if (outputMode != 0 && !outputConfigured) {
            microtonalSetup(outputMode, outputBendRange,
                            [&](const juce::MidiMessage& m) { outputMidi.addEvent(m, sample); });
            outputConfigured = true;
        }
        if (run) {
            while (nextNote < clip.count) {
                const auto& raw = clip.notes[nextNote];
                const double start =
                    audioChordPreview
                        ? raw.start
                        : std::min(clip.length - 0.001, swingTime(raw.start, clip.grid, swingAmount));
                if (start > phase + step * 0.5)
                    break;
                auto n = audioChordPreview ? raw : renderedNote(raw, clip, transpose, nextNote);
                if (n.start >= phase - step * 1.5)
                    noteOn(n, nextNote, sample, outputMidi);
                ++nextNote;
            }
        }
        scheduleChordLayer(run && monitor && accompany && !audioChordPreview, restartLayer, accompanimentBeat,
                           step, sample, outputMidi);
        double signal = 0.0;
        for (int p = 0; p < 128; ++p) {
            // A released voice below the tail threshold no longer changes the mix.
            if (voices[p].on || voices[p].gain > 0.00005)
                signal += renderVoice(voices[p], monitor, auditionSound, step);
            if (chordVoices[p].on || chordVoices[p].gain > 0.00005)
                signal += renderVoice(chordVoices[p], monitor, auditionSound, step) * 0.65;
        }
        const float value = float(std::tanh(signal * volume));
        for (int ch = 0; ch < audio.getNumChannels(); ++ch)
            audio.setSample(ch, sample, value);
        if (audio.getNumChannels() > 0)
            blockPeak = std::max(blockPeak, std::abs(value));
        if (run) {
            accompanimentBeat += step;
            phase += step;
            if (phase >= clip.length - step * 0.0001) {
                phase = std::max(0.0, phase - clip.length);
                nextNote = 0;
                loopBoundary = true;
            }
        }
    }
    heardChordLayer.store(chordLayerPlaying);
    chordBeat.store(wasPlaying && (audioChordPreview || chordLayerPlaying)
                        ? std::fmod(audioChordPreview ? phase : accompanimentBeat, audioChords.length)
                        : -1.0);
    playing.store(wasPlaying);
    playBeat.store(wasPlaying ? phase : -1.0);
    expectedPpq = ppq + samples * step;
    wasHostPlaying = hostPlaying;
    lastFollow = follow;
    midi.swapWith(outputMidi);
    publishVisualState(blockPeak);
}

namespace {
juce::ValueTree clipState(const composer::Clip& c, const char* type) {
    juce::ValueTree clip(type);
    clip.setProperty("length", c.length, nullptr);
    clip.setProperty("grid", c.grid, nullptr);
    clip.setProperty("numerator", c.numerator, nullptr);
    clip.setProperty("denominator", c.denominator, nullptr);
    clip.setProperty("favourite", c.favourite, nullptr);
    clip.setProperty("seed", juce::int64(c.seed), nullptr);
    for (int i = 0; i < c.count; ++i) {
        const auto& n = c.notes[i];
        juce::ValueTree note("Note");
        note.setProperty("start", n.start, nullptr);
        note.setProperty("duration", n.duration, nullptr);
        note.setProperty("pitch", n.pitch, nullptr);
        note.setProperty("velocity", n.velocity, nullptr);
        note.setProperty("locked", n.locked, nullptr);
        if (n.cents != 0)
            note.setProperty("cents", n.cents, nullptr);
        clip.addChild(note, -1, nullptr);
    }
    return clip;
}
bool readClipState(const juce::ValueTree& child, composer::Clip& c) {
    if (!child.isValid() || child.getNumChildren() > composer::maxNotes)
        return false;
    c = {};
    c.length = child.getProperty("length", 16.0);
    c.grid = child.getProperty("grid", 0.25);
    c.numerator = child.getProperty("numerator", 4);
    c.denominator = child.getProperty("denominator", 4);
    c.favourite = child.getProperty("favourite", false);
    c.seed = uint32_t(juce::int64(child.getProperty("seed", 0)));
    for (const auto& note : child)
        if (note.hasType("Note"))
            c.notes[c.count++] = {
                double(note.getProperty("start", 0.0)),  double(note.getProperty("duration", 0.25)),
                int(note.getProperty("pitch", 60)),      int(note.getProperty("velocity", 96)),
                bool(note.getProperty("locked", false)), int(note.getProperty("cents", 0))};
    composer::sanitise(c);
    return true;
}
// Validate saved inversions against the harmonic timeline before changing any live state.
// Octaves may differ; every pitch class must occur exactly once per chord.
bool validChordVoicing(const composer::Harmony& harmony, const composer::Clip& clip) {
    const auto reference = composer::voiceHarmony(harmony, clip.numerator, clip.denominator, {});
    if (clip.count != reference.count || std::abs(clip.length - harmony.length) > 1e-8)
        return false;
    std::array<bool, composer::maxNotes> matched{};
    for (int i = 0; i < clip.count; ++i) {
        const auto& n = clip.notes[i];
        if (n.cents != 0)
            return false;
        int match = -1;
        for (int j = 0; j < reference.count; ++j) {
            const auto& expected = reference.notes[j];
            if (!matched[j] && n.pitch % 12 == expected.pitch % 12 &&
                std::abs(n.start - expected.start) < 1e-8 &&
                std::abs(n.duration - expected.duration) < 1e-8) {
                match = j;
                break;
            }
        }
        if (match < 0)
            return false;
        matched[match] = true;
    }
    return true;
}
} // namespace
void Processor::getStateInformation(juce::MemoryBlock& dest) {
    juce::ValueTree state("GeneticComposer");
    state.setProperty("version", 5, nullptr);
    state.addChild(parameters.copyState(), -1, nullptr);
    {
        std::lock_guard<std::mutex> lock(documentMutex);
        state.setProperty("selected", selected.load(), nullptr);
        state.setProperty("harmony", juce::String(composer::harmonyText(customHarmony)), nullptr);
        for (const auto& c : bank)
            state.addChild(clipState(c, "Clip"), -1, nullptr);
        if (chordClip.count > 0)
            state.addChild(clipState(chordClip, "ChordVoicing"), -1, nullptr);
        if (grooveLoaded)
            state.addChild(clipState(grooveReference, "Groove"), -1, nullptr);
        if (comparisonAvailable.load())
            state.addChild(clipState(comparison, "Comparison"), -1, nullptr);
        // Unapplied previews are deliberately excluded from the saved document.
    }
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, dest);
}
bool Processor::restore(const void* data, int size) {
    if (data == nullptr || size <= 0 || size > 4 * 1024 * 1024)
        return false;
    const auto xml = getXmlFromBinary(data, size);
    if (!xml || !xml->hasTagName("GeneticComposer"))
        return false;
    const auto state = juce::ValueTree::fromXml(*xml);
    const int version = state.getProperty("version", 0);
    if ((version < 2 || version > 5) || !state.getChildWithName("Parameters").isValid())
        return false;
    Document restored;
    int index = 0;
    for (const auto& child : state)
        if (child.hasType("Clip")) {
            if (index >= composer::slotCount || !readClipState(child, restored.bank[index++]))
                return false;
        }
    if (index != composer::slotCount)
        return false;
    composer::Clip restoredComparison;
    bool hasA = false;
    if (version >= 3) {
        const auto harmony = state.getProperty("harmony").toString();
        std::string error;
        if (harmony.isNotEmpty() && !composer::parseHarmony(harmony.toStdString(), restored.harmony, error))
            return false;
        const auto groove = state.getChildWithName("Groove");
        restored.grooveLoaded = groove.isValid();
        if (restored.grooveLoaded && !readClipState(groove, restored.groove))
            return false;
        const auto a = state.getChildWithName("Comparison");
        hasA = a.isValid();
        if (hasA && !readClipState(a, restoredComparison))
            return false;
    }
    if (version >= 4) {
        const auto chords = state.getChildWithName("ChordVoicing");
        if (restored.harmony.count > 0) {
            if (!readClipState(chords, restored.chords) ||
                !validChordVoicing(restored.harmony, restored.chords))
                return false;
        } else if (chords.isValid())
            return false;
    } else if (restored.harmony.count > 0) {
        // Older sessions have chord names but no saved inversions. Reconstruct once, deterministically.
        const int slot = juce::jlimit(0, 11, int(state.getProperty("selected", 0)));
        const auto& clip = restored.bank[slot];
        restored.chords = composer::voiceHarmony(restored.harmony, clip.numerator, clip.denominator, {});
    }
    cancelled.store(true);
    panic();
    // A parameter missing from an older session takes its default, not the current value, so
    // earlier projects open with the microtonal tuning off.
    auto parameterState = state.getChildWithName("Parameters").createCopy();
    if (version < 5 && !parameterState.getChildWithProperty("id", "microEnabled").isValid()) {
        const auto legacyTuning = parameterState.getChildWithProperty("id", "tuning");
        parameterState.appendChild(
            juce::ValueTree("PARAM", {{"id", "microEnabled"},
                                      {"value", float(int(legacyTuning.getProperty("value", 0)) > 0)}}),
            nullptr);
    }
    if (version < 5 && restored.harmony.count > 0) {
        // Old sessions bypassed scale fit silently whenever custom harmony was present.
        // Make that previous audible behavior explicit in the restored switch.
        auto fit = parameterState.getChildWithProperty("id", "snap");
        if (fit.isValid())
            fit.setProperty("value", 0.0f, nullptr);
    }
    for (auto* parameter : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter);
            ranged && !parameterState.getChildWithProperty("id", ranged->paramID).isValid())
            parameterState.appendChild(juce::ValueTree("PARAM", {{"id", ranged->paramID}}), nullptr);
    parameters.replaceState(parameterState);
    {
        std::lock_guard<std::mutex> lock(documentMutex);
        restoreDocumentLocked(restored);
        comparison = restoredComparison;
        comparisonAvailable.store(hasA);
        compareRequested.store(false);
        selected.store(juce::jlimit(0, 11, int(state.getProperty("selected", 0))));
        audibleSlot.store(selected.load());
        undoStack.clear();
        redoStack.clear();
        publishLocked();
        assignStatusLocked("Session restored.");
    }
    return true;
}
void Processor::setStateInformation(const void* data, int size) {
    restore(data, size);
}
juce::Result Processor::saveBank(const juce::File& file) {
    juce::MemoryBlock data;
    getStateInformation(data);
    return file.replaceWithData(data.getData(), data.getSize())
               ? juce::Result::ok()
               : juce::Result::fail("Could not save this bank.");
}
juce::Result Processor::loadBank(const juce::File& file) {
    if (file.getSize() > 4 * 1024 * 1024)
        return juce::Result::fail("Bank is too large.");
    juce::MemoryBlock data;
    if (!file.loadFileAsData(data) || !restore(data.getData(), int(data.getSize())))
        return juce::Result::fail("Not a valid Genetic Composer 2 bank.");
    return juce::Result::ok();
}
juce::Result Processor::exportMidi(const juce::File& file) const {
    return exportClip(displayedClip(), file);
}
juce::Result Processor::exportChordMidi(const juce::File& file) const {
    return exportClip(chordClipSnapshot(), file, true, true);
}
juce::Result Processor::exportMelodyAndChords(const juce::File& file) const {
    const auto melody = displayedClip();
    const auto chords = chordClipSnapshot();
    if (melody.count == 0)
        return juce::Result::fail("This variation is empty.");
    if (chords.count == 0)
        return juce::Result::fail("Create a chord sequence before exporting melody and chords.");
    auto track = [&](const composer::Clip& clip, bool alreadyRendered, bool plain) {
        juce::MidiMessageSequence sequence;
        sequence.addEvent(juce::MidiMessage::tempoMetaEvent(int(60000000.0 / tempo())));
        sequence.addEvent(juce::MidiMessage::timeSignatureMetaEvent(clip.numerator, clip.denominator));
        appendClipMidi(sequence, clip, alreadyRendered, plain);
        auto end = juce::MidiMessage::endOfTrack();
        end.setTimeStamp(clip.length * 960);
        sequence.addEvent(end);
        sequence.updateMatchedPairs();
        return sequence;
    };
    const auto melodyTrack = track(melody, false, false);
    const auto chordTrack = track(chords, true, true);
    return writeMidiFile(file, melodyTrack, &chordTrack);
}
juce::Result Processor::exportMidiRange(const juce::File& file, double start, double end) const {
    const auto source = displayedClip();
    if (!std::isfinite(start) || !std::isfinite(end) || start < 0 || end <= start || end > source.length)
        return juce::Result::fail("Select a time range inside the phrase before exporting.");
    composer::Clip result = source;
    result.count = 0;
    result.length = end - start;
    for (int i = 0; i < source.count; ++i) {
        auto n = renderedNote(source.notes[i], source, 0, i);
        const double finish = std::min(end, n.start + n.duration);
        const double begin = std::max(start, n.start);
        if (finish <= begin)
            continue;
        n.start = begin - start;
        n.duration = finish - begin;
        result.notes[result.count++] = n;
    }
    return exportClip(result, file, true);
}
void Processor::appendClipMidi(juce::MidiMessageSequence& sequence, const composer::Clip& clip,
                               bool alreadyRendered, bool plainMidi) const {
    // Microtonal files carry the same setup and bends as live output. Sorted notes let an MPE
    // note take the member channel whose last note ended first. Plain tracks stay on channel 1.
    const int output = plainMidi ? 0 : microtonalOutput();
    const int range = output == 2 ? mpeBendRange : int(param("bendRange"));
    microtonalSetup(output, range, [&](const juce::MidiMessage& m) { sequence.addEvent(m); });
    std::array<double, 17> channelEnd;
    channelEnd.fill(-1.0);
    for (int i = 0; i < clip.count; ++i) {
        const auto n = alreadyRendered ? clip.notes[i] : renderedNote(clip.notes[i], clip, 0, i);
        int channel = 1;
        if (output == 2) {
            channel = 2;
            for (int candidate = 3; candidate <= 16; ++candidate)
                if (channelEnd[size_t(candidate)] < channelEnd[size_t(channel)])
                    channel = candidate;
            channelEnd[size_t(channel)] = n.start + n.duration;
        }
        if (output != 0) {
            auto bend = juce::MidiMessage::pitchWheel(channel, bendPosition(n.cents, range));
            bend.setTimeStamp(n.start * 960);
            sequence.addEvent(bend);
        }
        auto on = juce::MidiMessage::noteOn(channel, n.pitch, juce::uint8(n.velocity));
        on.setTimeStamp(n.start * 960);
        sequence.addEvent(on);
        auto off = juce::MidiMessage::noteOff(channel, n.pitch);
        off.setTimeStamp((n.start + n.duration) * 960);
        sequence.addEvent(off);
    }
}
juce::Result Processor::writeMidiFile(const juce::File& file, const juce::MidiMessageSequence& first,
                                      const juce::MidiMessageSequence* second) const {
    juce::MidiFile midi;
    midi.setTicksPerQuarterNote(960);
    midi.addTrack(first);
    if (second != nullptr)
        midi.addTrack(*second);
    juce::MemoryOutputStream stream;
    if (!midi.writeTo(stream, second != nullptr ? 1 : 0))
        return juce::Result::fail("Could not encode MIDI.");
    return file.replaceWithData(stream.getData(), stream.getDataSize())
               ? juce::Result::ok()
               : juce::Result::fail("Could not write MIDI file.");
}
juce::Result Processor::exportClip(const composer::Clip& clip, const juce::File& file, bool alreadyRendered,
                                   bool plainMidi) const {
    if (clip.count == 0)
        return juce::Result::fail("This variation is empty.");
    juce::MidiMessageSequence sequence;
    sequence.addEvent(juce::MidiMessage::tempoMetaEvent(int(60000000.0 / tempo())));
    sequence.addEvent(juce::MidiMessage::timeSignatureMetaEvent(clip.numerator, clip.denominator));
    appendClipMidi(sequence, clip, alreadyRendered, plainMidi);
    auto end = juce::MidiMessage::endOfTrack();
    end.setTimeStamp(clip.length * 960);
    sequence.addEvent(end);
    sequence.updateMatchedPairs();
    return writeMidiFile(file, sequence, nullptr);
}
juce::Result Processor::readMidi(const juce::File& file, composer::Clip& clip, bool& truncated) const {
    if (file.getSize() > 4 * 1024 * 1024)
        return juce::Result::fail("Choose a MIDI file smaller than 4 MB.");
    juce::FileInputStream stream(file);
    juce::MidiFile midi;
    if (!stream.openedOk() || !midi.readFrom(stream) || midi.getTimeFormat() <= 0)
        return juce::Result::fail("Choose a standard MIDI file with a musical timebase.");
    clip = {};
    clip.count = 0;
    clip.grid = 4.0 / (8 << int(param("grid")));
    double endTime = 0.0;
    truncated = false;
    std::array<int, composer::maxNotes> channels{};
    for (int track = 0; track < midi.getNumTracks(); ++track) {
        auto sequence = *midi.getTrack(track);
        sequence.updateMatchedPairs();
        for (int i = 0; i < sequence.getNumEvents(); ++i) {
            auto* event = sequence.getEventPointer(i);
            if (event->message.isTimeSignatureMetaEvent())
                event->message.getTimeSignatureInfo(clip.numerator, clip.denominator);
            if (event->message.isEndOfTrackMetaEvent())
                endTime = std::max(endTime, event->message.getTimeStamp() / midi.getTimeFormat());
            if (!event->message.isNoteOn() || event->noteOffObject == nullptr)
                continue;
            const double start = event->message.getTimeStamp() / midi.getTimeFormat();
            const double finish = event->noteOffObject->message.getTimeStamp() / midi.getTimeFormat();
            if (start >= 128.0 || clip.count >= composer::maxNotes) {
                truncated = true;
                continue;
            }
            channels[size_t(clip.count)] = event->message.getChannel();
            clip.notes[clip.count++] = {start, finish - start, event->message.getNoteNumber(),
                                        event->message.getVelocity(), false};
            endTime = std::max(endTime, finish);
        }
    }
    if (clip.count == 0)
        return juce::Result::fail("No complete notes were found in this MIDI file.");
    if (tuningIndex() > 0) {
        // The bend on a note's channel at its onset becomes its offset. Ranges follow RPN 0, and
        // an MPE zone message sets ±48 on its member channels; otherwise ±2 is assumed.
        juce::MidiMessageSequence merged;
        for (int track = 0; track < midi.getNumTracks(); ++track)
            merged.addSequence(*midi.getTrack(track), 0);
        std::array<double, 17> range;
        range.fill(2.0);
        juce::MidiRPNDetector rpn;
        struct Bend {
            double start, cents;
            int channel;
        };
        std::vector<Bend> bends;
        for (const auto* event : merged) {
            const auto& message = event->message;
            const int channel = message.getChannel();
            if (message.isController())
                if (const auto parsed =
                        rpn.tryParse(channel, message.getControllerNumber(), message.getControllerValue());
                    parsed && !parsed->isNRPN) {
                    const int value = parsed->is14BitValue ? parsed->value >> 7 : parsed->value;
                    if (parsed->parameterNumber == 0)
                        range[size_t(channel)] =
                            value + (parsed->is14BitValue ? (parsed->value & 127) / 100.0 : 0);
                    if (parsed->parameterNumber == 6 && (channel == 1 || channel == 16)) {
                        range[size_t(channel)] = 2;
                        for (int member = 1; member <= std::min(value, 15); ++member)
                            range[size_t(channel == 1 ? 1 + member : 16 - member)] = mpeBendRange;
                    }
                }
            if (message.isPitchWheel())
                bends.push_back(
                    {message.getTimeStamp() / midi.getTimeFormat(),
                     (message.getPitchWheelValue() - 8192) / 8192.0 * range[size_t(channel)] * 100, channel});
        }
        for (int i = 0; i < clip.count; ++i) {
            auto& note = clip.notes[i];
            double cents = 0;
            for (const auto& bend : bends) {
                if (bend.start > note.start + 1e-9)
                    break;
                if (bend.channel == channels[size_t(i)])
                    cents = bend.cents;
            }
            // Nearest key, so a +60 bend on E becomes F-40.
            const int total = note.pitch * 100 + int(std::lround(cents));
            note.pitch = int(std::floor((total + 50) / 100.0));
            note.cents = total - note.pitch * 100;
        }
    }
    const double bar = std::max(0.125, clip.numerator * 4.0 / std::max(1, clip.denominator));
    clip.length = std::clamp(std::ceil(endTime / bar - 0.0001) * bar, bar, 128.0);
    composer::sanitise(clip);
    if (clip.count == 0)
        return juce::Result::fail("No valid note events were found in this MIDI file.");
    return juce::Result::ok();
}
juce::Result Processor::importMidi(const juce::File& file) {
    if (hasDraft())
        return juce::Result::fail("Apply or discard the preview before importing a melody.");
    composer::Clip clip;
    bool truncated = false;
    if (auto result = readMidi(file, clip, truncated); result.failed())
        return result;
    setClip(selected.load(), clip);
    setStatus(truncated ? "MIDI imported; limited to 512 notes and 128 beats."
                        : "MIDI imported. Evolve it or keep its rhythm for new melodies.");
    return juce::Result::ok();
}
juce::AudioProcessorEditor* Processor::createEditor() {
    return new GeneticVSTComposerJUCEAudioProcessorEditor(*this);
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new Processor();
}
