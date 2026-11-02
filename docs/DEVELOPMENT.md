# Development

## Structure

This section describes version **2.8**. The 2.7 notes below remain the previous baseline. The dated sections below retain the implementation and validation history of 2.0–2.6. See [GA_ARCHITECTURE.md](GA_ARCHITECTURE.md) for the current chromosome, phrase construction, fitness terms, interval-cost tables and control mappings.

- `ComposerEngine.{h,cpp}` is a JUCE-independent, deterministic genetic search. The old `genetic`, `mingus` and `notes_generator` files live in `legacy/reference/` and are not compiled by CMake. The current engine uses a full population, tournament selection, crossover and mutation. Populations of 64 or more run as four islands, each keeping one unchanged elite; smaller populations use one island. The phrase-type feature does not replace the search with a preset lookup.
- `Clip` holds up to 512 sorted note events in quarter-note units. Each note carries pitch, velocity, duration, a lock flag and a cents offset (±50 around its key; `sanitise` respells larger offsets onto a nearer key). Each of twelve slots also carries phrase length, meter, grid, favourite status and seed. Fitness and similarity use the actual pitch, `100 * pitch + cents`.
- `Settings::phraseType` selects Riff, Motif or Melody independently of the Full melody / Rhythm only / Melody on rhythm operation. Riff repeats a one-bar cell, or the whole clip when shorter; Motif builds a returning short theme with variation and contrast; Melody develops the full phrase. Riff/Motif copies are constructed and updated before scoring, so fitness sees the entire phrase, harmonic changes and the last-to-first loop interval. Existing note locks and protected rhythmic templates take precedence over restructuring a parent.
- Microtonal cents are genes evaluated inside the search. `microEnabled` enables short passing-note proposals; `tuning` chooses their palette without replacing the selected Scale. A generated unlocked microtonal note must be short, lie inside the phrase and resolve to a nearby 12-TET anchor. Constraint repair can remove invalid offsets but does not add random cents after fitness evaluation. A result need not contain microtones when no useful valid passing note exists. Fresh generation with Micro OFF has zero cents; evolution clears unlocked offsets while preserving locked notes. Toggling Micro alone never rewrites the bank.
- Generation runs on a persistent, cancellable search thread. Fitness is cached once per individual per generation and scored in parallel; every requested generation runs unless cancelled. Final alternatives come from evaluated candidates rather than unscored filler mutations. Results replace only unpinned slots. A document revision check prevents an old worker from overwriting newer edits or restored state. Search statistics include actual evaluations, crossovers and evaluations of candidates containing nonzero cents.
- Microtonal MIDI is produced at note-on from stored offsets. Pitch bend mode sends RPN 0 for the Bend range at playback start and a bend before each note on channel 1. MPE declares a 15-member lower zone at ±48 and allocates the longest-idle member channel; voices retain their channel for note-off, panic and bypass. Leaving microtonal output centres bends and clears the MPE zone. Export uses the same encoding; import with Micro enabled reads onset bends using RPN 0 and MPE ranges. Micro OFF preserves stored cents but renders and exports plain 12-TET.
- The processor owns the document, bounded undo history and APVTS parameters. Document snapshots include the bank, custom harmony, its exact voiced chord clip and the groove reference. The captured A comparison is persistent but separate from undo history; an unaccepted arrangement/combination draft is transient. Closing the editor leaves the session and processor-owned worker intact.
- UI/worker writes publish fixed-size playback data through a short spin-lock handoff. The audio callback uses `ScopedTryLockType`; if publication is busy it retains the previous complete snapshot. The callback does not acquire the UI/document mutex.
- Playback tracks note lifetimes across callbacks and handles incoming MIDI at its sample offsets. Note-offs use the actual emitted pitches and channels. Selection/A-B requests can wait until the melody loop wraps, then release old notes and activate the replacement; editing and audible selections remain separate. MIDI triggers switch immediately. Host PPQ drives Follow DAW playback and detects discontinuities; local preview uses host tempo when available, otherwise the preview tempo.
- Melody playback and export share swing, gate and **Fit to scale**. Fit defaults OFF and, when enabled, remains effective with custom harmony. It snaps the MIDI key to the selected Root/Scale after live transposition and preserves existing cents while Micro is enabled; it never invents microtones. This is a performance projection of subsequent note starts, not a bank edit or a retuning of already-held notes. Borrowed chord tones can therefore change when Fit is explicitly enabled. Imported material is not quantized; ordinary note editing uses the clip grid.
- `ChordGenerator.{h,cpp}` remains JUCE-independent and separate from the melody GA. A phrase planner chooses cadence destinations and optional related-key sections. `HarmonicSearch.h` performs a stochastic beam search (width 12) over complete progressions, combining authored character palettes, common-tone/function preferences, repetition costs and a smoothed interval-context prior trained on UCI Bach Choral Harmony. The prior has 2,890 collapsed root/family transitions from 5,665 events in 60 chorales, with no stored source templates. `chordCharacter` selects Balanced / Pop / Jazz / Modal / Cinematic. Three independent amounts control movement (Originality), vocabulary (`chordColour`, default 0.45) and brightness (`chordMood`, default 0.5). Colour at or below 0.1 gates out suspensions and extensions in every profile; ninths require above 0.55. High Originality opens every chromatic root, reduces the corpus/consonance bias and rewards larger circle-of-fifths distances, including semitone motion, while the requested tonic ending remains fixed. Mood biases minor/diminished versus major/warmer chord families without switching the shared scale or bypassing Colour. The new amounts append after previous parameters, preserving IDs/order/ranges and state schema 6. Missing values restore to defaults, never the previous session's amounts. Changing controls does not rewrite stored chords or melodies; Generate reads them, while Revoice only reads inversions/register. `HarmonyModelData.h` is generated offline by `scripts/train-harmony-model.py`; builds and plugin use need no network/Python. See [data provenance and held-out validation](../data/harmony/README.md). Voicing uses dynamic programming over six central starting inversions and includes the last-to-first transition. Custom chord events carry quarter-note timing and root/quality. Text is parsed before commit; a failed parse leaves the document intact. MIDI chord estimation remains heuristic and editable.
- `ChordComposerPanel` is embedded in the editor's **Chords** page beside **Melody**, with Play below each piano roll. New chords only activates custom harmony without replacing melody slots. Melody from these chords spans the exact harmony length; New chords + melody creates both. Subsequent Generate/Evolve uses active harmony. Revoice changes only the voiced clip. Chord MIDI export preserves its exact notes/timings and uses ordinary 12-TET channel-1 notes, bypassing melody swing, gate, Fit and MPE. Chord-only audition also uses this exact source and emits chord MIDI; starting it disables Follow DAW, and enabling Follow returns to melody playback.
- **With chords** (`auditionChords`) adds the voiced chord clip to the internal monitor while the melody plays. `chordVoices` is independent of melody `voices`, so same-pitch collisions and melody pitch bends/MPE cannot release or detune accompaniment notes. Outgoing chord notes are a separate option, `chordMidiOut`, off by default: they use MIDI channel 16 and stay off while MPE occupies channels 2–16. Melody and chord file exports remain separate, and **Melody + chords** writes both as MIDI type 1. Disabling internal sound releases the accompaniment, and re-enabling it chases the currently held chord. `audibleChords()` identifies solo chord playback, while `audibleChordLayer()`, `chordVisualState()` and `chordPlayPosition()` also describe combined preview.
- The accompaniment has an independent continuous `accompanimentBeat` clock. It advances while the melody runs even with With chords OFF, and wraps only when scheduling against the chord clip's own length. A short riff looping does not restart a longer progression; enabling the layer mid-bar chases the correct held chord. Playback/source activation resets the local clock, while Follow DAW anchors it to absolute PPQ on start/seek. Chord changes restart layer scheduling at the current harmonic position. Melody notes remain the stored phrase; the accompaniment does not additionally reharmonize them during playback.
- Region operations affect unlocked note onsets in `[start, end)`. They do not apply a blanket microtonal retuning: changed pitches become 12-TET anchors, and retained offsets must remain valid. Combination draws timing, pitches and velocities from selected phrases. Arrangement produces four segments and rejects capacity overflow. Both create drafts requiring acceptance before replacing a bank slot; export uses the displayed draft when present. Groove MIDI is a separate reference.
- `MelodyAdaptation.cpp` implements explicit **Adapt melody / Adapt range** after a harmony change. It uses the actual chord vocabulary (including borrowed/sixth/ninth chords), not a root/scale snap. A bounded pitch-candidate DP minimises original-pitch and interval distortion and includes fixed neighbours/loop closure. Long-note costs integrate the chord timeline, including repeated short harmonies. Short weak passing/neighbor notes may retain scale tones; existing cents survive only as valid passing notes with Micro enabled. A repair pass avoids newly introduced same-key overlaps with protected/held voices. Rhythm, velocity, note count, length, seed, meter/grouping and favourites are copied unchanged; only unlocked onsets in the current scope can change. It acts on the selected slot, including explicitly edited pinned slots, never regenerates the bank, rejects drafts/missing harmony, cancels stale workers, resets A comparison and publishes only changed results as one undo step. A no-op preserves revision/history. Saved clips/MIDI use the adapted pitches. Fit to scale remains an independent live projection; an active Fit produces an explicit status hint for borrowed tones. Melody/harmony length remain independent: harmonic context follows the stored melody onset positions modulo the harmony length.
- A time selection automatically sets a processor-owned, transient audition range tied to one melody slot. A separate spin-lock handoff updates the audio snapshot without incrementing the document revision or invalidating a search. Playback keeps absolute clip coordinates, filters raw onsets to `[start, end)`, caps rendered note lengths and releases voices at the boundary. Follow DAW maps PPQ into the range; chord-only audition uses its whole source. The main evolution action becomes **Optimise range**, and **Each loop** uses the same scope. Both run the same GA with full phrase/harmony context. Before every evaluation it restores protected events from the parent and admits only unlocked onsets inside the range, reserving capacity for protected notes. It commits the best evaluated candidate only to the captured slot, including pinned slots, with the existing cancellation/revision checks and undo history. Double-clicking the ruler, Ctrl/Cmd+A in the roll or **Whole phrase** below it clears the selection. Without a selection, evolution keeps its bank-generation behavior.
- `ComposerTheme` owns native control drawing; `ActivityDisplay` shows emitted MIDI onsets and the actual internal output peak, including release tails. Bounded note-velocity/source-index arrays and onset counters are published through lock-free atomics, with separate chord telemetry. Keys follow emitted pitches; note blocks track source events through transposition, Fit, swing and gate. The editor normally updates at 60 Hz, drops to 30 Hz with Motion OFF or the guide open, and polls at 10 Hz while hidden. Motion is editor-local, not an automation parameter.
- The editor includes a chapter-based Polish/English guide, reference import, scoped edits, draft acceptance and A/B controls. **Microtones** stays visible in the Composition heading with a distinct state indicator; **Tuning / MIDI** contains its palette and encoding controls. Compare/Clear A appear after capture, and Finish capture appears while recording. Export/drag actions use secondary styling. Percentage amount, Apply/Clear retuning and manual cents controls are no longer part of the UI. The additive monitor provides immediate audition sound. VST3/AU use an instrument format with stereo audio and MIDI output for the Studio One instrument workflow.

