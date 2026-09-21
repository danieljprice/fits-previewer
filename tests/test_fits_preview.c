/*
 * test_fits_preview.c
 *
 * Build test images, then check which ones become a still, a cube,
 * or no preview. Run from the repository root.
 */

#include "fits_preview.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define TEST_IMAGES_DIR "tests/test_images"

static int g_failed;

/* Declared in tests/make_test_images.c. Writes one FITS file per case. */
int make_test_images(const char *dir);

/* Record a mismatch and keep going so one run reports every failure. */
static void expect_int(const char *name, int got, int want)
{
    if (got != want) {
        fprintf(stderr, "FAIL %s: got %d want %d\n", name, got, want);
        g_failed = 1;
    }
}

/* Load one test image. rc is the loader status, not the preview kind. */
static void load_named(const char *name, int edge, int frames,
                       fits_preview *out, int *rc)
{
    char path[512];

    snprintf(path, sizeof path, "%s/%s", TEST_IMAGES_DIR, name);
    *rc = fits_preview_load(path, edge, frames, out);
}

/* Ramp 0..47 maps pixel 0 to black and pixel 46 to white. */
static void test_ramp(void)
{
    fits_preview preview;
    int rc = 0;

    load_named("ramp.fits", 512, 32, &preview, &rc);
    expect_int("ramp rc", rc, 0);
    expect_int("ramp kind", preview.kind, FITS_PREVIEW_IMAGE);
    expect_int("ramp width", preview.width, 48);
    expect_int("ramp height", preview.height, 2);
    expect_int("ramp channels", preview.channels, 1);
    expect_int("ramp frames", preview.nframes, 1);
    if (preview.pixels != NULL) {
        expect_int("ramp pix0", preview.pixels[0], 0);
        expect_int("ramp pix47", preview.pixels[47], 255);
    } else {
        expect_int("ramp pixels", 0, 1);
    }
    fits_preview_free(&preview);
}

/* BLANK must not drag the percentile down to the null value. */
static void test_blank(void)
{
    fits_preview preview;
    int rc = 0;

    load_named("blank.fits", 512, 32, &preview, &rc);
    expect_int("blank rc", rc, 0);
    expect_int("blank kind", preview.kind, FITS_PREVIEW_IMAGE);
    if (preview.pixels != NULL && preview.width == 10) {
        expect_int("blank pix0", preview.pixels[0], 0);
        expect_int("blank pix8", preview.pixels[8], 255);
        expect_int("blank pix9", preview.pixels[9], 0);
    } else {
        expect_int("blank pixels", 0, 1);
    }
    fits_preview_free(&preview);
}

/* NaN samples stay black and do not pull the percentile down. */
static void test_nan(void)
{
    fits_preview preview;
    int rc = 0;

    load_named("nan.fits", 512, 32, &preview, &rc);
    expect_int("nan rc", rc, 0);
    expect_int("nan kind", preview.kind, FITS_PREVIEW_IMAGE);
    if (preview.pixels != NULL && preview.width == 4) {
        expect_int("nan pix0", preview.pixels[0], 0);
        expect_int("nan pix1", preview.pixels[1], 0);
        expect_int("nan pix2", preview.pixels[2], 255);
        expect_int("nan pix3", preview.pixels[3], 0);
    } else {
        expect_int("nan pixels", 0, 1);
    }
    fits_preview_free(&preview);
}

/* The image lives in the extension, not the empty primary HDU. */
static void test_extension(void)
{
    fits_preview preview;
    int rc = 0;

    load_named("extension.fits", 512, 32, &preview, &rc);
    expect_int("ext rc", rc, 0);
    expect_int("ext kind", preview.kind, FITS_PREVIEW_IMAGE);
    expect_int("ext width", preview.width, 5);
    expect_int("ext height", preview.height, 7);
    fits_preview_free(&preview);
}

