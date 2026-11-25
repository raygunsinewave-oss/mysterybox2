#!/usr/bin/env bash
# Mystery Box Synthesizer by Raygun -- macOS installer package (VST3 + AU + app).
#   installer/macos/build_pkg.sh <artefacts/Release dir> <output dir>
# Signs the .pkg when APPLE_INSTALLER_ID ("Developer ID Installer: ...") is set.
set -euo pipefail
A="${1:?artefacts dir}"
OUT="${2:?output dir}"
PRODUCT="Mystery Box Synthesizer"
ID="com.raygun.mysteryboxsynth"
VERSION="1.0.0"
WORK="$(mktemp -d)"
mkdir -p "$OUT"

component() # name src installLocation
{
  local root="$WORK/root_$1"
  mkdir -p "$root"
  ditto "$2" "$root/$(basename "$2")"
  pkgbuild --root "$root" --identifier "$ID.$1" --version "$VERSION" --install-location "$3" "$WORK/$1.pkg"
}
component vst3 "$A/VST3/$PRODUCT.vst3" "/Library/Audio/Plug-Ins/VST3"
component au "$A/AU/$PRODUCT.component" "/Library/Audio/Plug-Ins/Components"
component app "$A/Standalone/$PRODUCT.app" "/Applications"

cat > "$WORK/distribution.xml" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
  <title>$PRODUCT</title>
  <options customize="allow" require-scripts="false" hostArchitectures="arm64,x86_64"/>
  <os-version min="11.0"/>
  <choices-outline>
    <line choice="vst3"/><line choice="au"/><line choice="app"/>
  </choices-outline>
  <choice id="vst3" title="VST3 plug-in"><pkg-ref id="$ID.vst3"/></choice>
  <choice id="au" title="Audio Unit (AU) plug-in"><pkg-ref id="$ID.au"/></choice>
  <choice id="app" title="Standalone application"><pkg-ref id="$ID.app"/></choice>
  <pkg-ref id="$ID.vst3">vst3.pkg</pkg-ref>
  <pkg-ref id="$ID.au">au.pkg</pkg-ref>
  <pkg-ref id="$ID.app">app.pkg</pkg-ref>
</installer-gui-script>
EOF

SIGN=()
[[ -n "${APPLE_INSTALLER_ID:-}" ]] && SIGN=(--sign "$APPLE_INSTALLER_ID")
productbuild --distribution "$WORK/distribution.xml" --package-path "$WORK" ${SIGN[@]+"${SIGN[@]}"} "$OUT/MysteryBoxSynth_Installer.pkg"
rm -rf "$WORK"
echo "Wrote $OUT/MysteryBoxSynth_Installer.pkg"
