# Mystery Box Synthesizer by Raygun -- one-command local Windows build (x64).
#   powershell -ExecutionPolicy Bypass -File scripts\build_windows.ps1            # build + test
#   powershell -ExecutionPolicy Bypass -File scripts\build_windows.ps1 -Install   # also copy the VST3 (run as Administrator)
# Requirements: Visual Studio 2022 (Desktop development with C++), CMake >= 3.22, git.
param([switch]$Install)
$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")

$Product = "Mystery Box Synthesizer"
cmake -S . -B build-win -G "Visual Studio 17 2022" -A x64
if ($LASTEXITCODE -ne 0) { exit 1 }
cmake --build build-win --config Release --parallel --target MysteryBoxSynth_VST3 MysteryBoxSynth_Standalone mbx_tests mbx_plugin_tests
if ($LASTEXITCODE -ne 0) { exit 1 }

& build-win\Release\mbx_tests.exe
if ($LASTEXITCODE -ne 0) { exit 1 }
& build-win\Release\mbx_plugin_tests.exe
if ($LASTEXITCODE -ne 0) { exit 1 }

$A = "build-win\MysteryBoxSynth_artefacts\Release"
Write-Host ""
Write-Host "Built:"
Write-Host "  $A\VST3\$Product.vst3"
Write-Host "  $A\Standalone\$Product.exe"

if ($Install) {
    $dest = "C:\Program Files\Common Files\VST3"
    New-Item -ItemType Directory -Force $dest | Out-Null
    if (Test-Path "$dest\$Product.vst3") { Remove-Item -Recurse -Force "$dest\$Product.vst3" }
    Copy-Item -Recurse "$A\VST3\$Product.vst3" "$dest\$Product.vst3"
    Write-Host "Installed into $dest. Rescan plugins in your DAW."
}
