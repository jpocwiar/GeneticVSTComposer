# Downloads pluginval on first use and validates the Release VST3 at strictness 10.
# Builds the VST3 first when the artefact is missing.
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path -Parent $PSScriptRoot)

$version = 'v1.0.4'
$tools = '.tools'
$pluginval = Join-Path $tools 'pluginval.exe'
if (-not (Test-Path $pluginval)) {
    New-Item -ItemType Directory -Force -Path $tools | Out-Null
    $zip = Join-Path $tools 'pluginval.zip'
    Invoke-WebRequest -UseBasicParsing `
        "https://github.com/Tracktion/pluginval/releases/download/$version/pluginval_Windows.zip" -OutFile $zip
    Expand-Archive -Path $zip -DestinationPath $tools -Force
    Remove-Item $zip
    if (-not (Test-Path $pluginval)) {
        $found = Get-ChildItem $tools -Recurse -Filter pluginval.exe | Select-Object -First 1
        if (-not $found) { Write-Error 'pluginval.exe was not in the downloaded archive.' }
        Copy-Item $found.FullName $pluginval
    }
}

$vst3 = 'build\GeneticComposer_artefacts\Release\VST3\Genetic Composer.vst3'
if (-not (Test-Path $vst3)) {
    & scripts\build-windows.ps1
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
}

& $pluginval --strictness-level 10 --validate (Resolve-Path $vst3).Path
exit $LASTEXITCODE
