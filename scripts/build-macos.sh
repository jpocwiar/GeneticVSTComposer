#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p .deps
if command -v cmake >/dev/null 2>&1; then
    composer_cmake="$(command -v cmake)"
else
    if [[ ! -x .deps/python/cmake/data/bin/cmake ]]; then
        python3 -m pip install --target .deps/python --no-cache-dir cmake==3.31.6
    fi
    composer_cmake="$PWD/.deps/python/cmake/data/bin/cmake"
fi
if [[ ! -f .deps/JUCE-8.0.13/CMakeLists.txt ]]; then
    curl -L --fail --retry 2 https://github.com/juce-framework/JUCE/archive/refs/tags/8.0.13.tar.gz -o .deps/juce.tar.gz
    printf '%s\n' '6aedfcaf2474f6a1d841081f12a21e03035e4c39da5805d0c2b51d05da1373b7  .deps/juce.tar.gz' | shasum -a 256 -c -
    tar -xzf .deps/juce.tar.gz -C .deps
fi
"$composer_cmake" -S . -B build -DCMAKE_BUILD_TYPE=Release -DJUCE_SOURCE_DIR="$PWD/.deps/JUCE-8.0.13" "$@"
"$composer_cmake" --build build --config Release --parallel 6
"$(dirname "$composer_cmake")/ctest" --test-dir build -C Release --output-on-failure
printf '\nBuilt plugins: %s/build/GeneticComposer_artefacts/Release\n' "$PWD"
