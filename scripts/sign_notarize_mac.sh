#!/usr/bin/env bash
# Mystery Box Synthesizer by Raygun -- optional macOS code signing + notarization.
#   scripts/sign_notarize_mac.sh <artefacts/Release dir>
# Env: APPLE_DEVELOPER_ID ("Developer ID Application: Name (TEAMID)"), APPLE_TEAM_ID, APPLE_ID,
#      APPLE_APP_PASSWORD (app-specific password), APPLE_CERT_P12_BASE64 + APPLE_CERT_PASSWORD (CI only).
set -euo pipefail
A="${1:?artefacts dir}"
PRODUCT="Mystery Box Synthesizer"

if [[ -n "${APPLE_CERT_P12_BASE64:-}" ]]; then
  KEYCHAIN=build.keychain
  security create-keychain -p ci "$KEYCHAIN"
  security default-keychain -s "$KEYCHAIN"
  security unlock-keychain -p ci "$KEYCHAIN"
  echo "$APPLE_CERT_P12_BASE64" | base64 --decode > cert.p12
  security import cert.p12 -k "$KEYCHAIN" -P "$APPLE_CERT_PASSWORD" -T /usr/bin/codesign
  security set-key-partition-list -S apple-tool:,apple: -s -k ci "$KEYCHAIN"
  rm cert.p12
fi

for b in "$A/VST3/$PRODUCT.vst3" "$A/AU/$PRODUCT.component" "$A/Standalone/$PRODUCT.app"; do
  codesign --force --deep --options runtime --timestamp --sign "$APPLE_DEVELOPER_ID" "$b"
  codesign --verify --deep --strict "$b"
  ditto -c -k --keepParent "$b" notarize.zip
  xcrun notarytool submit notarize.zip --apple-id "$APPLE_ID" --team-id "$APPLE_TEAM_ID" --password "$APPLE_APP_PASSWORD" --wait
  xcrun stapler staple "$b"
  rm notarize.zip
done
echo "Signed, notarized and stapled."
