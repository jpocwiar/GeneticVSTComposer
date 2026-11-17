#include "AiMidiProvider.h"
#include "PlaybackHelpers.h"
#include "PluginProcessor.h"
#include <algorithm>
#include <cmath>

using Processor = GeneticVSTComposerJUCEAudioProcessor;
namespace {
bool validAmount(float value) {
    return std::isfinite(value) && value >= 0 && value <= 1;
}
composer::Clip resizedPhrase(const composer::Clip& source, double length) {
    composer::Clip result = source;
    result.count = 0;
    result.length = length;
    if (source.length <= 0 || !source.count)
        return result;
    // Repeat the source in musical time; do not stretch its rhythm when a section grows.
    for (double origin = 0; origin < length - 1e-8; origin += source.length)
        for (int i = 0; i < source.count; ++i) {
            auto note = source.notes[size_t(i)];
            note.start += origin;
            if (note.start >= length - 1e-8)
                continue;
            if (result.count >= composer::maxNotes)
                return {}; // The caller reports capacity rather than silently losing notes.
            note.duration = std::min(note.duration, length - note.start);
            result.notes[size_t(result.count++)] = note;
        }
    composer::sanitise(result);
    return result;
}
composer::Harmony sectionHarmony(const composer::Harmony& source, double length) {
    composer::Harmony result;
    result.length = length;
    if (!source.count || source.length <= 0)
        return result;
    for (double origin = 0; origin < length - 1e-8; origin += source.length)
        for (int i = 0; i < source.count; ++i) {
            auto chord = source.chords[size_t(i)];
            chord.start += origin;
            if (chord.start >= length - 1e-8)
                continue;
            if (result.count == int(result.chords.size()))
                return {};
            chord.duration = std::min(chord.duration, length - chord.start);
            result.chords[size_t(result.count++)] = chord;
        }
    return result;
}
} // namespace

