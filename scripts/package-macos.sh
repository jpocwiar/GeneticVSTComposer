#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
composer_products="$PWD/build/GeneticComposer_artefacts/Release"
composer_package="$PWD/build/package/Genetic Composer 2.1"
mkdir -p "$composer_package" dist
for composer_format in VST3 AU Standalone; do
    if [[ ! -d "$composer_products/$composer_format" ]]; then
        printf 'Missing %s build. Run scripts/build-macos.sh first.\n' "$composer_format" >&2
        exit 1
    fi
    ditto "$composer_products/$composer_format" "$composer_package/$composer_format"
done
cat > "$composer_package/Read Me.txt" <<'TEXT'
GENETIC COMPOSER 2.1.0 — macOS development build

VST3: Copy Genetic Composer.vst3 to ~/Library/Audio/Plug-Ins/VST3/
AU: Copy Genetic Composer.component to ~/Library/Audio/Plug-Ins/Components/
Standalone: Open Genetic Composer.app directly.

Studio One: rescan plugins, then load Genetic Composer from Instruments.
Double-click a variation to hear it, or generate a new bank. Drag MIDI to an
instrument track to use your own sound. Disable Internal sound for live MIDI routing.

Shift-click notes to lock them before evolving. Pin (+) variations to keep them.
Save bank stores all phrases and settings. The DAW project also saves them.
The ? Help button opens the built-in Polish/English guide with workflow chapters.
Use Region to edit part of a phrase, Capture A for comparison, Harmony and Groove
for separate references, and Arrange/Combine to preview ideas before keeping them.
Switch at loop changes variations and A/B at the next loop boundary.
Keep preview commits a draft. Discard leaves your bank unchanged. Unaccepted
drafts are not saved with the project. The Polish manual is included in this folder.

This is a local development build, not a notarized public installer.
See the source repository README and docs/DEVELOPMENT.md for build and validation details.
TEXT
cp docs/STUDIO_ONE_PL.md "$composer_package/Studio One - instrukcja PL.md"
composer_arch="$(uname -m)"
ditto -c -k --sequesterRsrc --keepParent "$composer_package" "dist/GeneticComposer-2.1.0-macOS-$composer_arch.zip"
printf 'Package: %s/dist/GeneticComposer-2.1.0-macOS-%s.zip\n' "$PWD" "$composer_arch"
