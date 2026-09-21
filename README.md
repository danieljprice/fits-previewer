# FitsPreviewer

This package is something I have been dreaming of for a while. It implements a Finder Quick Look for FITS files on macOS. You can then press the spacebar on a `.fits` or `.fits.gz` file to see a preview of the image or data cube (for data cubes it shows a playable movie).

## Install instructions

```sh
brew tap danieljprice/all
brew install --cask fits-previewer
```

## How to compile and install from source on your Mac

First, install CFITSIO via homebrew:
```sh
brew install cfitsio
```

Assuming you have Xcode installed, you should be then to build using:
```sh
make app
```

That writes `build/DerivedData/Build/Products/Release/FitsPreviewer.app`. Copy the app into `~/Applications` or `/Applications` and register it:

```sh
APP="$HOME/Applications/FitsPreviewer.app"
ditto build/DerivedData/Build/Products/Release/FitsPreviewer.app "$APP"
/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister -f -R -trusted "$APP"
qlmanage -r
```

If the spacebar still shows nothing, open System Settings → General → Login Items & Extensions → Quick Look and enable FitsPreviewer.

## Tests

```sh
make test
```

That writes FITS files under `tests/test_images` and checks images, cubes, degenerate axes, tables, and spectra.

## Distribution

Pushing a tag such as `v1.0.0` runs the release workflow. That attaches `FitsPreviewer.zip` and `FitsPreviewer.dmg` to the GitHub release for the tag. The zip is what the Homebrew cask downloads. The disk image is the manual install: open it and drag FitsPreviewer onto the Applications folder.

`./scripts/package.sh` builds the same files locally. It writes them under `dist/` and prints their sha256. Both are ad-hoc signed, so Gatekeeper blocks them until the app is notarized.

Currently the .app is unsigned because I am unwilling to pay $99 per year for the Apple developer program. If I paid the money then I would run:

```sh
xcrun notarytool store-credentials fits-previewer
./scripts/notarize.sh
```