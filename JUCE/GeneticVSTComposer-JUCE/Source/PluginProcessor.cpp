#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

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
        if (x.start != y.start || x.duration != y.duration || x.pitch != y.pitch || x.velocity != y.velocity)
            return false;
    }
    return true;
}
} // namespace
juce::StringArray Processor::rootNames() {
    return {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
}
juce::StringArray Processor::scaleNames() {
    return {"Major",          "Natural minor",    "Dorian",
            "Mixolydian",     "Major pentatonic", "Minor pentatonic",
            "Harmonic minor", "Lydian",           "Chromatic"};
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
    number("diversity", "Variety", 0, 1, 0.5f);
    number("syncopation", "Syncopation", 0, 1, 0.25f);
    number("tension", "Tension", 0, 1, 0.15f);
    number("space", "Space", 0, 0.85f, 0.15f);
    number("mutation", "Evolution amount", 0, 1, 0.35f);
    number("low", "Lowest note", 24, 96, 48, 1);
    number("high", "Highest note", 36, 108, 84, 1);
    number("swing", "Swing", 0, 0.75f, 0);
    number("gate", "Gate", 0.1f, 1, 0.9f);
    number("tempo", "Preview tempo", 40, 240, 120, 1);
    number("volume", "Monitor level", 0, 1, 0.65f);
    toggle("snap", "Snap playback to scale", true);
    toggle("lockRhythm", "Keep rhythm", false);
    toggle("follow", "Follow host transport", false);
    toggle("monitor", "Internal sound", true);
    toggle("quantiseSwitch", "Switch at next loop", true);
    return p;
}
GeneticVSTComposerJUCEAudioProcessor::GeneticVSTComposerJUCEAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "Parameters", createParameters()), bank(composer::makeInitialBank()),
      pendingBank(bank), audioBank(bank) {
    clearVisualState();
}
GeneticVSTComposerJUCEAudioProcessor::~GeneticVSTComposerJUCEAudioProcessor() {
    cancelled.store(true);
    if (worker.joinable())
        worker.join();
}
bool Processor::isBusesLayoutSupported(const BusesLayout& layout) const {
    return layout.getMainOutputChannelSet() == juce::AudioChannelSet::stereo() ||
           layout.getMainOutputChannelSet() == juce::AudioChannelSet::mono();
}
void Processor::prepareToPlay(double sampleRate, int) {
    rate = sampleRate;
    voices = {};
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
    audioCompare = audioDraftActive = loopBoundary = false;
    heardComparison.store(false);
    for (int p = 0; p < 128; ++p)
        phaseIncrements[p] = 440.0 * std::exp2((p - 69.0) / 12.0) / rate;
    outputMidi.ensureSize(65536);
}
void Processor::releaseResources() {
    playing.store(false);
    preview.store(false);
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
void Processor::clearVisualState() noexcept {
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
    return status;
}
void Processor::setStatus(const juce::String& text) {
    std::lock_guard<std::mutex> lock(documentMutex);
    status = text;
}
void Processor::selectClip(int index) {
    index = juce::jlimit(0, 11, index);
    std::lock_guard<std::mutex> lock(documentMutex);
    const bool changed = selected.exchange(index) != index;
    if (changed && draftActive) {
        draftActive = false;
        status = "Preview discarded after selecting another variation.";
        publishLocked();
    }
    if (!playing.load())
        audibleSlot.store(index);
}
Processor::Document Processor::documentLocked() const {
    return {bank, customHarmony, grooveReference, grooveLoaded};
}
void Processor::restoreDocumentLocked(const Document& d) {
    bank = d.bank;
    customHarmony = d.harmony;
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
    pendingHasDraft = draftActive;
    pendingDraftSlot = draftSlot;
    pendingRevision = revision.fetch_add(1) + 1;
}
void Processor::setClip(int index, composer::Clip clip) {
    cancelled.store(true);
    composer::sanitise(clip);
    std::lock_guard<std::mutex> lock(documentMutex);
    if (draftActive) {
        status = "Apply or discard the preview before editing a phrase.";
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
}
composer::Settings Processor::settings() const {
    composer::Settings s;
    s.root = int(param("root"));
    s.scale = int(param("scale"));
    s.bars = 1 << int(param("bars"));
    s.division = 8 << int(param("grid"));
    const int nums[]{4, 3, 5, 7, 6}, dens[]{4, 4, 4, 8, 8};
    const int m = int(param("meter"));
    s.numerator = nums[m];
    s.denominator = dens[m];
    s.low = int(param("low"));
    s.high = int(param("high"));
    s.mode = int(param("mode"));
    s.progression = int(param("progression"));
    s.quality = int(param("quality"));
    s.density = param("density");
    s.diversity = param("diversity");
    s.syncopation = param("syncopation");
    s.tension = param("tension");
    s.space = param("space");
    s.mutation = param("mutation");
    s.lockRhythm = param("lockRhythm") > 0.5f;
    s.harmony = harmonySnapshot();
    return s;
}
void Processor::generate(bool evolve) {
    if (hasDraft()) {
        setStatus("Apply or discard the preview before generating.");
        return;
    }
    if (busy.exchange(true))
        return;
    if (worker.joinable())
        worker.join();
    cancelled.store(false);
    progress.store(0);
    auto s = settings();
    s.seed = uint32_t(seedGenerator.nextInt());
    const auto parent = selectedClip();
    const auto startRevision = revision.load();
    const bool useParent = evolve || s.mode == 2;
    setStatus(evolve ? "Evolving the selected phrase..." : "Searching for new phrases...");
    worker = std::thread([this, s, parent, startRevision, useParent] {
        try {
            auto results = composer::generate(s, useParent ? &parent : nullptr, cancelled,
                                              [this](float p) { progress.store(p); });
            std::lock_guard<std::mutex> lock(documentMutex);
            if (!results.empty() && !cancelled.load() && revision.load() == startRevision) {
                pushHistoryLocked();
                int next = 0;
                for (auto& c : bank)
                    if (!c.favourite)
                        c = results[size_t(next++)];
                publishLocked();
                status = next > 0 ? "New phrases ready. Pin favourites to keep them in the next generation."
                                  : "All variations are pinned. Unpin a slot to generate into it.";
            } else
                status = "Generation cancelled. Your previous phrases are safe.";
        } catch (const std::exception& e) {
            setStatus("Generation failed: " + juce::String(e.what()));
        }
        busy.store(false);
    });
}
void Processor::setParameter(const char* id, float value) {
    auto* p = parameters.getParameter(id);
    p->beginChangeGesture();
    p->setValueNotifyingHost(p->convertTo0to1(value));
    p->endChangeGesture();
}
void Processor::applyPreset(int i) {
    const float macros[][6]{{0.5f, 0.5f, 0.25f, 0.15f, 0.15f, 0.35f},
                            {0.25f, 0.25f, 0.1f, 0.05f, 0.35f, 0.25f},
                            {0.8f, 0.65f, 0.55f, 0.15f, 0.08f, 0.45f},
                            {0.6f, 0.8f, 0.7f, 0.75f, 0.2f, 0.6f},
                            {0.35f, 0.3f, 0.4f, 0.1f, 0.3f, 0.3f}};
    const char* ids[]{"density", "diversity", "syncopation", "tension", "space", "mutation"};
    i = juce::jlimit(0, 4, i);
    for (int j = 0; j < 6; ++j)
        setParameter(ids[j], macros[i][j]);
    setParameter("low", i == 4 ? 36 : 48);
    setParameter("high", i == 4 ? 60 : 84);
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
    cancelled.store(true);
    std::lock_guard<std::mutex> lock(documentMutex);
    pushHistoryLocked();
    customHarmony = parsed;
    customHarmonyEnabled.store(parsed.count > 0);
    publishLocked();
    status = "Custom chords set. Generate or evolve to use them; playback scale snap is bypassed.";
    return juce::Result::ok();
}
void Processor::clearCustomHarmony() {
    cancelled.store(true);
    std::lock_guard<std::mutex> lock(documentMutex);
    if (customHarmony.count == 0)
        return;
    pushHistoryLocked();
    customHarmony = {};
    customHarmonyEnabled.store(false);
    publishLocked();
    status = "Custom harmony cleared. The scale and progression controls apply again.";
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
    cancelled.store(true);
    std::lock_guard<std::mutex> lock(documentMutex);
    pushHistoryLocked();
    customHarmony = inferred;
    customHarmonyEnabled.store(true);
    publishLocked();
    status = truncated ? "Harmony imported from a truncated reference. Check the chord names and lengths."
                       : "Chords estimated from MIDI. Check and edit the chord names before generating.";
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
    status = truncated ? "Groove imported; reference limited to 512 notes and 128 beats."
                       : "Groove reference loaded. Adjust Timing and Velocity, then apply.";
    return juce::Result::ok();
}
void Processor::useSelectedAsGroove() {
    cancelled.store(true);
    std::lock_guard<std::mutex> lock(documentMutex);
    const auto& source = draftActive ? draft : bank[selected.load()];
    if (source.count == 0) {
        status = "Choose a non-empty phrase as the groove reference.";
        return;
    }
    pushHistoryLocked();
    grooveReference = source;
    grooveLoaded = true;
    publishLocked();
    status = "Groove captured. Select a target phrase, then apply timing and velocity.";
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
    status = "Groove applied. Locked notes are preserved; Undo restores the previous performance.";
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
    status = "Selected region edited. Notes outside the range and locked notes are preserved.";
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
    status = "Preview ready. Play to audition, then Apply or Discard. The saved bank is unchanged.";
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
    status =
        "Arrangement preview ready. A stays intact, A' develops, B answers, A'' resolves. Apply or Discard.";
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
    status = "Preview applied to the selected variation. Undo restores the original.";
}
void Processor::discardDraft() {
    cancelled.store(true);
    std::lock_guard<std::mutex> lock(documentMutex);
    if (!draftActive)
        return;
    draftActive = false;
    publishLocked();
    status = "Preview discarded. The bank is unchanged.";
}
void Processor::captureComparison() {
    std::lock_guard<std::mutex> lock(documentMutex);
    comparison = draftActive && draftSlot == selected.load() ? draft : bank[selected.load()];
    comparisonAvailable.store(true);
    compareRequested.store(false);
    publishLocked();
    status = "A captured. Edit or choose a variation for B; switch A/B to compare.";
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
composer::Note Processor::renderedNote(composer::Note n, const composer::Clip& clip,
                                       int transposition) const {
    n.start = swingTime(n.start, clip.grid, param("swing"));
    n.start = std::min(n.start, clip.length - 0.001);
    n.duration = std::max(0.001, std::min(n.duration * param("gate"), clip.length - n.start));
    n.pitch = juce::jlimit(0, 127, n.pitch + transposition);
    if (param("snap") > 0.5f && !customHarmonyEnabled.load())
        n.pitch = composer::snapPitch(n.pitch, int(param("root")), int(param("scale")));
    return n;
}
void Processor::allOff(int sample, juce::MidiBuffer& midi) {
    for (int p = 0; p < 128; ++p)
        if (voices[p].on) {
            midi.addEvent(juce::MidiMessage::noteOff(1, p), sample);
            voices[p].on = false;
            voices[p].sourceIndex = -1;
            voices[p].target = 0;
        }
}
void Processor::processBlockBypassed(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi) {
    audio.clear();
    if (audio.getNumSamples() > 0)
        allOff(0, midi);
    voices = {};
    heldTriggers = {};
    heldTranspose = {};
    trigger = -1;
    transpose = 0;
    wasPlaying = false;
    wasHostPlaying = false;
    preview.store(false);
    playing.store(false);
    playBeat.store(-1);
    clearVisualState();
}
void Processor::noteOn(const composer::Note& n, int sourceIndex, int sample, juce::MidiBuffer& midi) {
    auto& v = voices[n.pitch];
    if (v.on)
        midi.addEvent(juce::MidiMessage::noteOff(1, n.pitch), sample);
    v.on = true;
    v.sourceIndex = sourceIndex;
    v.remaining = n.duration;
    v.target = n.velocity / 127.0;
    midi.addEvent(juce::MidiMessage::noteOn(1, n.pitch, juce::uint8(n.velocity)), sample);
    ++audioNoteOnSerial;
}
void Processor::processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    audio.clear();
    outputMidi.clear();
    const int samples = audio.getNumSamples();
    if (samples <= 0)
        return;
    bool bankChanged = false;
    {
        juce::SpinLock::ScopedTryLockType lock(exchangeLock);
        if (lock.isLocked() && pendingRevision != audioRevision) {
            const int slot = juce::jlimit(0, 11, audioSelection);
            const bool nextHasDraft = pendingHasDraft && pendingDraftSlot == slot;
            const auto& nextClip = audioCompare   ? pendingComparison
                                   : nextHasDraft ? pendingDraft
                                                  : pendingBank[slot];
            const bool switchingLater =
                wasPlaying && param("quantiseSwitch") > 0.5f && selected.load() != audioSelection;
            bankChanged = audioSelection >= 0 && !switchingLater && !samePlayback(activeClip, nextClip);
            audioBank = pendingBank;
            audioComparison = pendingComparison;
            audioDraft = pendingDraft;
            audioHasDraft = pendingHasDraft;
            audioDraftSlot = pendingDraftSlot;
            audioRevision = pendingRevision;
        }
    }
    double bpm = param("tempo"), ppq = 0.0;
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
    const bool follow = param("follow") > 0.5f;
    const bool hostJump =
        follow && hostPlaying && hasPosition && (!wasHostPlaying || std::abs(ppq - expectedPpq) > step * 2.0);
    const bool panicNow = panicRequested.exchange(false);
    if (panicNow) {
        allOff(0, outputMidi);
        outputMidi.addEvent(juce::MidiMessage::controllerEvent(1, 64, 0), 0);
        outputMidi.addEvent(juce::MidiMessage::allNotesOff(1), 0);
        heldTriggers = {};
        heldTranspose = {};
        trigger = -1;
        transpose = 0;
    }
    bool restart = bankChanged || hostJump || follow != lastFollow;
    auto incoming = midi.cbegin();
    const auto end = midi.cend();
    const bool monitor = param("monitor") > 0.5f;
    const float volume = param("volume");
    const double swingAmount = param("swing");
    const bool quantise = param("quantiseSwitch") > 0.5f;
    float blockPeak = 0;
    for (int sample = 0; sample < samples; ++sample) {
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
                trigger = -1;
                heldTriggers = {};
                heldTranspose = {};
                transpose = 0;
                preview.store(false);
                outputMidi.addEvent(message, sample);
            } else {
                outputMidi.addEvent(message, sample);
            }
        }
        const int slot = selected.load();
        const bool requestedA = compareRequested.load() && comparisonAvailable.load();
        const bool requestedDraft = audioHasDraft && audioDraftSlot == slot;
        const bool run = !panicNow && (follow ? hostPlaying : (preview.load() || trigger >= 0));
        const bool requestChanged =
            slot != audioSelection || requestedA != audioCompare || requestedDraft != audioDraftActive;
        const bool boundarySwitch = loopBoundary && requestChanged;
        if (restart || run != wasPlaying || audioSelection < 0 ||
            (requestChanged && (!quantise || !wasPlaying || loopBoundary))) {
            allOff(sample, outputMidi);
            audioSelection = slot;
            audioCompare = requestedA;
            audioDraftActive = requestedDraft;
            audibleSlot.store(slot);
            heardComparison.store(requestedA);
            activeClip = audioCompare ? audioComparison : requestedDraft ? audioDraft : audioBank[slot];
            phase = boundarySwitch          ? 0.0
                    : follow && hasPosition ? std::fmod(std::max(0.0, ppq + sample * step), activeClip.length)
                                            : 0.0;
            nextNote = 0;
            while (nextNote < activeClip.count &&
                   renderedNote(activeClip.notes[nextNote], activeClip).start < phase - step * 0.5)
                ++nextNote;
            restart = false;
        }
        loopBoundary = false;
        const auto& clip = activeClip;
        wasPlaying = run;
        for (int p = 0; p < 128; ++p) {
            auto& v = voices[p];
            if (v.on && v.remaining <= 1.0e-9) {
                outputMidi.addEvent(juce::MidiMessage::noteOff(1, p), sample);
                v.on = false;
                v.sourceIndex = -1;
                v.target = 0;
            }
        }
        if (run) {
            while (nextNote < clip.count) {
                const auto& raw = clip.notes[nextNote];
                const double start =
                    std::min(clip.length - 0.001, swingTime(raw.start, clip.grid, swingAmount));
                if (start > phase + step * 0.5)
                    break;
                auto n = renderedNote(raw, clip, transpose);
                if (n.start >= phase - step * 1.5)
                    noteOn(n, nextNote, sample, outputMidi);
                ++nextNote;
            }
        }
        double signal = 0.0;
        for (int p = 0; p < 128; ++p) {
            auto& v = voices[p];
            if (v.on)
                v.remaining -= step;
            const double target = monitor ? v.target : 0.0;
            v.gain += (target - v.gain) * (target > v.gain ? 0.004 : 0.0015);
            if (v.gain > 0.00005) {
                v.phase += phaseIncrements[p];
                v.phase -= std::floor(v.phase);
                const double angle = juce::MathConstants<double>::twoPi * v.phase;
                signal += v.gain * (std::sin(angle) + 0.22 * std::sin(angle * 2.0)) * 0.1;
            }
        }
        const float value = float(std::tanh(signal * volume));
        for (int ch = 0; ch < audio.getNumChannels(); ++ch)
            audio.setSample(ch, sample, value);
        if (audio.getNumChannels() > 0)
            blockPeak = std::max(blockPeak, std::abs(value));
        if (run) {
            phase += step;
            if (phase >= clip.length - step * 0.0001) {
                phase = std::max(0.0, phase - clip.length);
                nextNote = 0;
                loopBoundary = true;
            }
        }
    }
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
            c.notes[c.count++] = {double(note.getProperty("start", 0.0)),
                                  double(note.getProperty("duration", 0.25)),
                                  int(note.getProperty("pitch", 60)), int(note.getProperty("velocity", 96)),
                                  bool(note.getProperty("locked", false))};
    composer::sanitise(c);
    return true;
}
} // namespace
void Processor::getStateInformation(juce::MemoryBlock& dest) {
    juce::ValueTree state("GeneticComposer");
    state.setProperty("version", 3, nullptr);
    state.addChild(parameters.copyState(), -1, nullptr);
    {
        std::lock_guard<std::mutex> lock(documentMutex);
        state.setProperty("selected", selected.load(), nullptr);
        state.setProperty("harmony", juce::String(composer::harmonyText(customHarmony)), nullptr);
        for (const auto& c : bank)
            state.addChild(clipState(c, "Clip"), -1, nullptr);
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
    if ((version != 2 && version != 3) || !state.getChildWithName("Parameters").isValid())
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
    cancelled.store(true);
    panic();
    parameters.replaceState(state.getChildWithName("Parameters"));
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
        status = "Session restored.";
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
juce::Result Processor::exportMidiRange(const juce::File& file, double start, double end) const {
    const auto source = displayedClip();
    if (!std::isfinite(start) || !std::isfinite(end) || start < 0 || end <= start || end > source.length)
        return juce::Result::fail("Select a time range inside the phrase before exporting.");
    composer::Clip result = source;
    result.count = 0;
    result.length = end - start;
    for (int i = 0; i < source.count; ++i) {
        auto n = renderedNote(source.notes[i], source);
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
juce::Result Processor::exportClip(const composer::Clip& clip, const juce::File& file,
                                   bool alreadyRendered) const {
    if (clip.count == 0)
        return juce::Result::fail("This variation is empty.");
    juce::MidiMessageSequence sequence;
    sequence.addEvent(juce::MidiMessage::tempoMetaEvent(int(60000000.0 / tempo())));
    sequence.addEvent(juce::MidiMessage::timeSignatureMetaEvent(clip.numerator, clip.denominator));
    for (int i = 0; i < clip.count; ++i) {
        const auto n = alreadyRendered ? clip.notes[i] : renderedNote(clip.notes[i], clip);
        auto on = juce::MidiMessage::noteOn(1, n.pitch, juce::uint8(n.velocity));
        on.setTimeStamp(n.start * 960);
        sequence.addEvent(on);
        auto off = juce::MidiMessage::noteOff(1, n.pitch);
        off.setTimeStamp((n.start + n.duration) * 960);
        sequence.addEvent(off);
    }
    auto end = juce::MidiMessage::endOfTrack();
    end.setTimeStamp(clip.length * 960);
    sequence.addEvent(end);
    sequence.updateMatchedPairs();
    juce::MidiFile midi;
    midi.setTicksPerQuarterNote(960);
    midi.addTrack(sequence);
    juce::MemoryOutputStream stream;
    if (!midi.writeTo(stream, 0))
        return juce::Result::fail("Could not encode MIDI.");
    return file.replaceWithData(stream.getData(), stream.getDataSize())
               ? juce::Result::ok()
               : juce::Result::fail("Could not write MIDI file.");
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
            clip.notes[clip.count++] = {start, finish - start, event->message.getNoteNumber(),
                                        event->message.getVelocity(), false};
            endTime = std::max(endTime, finish);
        }
    }
    if (clip.count == 0)
        return juce::Result::fail("No complete notes were found in this MIDI file.");
    const double bar = std::max(1.0, clip.numerator * 4.0 / std::max(1, clip.denominator));
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
