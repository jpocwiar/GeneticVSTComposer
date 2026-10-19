# Development

## Structure

- `ComposerEngine.{h,cpp}` is a JUCE-independent, deterministic genetic search. The old `genetic`, `mingus` and `notes_generator` files remain as research/reference sources and are not compiled by CMake.
- `Clip` holds bounded, sorted note events in musical quarter-note units. Each note carries pitch, velocity, duration and a lock flag. Each of twelve slots also carries phrase length, meter, grid, favourite status and seed.
- The processor owns the document, bounded undo history and APVTS parameters. Document snapshots include the bank, custom harmony and groove reference. The captured A comparison is persistent but separate from undo history; an unaccepted arrangement/combination draft is transient. Closing the editor cannot reset a session or abort a worker by destroying UI-owned data.
- Generation runs on a joined, cancellable worker. Each generation caches fitness scores before selection. Elitism preserves the best candidates and the final selection penalises near-duplicates. Results replace only unpinned slots. A document revision check prevents an old worker overwriting newer edits or restored state.
- UI/worker writes publish a fixed-size bank through a short spin-lock handoff. The audio callback uses `ScopedTryLockType`; if the publisher is busy it keeps the previous complete bank. No UI/document mutex is acquired by the callback.
- Playback tracks note lifetime across blocks. MIDI input is handled at sample offsets, not collapsed to a final block-wide trigger state. Note offs use the actual emitted pitches. Selection/A-B requests can wait until the current loop wraps, then release active notes and begin the replacement phrase at that boundary. Editing selection and audible selection are tracked separately. MIDI key triggers remain immediate. PPQ drives host-follow phase and detects discontinuities; a free-running preview uses host tempo when available, or the preview tempo otherwise.
- Export and playback use the same swing, gate and snapping transformation. Custom harmony bypasses the global scale snap so it cannot erase chord tones outside the selected scale. Imported material is not quantized; subsequent editing snaps to the clip grid.
- Custom chord events use quarter-note start/duration and root/quality values. Chord text is parsed before committing; a failed parse leaves the prior harmony intact. MIDI chord estimation is heuristic and its editable result is exposed to the user. Groove MIDI is held as a separate reference.
- Region operations affect unlocked note onsets in the half-open interval `[start, end)`. Combination draws timing from one phrase and maps pitch/velocity traits from other phrases. Arrangement produces four segments; capacity overflow is rejected. Both workflows require explicit acceptance before replacing a bank slot, and export uses the displayed draft when one exists.
- The native editor includes a chapter-based Polish/English guide. Reference imports, range selection, draft acceptance and A/B state are available directly from the workflow controls.
- `ComposerTheme` owns the native control drawing. `ActivityDisplay` renders emitted MIDI onsets and the internal audio peak, including release tails. The audio callback publishes bounded arrays of output velocities, source-note indices and counters through lock-free atomics; the editor does not access audio-owned voices. Keys highlight the output pitches; note blocks track their source events even when transposition, scale snap, swing or gate changes playback. The editor updates at 60 Hz, drops to 30 Hz with Motion off or the guide open, and polls at 10 Hz while hidden. Motion is an editor-local preference; it does not add an automation parameter.
- A lightweight additive monitor provides immediate sound. Disable it for MIDI-only use. This is intentionally an instrument-format VST3/AU with stereo audio and MIDI output to fit Studio One's instrument workflow.

## Tests

`ctest --test-dir build -C Release --output-on-failure`

Engine coverage: valid pitches/timing, deterministic seeds, scale snapping across all keys, alternative diversity, exact locked-event preservation, rhythm preservation, odd meter, very short clips, cancellation and malformed notes.

Processor coverage: parameters/bank/locks/favourites round trip through host state, undo/redo, invalid-state rejection, MIDI export/import, note offs crossing callback boundaries, timestamps restricted to the current callback, audible monitor, a trigger pressed and released within the same callback, and host start/stop flushing.

`tests/WorkflowTests.cpp` adds document-state checks for custom harmony and groove references, independent reference imports, preservation of events outside a selected region, draft preview/discard/accept semantics, draft export and exclusion from serialized state, comparison-snapshot persistence, range export with clipped notes and rebased timing, arrangement segment/capacity checks, and sample-position checks for loop-aligned selection and A/B changes with both preview and host-follow transport.

`build/plugin_tests_artefacts/Release/plugin_tests images/ui-v2.png` also renders the actual JUCE editor to a PNG. This tests the real layout rather than an unrelated HTML mockup.

`tests/VisualTelemetryTests.cpp` checks emitted pitches and velocities, source-note identity through transposition/scale snapping/retriggers, swing/gate timing, audio peak measurement, MIDI activity with the monitor muted, release tails, short notes, panic, bypass and lifecycle resets. To capture a four-second sequence of the actual editor during audio processing, add a second output path: `build/plugin_tests_artefacts/Release/plugin_tests "$PWD/images/ui-v2.png" "$PWD/build/ui-motion"`. This produces 120 PNG frames for playback at 30 fps. Explicit `refreshDisplay()` updates support offscreen rendering; the normal timer skips hidden editors.

Before a public release, qualify VST3/AU in each supported host, test routing and native file dragging, run pluginval, build Windows and Intel macOS, and sign/notarize distribution bundles. The local automated checks do not establish every host integration.

## Limits

- Twelve phrase slots, 512 events per slot, 128 quarter-note beats per imported phrase, 24 undo snapshots.
- Four predefined scale-relative progressions plus up to 64 custom chord events. MIDI chord estimation is not a full harmonic analysis system; ambiguous/polyphonic material can need manual correction.
- Arrangement expands a source fourfold: source length must be at most 32 quarter-note beats and source note count at most 128, with the final result also limited to 512 notes. A/B stores one reference snapshot; draft previews are not serialized.
- Imported polyphony is retained, but the generative model favours melodic lines. Same-pitch retriggers release the previous emitted note before retriggering.
- Internal synthesis is an audition aid, with no effects or patch editing.
- Swing and scale-snap automation affect subsequent note starts; active note-off tracking remains tied to already-emitted pitches.
- The piano roll displays the editable, unswung phrase. MIDI export includes the performance controls.

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