### Additions in 2.6

`chordLength` is appended: Follow melody / 1 bar / 2 bars / 4 bars / 8 bars, default 4 bars. Root, Scale and Meter remain shared. `Settings::lengthOverride` lets generation from the Chords page cover an exact custom sequence, including fractional bars. Existing automation IDs and state schema 5 remain unchanged. Density is visible again. Search counts and elapsed time remain visible after completion. Space previews the currently visible part.

The GA adds rhythm insertion/deletion/onset shifts/joins/fragment copying, a denser duration target, partial chord conformity on weak beats, and an excess-unison penalty. Two-chord resolved progressions use V–I rather than I–I. See [REFERENCE_AUDIT_2.6.md](REFERENCE_AUDIT_2.6.md) for reference execution and measurements.

### Additions in 2.8

State schema is **6**. Readers still accept schemas 2–5. A missing `auditionReverb` parameter restores as 0 so older projects stay dry; new instances default to 0.15. Grouping is a document property and is also stored on each clip. Schema 2–5 keep the meter’s default grouping.

`metricWeight` scores a bar start as 3, a group start as 2, another grouping-unit grid point as 1, and everything else as 0. Default x/4 grouping is one group per quarter, so integer quarters stay strong. Default x/8 grouping is a 2-and-3 tiling that ends on 3 when the number of eighths is odd (6/8 is 3+3). Evolve copies the parent clip’s grouping.

