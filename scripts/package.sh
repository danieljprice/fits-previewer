#!/bin/bash
# Build the app and zip it for a GitHub release or a Homebrew cask.
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
rm -f dist/FitsPreviewer.zip
ditto -c -k --keepParent \
    "$APP" \
    dist/FitsPreviewer.zip
echo "wrote dist/FitsPreviewer.zip"
shasum -a 256 dist/FitsPreviewer.zip
