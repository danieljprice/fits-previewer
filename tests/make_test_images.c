/*
 * make_test_images.c
 *
 * Write the small FITS files the preview tests read back. Each routine
 * builds one file with CFITSIO so the tests do not depend on Python.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fitsio.h>

/* Print a CFITSIO status and return it unchanged. */
static int report(int status, const char *path)
{
    char msg[FLEN_STATUS];

    if (status) {
        fits_get_errstatus(status, msg);
        fprintf(stderr, "%s: %s\n", path, msg);
    }
    return status;
}

/* Create a new file, replacing any previous copy. */
static int open_new(const char *path, fitsfile **fptr)
{
    int status = 0;
    char name[512];

    snprintf(name, sizeof name, "!%s", path);
    if (fits_create_file(fptr, name, &status)) {
        return report(status, path);
    }
    return 0;
}

/* Write a 48 by 2 ramp so percentile stretch has a known answer. */
static int write_ramp(const char *path)
{
    fitsfile *fptr = NULL;
    int status = 0;
    long naxes[2] = {48, 2};
    short pix[96];
    int i;

    if (open_new(path, &fptr)) {
        return 1;
    }
    for (i = 0; i < 96; i++) {
        pix[i] = (short)(i % 48);
    }
    fits_create_img(fptr, SHORT_IMG, 2, naxes, &status);
    fits_write_img(fptr, TSHORT, 1, 96, pix, &status);
    fits_close_file(fptr, &status);
    return report(status, path);
}

/* Integer image whose last pixel is BLANK and must not set the scale. */
static int write_blank(const char *path)
{
    fitsfile *fptr = NULL;
    int status = 0;
    long naxes[2] = {10, 2};
    short pix[20];
    int i;

    if (open_new(path, &fptr)) {
        return 1;
    }
    for (i = 0; i < 20; i++) {
        pix[i] = (short)(i % 10);
    }
    pix[9] = -9999;
    pix[19] = -9999;
    fits_create_img(fptr, SHORT_IMG, 2, naxes, &status);
    fits_write_img(fptr, TSHORT, 1, 20, pix, &status);
    fits_update_key_lng(fptr, "BLANK", -9999, "null", &status);
    fits_close_file(fptr, &status);
    return report(status, path);
}

/* Float image with NaNs that must become black. */
static int write_nan(const char *path)
{
    fitsfile *fptr = NULL;
    int status = 0;
    long naxes[2] = {4, 2};
    float pix[8];
    int i;

    for (i = 0; i < 8; i++) {
        pix[i] = (i % 4 == 1 || i % 4 == 3) ? nanf("") : (i % 4 == 0 ? 0.0f : 10.0f);
    }
    if (open_new(path, &fptr)) {
        return 1;
    }
    fits_create_img(fptr, FLOAT_IMG, 2, naxes, &status);
    fits_write_img(fptr, TFLOAT, 1, 8, pix, &status);
    fits_close_file(fptr, &status);
    return report(status, path);
}

/* Empty primary HDU plus an image extension. */
static int write_extension(const char *path)
{
    fitsfile *fptr = NULL;
    int status = 0;
    long naxes[2] = {5, 7};
    short pix[35];

    memset(pix, 0, sizeof pix);
    pix[0] = 1;
    pix[34] = 2;
    if (open_new(path, &fptr)) {
        return 1;
    }
    fits_create_img(fptr, BYTE_IMG, 0, NULL, &status);
    fits_create_img(fptr, SHORT_IMG, 2, naxes, &status);
    fits_write_img(fptr, TSHORT, 1, 35, pix, &status);
    fits_close_file(fptr, &status);
    return report(status, path);
}

/* Binary table and nothing else drawable. */
static int write_table(const char *path)
{
    fitsfile *fptr = NULL;
    int status = 0;
    char *ttype[] = {"COL"};
    char *tform[] = {"1J"};
    char *tunit[] = {""};

    if (open_new(path, &fptr)) {
        return 1;
    }
    fits_create_tbl(fptr, BINARY_TBL, 0, 1, ttype, tform, tunit, NULL, &status);
    fits_close_file(fptr, &status);
    return report(status, path);
}

/* One-dimensional spectrum with a rising ramp. */
static int write_spectrum(const char *path)
{
    fitsfile *fptr = NULL;
    int status = 0;
    long naxes[1] = {32};
    short pix[32];
    int i;

    for (i = 0; i < 32; i++) {
        pix[i] = (short)i;
    }
    if (open_new(path, &fptr)) {
        return 1;
    }
    fits_create_img(fptr, SHORT_IMG, 1, naxes, &status);
    fits_write_img(fptr, TSHORT, 1, 32, pix, &status);
    fits_close_file(fptr, &status);
    return report(status, path);
}