The audio callback publishes the host time signature and, while **Host** is on, a 30 Hz timer applies it. **Capture phrase** writes note on/off and PPQ into a fixed 65536-event ring with no allocation; finishing an empty capture does not replace the bank. **Each loop** only sets an atomic request on the loop boundary. The same timer calls `generate(true)` with the current Evolution amount and selects the best unpinned slot. Pinned slots are still skipped.

Audition reverb is an 8-line FDN after the internal voice sum and before the output tanh. It is bypassed when the smoothed mix is 0, and it never touches outgoing MIDI. The FDN is prepared in `prepareToPlay`.

Chord text accepts slash bass (`C/E`) and common quality aliases on the existing quality indices, plus `mM7` where the minor-major seventh cannot be mapped. MIDI export writes `RHYTHMS_GROUPS:` at tick 0 and type-6 chord-name markers. Import prefers those markers over inferred harmony, and melody/groove import restores the grouping.

| Parameter ID | Role / values | Default |
| --- | --- | --- |
| `hostMeter` | Apply the host time signature on the message thread | Off |
| `capturePhrase` | Record incoming notes instead of using them as triggers | Off |
| `evolveEachLoop` | Evolve when the playback loop wraps | Off |
| `auditionReverb` | Internal monitor reverb mix, 0–1 | 0.15 (0 when restoring an older session) |

### Additions in 2.7

New parameters are appended after the 2.6 entries. State schema stays **5**. Readers still accept schemas 2–5.

| Parameter ID | Role / values | Default |
| --- | --- | --- |
| `humanize` | Melody timing and velocity variation in preview and export only | 0 |
| `extendedBars` | 0 use phrase length; 1 = 3, 2 = 6, 3 = 12, 4 = 16 bars | 0 |
| `extendedChordLength` | 0 use chord length; 1 = 3, 2 = 6, 3 = 12, 4 = 16 bars | 0 |
| `extendedScale` | 0 use Scale; 1–6 Phrygian, Locrian, melodic minor, whole tone, blues, Phrygian dominant | 0 |
| `chordRhythmShape` | Even / Long-short / Short-long | Even |
| `chordMidiOut` | Chord note-ons on channel 16; forced off in MPE | Off |

The phrase, chord-length and scale menus are not ComboBox attachments, so they do not remap the original automation ranges. Populations of 64 or more run as four islands with one elite each and a deterministic per-island seed. Migration every 10 generations copies a neighbour's scored best onto the worst individual and does not add an evaluation. `composer_probe` compiles `scripts/probe-musical-engine.cpp` against `composer_engine`, is excluded from the default build, and is not a CTest. CTest registers `engine`, `harmony`, `search`, `architecture`, `plugin`, `midi` and `playback`. Either test binary with no argument still runs every suite. The editor language is local to the editor and stored in a properties file, not in the plugin state.

### Parameters and state added in 2.5

New parameters are appended, preserving existing automation IDs, ordering and normalized ranges:

| Parameter ID | Role / values | Default |
| --- | --- | --- |
| `microEnabled` | Sole Micro ON/OFF switch for generation and playback projection | OFF |
| `phraseType` | 0 Riff / 1 Motif / 2 Melody | Riff |
| `auditionChords` | Internal accompaniment during melody playback | OFF |
| `dynamics` | Rhythmic-duration and pitch-range variation, not audio volume | 0.5 |
| `valence` | Consonance preference, register and upward movement | 0.5 |
| `arousal` | Note activity and register/range | 0.5 |
| `weirdness` | Offbeat preference and wider movement | 0.1 |

