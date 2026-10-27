#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
composer_products="$PWD/build/GeneticComposer_artefacts/Release"
composer_package="$PWD/build/package/Genetic Composer 2.5"
mkdir -p "$composer_package" dist
for composer_format in VST3 AU Standalone; do
    if [[ ! -d "$composer_products/$composer_format" ]]; then
        printf 'Missing %s build. Run scripts/build-macos.sh first.\n' "$composer_format" >&2
        exit 1
    fi
    ditto "$composer_products/$composer_format" "$composer_package/$composer_format"
done
cat > "$composer_package/Read Me.txt" <<'TEXT'
GENETIC COMPOSER 2.5.0 — macOS development build

VST3: Copy Genetic Composer.vst3 to ~/Library/Audio/Plug-Ins/VST3/
AU: Copy Genetic Composer.component to ~/Library/Audio/Plug-Ins/Components/
Standalone: Open Genetic Composer.app directly.

Studio One: rescan plugins, then load Genetic Composer from Instruments.
Melody and Chords are two main tabs inside the same interface. Both piano rolls
have a triangular Play button directly below them; Stop ends preview. Choose
Soft keys, Sine or Warm organ in the bottom toolbar. Edit notes is optional.

On Melody choose Phrase type: Riff for a repeated core, Motif for a recognisable
idea with variations, or Melody for a freer developing line. Generate bank makes
12 alternatives. The original musical controls are Diversity, Dynamics (melodic
motion, not loudness), Jazziness, Valence, Arousal, Weirdness and Pause Amount.
Evolution controls changes from a parent phrase. Their scoring belongs to the
new engine rather than reproducing the original fitness weights exactly.

Fit to scale is off by default. When enabled it quantises melody pitches to Scale
live during playback and export, after transposition, without rewriting the bank.
It preserves genetic microtonal offsets and remains available with custom harmony;
turn it off to retain borrowed/chromatic pitches exactly as generated.

On Chords, Originality shapes the harmonic sequence. Choose harmonic rhythm and
resolution, then Generate chords. Generate melody starts the genetic search over
that harmony and returns to Melody. Revoice applies inversions and register changes.
Play below the chord roll previews the chords alone. With chords + Play on Melody,
or Play together on Chords, previews both parts through independent internal voices.
Both layers use the selected sound preset; the chord voices remain independently tuned.
This accompaniment is internal audio only: outgoing MIDI still carries the melody.
Drag melody MIDI from Melody and voiced chord MIDI from Chords to separate tracks.
Chords stay in standard 12-TET; melody pitch bend/MPE cannot detune their internal layer.

Micro has an explicit on/off switch. With it on, the genetic search chooses short
microtonal passing notes and their resolutions. Its colour selector does not replace
Scale. There is no amount slider or blanket retuning operation. Internal playback
previews the exact pitches; external instruments need matching pitch-bend/MPE settings.

Shift-click notes to lock them before evolving. Pin (+) variations to keep them.
Save bank and DAW projects preserve the bank, harmony, exact chord voicings and settings.
The Motion switch reduces animation; the bottom MIDI/OUT panel shows actual activity.
The ? Help button opens the built-in Polish/English guide. The Polish manual and
GA_ARCHITECTURE.md description of the genetic search are included.
Use Edit region for local changes, Capture A for comparison, Harmony/Groove for references,
and Arrange/Combine for drafts. Keep preview commits a draft; Discard leaves the bank
unchanged. Unaccepted drafts are not saved with the project. Switch at loop changes
variations and A/B at the next loop boundary. Meter accepts typed signatures such as 11/8.
Search shows actual generations, fitness and crossover counts; custom budgets are available.
Disable Internal sound when routing melody MIDI to another instrument.

This is a local development build, not a notarized public installer.
See the source repository README and docs/DEVELOPMENT.md for build and validation details.
TEXT
cp docs/STUDIO_ONE_PL.md "$composer_package/Studio One - instrukcja PL.md"
cp docs/GA_ARCHITECTURE.md "$composer_package/GA_ARCHITECTURE.md"
composer_arch="$(uname -m)"
ditto -c -k --sequesterRsrc --keepParent "$composer_package" "dist/GeneticComposer-2.5.0-macOS-$composer_arch.zip"
printf 'Package: %s/dist/GeneticComposer-2.5.0-macOS-%s.zip\n' "$PWD" "$composer_arch"
