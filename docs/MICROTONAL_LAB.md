# Independent microtonal laboratory

`MicrotonalEngine.{h,cpp}` is JUCE-independent and does not call `composer::generate`, `scaleIntervals`, `snapPitch`, `fitnessScore`, the harmonic prior or the chord generator. It defines separate `Tuning`, `Settings`, `Event`, `Clip`, `Score` and `Stats` types in namespace `microtonal`. The conventional engine and its optional microtonal passing-note rules are unchanged.

## Pitch representation

Events store integer **degrees**, quarter-note timing and velocity. An EDO tuning stores its division count `N`, with `cents(d) = 1200*d/N` and `Hz(d) = baseHz*2^(d/N)`. A rational tuning stores ordered, reduced positive fractions in `[1, 2)`, beginning at `1/1`. For `d = octave*N + index`, `Hz(d) = baseHz*2^octave*p[index]/q[index]`. Rational steps have unequal sizes; interval scoring uses the actual frequency ratio between endpoints, rather than their degree difference alone.

Each clip owns its tuning, base frequency and duration. Workspace settings configure the next search. Changing a tuning control never changes an already generated clip. The roll uses degree lanes and inspectors show EDO step notation or the exact fraction and octave, plus cents and Hz. Conventional MIDI note names do not enter this model.

## Objective

The objective combines:

- **Ratio affinity:** 75% consecutive-interval affinity (including last-to-first) and 25% affinity relative to the base. For an octave-reduced interval, search reduced fractions `p/q` with `q <= 16` and `q <= p <= 2*q`. A candidate's affinity is `exp(-0.5*(errorCents/35)^2) / (1 + log2(p*q))`; keep the largest affinity. Error is the shortest signed distance modulo 1200 cents, making the unison/octave boundary continuous. The denominator is a complexity penalty inspired by Tenney height, not a probability of sounding pleasant. EDO notes stay exactly on their grid; ratio approximations are scores and annotations, never a retuning pass.
- **Variety:** closeness of distinct degree classes to a target between two and the available distinct classes, controlled by Diversity.
- **Movement:** closeness of mean actual interval size, including the loop, to `0.04 + 0.65*motion` octaves, with extreme jumps capped at two octaves in this mean.
- **Closure:** affinity between the final and first pitches, plus a preference for ending on the base's octave class.
- **Repetition penalty:** the share of adjacent equal degrees, including the loop.

The total is `60*consonance*affinity + 30*variety + 25*movement + 10*closure - 22*repetitionShare`. Scores are internal and are not calibrated across EDO systems. Affinity alone prefers unisons; the other terms counter this degeneration. The fixed 35-cent tolerance is an authored search preference.

Simple ratios are a useful starting point for harmonic spectra, but neither ratio simplicity nor this objective is a universal measure of beauty. Timbre, roughness, musical exposure and context matter. [Harrison et al., Nature Communications (2024)](https://www.nature.com/articles/s41467-024-45812-z) demonstrates effects of timbre on consonance and scale preferences. The current monitor uses a harmonic tone; the objective does not model a user's external synthesizer or perform a full psychoacoustic roughness analysis.

## Search

The independent GA uses a seeded Mersenne Twister, tournament selection, degree crossover, bounded mutation and one unchanged elite. The initial population is scored, then each generation evaluates `population - 1` new individuals: total `population + generations*(population - 1)`. Final output is the evaluated elite with its stored score. The search runs on the processor-owned search thread; only one search runs at a time. Cancellation returns no replacement sequence. A separate workspace revision prevents stale microtonal results from overwriting a new setting or restored session.

Fresh generation places monophonic events on an eighth-note grid with density-controlled omissions and short gaps. Evolution keeps the source timing and changes degrees/velocities. Bounds: 2-72 divisions or rational degrees per octave, 1-3 octaves, 20-1000 Hz base, 1-64 quarter-note beats, 128 events, population 8-256 and 1-1000 generations. This workspace currently generates monophonic sequences; it does not generate polyphonic microtonal chord progressions.

## Playback, export and persistence

`ProcessorMicrotonal.cpp` owns the independent playback path, transport, export and state conversion. The message thread publishes fixed-size snapshots under the existing short exchange lock. The audio thread only tries that lock and retains a complete previous snapshot if busy. Ratio matching and fitness never execute on the audio thread. Existing internal voice synthesis and MIDI channel ownership are reused through `noteOnTuned`, accepting a double-precision cent offset. The original melody call passes its original stored cents through that same function.

At the output boundary only, Hz is encoded as a nearest MIDI key plus a double-precision bend calculation. Channel-1 pitch bend uses +/-2 semitones; MPE uses member channels 2-16 at +/-48 semitones. Approximation is limited to the 14-bit MIDI bend, roughly 0.0244 cents per step in mono and 0.5859 cents per step in MPE. Internal playback retains the exact frequency. Export sends the same setup and notes, a tuning annotation and bend/MPE cleanup after the final note-off. No conventional swing, gate, scale projection or chord layer is applied.

The optional `MicrotonalWorkspace` child has its own extension version **1** inside the existing schema-6 document. It stores workspace settings and a separately tuned sequence. All validation precedes the existing atomic state commit. Missing extension data restores defaults and an empty sequence. Restoration cancels pending searches, stops audition and invalidates stale results. Conventional undo history continues to contain only the original melody/chord document.

## Validation

`microtonal_engine_tests` covers 2/7/8/19/31/72-EDO, rational parsing and exact frequencies, nonuniform rational interval scoring, seeded determinism, actual evaluation budgets, elite preservation, winner-score consistency, invalid input and cancellation. `plugin_tests microtonal-lab` covers conventional document/parameter isolation, worker evolution, DAW state and legacy restoration, atomic invalid-state rejection, precise live/exported MIDI, internal oscillator pitch, MPE release on mode changes, host start/seek/stop, zero audio-thread allocations and embedded controls at both supported editor sizes. Pass an absolute directory after `microtonal-lab` to save native editor screenshots there.