Diversity retains `diversity`; the UI names Jazziness, Pause amount and Evolution retain `tension`, `space` and `mutation`. Historical `density` and `syncopation` parameters still contribute to the engine's effective targets. Legacy `microtones` remains for state/automation compatibility but is ignored by generation/evolution; the processor's `applyMicrotones()` editing API has been removed. Low-level `retuneRegion`, `tuningScale` and `snapTuned` remain compatibility helpers, not the current generation or performance-snap path. At the processor boundary, `tuningIndex()` is 0 when Micro is OFF and `max(1, raw tuning)` when ON. In the direct engine API, `Settings::tuning == 0` with microtones enabled uses the 24-EDO palette.

State schema **5** stores the new parameters and the existing exact document, including cents and chord voicings. Readers accept schemas **2–5** and validate the complete document before committing it. Restoration stops transient audition/drafts and clears undo history. Migration rules are:

- For schemas 2–4, when `microEnabled` is absent, derive it from the old nonzero `tuning`. An explicit `microEnabled` value wins; missing pre-microtonal tuning stays OFF.
- For schemas before 5 with custom harmony, restore `snap` as OFF to preserve the former audible behavior, which silently bypassed Fit. Sessions without custom harmony retain their saved Fit setting.
- Missing parameters receive defaults instead of retaining values from the previously open session. Schema 3 harmony receives a reconstructed voicing; schemas 4–5 restore exact saved voicings. Schema 2 has no custom harmony.

## Independent microtonal workspace

The third main tab has Melody/Chords views, its own twelve-slot bank, chord sequence, pitch domain, fitness and Undo/Redo. It reuses the conventional rhythm search and shared meter/grouping helpers while evolving microtonal degrees and generating polyphonic chords with separate objectives. Extension version 2 reads version 1 laboratory sessions. See [representation, fitness, transport and validation](MICROTONAL_LAB.md). CTest adds `microtonal_engine` and `microtonal_lab` to the existing suites, covering odd meters, rhythm locks, polyphony with shared MIDI keys, bank pins, chord workflows and isolation from conventional parameters/data.

## Tests

`plugin_tests budget-audit` is an opt-in Release diagnostic for concerns about searches completing too quickly. It measures normal melody, microtonal melody and microtonal chords at 128 x 300, 128 x 600 and 512 x 300, plus a longer phrase. It asserts every generation report and exact fitness-evaluation budgets, and also checks 600 generations through the processor's settings/worker path. Output includes wall time and initial/final fitness. Time itself is never a pass/fail criterion; no artificial delays are inserted. The regular search and microtonal-engine suites also include 300-generation regression checks.

Run the configured suites with:

```sh
ctest --test-dir build -C Release --output-on-failure
```

These are coverage descriptions, not a final 2.5 release-validation record.

- `tests/EngineTests.cpp` and `tests/SearchTests.cpp` cover valid pitches/timing, deterministic seeds, scale snapping across keys, exact locked events, protected rhythm, odd meters and short clips, custom harmony, cancellation, malformed notes, full search budgets, evaluation/crossover counts and elite preservation. Compatibility tests for the legacy tuning helpers remain separate from the current generation contract.
- `tests/MusicalArchitectureTests.cpp` checks cent-aware interval costs and octave-sized leaps, the last-to-first loop interval, changed fitness when only cents change, resolution constraints, microtonal candidates actually reaching evaluation, exact winner fitness after final selection, seed determinism, OFF/lock semantics, and independence from the legacy amount parameter. It also checks repeated Riff cells, Motif return/contrast, Melody contour and the actual influence of Dynamics, Valence, Arousal and Weirdness. Detailed rules are in [GA_ARCHITECTURE.md](GA_ARCHITECTURE.md).
- `tests/ChordGeneratorTests.cpp` checks model provenance and held-out prediction, structural diversity at zero Originality, independent colour/mood/movement trends over shared seeds, all eight control corners across every mode/profile and short form, per-character vocabulary differences, nonmechanical long phrases, seeded determinism, all-key transposition, originality/resolution, meters/capacity, three voicing styles and registers, pitch-class membership, unique tones, 12-TET notes, exact timing, slash names and movement reduction. `python scripts/train-harmony-model.py --check` verifies byte-identical regeneration. `plugin_tests render-harmony <absolute-output-directory>` writes five deterministic WAV/MIDI examples with source attribution and exact settings for listening; it checks non-silence and clipping, not subjective musical quality.
- `tests/PluginTests.cpp` and `tests/WorkflowTests.cpp` cover host-state/undo round trips, atomic invalid-state rejection, MIDI import/export, cross-block note-offs, sample timestamps, short MIDI triggers, host start/stop, custom harmony/groove references, scoped edits, draft preview/discard/accept/export, comparison persistence, arrangement limits and loop-aligned selection/A-B changes.
- `tests/MicrotonalTests.cpp` checks plain channel-1 output, RPN/bends and MPE member allocation, note-off ownership, panic/mode changes, internal oscillator frequency, MIDI round trips, Fit preserving cents without adding new offsets, schema-6 parameters and schema-2–5 migration, a nondestructive Micro toggle, and generation with Micro OFF despite a retained nonzero palette.
- `tests/ChordPluginTests.cpp` checks harmony/voicing undo and exact state restoration, conditioning melody generation on actual generated chords, atomic rejection of incompatible voicings, exact plain chord export despite melody performance/MPE settings, chord-only playback timestamps, and sample-accurate return to melody/MPE. Migration cases preserve historical Fit behavior with custom harmony.
- `tests/PlaybackArchitectureTests.cpp` compares outgoing MIDI byte-for-byte and sample-for-sample with accompaniment ON/OFF. It isolates the rendered chord signal to detect interference from melody bends in mono and MPE modes, including shared MIDI pitches. It checks chord telemetry, mid-bar chase, monitor/With chords toggles, stop/panic/clear/release, host seek/loop, Follow returning from solo chords, and a four-beat progression continuing beneath a one-beat riff. Live Fit is checked with custom harmony and transposition while preserving cents and the bank.
- `tests/ChordPanelTests.cpp` and `tests/UsabilityTests.cpp` exercise native actions, layout/gesture dispatch, Play beneath both rolls, in-editor Melody/Chords navigation without a new desktop window, read-only chord dragging, With chords preview, the explicit Micro switch and removal of blanket retuning actions. They also cover audition sounds, meter export and parameter compatibility; OS/DAW drop acceptance is not simulated.
- `tests/VisualTelemetryTests.cpp` checks emitted velocities and source-note identity through transposition/Fit/retriggers, swing/gate timing, actual audio peak, MIDI visibility with the monitor muted, release tails, short notes, panic, bypass and lifecycle resets.

