#!/bin/bash
# Build the app, zip it for the Homebrew cask, and make a drag-install disk image.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
make app
APP="build/DerivedData/Build/Products/Release/FitsPreviewer.app"
mkdir -p "$APP/Contents/Resources"
cp -f LICENSE "$APP/Contents/Resources/LICENSE"
# The copy changes a sealed file. Sign the bundle again or Gatekeeper
# reports the app as damaged.
codesign --force --sign - \
    --entitlements "$ROOT/FitsPreviewer/FitsPreviewer.entitlements" \
    --timestamp=none \
    "$APP"
mkdir -p dist
rm -f dist/FitsPreviewer.zip dist/FitsPreviewer.dmg
ditto -c -k --keepParent \
    "$APP" \
    dist/FitsPreviewer.zip
echo "wrote dist/FitsPreviewer.zip"
shasum -a 256 dist/FitsPreviewer.zip

# Copy the signed app. Do not add files to the original bundle after codesign.
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
ditto "$APP" "$STAGE/FitsPreviewer.app"
ln -s /Applications "$STAGE/Applications"
hdiutil create -volname FitsPreviewer -srcfolder "$STAGE" -ov -format UDZO \
    dist/FitsPreviewer.dmg
echo "wrote dist/FitsPreviewer.dmg"
shasum -a 256 dist/FitsPreviewer.dmg
