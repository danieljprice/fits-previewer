#!/bin/bash
# Copy Homebrew's CFITSIO into the app and point every binary at that copy.
set -euo pipefail

APP="${1:?app bundle required}"
SRC="$(python3 -c 'import os; print(os.path.realpath("/opt/homebrew/opt/cfitsio/lib/libcfitsio.dylib"))')"
DEST="$APP/Contents/Frameworks"

mkdir -p "$DEST"
cp -f "$SRC" "$DEST/libcfitsio.10.dylib"
chmod 755 "$DEST/libcfitsio.10.dylib"
# Homebrew signs the dylib. Clear that before rewriting the id, or
# install_name_tool warns that the signature is now invalid.
codesign --remove-signature "$DEST/libcfitsio.10.dylib"
install_name_tool -id "@rpath/libcfitsio.10.dylib" "$DEST/libcfitsio.10.dylib"
codesign --force --sign - --timestamp=none "$DEST/libcfitsio.10.dylib"

# Point one already-signed binary at the embedded dylib and sign its bundle again.
# The signature is cleared first so install_name_tool does not warn about it.
relink() {
    local bin="$1"
    local old="$2"
    local bundle
    local entitlements

    bundle="$(dirname "$(dirname "$(dirname "$bin")")")"
    if [ ! -f "$bundle/Contents/Info.plist" ]; then
        codesign --remove-signature "$bin" 2>/dev/null || true
        install_name_tool -change "$old" "@rpath/libcfitsio.10.dylib" "$bin"
        return 0
    fi
    entitlements="$(mktemp)"
    codesign -d --entitlements :- "$bundle" >"$entitlements" 2>/dev/null
    codesign --remove-signature "$bin"
    install_name_tool -change "$old" "@rpath/libcfitsio.10.dylib" "$bin"
    codesign --force --sign - --entitlements "$entitlements" --timestamp=none "$bundle"
    rm -f "$entitlements"
}

while IFS= read -r bin; do
    case "$bin" in
        *libcfitsio*) continue ;;
    esac
    if ! otool -L "$bin" 2>/dev/null | grep -q libcfitsio; then
        continue
    fi
    old="$(otool -L "$bin" | awk '/libcfitsio/ && $1 !~ /^@rpath/ {print $1; exit}')"
    if [ -n "${old}" ]; then
        relink "$bin" "$old"
    fi
done < <(find "$APP" -type f)