The plugin test executable accepts optional output paths after running its regressions:

```sh
build/plugin_tests_artefacts/Release/plugin_tests "$PWD/images/ui-v2.png"
```

`argv[1]` writes the actual JUCE editor and sibling captures, including compact layout, embedded Chords, Micro, Search, arrangement and guide pages. Add `argv[2]` for a directory containing 120 editor frames from four seconds of audio processing, suitable for playback at 30 fps. Explicit `refreshDisplay()` supports offscreen capture; the normal timer skips hidden editors.

The optional fourth command-line entry, **`argv[3]`** (the third output path after the executable), calls `renderMusicalExamples` from `tests/RenderMusicalExamples.cpp`:

```sh
build/plugin_tests_artefacts/Release/plugin_tests \
  "$PWD/images/ui-v2.png" "$PWD/build/ui-motion" "$PWD/build/musical-examples"
```

It generates reproducible Riff, Motif and Melody examples with seed 41 over `Cm:4 Fm:4 Ab:4 G7:4`. Each type produces a 48 kHz / 24-bit stereo `*-micro-with-chords.wav`, separate `*-melody-MPE.mid` and plain `*-chords.mid` files, plus printed note/microtone/repetition diagnostics. WAV uses the real internal melody/accompaniment playback path. These are listening and inspection artifacts, not a musical-quality assertion; they are not generated by ordinary CTest invocation.

Before a public release, qualify VST3/AU in each supported host, test routing and native file dragging, run pluginval, build Windows and Intel macOS, and sign/notarize distribution bundles. Local automated checks do not establish every host integration.

## Limits

- Twelve phrase slots, 512 events per slot, 128 quarter-note beats per imported phrase, 24 undo snapshots.
- Twenty-five predefined scale-relative progressions plus Scale only and up to 64 custom chord events. MIDI chord estimation is not a full harmonic analysis system; ambiguous/polyphonic material can need manual correction.
- Generated harmony is limited to 128 quarter-note beats. If the requested harmonic rhythm exceeds 64 events, adjacent steps are grouped without truncating the phrase; the minimum step is 0.125 beats. Each voicing contains three or four unique chord tones, so a generated chord clip uses at most 256 note events. Pentatonic scales use a parent major/minor vocabulary, chromatic uses major, and Saba uses minor for conventional 12-TET chord output.
- Arrangement expands a source fourfold: source length must be at most 32 quarter-note beats and source note count at most 128, with the final result also limited to 512 notes. A/B stores one reference snapshot; draft previews are not serialized.
- Imported polyphony is retained, but the generative model favours melodic lines. Same-pitch retriggers release the previous emitted note before retriggering.
- Internal synthesis is an audition aid, with no effects or patch editing.
- Swing and scale-snap automation affect subsequent note starts; active note-off tracking remains tied to already-emitted pitches.
- The piano roll displays the editable, unswung phrase. MIDI export includes the performance controls.
- Microtonal tunings are 24-EDO approximations. Pitch bend mode shares one bend between overlapping notes and release tails; MPE allows 15 simultaneous bends, after which notes share the longest-idle channel. Within the melody voice pool, a same-key retrigger replaces the previous note even when its cents differ. The independent internal chord layer can sustain the equal-tempered key alongside a microtonal melody note. Imported files without RPN 0 or an MPE configuration message are read at ±2, and only the bend at each onset is used.

## Version 2.0 baseline validation — 2026-10-20

The preceding 2.0 build was validated on macOS / Apple Silicon with Apple Clang 21, CMake 3.31.6 and JUCE 8.0.13. These results do not by themselves validate the new 2.1 workflow implementation:

- Release VST3, AU and Standalone bundles built successfully. Deployment target confirmed as macOS 11.0. Ad-hoc bundle signatures verified with `codesign --verify --deep --strict`.
- CTest engine and processor suites passed, including overlapping triggers, bypass releasing transposed notes, PPQ seeking, worker completion preserving favourites and stale-result rejection after editing.
- Engine suite passed AddressSanitizer and UndefinedBehaviorSanitizer.
- The final VST3 passed **pluginval strictness level 5**. The local transcript is `build/validation/pluginval-macos-arm64.txt`.
- Actual JUCE editor snapshots were inspected at default and compact (870 × 630) sizes: `images/ui-v2.png` and `images/ui-v2-compact.png`.

Live MIDI routing and native dragging inside Studio One/Studio Pro have not yet been manually verified. AU was built and signature-checked, but not separately qualified with auval. Windows and Intel macOS builds have not been executed locally; the CI workflow provides macOS/Windows build and test jobs. Public-release signing/notarization is not included.

