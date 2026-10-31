# Windows counterpart of scripts/install-macos.sh: installs the current Release VST3 build,
# replacing any previous copy.
# Usage: scripts\install-windows.ps1 [-Build] [-User]
#   -Build  run scripts\build-windows.ps1 first
#   -User   install into %LOCALAPPDATA%\Programs\Common\VST3 (no admin; not every DAW scans it)
#           instead of C:\Program Files\Common Files\VST3 (asks for admin rights)
param([switch]$Build, [switch]$User)
$ErrorActionPreference = 'Stop'

$Name   = 'Genetic Composer.vst3'
$Root   = Split-Path -Parent $PSScriptRoot
$Source = Join-Path $Root "build\GeneticComposer_artefacts\Release\VST3\$Name"
$Target = if ($User) { Join-Path $env:LOCALAPPDATA 'Programs\Common\VST3' }
          else { Join-Path $(if ($env:CommonProgramW6432) { $env:CommonProgramW6432 } else { $env:CommonProgramFiles }) 'VST3' }

if ($Build) {
    & (Join-Path $PSScriptRoot 'build-windows.ps1')
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
}

if (-not (Test-Path $Source)) {
    Write-Error "Missing $Source. Run scripts\build-windows.ps1 first, or use -Build."
}

$IsAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)

if (-not $User -and -not $IsAdmin) {
    Write-Host "Writing to $Target needs administrator rights; confirm the UAC prompt."
    $p = Start-Process powershell.exe -Verb RunAs -Wait -PassThru -WindowStyle Hidden -ArgumentList @(
        '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$PSCommandPath`"")
    if ($p.ExitCode) { Write-Error "Elevated install failed (exit code $($p.ExitCode))." }
} else {
    New-Item -ItemType Directory -Force -Path $Target | Out-Null
    # Remove the old bundle first so files deleted from a new build do not linger.
    # Fails if a running DAW still has the plugin loaded.
    if (Test-Path (Join-Path $Target $Name)) { Remove-Item -Recurse -Force (Join-Path $Target $Name) }
    Copy-Item -Recurse $Source $Target
}

Write-Host "Installed $(Join-Path $Target $Name)"
Write-Host 'Restart Studio One or rescan plug-ins, then load Genetic Composer from Instruments.'
