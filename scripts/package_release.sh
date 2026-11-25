#!/usr/bin/env bash
# Mystery Box Synthesizer by Raygun -- assembles MysteryBoxSynth_by_Raygun_v1.0.0.zip
#   scripts/package_release.sh <staging dir containing macOS/ Windows/ [Linux/]>
# Missing platform folders get a README explaining how to produce them (binaries are never faked).
set -euo pipefail
cd "$(dirname "$0")/.."
STAGING="${1:?staging dir}"
VERSION="1.0.0"
NAME="MysteryBoxSynth_by_Raygun_v${VERSION}"
OUT="dist/${NAME}"

rm -rf "$OUT" "${NAME}.zip"
mkdir -p "$OUT"

for platform in macOS Windows Linux; do
  if [[ -d "$STAGING/$platform" ]] && [[ -n "$(ls -A "$STAGING/$platform")" ]]; then
    cp -R "$STAGING/$platform" "$OUT/$platform"
  elif [[ "$platform" != "Linux" ]]; then
    mkdir -p "$OUT/$platform"
    cat > "$OUT/$platform/README_BUILD_${platform}.txt" <<EOF
The ${platform} binaries are not included in this archive because it was assembled on a machine
that cannot compile ${platform} plugins. Push this source to GitHub: the included workflow
(.github/workflows/build.yml) builds, tests (pluginval strictness 10$( [[ $platform == macOS ]] && echo ", auval")) and packages
them automatically. Or build locally with Source/scripts/build_$( [[ $platform == macOS ]] && echo "mac.sh" || echo "windows.ps1").
EOF
  fi
done

# Full source (no build folders, no VCS metadata)
mkdir -p "$OUT/Source"
tar --exclude='__pycache__' --exclude='./build*' --exclude='./dist' --exclude='./.git' --exclude='*.zip' -cf - . | (cd "$OUT/Source" && tar -xf -)

cp README.md INSTALL.md LICENSE_NOTES.md "$OUT/"
( cd dist && zip -qry "../${NAME}.zip" "${NAME}" )
echo "Wrote ${NAME}.zip"
