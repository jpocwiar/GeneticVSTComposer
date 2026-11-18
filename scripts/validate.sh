#!/usr/bin/env bash
# Builds and validates current Release VST3/AU with pluginval, without installation.
# pluginval is downloaded into .tools/ on first use.
set -euo pipefail

cd "$(dirname "$0")/.."

PLUGINVAL_VERSION="v1.0.4"
TOOLS=".tools"
PLUGINVAL="$TOOLS/pluginval.app/Contents/MacOS/pluginval"
VST3="build/GeneticComposer_artefacts/Release/VST3/Genetic Composer.vst3"
AU="build/GeneticComposer_artefacts/Release/AU/Genetic Composer.component"

if [[ ! -x "$PLUGINVAL" ]]; then
    mkdir -p "$TOOLS"
    curl -sSL -o "$TOOLS/pluginval.zip" \
        "https://github.com/Tracktion/pluginval/releases/download/$PLUGINVAL_VERSION/pluginval_macOS.zip"
    unzip -q -o "$TOOLS/pluginval.zip" -d "$TOOLS"
    rm "$TOOLS/pluginval.zip"
fi

# Always rebuild changed sources. Existing bundle directories do not establish
# that the binaries match the working tree being validated.
./scripts/build-macos.sh

echo "== pluginval VST3"
"$PLUGINVAL" --strictness-level 10 --validate "$VST3"
echo "== pluginval exact AU factory (process-local registration)"
scripts/validate-au-local.sh "$PLUGINVAL" "$AU"

echo "== Native auval requires an installed AU; skipped (this script does not install plugins)."
