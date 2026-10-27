#!/bin/bash
# Installs the current Release build for this user, replacing any previous copy.
# Usage: scripts/install-macos.sh [--build] [--au]
#   --build  run scripts/build-macos.sh first
#   --au     also install the Audio Unit
set -euo pipefail
cd "$(dirname "$0")/.."
composer_build=0
composer_au=0
for composer_arg in "$@"; do
    case "$composer_arg" in
    --build) composer_build=1 ;;
    --au) composer_au=1 ;;
    *)
        printf 'Unknown option: %s\nUsage: scripts/install-macos.sh [--build] [--au]\n' "$composer_arg" >&2
        exit 1
        ;;
    esac
done
if [[ $composer_build == 1 ]]; then
    ./scripts/build-macos.sh
fi
composer_products="$PWD/build/GeneticComposer_artefacts/Release"
composer_install() {
    local source="$composer_products/$1/$2" target="$HOME/Library/Audio/Plug-Ins/$3"
    if [[ ! -d "$source" ]]; then
        printf 'Missing %s. Run scripts/build-macos.sh first, or use --build.\n' "$source" >&2
        exit 1
    fi
    mkdir -p "$target"
    # Remove the old bundle first so files deleted from a new build do not linger.
    rm -rf "$target/$2"
    ditto "$source" "$target/$2"
    printf 'Installed %s\n' "$target/$2"
}
composer_install VST3 "Genetic Composer.vst3" VST3
if [[ $composer_au == 1 ]]; then
    composer_install AU "Genetic Composer.component" Components
fi
printf 'Restart Studio One or rescan plug-ins, then load Genetic Composer from Instruments.\n'
