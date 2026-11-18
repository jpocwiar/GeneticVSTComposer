#!/usr/bin/env bash
# Validate an exact AU factory without installing it or restarting the AU registrar.
# Usage: scripts/validate-au-local.sh /path/pluginval[.app] /path/Fresh.component
set -euo pipefail

if [[ $# -ne 2 || "$(uname -s)" != Darwin ]]; then
    echo "Usage (macOS): $0 /path/pluginval[.app] /path/Fresh.component" >&2
    exit 2
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
repo_dir="$(cd "$script_dir/.." && pwd -P)"
pluginval_input="$1"
if [[ -d "$pluginval_input" ]]; then
    pluginval_input="$pluginval_input/Contents/MacOS/pluginval"
fi
if [[ ! -x "$pluginval_input" || ! -d "$2" || ! -f "$2/Contents/Info.plist" ]]; then
    echo "A pluginval executable/app and an existing AU component bundle are required." >&2
    exit 2
fi
pluginval_binary="$(cd "$(dirname "$pluginval_input")" && pwd -P)/$(basename "$pluginval_input")"
pluginval_bundle="$(cd "$(dirname "$pluginval_binary")/../.." && pwd -P)"
if [[ ! -f "$pluginval_bundle/Contents/Info.plist" ]]; then
    echo "pluginval must be supplied as its app bundle or the executable inside that bundle." >&2
    exit 2
fi
au_bundle="$(cd "$(dirname "$2")" && pwd -P)/$(basename "$2")"
validation_dir="${AU_VALIDATION_DIR:-$repo_dir/build/validation}"
mkdir -p "$validation_dir"
validation_dir="$(cd "$validation_dir" && pwd -P)"
staging_dir="$(mktemp -d "$validation_dir/au-local.XXXXXX")"
trap 'rm -rf "$staging_dir"' EXIT

shim="$staging_dir/au-local-factory.dylib"
receipt="$staging_dir/registration.txt"
/usr/bin/c++ -std=c++20 -dynamiclib -arch "$(uname -m)" -mmacosx-version-min=11.0 \
    "$script_dir/au-local-factory.cpp" -o "$shim" -framework AudioToolbox -framework CoreFoundation

# Upstream pluginval's hardened signature blocks dyld injection. Sign only this
# disposable copy ad-hoc; neither the original validator nor the AU is changed.
staged_app="$staging_dir/pluginval.app"
/usr/bin/ditto "$pluginval_bundle" "$staged_app"
/usr/bin/codesign --force --deep --sign - "$staged_app"
# Apple's system auval subprocess strips dyld injection under SIP and would test
# the installed AU, not this process-local factory. Do not count that result.
disabled_tests="$staging_dir/disabled-tests.txt"
printf 'auval\n' > "$disabled_tests"
echo "The external system auval subtest is disabled: it cannot select this process-local factory." >&2
status=0
AU_VALIDATE_BUNDLE="$au_bundle" AU_VALIDATE_RECEIPT="$receipt" DYLD_INSERT_LIBRARIES="$shim" \
    "$staged_app/Contents/MacOS/$(basename "$pluginval_binary")" \
    --strictness-level 10 --disabled-tests "$disabled_tests" --validate "$au_bundle" || status=$?

if [[ ! -s "$receipt" ]]; then
    echo "AU validation did not load the exact factory; refusing a possibly stale registry result." >&2
    exit 1
fi
exit "$status"
