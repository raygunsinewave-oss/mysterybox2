#!/usr/bin/env bash
# Mystery Box Synthesizer by Raygun -- one-command local macOS build.
# Builds a Universal (arm64 + x86_64) VST3, AU and Standalone app, runs the tests,
# and (optionally) installs into ~/Library/Audio/Plug-Ins.
#   scripts/build_mac.sh            build + test
#   scripts/build_mac.sh --install  also copy the plugins into ~/Library/Audio/Plug-Ins
# Requirements: Xcode command-line tools, CMake >= 3.22, git. JUCE and Catch2 are fetched automatically.
set -euo pipefail
cd "$(dirname "$0")/.."

PRODUCT="Mystery Box Synthesizer"
cmake -S . -B build-mac -G Xcode -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
cmake --build build-mac --config Release --parallel \
  --target MysteryBoxSynth_VST3 MysteryBoxSynth_AU MysteryBoxSynth_Standalone mbx_tests mbx_plugin_tests

./build-mac/Release/mbx_tests
./build-mac/Release/mbx_plugin_tests

A="build-mac/MysteryBoxSynth_artefacts/Release"
echo
echo "Built:"
echo "  $A/VST3/$PRODUCT.vst3"
echo "  $A/AU/$PRODUCT.component"
echo "  $A/Standalone/$PRODUCT.app"

if [[ "${1:-}" == "--install" ]]; then
  mkdir -p ~/Library/Audio/Plug-Ins/VST3 ~/Library/Audio/Plug-Ins/Components
  rm -rf ~/Library/Audio/Plug-Ins/VST3/"$PRODUCT.vst3" ~/Library/Audio/Plug-Ins/Components/"$PRODUCT.component"
  ditto "$A/VST3/$PRODUCT.vst3" ~/Library/Audio/Plug-Ins/VST3/"$PRODUCT.vst3"
  ditto "$A/AU/$PRODUCT.component" ~/Library/Audio/Plug-Ins/Components/"$PRODUCT.component"
  killall -9 AudioComponentRegistrar 2>/dev/null || true
  echo "Installed into ~/Library/Audio/Plug-Ins. Rescan plugins in your DAW."
fi
