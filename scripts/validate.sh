#!/usr/bin/env bash
# Validates Release VST3 and AU with pluginval (strictness 10) and auval.
# pluginval is downloaded into .tools/ on first use. Builds when artefacts are missing.
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

if [[ ! -d "$VST3" || ! -d "$AU" ]]; then
    ./scripts/build-macos.sh
fi

for plugin in "$VST3" "$AU"; do
    echo "== pluginval $(basename "$plugin")"
    "$PLUGINVAL" --strictness-level 10 --validate "$plugin"
done

echo "== auval"
killall -9 AudioComponentRegistrar 2>/dev/null || true
auval -strict -v aumu Gnc2 Gnco