## Local validation — 2.1.0 workflow build, 2026-10-20

- Release VST3, AU and Standalone rebuilt on Apple Silicon. All three bundle signatures passed `codesign --verify --deep --strict`.
- Both CTest suites passed, including the new workflow tests for reference imports, document undo/state, scoped edits, non-destructive drafts, exact range export, arrangement limits and absolute harmonic position, and loop-aligned A/B/variation switching in preview and host-follow playback.
- The current engine tests passed AddressSanitizer and UndefinedBehaviorSanitizer after the arrangement harmony fix.
- The final VST3 passed pluginval strictness level 5, including editor creation while processing and parameter automation. Transcript: `build/validation/pluginval-2.1-macos-arm64.txt`. Steinberg's separate VST3 validator was not configured.
- Actual editor snapshots were inspected at 1180 × 900 and 870 × 630, including the workflow tabs, arrangement preview and Polish in-plugin guide. Examples: `images/ui-v2.png`, `images/ui-arrangement.png`, `images/ui-guide-pl.png`, `images/ui-guide-compact.png`.
- Package: `dist/GeneticComposer-2.1.0-macOS-arm64.zip` (VST3, AU, Standalone and Polish manual).

Studio One live routing/native dragging still require manual host qualification. AU was built and signature-checked, not separately validated with auval. Windows/Intel builds and public distribution signing/notarization remain outside these local checks. Chord inference is heuristic; automated tests establish invariants and transport behavior, not musical preference.

## Local validation — 2.2.0 interface build, 2026-10-20

- Release VST3, AU and Standalone rebuilt on Apple Silicon; all three bundle signatures passed `codesign --verify --deep --strict`.
- Both CTest suites passed, including the previous workflow coverage and the new visual-telemetry checks. The musical engine and parameter IDs are unchanged.
- VST3 passed pluginval strictness level 5, including editor creation during processing and editor automation. Transcript: `build/validation/pluginval-2.2-macos-arm64.txt`. The separate Steinberg VST3 validator was not configured.
- Inspected actual native editor captures at default and compact sizes, plus a playback frame showing active note, key, playhead, variation and output indicators. `images/ui-motion.gif` is captured from the actual JUCE editor while processing audio, not a design mockup.
- Package: `dist/GeneticComposer-2.2.0-macOS-arm64.zip` contains VST3, AU, Standalone and the updated Polish manual.

Studio One routing/native dragging, AU validation, Windows/Intel builds and public signing/notarization have not been newly qualified by this UI update.

## Local validation — optional microtonal tunings, 2026-10-21

- Release VST3, AU and Standalone rebuilt on Apple Silicon without compiler warnings; all three bundle signatures passed `codesign --verify --deep --strict`.
- Both CTest suites passed, including the new engine checks and `tests/MicrotonalTests.cpp`. Deliberately breaking one expected MPE bend position made the suite fail, confirming the checks are live.
- With the tuning off, a probe comparing this engine with the previous commit produced byte-identical results for 120 combinations of seed, scale, mode, progression and parent (generation, evolution, all region actions, combination and groove). The earlier processor tests, which check exact MIDI events, pass unchanged.
- Engine tests passed AddressSanitizer and UndefinedBehaviorSanitizer.
- VST3 passed pluginval strictness level 5; its automation tests also randomise the new parameters during processing. Transcript: `build/validation/pluginval-microtonal-macos-arm64.txt`.
- The Micro tab was inspected in a capture of the actual editor at 1180 × 900.

Not yet verified: listening tests with external synths, pitch-bend and MPE routing in Studio One/Studio Pro, and whether Studio One keeps per-channel MPE data when a MIDI file is dragged in. AU was not separately validated with auval.


## Genetic search audit and workflow changes — 2.3.0, 2026-10-22

The original `genetic.cpp::run` uses local constants: population 128, generations 100, crossover 0.9. It recalculates population-dependent fitness in tournament selection and in the final sort comparator. The 2.2 engine was a rewrite: populations 40/72/112 and generations 20/40/72, cached fitness, two elites and pitch crossover; its fitness function is different from the original. Those smaller budgets and the changed scoring implementation explain the short runtime. Version 2.3 does not claim to restore the original engine verbatim.

- Preset budgets are now 64×40, 128×100 and 256×300. Custom budgets accept 16–512 individuals and 1–2000 generations; crossover defaults to 90%. Every requested generation runs unless cancelled. The initial population is also evaluated.
- One-point crossover exchanges phrase sections, including rhythm, on grid boundaries. Locked events, Keep rhythm and Melody on rhythm instead use pitch/velocity crossover to preserve exact timings. Mutation follows crossover. Two unchanged elites preserve the best fitness; tournament size is four.
- Search statistics report actual evaluated generations, population, cumulative evaluations/crossover operations, initial/best/mean fitness and elapsed time. Fitness is cached once per individual per generation, so evaluations equal population × (generations + 1). Crossovers count mating operations, including identical parents; they do not claim every operation changes notes. The final selection prioritises diverse alternatives and may mutate fillers, so not every exported variant has the winning score.
- A local Release probe, four bars, default settings, seed 42 on this Apple Silicon Mac measured: Quick 24.7 ms / 2,624 evaluations; Balanced 103.5 ms / 12,928 evaluations / 11,314 crossovers; Deep 579.2 ms / 77,056 evaluations. Balanced best fitness changed from −0.478083 to 0.385520. These are single-run illustrative timings, not performance guarantees or a direct benchmark against the old implementation.
- The original five meter and five progression choices keep their parameter IDs, order and normalized automation ranges. Additional choices use appended override parameters. Typed meter accepts 1–32 over 1/2/4/8/16/32; new phrase lengths above 128 quarter-note beats are rejected. The catalog has 27 meters and 25 progressions plus Scale only. Harmony parsing still supports named custom chords and durations.
- Audition explicitly enables the monitor, disables Follow DAW and restores a zero monitor level to 65%. Three basic sounds retain the existing microtonal frequency path. Piano-roll dragging routes to the same MIDI file export as the existing Drag MIDI button; simple clicks do not draw. The optional Edit notes switch preserves manual and microtonal editing. Ruler selection and Shift-click locks remain available.
- `SearchTests.cpp` verifies meter parsing/generation limits, every preset's scale degrees and repeats, real generation/evaluation/crossover counts across modes, monotonic elite fitness and cancellation. `UsabilityTests.cpp` checks automation range compatibility, state round trips, typed meter in exported MIDI, audible preview in all three sounds, and gesture dispatch without invoking an OS drag.


