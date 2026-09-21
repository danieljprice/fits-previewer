#!/bin/bash
# Build the app and zip it for a GitHub release or a Homebrew cask.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
make app
mkdir -p dist
rm -f dist/FitsPreviewer.zip
ditto -c -k --keepParent \
    build/DerivedData/Build/Products/Release/FitsPreviewer.app \
    dist/FitsPreviewer.zip
echo "wrote dist/FitsPreviewer.zip"
shasum -a 256 dist/FitsPreviewer.zip
