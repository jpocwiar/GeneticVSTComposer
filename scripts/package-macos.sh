#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
composer_products="$PWD/build/GeneticComposer_artefacts/Release"
composer_version="$(sed -nE 's/^project\(GeneticComposer VERSION ([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' CMakeLists.txt)"
if [[ ! "$composer_version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    printf 'Cannot read the project version from CMakeLists.txt.\n' >&2
    exit 1
fi
if [[ "${1:-}" == "--dry-run" ]]; then
    printf 'Version: %s\nProducts: %s\nPackage: Genetic Composer %s\n' "$composer_version" "$composer_products" "$composer_version"
    exit 0
fi
# Packaging must use binaries from these sources, even when the version number
# has not changed since a previous build. This also runs the current test suite.
./scripts/build-macos.sh
mkdir -p build/package dist
composer_staging="$(mktemp -d "$PWD/build/package/stage.XXXXXX")"
trap 'rm -rf "$composer_staging"' EXIT
composer_package="$composer_staging/Genetic Composer $composer_version"
mkdir -p "$composer_package"
for composer_format in VST3 AU Standalone; do
    if [[ ! -d "$composer_products/$composer_format" ]]; then
        printf 'Missing %s build. Run scripts/build-macos.sh first.\n' "$composer_format" >&2
        exit 1
    fi
    case "$composer_format" in
        VST3) composer_bundle='Genetic Composer.vst3' ;;
        AU) composer_bundle='Genetic Composer.component' ;;
        Standalone) composer_bundle='Genetic Composer.app' ;;
    esac
    composer_built_version="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$composer_products/$composer_format/$composer_bundle/Contents/Info.plist")"
    if [[ "$composer_built_version" != "$composer_version" ]]; then
        printf '%s has version %s, expected %s. Run scripts/build-macos.sh first.\n' "$composer_format" "$composer_built_version" "$composer_version" >&2
        exit 1
    fi
    ditto "$composer_products/$composer_format" "$composer_package/$composer_format"
done
printf 'GENETIC COMPOSER %s — macOS development build\n\n' "$composer_version" > "$composer_package/Read Me.txt"
cat >> "$composer_package/Read Me.txt" <<'TEXT'
VST3: Copy Genetic Composer.vst3 to ~/Library/Audio/Plug-Ins/VST3/
AU: Copy Genetic Composer.component to ~/Library/Audio/Plug-Ins/Components/
Standalone: Open Genetic Composer.app directly.

Studio One: rescan plugins, then load Genetic Composer from Instruments.
Melody and Chords are two main tabs inside the same interface. Both piano rolls
have a triangular Play button directly below them; Stop ends preview. Choose
Soft keys, Sine or Warm organ in the bottom toolbar. Edit notes is optional.

On Melody choose Phrase type: Riff for a repeated core, Motif for a recognisable
idea with variations, or Melody for a freer developing line. Generate melody bank makes
12 alternatives. The musical controls include Diversity, Melody motion (the Dynamics
automation parameter), Jazziness, Valence, Arousal, Weirdness and Pause Amount.
Density directly controls note activity. Evolution controls changes from a parent phrase. Their scoring belongs to the
new engine rather than reproducing the original fitness weights exactly.

Fit to scale is off by default. When enabled it quantises melody pitches to Scale
live during playback and export, after transposition, without rewriting the bank.
It preserves genetic microtonal offsets and remains available with custom harmony;
turn it off to retain borrowed/chromatic pitches exactly as generated.

On Chords, Originality shapes the harmonic sequence. Choose harmonic rhythm and
resolution and the separate Length (default 4 bars). New chords only keeps melodies.
Melody from these chords keeps harmony and generates a bank over its entire length.
New chords + melody creates both. The latter two enable With chords and return to Melody. Revoice applies inversions and register changes.
Play below the chord roll previews the chords alone. With chords + Play on Melody,
or Play together on Chords, previews both parts through independent internal voices.
Both layers use the selected sound preset; the chord voices remain independently tuned.
Enable Chord MIDI to send the accompaniment on channel 16 as well as the melody.
Internal sound can be off when routing both layers to external instruments.
MPE uses channels 2–16, so Chord MIDI is unavailable while the melody uses MPE.
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
REFERENCE_AUDIT_2.6.md explains the comparison with the original Python generator.
Use Edit region for local changes, Capture A for comparison, Chord text/Groove for references,
and Arrange/Combine for drafts. Keep preview commits a draft; Discard leaves the bank
unchanged. Unaccepted drafts are not saved with the project. Switch at loop changes
variations and A/B at the next loop boundary. Meter accepts typed signatures such as 11/8.
Search shows actual generations, fitness and crossover counts; custom budgets are available.
Disable Internal sound when routing melody MIDI to another instrument.

This is a local development build, not a notarized public installer.
Build provenance and binary checksums are recorded in build-info.txt.
Studio One drag/drop and routing still need testing with the packaged binaries.
See the source repository README and docs/DEVELOPMENT.md for build and validation details.
TEXT
cp docs/STUDIO_ONE_PL.md "$composer_package/Studio One - instrukcja PL.md"
cp docs/REFERENCE_AUDIT_2.6.md "$composer_package/REFERENCE_AUDIT_2.6.md"
cp docs/GA_ARCHITECTURE.md "$composer_package/GA_ARCHITECTURE.md"
cp README.md "$composer_package/README.md"
cp docs/COMPOSING.md docs/AI_MIDI.md "$composer_package/"
composer_binary="$composer_package/VST3/Genetic Composer.vst3/Contents/MacOS/Genetic Composer"
composer_arch="$(lipo -archs "$composer_binary")"
if [[ "$composer_arch" == *arm64* && "$composer_arch" == *x86_64* ]]; then
    composer_arch=universal
else
    composer_arch="${composer_arch// /-}"
fi
{
    printf 'Version: %s\nArchitecture: %s\n' "$composer_version" "$composer_arch"
    printf 'Source revision: %s\n' "$(git rev-parse HEAD)"
    if [[ -n "$(git status --porcelain)" ]]; then printf 'Working tree: modified\n'; else printf 'Working tree: clean\n'; fi
    (cd "$composer_package"; find VST3 AU Standalone -type f -path '*/Contents/MacOS/*' -exec shasum -a 256 {} \;)
} > "$composer_package/build-info.txt"
composer_archive="dist/GeneticComposer-$composer_version-macOS-$composer_arch.zip"
ditto -c -k --sequesterRsrc --keepParent "$composer_package" "$composer_archive"
printf 'Package: %s/%s\n' "$PWD" "$composer_archive"