Local validation for 2.3.0: Release VST3/AU/Standalone builds and their ad-hoc signatures passed; both CTest suites passed, including legacy-state restoration with absent extension parameters. The engine suite passed AddressSanitizer and UndefinedBehaviorSanitizer. VST3 passed pluginval strictness 5 (`build/validation/pluginval-2.3-macos-arm64.txt`); the separate Steinberg validator was not configured. Actual editor captures were inspected at 1180×900 and 870×630, including Search statistics (`images/ui-search.png`, `images/ui-search-compact.png`). The bundled Polish/English guide includes chapter 10 on genetic search.

Package: `dist/GeneticComposer-2.3.0-macOS-arm64.zip`. Native file dropping and live routing inside Studio One still require manual host qualification; automated gesture tests cover dispatch and exported MIDI, not host acceptance. AU was built and signature-checked, not separately validated with auval. Windows/Intel builds and public-release signing/notarization were not performed for this update.

## Chord composition — 2.4.0, 2026-10-24

`generateHarmony` begins with scale-relative functional templates. Originality increases the probabilities of substitutions, diatonic seventh extensions and borrowed chords; at higher levels, a backwards pass inserts secondary dominants, leading-tone diminished chords or tritone substitutions aimed at the final form of the next chord. The first chord establishes the tonic, and Resolve fixes the final tonic chord. Random decisions use an explicitly converted `mt19937` stream, making generation reproducible for the same settings and seed. The processor supplies a fresh seed for each user request.

`voiceHarmony` enumerates closed inversions and octave placements, or root-position / open drop-2 candidates. Dynamic programming selects a sequence using a movement cost, extra penalties for large leaps and distance from the preferred register. Three-to-four-voice transitions match ordered voices while allowing a seventh to enter or leave. Every chord tone appears once. `voicedChordName` derives the slash bass from the actual exported notes.

Generate chords and Revoice create document undo steps; changing their controls alone does not alter existing notes. Main Root, Scale, Length and Meter supply generation context, without changing Root or the melody bank. Originality/rhythm/resolution are read on Generate chords; voicing/register are read on Generate chords or Revoice. Generate melody invokes the existing genetic worker with its configured budget and the active custom harmony. No epochs, crossover operations or mutation stages were removed by the harmony feature.

The APVTS parameter audit adds these parameters after the existing entries, preserving their IDs, order and ranges:

| Parameter ID | Display name | Range / choices | Default |
| --- | --- | --- | --- |
| `chordOriginality` | Chord originality | 0–1; UI 0–100% | 0.35 |
| `chordRhythm` | Chord rhythm | Two bars / One bar / Half bar | One bar |
| `chordVoicing` | Chord voicing | Smooth inversions / Root position / Open voicings | Smooth inversions |
| `chordCentre` | Chord register | MIDI 36–72, integer | 60 |
| `chordResolve` | Resolve chords to tonic | Off / On | On |

State schema **4** stores the exact chord clip alongside custom harmony, so reloading a session and undoing a chord change restore the original voicings even if the current voicing parameters differ. Readers accept schemas **2**, **3** and **4**. Schema 3's custom harmony receives a reconstructed conventional voicing because that format has no saved chord clip. Missing chord parameters receive their defaults; schema 2 retains its original bank/parameter compatibility. Audition is transient and stops on restore.

`tests/ChordGeneratorTests.cpp` covers deterministic seeds, transposition through all keys, 256 seeds at four originality levels, tonic resolution, every meter preset and short odd signatures, full-length capacity handling, and all three voicing styles at three registers. It checks pitch classes, unique chord tones, conventional tuning, exact note timing, root-position basses, open spans, slash names, repeatable voicing decisions and reduced movement on a test cadence.

Local validation for 2.4.0: Release VST3, AU and Standalone built successfully. Both CTest suites passed (**2/2**), including the new chord engine, processor and panel tests. The engine suite passed with **AddressSanitizer and UndefinedBehaviorSanitizer** (**1/1**). VST3 passed **pluginval strictness 5**, including editor creation during processing and parameter automation; transcript: `build/validation/pluginval-2.4-macos-arm64.txt`. The separate Steinberg validator was not configured.