/* Spectrum stored with a dummy axis of length 1. */
static int write_spectrum_flat(const char *path)
{
    fitsfile *fptr = NULL;
    int status = 0;
    long naxes[2] = {32, 1};
    short pix[32];
    int i;

    for (i = 0; i < 32; i++) {
        pix[i] = (short)(i * 2);
    }
    if (open_new(path, &fptr)) {
        return 1;
    }
    fits_create_img(fptr, SHORT_IMG, 2, naxes, &status);
    fits_write_img(fptr, TSHORT, 1, 32, pix, &status);
    fits_close_file(fptr, &status);
    return report(status, path);
}

/* One numeric column. That is a 1D table, drawn as a line. */
static int write_line_table(const char *path)
{
    fitsfile *fptr = NULL;
    int status = 0;
    char *ttype[] = {"FLUX"};
    char *tform[] = {"1E"};
    char *tunit[] = {""};
    float pix[8];
    int i;

    for (i = 0; i < 8; i++) {
        pix[i] = (float)i;
    }
    if (open_new(path, &fptr)) {
        return 1;
    }
    fits_create_tbl(fptr, BINARY_TBL, 0, 1, ttype, tform, tunit, NULL, &status);
    fits_write_col(fptr, TFLOAT, 1, 1, 1, 8, pix, &status);
    fits_close_file(fptr, &status);
    return report(status, path);
}

/* Many numeric columns are not a 1D spectrum. */
static int write_wide_table(const char *path)
{
    fitsfile *fptr = NULL;
    int status = 0;
    char *ttype[] = {"A", "B", "C", "D", "E", "F"};
    char *tform[] = {"1E", "1E", "1E", "1E", "1E", "1E"};
    char *tunit[] = {"", "", "", "", "", ""};
    float pix[4] = {1.0f, 2.0f, 3.0f, 4.0f};
    int col;

    if (open_new(path, &fptr)) {
        return 1;
    }
    fits_create_tbl(fptr, BINARY_TBL, 0, 6, ttype, tform, tunit, NULL, &status);
    for (col = 1; col <= 6; col++) {
        fits_write_col(fptr, TFLOAT, col, 1, 1, 4, pix, &status);
    }
    fits_close_file(fptr, &status);
    return report(status, path);
}

/* Five planes. The first has a bright patch and the rest are dark, so a
 * one-frame preview must choose that plane, not the middle. */
static int write_bright_cube(const char *path)
{
    fitsfile *fptr = NULL;
    int status = 0;
    long naxes[3] = {4, 4, 5};
    short pix[4 * 4 * 5];

    memset(pix, 0, sizeof pix);
    pix[0] = 1000;
    pix[1] = 1000;
    pix[2] = 1000;
    pix[3] = 1000;
    if (open_new(path, &fptr)) {
        return 1;
    }
    fits_create_img(fptr, SHORT_IMG, 3, naxes, &status);
    fits_write_img(fptr, TSHORT, 1, 4 * 4 * 5, pix, &status);
    fits_close_file(fptr, &status);
    return report(status, path);
}

/* Small cube, one constant plane per index so the frame count is the test. */
static int write_cube(const char *path, int naxis, const long *naxes)
{
    fitsfile *fptr = NULL;
    int status = 0;
    long npix = 1;
    int i;
    short *pix;

    for (i = 0; i < naxis; i++) {
        npix *= naxes[i];
    }
    pix = calloc((size_t)npix, sizeof(short));
    if (pix == NULL) {
        return 1;
    }
    for (i = 0; i < npix; i++) {
        pix[i] = (short)(i % 17);
    }
    if (open_new(path, &fptr)) {
        free(pix);
        return 1;
    }
    fits_create_img(fptr, SHORT_IMG, naxis, (long *)naxes, &status);
    fits_write_img(fptr, TSHORT, 1, npix, pix, &status);
    fits_close_file(fptr, &status);
    free(pix);
    return report(status, path);
}

/* Three-plane color image. */
static int write_rgb(const char *path)
{
    fitsfile *fptr = NULL;
    int status = 0;
    long naxes[3] = {4, 3, 3};
    short pix[36];
    int i;

    for (i = 0; i < 36; i++) {
        pix[i] = (short)(i + 1);
    }
    if (open_new(path, &fptr)) {
        return 1;
    }
    fits_create_img(fptr, SHORT_IMG, 3, naxes, &status);
    fits_write_img(fptr, TSHORT, 1, 36, pix, &status);
    fits_update_key_str(fptr, "CTYPE3", "RGB", "color axis", &status);
    fits_close_file(fptr, &status);
    return report(status, path);
}

