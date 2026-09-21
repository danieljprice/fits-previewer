# Core tests link the Homebrew CFITSIO. The app embeds that same library.

CFITSIO_PREFIX ?= /opt/homebrew
CC ?= clang
CFLAGS ?= -std=c11 -Wall -Wextra -IFitsPreviewCore -I$(CFITSIO_PREFIX)/include
LDFLAGS ?= -L$(CFITSIO_PREFIX)/lib -lcfitsio

.PHONY: test app clean install

test: build/test_fits_preview build/encode_check
	./build/test_fits_preview
	./build/encode_check

build/test_fits_preview: FitsPreviewCore/fits_preview.c tests/test_fits_preview.c tests/make_test_images.c FitsPreviewCore/fits_preview.h
	mkdir -p build tests/test_images
	$(CC) $(CFLAGS) -o $@ FitsPreviewCore/fits_preview.c tests/test_fits_preview.c tests/make_test_images.c $(LDFLAGS)

SDK ?= /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk

build/encode_check: FitsPreviewCore/fits_preview.c FitsPreviewRender/FitsPreviewRender.m tests/encode_check.m
	mkdir -p build
	clang -fobjc-arc -Wall -Wextra -isysroot $(SDK) -mmacosx-version-min=14.0 -arch arm64 \
		-IFitsPreviewCore -IFitsPreviewRender -I/opt/homebrew/include \
		-L/opt/homebrew/lib -lcfitsio \
		-framework Foundation -framework ImageIO -framework CoreGraphics \
		-framework UniformTypeIdentifiers -framework AVFoundation \
		-framework CoreMedia -framework CoreVideo \
		-o $@ FitsPreviewCore/fits_preview.c FitsPreviewRender/FitsPreviewRender.m tests/encode_check.m

DEVELOPER_DIR ?= /Applications/Xcode.app/Contents/Developer
APP_BUNDLE := build/DerivedData/Build/Products/Release/FitsPreviewer.app
INSTALL_DIR ?= $(HOME)/Applications
LSREGISTER := /System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister

app:
	DEVELOPER_DIR="$(DEVELOPER_DIR)" xcodebuild \
		-project FitsPreviewer.xcodeproj \
		-scheme FitsPreviewer -configuration Release \
		-derivedDataPath build/DerivedData \
		-arch arm64 CODE_SIGN_IDENTITY="-" CODE_SIGN_STYLE=Manual \
		build

# Copy the built app into ~/Applications and register the Quick Look extension.
install: app
	mkdir -p "$(INSTALL_DIR)"
	ditto "$(APP_BUNDLE)" "$(INSTALL_DIR)/FitsPreviewer.app"
	xattr -dr com.apple.quarantine "$(INSTALL_DIR)/FitsPreviewer.app"
	"$(LSREGISTER)" -f -R -trusted "$(INSTALL_DIR)/FitsPreviewer.app"
	qlmanage -r

clean:
	rm -rf build tests/test_images
