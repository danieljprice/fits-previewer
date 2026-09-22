# FitsPreviewer

This package is something I have been dreaming of for a while. It implements a Finder Quick Look for FITS files on macOS. You can then press the spacebar on a `.fits` or `.fits.gz` file to see a preview of the image or data cube (for data cubes it shows a playable movie).



https://github.com/user-attachments/assets/ab9a27bd-943a-4aa5-b348-41dad345f9c7





## How to install

Either download the .dmg file and drag to Applications, or install with homebrew: 
```sh
brew tap danieljprice/all
brew trust danieljprice/all
brew install fits-previewer
```
Launch FitsPreviewer once after installing. Then select a `.fits` or `.fits.gz` file in Finder and press the spacebar. 

If the spacebar still shows nothing, open System Settings → General → Login Items & Extensions → Quick Look and enable FitsPreviewer.

## Contributing

If you come across an unusual FITS file and a good idea for how to preview it, feel free to get in touch via the github issues. I also welcome contributions via pull request.

## How to compile and install from source on your Mac

First, install CFITSIO via homebrew:
```sh
brew install cfitsio
```

Assuming you have Xcode installed, build and install with:

```sh
make install INSTALL_DIR=/Applications
```

That copies the app to `/Applications/FitsPreviewer.app` and registers it. Use `make install` to put it in `~/Applications` instead.

```sh
make uninstall
```

That removes the installed app and drops the Quick Look extensions from System Settings. Pass the same `INSTALL_DIR` you used for install.

## Tests

```sh
make test
```

That writes FITS files under `tests/test_images` and checks images, cubes, degenerate axes, tables, and spectra.

## Distribution

Pushing a tag such as `v1.0.0` runs the release workflow. That attaches `FitsPreviewer.zip` and `FitsPreviewer.dmg` to the GitHub release for the tag. For a manual install you can just open the .dmg and drag FitsPreviewer onto the Applications folder.

Currently the .app is unsigned because I don't have funds to pay $99 per year for the Apple developer program. If I can raise USD 99 per year from https://buymeacoffee.com/danieljprice then I would run:

```sh
xcrun notarytool store-credentials fits-previewer
./scripts/notarize.sh
```
...and you would get a signed app.

## Credits

Credits for the IM Lupi .fits file used to create the logo belong to Avenhaus et al. (2018) taken with the SPHERE instrument on ESO's Very Large Telescope. The sample cube I preview in the demo was from the MAPS project on the ALMA telescope (Öberg et al. 2021; Zhang et al. 2021). The background image on my desktop is the Hubble Space Telescope image of the Butterfly Nebula NGC 6302 (Credit: NASA/ESA).
