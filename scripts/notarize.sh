#!/bin/bash
# Sign dist/FitsPreviewer.zip with Developer ID and submit it to Apple's
# notary service. This does not run until a Developer ID certificate exists.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
APP="$ROOT/build/DerivedData/Build/Products/Release/FitsPreviewer.app"
ZIP="$ROOT/dist/FitsPreviewer.zip"
PROFILE="${NOTARY_PROFILE:-fits-previewer}"

IDENTITY="$(security find-identity -v -p codesigning | awk -F '"' '/Developer ID Application/ {print $2; exit}')"
if [ -z "$IDENTITY" ]; then
    echo "No Developer ID Application certificate is installed." >&2
    echo "The app in build/ is ad-hoc signed, which is enough to run on this Mac." >&2
    echo "Notarization needs the Apple Developer Program (\$99/year) and a Developer ID certificate." >&2
    exit 1
fi

ENT="$ROOT/FitsPreviewer/FitsPreviewer.entitlements"
codesign --force --options runtime --timestamp --sign "$IDENTITY" \
    "$APP/Contents/Frameworks/libcfitsio.10.dylib"
codesign --force --options runtime --timestamp --entitlements "$ENT" --sign "$IDENTITY" \
    "$APP/Contents/PlugIns/FitsPreview.appex"
codesign --force --options runtime --timestamp --entitlements "$ENT" --sign "$IDENTITY" \
    "$APP/Contents/PlugIns/FitsThumbnail.appex"
codesign --force --options runtime --timestamp --entitlements "$ENT" --sign "$IDENTITY" "$APP"

rm -f "$ZIP"
ditto -c -k --keepParent "$APP" "$ZIP"
xcrun notarytool submit "$ZIP" --keychain-profile "$PROFILE" --wait
xcrun stapler staple "$APP"
echo "notarized and stapled $APP"