Processor::CompositionWorkspace Processor::compositionSnapshot() const {
    std::lock_guard<std::mutex> lock(documentMutex);
    return composition;
}
std::shared_ptr<const composer::CreativeContext> Processor::creativeContextLocked() const {
    if (!composition.enabled)
        return {};
    auto context = std::make_shared<composer::CreativeContext>();
    context->hook = composition.hook;
    context->drums = composition.drums;
    context->bass = composition.bass;
    context->preserveHook = composition.preserveHook && composition.hook.count > 0;
    context->identity = composition.identity;
    context->contextAmount = composition.contextAmount;
    context->role = static_cast<composer::InstrumentRole>(composition.role);
    context->preference = composition.preference;
    return context;
}
void Processor::generateCreativeBank(bool evolve) {
    {
        std::lock_guard<std::mutex> lock(documentMutex);
        if (!composition.enabled) {
            pushHistoryLocked();
            composition.enabled = true;
            publishLocked();
        }
    }
    generate(evolve);
}
juce::Result Processor::setCompositionRole(int role) {
    if (role < 0 || role > 3)
        return juce::Result::fail("Choose Lead, Bass, Riff or Arpeggio.");
    std::lock_guard<std::mutex> lock(documentMutex);
    if (composition.enabled && composition.role == role)
        return juce::Result::ok();
    cancelled.store(true);
    pushHistoryLocked();
    composition.enabled = true;
    composition.rolePreferences[size_t(composition.role)] = composition.preference;
    composition.role = role;
    composition.preference = composition.rolePreferences[size_t(role)];
    publishLocked();
    return juce::Result::ok();
}
juce::Result Processor::setMotifIdentity(float amount) {
    if (!validAmount(amount))
        return juce::Result::fail("Identity must be between 0 and 1.");
    std::lock_guard<std::mutex> lock(documentMutex);
    if (composition.enabled && composition.identity == amount)
        return juce::Result::ok();
    cancelled.store(true);
    pushHistoryLocked();
    composition.enabled = true;
    composition.identity = amount;
    publishLocked();
    return juce::Result::ok();
}
juce::Result Processor::setContextAmount(float amount) {
    if (!validAmount(amount))
        return juce::Result::fail("Context amount must be between 0 and 1.");
    std::lock_guard<std::mutex> lock(documentMutex);
    if (composition.enabled && composition.contextAmount == amount)
        return juce::Result::ok();
    cancelled.store(true);
    pushHistoryLocked();
    composition.enabled = true;
    composition.contextAmount = amount;
    publishLocked();
    return juce::Result::ok();
}
juce::Result Processor::captureMotif(double start, double end) {
    std::lock_guard<std::mutex> lock(documentMutex);
    const auto& source = bank[size_t(selected.load())];
    if (draftActive)
        return juce::Result::fail("Keep or discard the preview before capturing a motif.");
    if (!std::isfinite(start) || !std::isfinite(end) || start < 0 || end > source.length || end <= start)
        return juce::Result::fail("Select a time range containing your motif.");
    auto hook = source;
    hook.count = 0;
    hook.length = end - start;
    hook.favourite = false;
    for (int i = 0; i < source.count; ++i) {
        auto note = source.notes[size_t(i)];
        if (note.start < start || note.start >= end)
            continue;
        note.start -= start;
        note.duration = std::min(note.duration, hook.length - note.start);
        note.locked = false;
        hook.notes[size_t(hook.count++)] = note;
    }
    if (!hook.count)
        return juce::Result::fail("The selected motif contains no note onsets.");
    cancelled.store(true);
    pushHistoryLocked();
    composition.enabled = composition.preserveHook = true;
    composition.hook = hook;
    publishLocked();
    assignStatusLocked("Motif captured. Identity guides its contour and rhythm; note locks remain exact.");
    return juce::Result::ok();
}
void Processor::clearMotif() {
    std::lock_guard<std::mutex> lock(documentMutex);
    if (!composition.hook.count && !composition.preserveHook)
        return;
    cancelled.store(true);
    pushHistoryLocked();
    composition.hook = {};
    composition.preserveHook = false;
    publishLocked();
}
juce::Result Processor::developSelectedMotif(int action) {
    if (action < 0 || action > 3)
        return juce::Result::fail("Choose a motif development action.");
    auto s = settings();
    std::lock_guard<std::mutex> lock(documentMutex);
    if (draftActive)
        return juce::Result::fail("Keep or discard the preview before developing a motif.");
    auto& source = bank[size_t(selected.load())];
    if (!source.count)
        return juce::Result::fail("Select a melody with notes first.");
    s.creative = creativeContextLocked();
    if (pendingRegion.enabled && pendingRegion.slot == selected.load()) {
        s.regionOnly = true;
        s.regionStart = pendingRegion.start;
        s.regionEnd = pendingRegion.end;
    }
    auto developed = composer::developMotif(source, static_cast<composer::DevelopmentAction>(action), s,
                                            uint32_t(seedGenerator.nextInt()));
    if (!developed.count)
        return juce::Result::fail("No editable notes are available for development.");
    cancelled.store(true);
    pushHistoryLocked();
    composition.enabled = true;
    developed.favourite = source.favourite;
    source = developed;
    compareRequested.store(false);
    publishLocked();
    assignStatusLocked("Motif developed. Compare, keep it or Undo to return to the original.");
    return juce::Result::ok();
}
juce::Result Processor::preferVariation(int preferred, int other) {
    if (preferred < 0 || preferred >= composer::slotCount || other < 0 || other >= composer::slotCount ||
        preferred == other)
        return juce::Result::fail("Choose two different variations to compare.");
    auto s = settings();
    std::lock_guard<std::mutex> lock(documentMutex);
    if (!bank[size_t(preferred)].count || !bank[size_t(other)].count)
        return juce::Result::fail("Both comparison variations need notes.");
    cancelled.store(true);
    pushHistoryLocked();
    composition.enabled = true;
    composer::learnPreference(composition.preference, bank[size_t(preferred)], bank[size_t(other)], s);
    composition.rolePreferences[size_t(composition.role)] = composition.preference;
    selected.store(preferred);
    publishLocked();
    assignStatusLocked("Preference learned locally for this project. The next idea bank uses your choices.");
    return juce::Result::ok();
}
void Processor::resetPreferences() {
    std::lock_guard<std::mutex> lock(documentMutex);
    if (!composition.preference.comparisons)
        return;
    cancelled.store(true);
    pushHistoryLocked();
    composition.preference = {};
    composition.rolePreferences[size_t(composition.role)] = {};
    publishLocked();
}
juce::Result Processor::importCompositionReference(const juce::File& file, bool bass) {
    composer::Clip reference;
    bool truncated = false;
    if (auto result = readMidi(file, reference, truncated); result.failed())
        return result;
    if (truncated)
        return juce::Result::fail(
            "Reference exceeds 512 notes or 128 beats. Export a shorter MIDI reference.");
    std::lock_guard<std::mutex> lock(documentMutex);
    cancelled.store(true);
    pushHistoryLocked();
    composition.enabled = true;
    (bass ? composition.bass : composition.drums) = reference;
    publishLocked();
    assignStatusLocked(bass ? "Bass reference loaded for new idea generation."
                            : "Drum reference loaded for new idea generation.");
    return juce::Result::ok();
}
void Processor::clearCompositionReference(bool bass) {
    std::lock_guard<std::mutex> lock(documentMutex);
    auto& clip = bass ? composition.bass : composition.drums;
    if (!clip.count)
        return;
    cancelled.store(true);
    pushHistoryLocked();
    clip = {};
    publishLocked();
}
juce::Result Processor::createComposition() {
    auto s = settings();
    std::lock_guard<std::mutex> lock(documentMutex);
    if (draftActive)
        return juce::Result::fail("Keep or discard the preview before creating sections.");
    const auto& source = bank[size_t(selected.load())];
    if (!source.count)
        return juce::Result::fail("Select a melody with notes first.");
    std::vector<CompositionSection> sections;
    const char* names[]{"A — Theme", "A' — Development", "B — Contrast", "A'' — Return"};
    const int actions[]{0, 0, 2, 3};
    const float energies[]{0.4f, 0.6f, 0.75f, 0.5f};
    const float contrasts[]{0.0f, 0.25f, 0.8f, 0.15f};
    s.creative = creativeContextLocked();
    for (int index = 0; index < 4; ++index) {
        CompositionSection section;
        section.name = juce::String::fromUTF8(names[index]);
        section.energy = energies[index];
        section.contrast = contrasts[index];
        s.arousal = section.energy;
        section.melody =
            index == 0
                ? source
                : composer::developMotif(source, static_cast<composer::DevelopmentAction>(actions[index]), s,
                                         uint32_t(seedGenerator.nextInt()));
        section.melody.favourite = false;
        section.harmony = sectionHarmony(customHarmony, section.melody.length);
        if (customHarmony.count && !section.harmony.count)
            return juce::Result::fail(
                "The harmony is too dense for a section. Use a slower harmonic rhythm.");
        if (section.harmony.count) {
            section.chords = composer::voiceHarmony(section.harmony, source.numerator, source.denominator,
                                                    voicingSettings());
        }
        sections.push_back(std::move(section));
    }
    cancelled.store(true);
    compositionRequested.store(false);
    pushHistoryLocked();
    composition.enabled = true;
    composition.sections = std::move(sections);
    composition.selected = 0;
    publishLocked();
    assignStatusLocked(
        "Four related sections created. Edit their notes, lengths, harmony and energy independently.");
    return juce::Result::ok();
}
juce::Result Processor::addCompositionSection() {
    std::lock_guard<std::mutex> lock(documentMutex);
    if (composition.sections.size() >= 8)
        return juce::Result::fail("A composition can contain up to eight sections.");
    CompositionSection section;
    section.name = "Section " + juce::String(int(composition.sections.size()) + 1);
    section.melody = bank[size_t(selected.load())];
    if (!section.melody.count)
        return juce::Result::fail("Select a melody with notes to add as a section.");
    section.melody.favourite = false;
    section.harmony = sectionHarmony(customHarmony, section.melody.length);
    if (customHarmony.count && !section.harmony.count)
        return juce::Result::fail("The harmony is too dense for this section.");
    if (section.harmony.count)
        section.chords = composer::voiceHarmony(section.harmony, section.melody.numerator,
                                                section.melody.denominator, voicingSettings());
    cancelled.store(true);
    compositionRequested.store(false);
    pushHistoryLocked();
    composition.enabled = true;
    composition.sections.push_back(std::move(section));
    composition.selected = int(composition.sections.size()) - 1;
    publishLocked();
    return juce::Result::ok();
}
juce::Result Processor::removeCompositionSection(int index) {
    std::lock_guard<std::mutex> lock(documentMutex);
    if (index < 0 || index >= int(composition.sections.size()))
        return juce::Result::fail("Select a section first.");
    cancelled.store(true);
    compositionRequested.store(false);
    pushHistoryLocked();
    composition.sections.erase(composition.sections.begin() + index);
    composition.selected =
        std::clamp(composition.selected, 0, std::max(0, int(composition.sections.size()) - 1));
    publishLocked();
    return juce::Result::ok();
}
juce::Result Processor::selectCompositionSection(int index) {
    std::lock_guard<std::mutex> lock(documentMutex);
    if (index < 0 || index >= int(composition.sections.size()))
        return juce::Result::fail("Select a section first.");
    composition.selected = index;
    publishLocked();
    return juce::Result::ok();
}
juce::Result Processor::moveCompositionSection(int index, int direction) {
    std::lock_guard<std::mutex> lock(documentMutex);
    const int target = index + direction;
    if ((direction != -1 && direction != 1) || index < 0 || target < 0 ||
        index >= int(composition.sections.size()) || target >= int(composition.sections.size()))
        return juce::Result::fail("Choose a neighbouring section position.");
    cancelled.store(true);
    compositionRequested.store(false);
    pushHistoryLocked();
    std::swap(composition.sections[size_t(index)], composition.sections[size_t(target)]);
    composition.selected = target;
    publishLocked();
    return juce::Result::ok();
}
juce::Result Processor::updateCompositionSection(int index, const juce::String& name, int bars, float energy,
                                                 float contrast) {
    if (!validAmount(energy) || !validAmount(contrast) || bars < 1 || bars > 16 || name.trim().isEmpty() ||
        name.length() > 80)
        return juce::Result::fail("Use a section name, 1–16 bars and amounts between 0 and 1.");
    std::lock_guard<std::mutex> lock(documentMutex);
    if (index < 0 || index >= int(composition.sections.size()))
        return juce::Result::fail("Select a section first.");
    const auto& old = composition.sections[size_t(index)];
    const double length = bars * old.melody.numerator * 4.0 / old.melody.denominator;
    if (old.name == name.trim() && old.melody.length == length && old.energy == energy &&
        old.contrast == contrast)
        return juce::Result::ok();
    if (length > 128)
        return juce::Result::fail("Each section must fit 128 quarter-note beats.");
    if (std::any_of(old.melody.notes.begin(), old.melody.notes.begin() + old.melody.count,
                    [=](const composer::Note& note) {
                        return note.locked && note.start + note.duration > length + 1e-8;
                    }))
        return juce::Result::fail("This length would remove or shorten a locked note. Unlock it first.");
    auto melody = resizedPhrase(old.melody, length);
    if (old.melody.count && !melody.count)
        return juce::Result::fail("This length exceeds 512 notes. Simplify the section before extending it.");
    auto harmony = sectionHarmony(old.harmony, length);
    if (old.harmony.count && !harmony.count)
        return juce::Result::fail("This length exceeds 64 chord events. Use fewer chord changes.");
    auto chords = harmony.count ? composer::voiceHarmony(harmony, melody.numerator, melody.denominator,
                                                         voicingSettings())
                                : composer::Clip{};
    cancelled.store(true);
    compositionRequested.store(false);
    pushHistoryLocked();
    auto& section = composition.sections[size_t(index)];
    section.name = name.trim();
    section.melody = melody;
    section.harmony = harmony;
    section.chords = chords;
    section.energy = energy;
    section.contrast = contrast;
    publishLocked();
    return juce::Result::ok();
}
juce::Result Processor::setCompositionSectionHarmony(int index, const juce::String& text) {
    composer::Harmony parsed;
    std::string error;
    if (text.trim().isNotEmpty() && !composer::parseHarmony(text.toStdString(), parsed, error))
        return juce::Result::fail(juce::String(error));
    std::lock_guard<std::mutex> lock(documentMutex);
    if (index < 0 || index >= int(composition.sections.size()))
        return juce::Result::fail("Select a section first.");
    const auto& melody = composition.sections[size_t(index)].melody;
    auto harmony = sectionHarmony(parsed, melody.length);
    if (parsed.count && !harmony.count)
        return juce::Result::fail("The chord sequence exceeds 64 events in this section.");
    auto chords = harmony.count ? composer::voiceHarmony(harmony, melody.numerator, melody.denominator,
                                                         voicingSettings())
                                : composer::Clip{};
    cancelled.store(true);
    compositionRequested.store(false);
    pushHistoryLocked();
    composition.sections[size_t(index)].harmony = harmony;
    composition.sections[size_t(index)].chords = chords;
    publishLocked();
    assignStatusLocked("Section harmony changed. Develop its melody to follow the new chords.");
    return juce::Result::ok();
}
juce::Result Processor::setCompositionSectionClip(int index, composer::Clip clip) {
    composer::sanitise(clip);
    std::lock_guard<std::mutex> lock(documentMutex);
    if (index < 0 || index >= int(composition.sections.size()))
        return juce::Result::fail("Select a section first.");
    const auto& old = composition.sections[size_t(index)].melody;
    if (clip.length != old.length || clip.numerator != old.numerator || clip.denominator != old.denominator)
        return juce::Result::fail("Change section length with its Length control.");
    cancelled.store(true);
    compositionRequested.store(false);
    pushHistoryLocked();
    composition.sections[size_t(index)].melody = clip;
    publishLocked();
    return juce::Result::ok();
}
juce::Result Processor::developCompositionSection(int action) {
    if (action < 0 || action > 3)
        return juce::Result::fail("Choose a development action.");
    auto s = settings();
    std::lock_guard<std::mutex> lock(documentMutex);
    if (composition.sections.empty())
        return juce::Result::fail("Create sections from your melody first.");
    const int index = composition.selected;
    const auto& section = composition.sections[size_t(index)];
    s.creative = creativeContextLocked();
    s.harmony = section.harmony;
    s.arousal = section.energy;
    s.density = std::clamp(s.density + (section.energy - 0.5f) * 0.5f, 0.0f, 1.0f);
    s.mutation = std::clamp(0.2f + section.contrast * 0.7f, 0.0f, 1.0f);
    s.tension = std::clamp(s.tension + section.contrast * 0.25f, 0.0f, 1.0f);
    auto melody = composer::developMotif(section.melody, static_cast<composer::DevelopmentAction>(action), s,
                                         uint32_t(seedGenerator.nextInt()));
    if (s.harmony.count)
        melody = composer::adaptToHarmony(melody, 0, melody.length, s);
    // Lead into the following section without overwriting a locked ending.
    if (index + 1 < int(composition.sections.size()) && melody.count && action != 3) {
        auto& ending = melody.notes[size_t(melody.count - 1)];
        const auto& next = composition.sections[size_t(index + 1)].melody;
        if (!ending.locked && next.count && std::abs(ending.pitch - next.notes[0].pitch) > 12) {
            ending.pitch += ending.pitch < next.notes[0].pitch ? 12 : -12;
            ending.cents = 0;
        }
    }
    cancelled.store(true);
    compositionRequested.store(false);
    pushHistoryLocked();
    composition.sections[size_t(index)].melody = melody;
    publishLocked();
    assignStatusLocked("Section developed in its harmonic and energy context. Other sections are unchanged.");
    return juce::Result::ok();
}