/* An empty table, or one with too many columns, has no preview. */
static void test_none(const char *name)
{
    fits_preview preview;
    int rc = 0;

    load_named(name, 512, 32, &preview, &rc);
    expect_int(name, rc, 0);
    expect_int(name, preview.kind, FITS_PREVIEW_NONE);
    fits_preview_free(&preview);
}

/* A spectrum or 1D table is a line, with ink and empty background. */
static void test_line(const char *name)
{
    fits_preview preview;
    int rc = 0;
    int i;
    int ink = 0;
    int rows = 0;
    int y;

    load_named(name, 512, 32, &preview, &rc);
    expect_int(name, rc, 0);
    expect_int(name, preview.kind, FITS_PREVIEW_IMAGE);
    expect_int(name, preview.width, 512);
    expect_int(name, preview.height, 256);
    expect_int(name, preview.nframes, 1);
    expect_int(name, preview.channels, 1);
    if (preview.pixels != NULL) {
        for (y = 0; y < preview.height; y++) {
            int hit = 0;

            for (i = 0; i < preview.width; i++) {
                if (preview.pixels[(size_t)y * (size_t)preview.width + (size_t)i] == 255) {
                    ink++;
                    hit = 1;
                }
            }
            rows += hit;
        }
    }
    expect_int(name, ink > 0, 1);
    expect_int(name, rows > preview.height / 4, 1);
    fits_preview_free(&preview);
}

/* A real third axis becomes a movie. */
static void test_cube(void)
{
    fits_preview preview;
    int rc = 0;

    load_named("cube.fits", 512, 32, &preview, &rc);
    expect_int("cube rc", rc, 0);
    expect_int("cube kind", preview.kind, FITS_PREVIEW_CUBE);
    expect_int("cube width", preview.width, 6);
    expect_int("cube height", preview.height, 4);
    expect_int("cube frames", preview.nframes, 5);
    expect_int("cube channels", preview.channels, 1);
    fits_preview_free(&preview);
}

/* NAXIS=4 with two trailing singleton axes is a still. */
static void test_degenerate_still(void)
{
    fits_preview preview;
    int rc = 0;

    load_named("degenerate_still.fits", 512, 32, &preview, &rc);
    expect_int("still rc", rc, 0);
    expect_int("still kind", preview.kind, FITS_PREVIEW_IMAGE);
    expect_int("still width", preview.width, 6);
    expect_int("still height", preview.height, 4);
    expect_int("still frames", preview.nframes, 1);
    fits_preview_free(&preview);
}

/* A singleton middle axis is dropped. The movie follows the longer axis. */
static void test_degenerate_cube(void)
{
    fits_preview preview;
    int rc = 0;

    load_named("degenerate_cube.fits", 512, 32, &preview, &rc);
    expect_int("degcube rc", rc, 0);
    expect_int("degcube kind", preview.kind, FITS_PREVIEW_CUBE);
    expect_int("degcube width", preview.width, 6);
    expect_int("degcube height", preview.height, 4);
    expect_int("degcube frames", preview.nframes, 5);
    fits_preview_free(&preview);
}

/* CTYPE3=RGB is one color still, not a three-frame movie. */
static void test_rgb(void)
{
    fits_preview preview;
    int rc = 0;

    load_named("rgb.fits", 512, 32, &preview, &rc);
    expect_int("rgb rc", rc, 0);
    expect_int("rgb kind", preview.kind, FITS_PREVIEW_IMAGE);
    expect_int("rgb channels", preview.channels, 3);
    expect_int("rgb frames", preview.nframes, 1);
    expect_int("rgb width", preview.width, 4);
    expect_int("rgb height", preview.height, 3);
    fits_preview_free(&preview);
}