/* Wide image used to check spatial subsampling. */
static int write_wide(const char *path)
{
    fitsfile *fptr = NULL;
    int status = 0;
    long naxes[2] = {300, 2};
    short pix[600];
    int i;

    for (i = 0; i < 600; i++) {
        pix[i] = (short)(i % 50);
    }
    if (open_new(path, &fptr)) {
        return 1;
    }
    fits_create_img(fptr, SHORT_IMG, 2, naxes, &status);
    fits_write_img(fptr, TSHORT, 1, 600, pix, &status);
    fits_close_file(fptr, &status);
    return report(status, path);
}

/* Rice-compressed image. CFITSIO must decompress it on read. */
static int write_compressed(const char *path)
{
    fitsfile *fptr = NULL;
    int status = 0;
    long naxes[2] = {8, 6};
    short pix[48];
    int i;

    for (i = 0; i < 48; i++) {
        pix[i] = (short)i;
    }
    if (open_new(path, &fptr)) {
        return 1;
    }
    fits_set_compression_type(fptr, RICE_1, &status);
    fits_create_img(fptr, SHORT_IMG, 2, naxes, &status);
    fits_write_img(fptr, TSHORT, 1, 48, pix, &status);
    fits_close_file(fptr, &status);
    return report(status, path);
}

/* Two rows, brighter on the second. DS9 puts that row at the top. */
static int write_up(const char *path)
{
    fitsfile *fptr = NULL;
    int status = 0;
    long naxes[2] = {2, 2};
    short pix[4] = {0, 1, 2, 100};

    if (open_new(path, &fptr)) {
        return 1;
    }
    fits_create_img(fptr, SHORT_IMG, 2, naxes, &status);
    fits_write_img(fptr, TSHORT, 1, 4, pix, &status);
    fits_close_file(fptr, &status);
    return report(status, path);
}

/* Write every test image the driver expects. Returns 0 on success. */
int make_test_images(const char *dir)
{
    char path[512];
    long cube[3] = {6, 4, 5};
    long long_cube[3] = {2, 2, 40};
    long still4[4] = {6, 4, 1, 1};
    long cube4[4] = {6, 4, 1, 5};
    long plain3[3] = {4, 3, 3};

    snprintf(path, sizeof path, "%s/ramp.fits", dir);
    if (write_ramp(path)) {
        return 1;
    }
    snprintf(path, sizeof path, "%s/blank.fits", dir);
    if (write_blank(path)) {
        return 1;
    }
    snprintf(path, sizeof path, "%s/nan.fits", dir);
    if (write_nan(path)) {
        return 1;
    }
    snprintf(path, sizeof path, "%s/extension.fits", dir);
    if (write_extension(path)) {
        return 1;
    }
    snprintf(path, sizeof path, "%s/table.fits", dir);
    if (write_table(path)) {
        return 1;
    }
    snprintf(path, sizeof path, "%s/line_table.fits", dir);
    if (write_line_table(path)) {
        return 1;
    }
    snprintf(path, sizeof path, "%s/wide_table.fits", dir);
    if (write_wide_table(path)) {
        return 1;
    }
    snprintf(path, sizeof path, "%s/spectrum.fits", dir);
    if (write_spectrum(path)) {
        return 1;
    }
    snprintf(path, sizeof path, "%s/spectrum_flat.fits", dir);
    if (write_spectrum_flat(path)) {
        return 1;
    }
    snprintf(path, sizeof path, "%s/bright_cube.fits", dir);
    if (write_bright_cube(path)) {
        return 1;
    }
    snprintf(path, sizeof path, "%s/cube.fits", dir);
    if (write_cube(path, 3, cube)) {
        return 1;
    }
    snprintf(path, sizeof path, "%s/long_cube.fits", dir);
    if (write_cube(path, 3, long_cube)) {
        return 1;
    }
    snprintf(path, sizeof path, "%s/degenerate_still.fits", dir);
    if (write_cube(path, 4, still4)) {
        return 1;
    }
    snprintf(path, sizeof path, "%s/degenerate_cube.fits", dir);
    if (write_cube(path, 4, cube4)) {
        return 1;
    }
    snprintf(path, sizeof path, "%s/rgb.fits", dir);
    if (write_rgb(path)) {
        return 1;
    }
    snprintf(path, sizeof path, "%s/plain_cube.fits", dir);
    if (write_cube(path, 3, plain3)) {
        return 1;
    }
    snprintf(path, sizeof path, "%s/wide.fits", dir);
    if (write_wide(path)) {
        return 1;
    }
    snprintf(path, sizeof path, "%s/up.fits", dir);
    if (write_up(path)) {
        return 1;
    }
    snprintf(path, sizeof path, "%s/compressed.fits", dir);
    if (write_compressed(path)) {
        return 1;
    }
    return 0;
}