Processor regressions compare exact note-on/off samples through loop boundaries, voiced MIDI export with melody transforms and MPE enabled, immediate return to a microtonal melody via audition or a timestamped MIDI trigger, schema 4 round-trips, atomic rejection of inconsistent voicings, legacy schema 2/3 restoration, undo/redo, and generation conditioned by the actual generated chords. Panel tests exercise generation, revoicing, audition/stop and read-only piano-roll drag dispatch at 940 × 660 and 760 × 540. Actual captures were inspected at both sizes (`images/ui-chords.png`, `images/ui-chords-compact.png`), alongside the main Harmony tab. The built-in Polish/English guide includes chapter 11 for chord composition.

Native MIDI dropping and live routing inside Studio One still require manual host verification. The gesture tests check dispatch and the exported file, not the host's drop acceptance. AU was built but not separately qualified with auval; Windows/Intel builds and public signing/notarization were not performed.

The three 2.4.0 bundle signatures passed `codesign --verify --deep --strict`. Distribution archive: `dist/GeneticComposer-2.4.0-macOS-arm64.zip` (VST3, AU, Standalone and Polish manual); ZIP CRC and all three embedded bundle versions were verified.

## Musical architecture and integrated playback — 2.5.0, 2026-10-27

- Release VST3, AU and Standalone built on Apple Silicon. All three ad-hoc bundle signatures passed `codesign --verify --deep --strict`.
- Final CTest passed **2/2** in 1.90 seconds. Coverage includes cent-aware genetic scoring and inheritance, Riff/Motif/Melody construction, strict harmony with short non-chord microtonal passing notes, state migration, live Fit, in-editor navigation, independent chord audio and a longer progression continuing beneath a shorter looping riff.
- The final engine passed **AddressSanitizer and UndefinedBehaviorSanitizer**, **1/1**, in 2.01 seconds. A regression covers the actual seed-41 Motif example: correctly resolved passing notes receive partial harmonic credit, and the soft 14% microtonal target measures sounding duration rather than note count. The generated winner still exactly matches its reported fitness.
- The final VST3 passed **pluginval strictness 5**, including audio processing, host-state restoration, editor creation during playback and parameter/editor automation. Transcript: `build/validation/pluginval-2.5-macos-arm64.txt`. The separate Steinberg validator was not configured.
- Actual native editor captures were inspected at default and compact sizes, including Melody, embedded Chords, Micro, Search and the Polish guide. The playback GIF was rebuilt from 120 actual editor frames. The chord cards and timeline now share chord-name spelling.
- Reproducible WAV and separate melody/chord MIDI examples are in `dist/examples-2.5`. With the same seed 41 and harmony, the final Riff has 20 notes / 3 microtonal passing notes, Motif 24 / 6, and Melody 20 / 6. All passing notes satisfy the constraints; these artifacts support listening comparison, not a claim of objectively better music.

The distribution includes VST3, AU, Standalone, the Polish Studio One manual and `GA_ARCHITECTURE.md`. Studio One native MIDI dragging, external pitch-bend/MPE routing and musical listening evaluation still require manual host qualification. AU was built and signature-checked, not separately validated with auval. Windows/Intel builds and public-release signing/notarization were not performed.

Archives: `dist/GeneticComposer-2.5.0-macOS-arm64.zip` and `dist/GeneticComposer-2.5.0-examples.zip`. ZIP integrity checks passed; all three bundled products report 2.5.0, both manuals are present, and the examples contain three non-silent, unclipped WAVs and six MIDI files.


## Reference audit and generation workflow — 2.6.0, completed 2026-10-28

- Release VST3, AU and Standalone built successfully on Apple Silicon. Each bundle reports 2.6.0 and passes `codesign --verify --deep --strict` with its local ad-hoc signature.
- Final CTest passed **2/2** in 3.59 seconds. New cases cover density, rhythmic evolution with protected templates, resolved two-position progressions, full/fractional harmony lengths, all three chord-generation actions and Space on Chords.
- The engine passed **AddressSanitizer and UndefinedBehaviorSanitizer**, **1/1**, in 20.80 seconds.
- **pluginval was not run for 2.6.0.** Its requested unsandboxed launch was rejected by automatic approval review because of an account usage limit. Previous-version passes do not qualify this binary. No alternative launch was used to bypass that rejection.
- The actual final Melody, Chords and Search editor captures were inspected at the compact size; their controls and counters fit. Captures and the playback GIF were regenerated from the native editor. The Polish/English in-plugin guide documents the three generation actions and separate chord length.
- `dist/examples-2.6` contains six WAV previews (three phrase types, Micro OFF and ON) and nine separate MIDI files, generated reproducibly with seed 41. WAV samples were checked for non-silence and clipping; MIDI headers were checked. Functional MIDI round-trip/export coverage remains in the CTest suite. Source: `tests/RenderMusicalExamples.cpp`.
- The unmodified neighbouring Python generators were executed with six fixed cases. Controlled C++ before/after probes cover 72 cases. Raw measurements and caveats are in [REFERENCE_AUDIT_2.6.md](REFERENCE_AUDIT_2.6.md) and `docs/audit-2.6/`. Musical preference still requires listening; this is not a claim of an exact reference port.

Manual qualification inside Studio One (native MIDI dropping and external routing) is still outstanding. AU was built and signature-checked but not run through auval. Windows/Intel builds and public notarization were not performed.

Archives `dist/GeneticComposer-2.6.0-macOS-arm64.zip` and `dist/GeneticComposer-2.6.0-examples.zip` passed ZIP CRC checks. The plugin archive contains all three 2.6.0 products and the Polish manual, GA architecture and reference audit; the example archive contains six WAVs and nine MIDI files.