void Processor::setAiPythonExecutable(const juce::File& file) {
    std::lock_guard<std::mutex> lock(documentMutex);
    pushHistoryLocked(false);
    aiPythonExecutable = file;
    composition.aiPython = file.getFullPathName();
    publishLocked();
}
juce::String Processor::aiAvailability() const {
    std::lock_guard<std::mutex> lock(documentMutex);
    return composer::ai::availability(aiPythonExecutable.getFullPathName());
}
juce::Result Processor::generateAiIdeas() {
    {
        std::lock_guard<std::mutex> lock(documentMutex);
        if (draftActive)
            return juce::Result::fail("Keep or discard the preview before generating AI ideas.");
        if (std::all_of(bank.begin(), bank.end(), [](const composer::Clip& c) { return c.favourite; }))
            return juce::Result::fail("Unpin a variation to receive new AI ideas.");
    }
    if (busy.exchange(true))
        return juce::Result::fail("A search is already running. Cancel it or wait for completion.");
    cancelled.store(false);
    progress.store(0);
    SearchJob job;
    job.aiIdeas = true;
    job.settings = settings();
    job.settings.seed = uint32_t(seedGenerator.nextInt());
    {
        std::lock_guard<std::mutex> lock(documentMutex);
        job.parent = bank[size_t(selected.load())];
        if (!job.settings.creative) {
            auto context = std::make_shared<composer::CreativeContext>();
            context->role = static_cast<composer::InstrumentRole>(composition.role);
            job.settings.creative = context;
        }
        job.startContent = contentRevision.load();
    }
    setStatus("Local AI is proposing a continuation. Cancel keeps your current bank.");
    submitSearch(std::move(job));
    return juce::Result::ok();
}
void Processor::performAiSearch(const SearchJob& job) {
    const auto directory = juce::File::getSpecialLocation(juce::File::tempDirectory)
                               .getNonexistentChildFile("genetic-ai-job", "", false);
    const auto finish = [this] {
        busy.store(false);
        searchSerialValue.fetch_add(1);
    };
    try {
        if (directory.createDirectory().failed()) {
            setStatus("Could not create the temporary AI workspace.");
            finish();
            return;
        }
        composer::ai::Request request;
        request.seedMidi = directory.getChildFile("seed.mid");
        request.outputMidi = directory.getChildFile("proposal.mid");
        request.root = job.settings.root;
        request.bars = job.settings.bars;
        request.numerator = job.settings.numerator;
        request.denominator = job.settings.denominator;
        request.seed = job.settings.seed;
        request.role = job.settings.creative ? int(job.settings.creative->role) : 0;
        {
            std::lock_guard<std::mutex> lock(documentMutex);
            request.pythonExecutable = aiPythonExecutable.existsAsFile()
                                           ? aiPythonExecutable.getFullPathName()
                                           : composer::ai::defaultPython().getFullPathName();
        }
        const auto& prompt =
            job.settings.creative && job.settings.creative->preserveHook && job.settings.creative->hook.count
                ? job.settings.creative->hook
                : job.parent;
        auto result = exportClip(prompt, request.seedMidi, true, true);
        if (result.wasOk())
            result = composer::ai::generate(request, cancelled);
        composer::Clip proposal;
        bool truncated = false;
        if (result.wasOk())
            result = readMidi(request.outputMidi, proposal, truncated);
        if (result.failed() || cancelled.load() || !proposal.count) {
            setStatus(cancelled.load() ? "AI generation cancelled. Your current bank is unchanged."
                                       : "AI proposal failed: " + result.getErrorMessage());
        } else {
            // AI supplies a seed; the same evaluated search and chord constraints develop it.
            auto s = job.settings;
            proposal.numerator = s.numerator;
            proposal.denominator = s.denominator;
            if (s.lockRhythm || s.mode == 2) {
                proposal = composer::combineParts(job.parent, proposal, proposal);
            } else {
                // MIDI does not encode locks. Restore protected source events before
                // evaluating any neural proposal, reserving their complete capacity.
                composer::Clip protectedProposal = proposal;
                protectedProposal.count = 0;
                for (int i = 0; i < job.parent.count; ++i) {
                    const auto& note = job.parent.notes[size_t(i)];
                    if (note.locked) {
                        protectedProposal.notes[size_t(protectedProposal.count++)] = note;
                        protectedProposal.length =
                            std::max(protectedProposal.length, note.start + note.duration);
                    }
                }
                for (int i = 0; i < proposal.count && protectedProposal.count < composer::maxNotes; ++i) {
                    const auto& note = proposal.notes[size_t(i)];
                    const bool overlap =
                        std::any_of(job.parent.notes.begin(), job.parent.notes.begin() + job.parent.count,
                                    [&](const composer::Note& locked) {
                                        return locked.locked && locked.pitch == note.pitch &&
                                               note.start < locked.start + locked.duration &&
                                               locked.start < note.start + note.duration;
                                    });
                    if (!overlap)
                        protectedProposal.notes[size_t(protectedProposal.count++)] = note;
                }
                composer::sanitise(protectedProposal);
                proposal = protectedProposal;
            }
            if (s.harmony.count)
                proposal = composer::adaptToHarmony(proposal, 0, proposal.length, s);
            auto results =
                composer::generate(s, &proposal, cancelled, [this](float value) { progress.store(value); });
            std::lock_guard<std::mutex> lock(documentMutex);
            if (!cancelled.load() && contentRevision.load() == job.startContent && !results.empty() &&
                std::any_of(bank.begin(), bank.end(), [](const composer::Clip& c) { return !c.favourite; })) {
                pushHistoryLocked();
                composition.enabled = true;
                size_t next = 0;
                for (auto& clip : bank)
                    if (!clip.favourite && next < results.size())
                        clip = results[next++];
                publishLocked();
                assignStatusLocked("AI ideas developed and evaluated. Your pinned variations are preserved.");
            } else
                assignStatusLocked(
                    "AI result discarded because the document changed or the search was cancelled.");
        }
    } catch (const std::exception& e) {
        setStatus("AI proposal failed: " + juce::String(e.what()));
    }
    directory.deleteRecursively();
    finish();
}
