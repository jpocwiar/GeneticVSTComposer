# Windows counterpart of scripts/build-macos.sh: fetches the pinned JUCE 8.0.13 into .deps,
# builds VST3 + Standalone in Release with MSVC and Ninja and runs the tests.
# Extra arguments go to the CMake configure step.
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path -Parent $PSScriptRoot)
New-Item -ItemType Directory -Force -Path .deps | Out-Null

# Enter the MSVC x64 environment unless the shell already has it (Developer PowerShell).
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    $vswhere = Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio\Installer\vswhere.exe'
    $vs = & $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) { Write-Error 'Visual Studio 2022 with "Desktop development with C++" not found.' }
    Import-Module (Join-Path $vs 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
    Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
}
if (-not (Test-Path .deps\JUCE-8.0.13\CMakeLists.txt)) {
    Invoke-WebRequest -UseBasicParsing https://github.com/juce-framework/JUCE/archive/refs/tags/8.0.13.tar.gz -OutFile .deps\juce.tar.gz
    $hash = (Get-FileHash -Algorithm SHA256 .deps\juce.tar.gz).Hash
    if ($hash -ne '6aedfcaf2474f6a1d841081f12a21e03035e4c39da5805d0c2b51d05da1373b7') {
        Write-Error "JUCE archive checksum mismatch: $hash"
    }
    tar -xzf .deps\juce.tar.gz -C .deps
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
}

$juce = (Resolve-Path .deps\JUCE-8.0.13).Path -replace '\\', '/'
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release "-DJUCE_SOURCE_DIR=$juce" @args
if ($LASTEXITCODE) { exit $LASTEXITCODE }
cmake --build build --parallel 6
if ($LASTEXITCODE) { exit $LASTEXITCODE }
ctest --test-dir build -C Release --output-on-failure
if ($LASTEXITCODE) { exit $LASTEXITCODE }
Write-Host "`nBuilt plugins: $PWD\build\GeneticComposer_artefacts\Release"