/* The same shape without a color keyword is a cube. */
static void test_plain_cube(void)
{
    fits_preview preview;
    int rc = 0;

    load_named("plain_cube.fits", 512, 32, &preview, &rc);
    expect_int("plain rc", rc, 0);
    expect_int("plain kind", preview.kind, FITS_PREVIEW_CUBE);
    expect_int("plain frames", preview.nframes, 3);
    expect_int("plain channels", preview.channels, 1);
    fits_preview_free(&preview);
}

/* Longest edge is capped, and both axes share that step so the shape holds. */
static void test_wide(void)
{
    fits_preview preview;
    int rc = 0;

    load_named("wide.fits", 50, 1, &preview, &rc);
    expect_int("wide rc", rc, 0);
    expect_int("wide kind", preview.kind, FITS_PREVIEW_IMAGE);
    expect_int("wide width", preview.width, 50);
    expect_int("wide height", preview.height, 1);
    fits_preview_free(&preview);
}

/* Tile compression is still an image. */
static void test_compressed(void)
{
    fits_preview preview;
    int rc = 0;

    load_named("compressed.fits", 512, 32, &preview, &rc);
    expect_int("rice rc", rc, 0);
    expect_int("rice kind", preview.kind, FITS_PREVIEW_IMAGE);
    expect_int("rice width", preview.width, 8);
    expect_int("rice height", preview.height, 6);
    fits_preview_free(&preview);
}

/* A missing path is an open failure, not an empty preview. */
static void test_missing(void)
{
    fits_preview preview;
    int rc;

    rc = fits_preview_load("tests/test_images/no-such-file.fits", 32, 1, &preview);
    expect_int("missing rc", rc != 0, 1);
    expect_int("missing kind", preview.kind, FITS_PREVIEW_NONE);
    fits_preview_free(&preview);
}

/* The brighter FITS row is the top row, matching DS9. */
static void test_up(void)
{
    fits_preview preview;
    int rc = 0;

    load_named("up.fits", 32, 1, &preview, &rc);
    expect_int("up rc", rc, 0);
    expect_int("up kind", preview.kind, FITS_PREVIEW_IMAGE);
    if (preview.pixels != NULL && preview.width == 2 && preview.height == 2) {
        expect_int("up peak", preview.pixels[1] >= 250, 1);
        expect_int("up bottom", preview.pixels[2], 0);
        expect_int("up row", preview.pixels[0] > preview.pixels[2], 1);
    } else {
        expect_int("up pixels", 0, 1);
    }
    fits_preview_free(&preview);
}

/* One thumbnail frame of a cube is its brightest plane, as a still. */
static void test_bright_frame(void)
{
    fits_preview preview;
    int rc = 0;
    int i;
    int peak = 0;

    load_named("bright_cube.fits", 64, 1, &preview, &rc);
    expect_int("bright rc", rc, 0);
    expect_int("bright kind", preview.kind, FITS_PREVIEW_IMAGE);
    expect_int("bright frames", preview.nframes, 1);
    if (preview.pixels != NULL) {
        for (i = 0; i < preview.width * preview.height; i++) {
            if (preview.pixels[i] > peak) {
                peak = preview.pixels[i];
            }
        }
    }
    expect_int("bright peak", peak >= 250, 1);
    fits_preview_free(&preview);
}

/* Write the test images, then run every check. */
int main(void)
{
    if (make_test_images(TEST_IMAGES_DIR)) {
        fprintf(stderr, "could not write test images\n");
        return 1;
    }
    test_ramp();
    test_blank();
    test_nan();
    test_extension();
    test_none("table.fits");
    test_none("wide_table.fits");
    test_line("spectrum.fits");
    test_line("spectrum_flat.fits");
    test_line("line_table.fits");
    test_cube();
    test_degenerate_still();
    test_degenerate_cube();
    test_rgb();
    test_plain_cube();
    test_wide();
    test_up();
    test_compressed();
    test_missing();
    test_bright_frame();
    if (g_failed) {
        fprintf(stderr, "preview tests failed\n");
        return 1;
    }
    printf("preview tests passed\n");
    return 0;
}
