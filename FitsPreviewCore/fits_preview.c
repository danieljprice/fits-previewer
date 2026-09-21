/*
 * fits_preview.c
 *
 * Walk HDUs with CFITSIO, drop axes of length 1, and scale the first
 * drawable image. Cubes are subsampled along the third remaining axis.
 * A single remaining axis, or a short numeric table, is drawn as a line
 * with no axes or labels. The first FITS row is the bottom row, as in DS9.
 */

#include "fits_preview.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fitsio.h>

#define FITS_PREVIEW_MAX_AXES 16
#define FITS_PREVIEW_CUBE_EDGE 512
#define FITS_PREVIEW_CUBE_FRAMES 32
#define FITS_PREVIEW_PLOT_POINTS 4096
#define FITS_PREVIEW_MAX_LINE_COLS 4

/* Release any pixel buffer and mark the preview empty. */
void fits_preview_free(fits_preview *preview)
{
    if (preview == NULL) {
        return;
    }
    free(preview->pixels);
    preview->pixels = NULL;
    preview->kind = FITS_PREVIEW_NONE;
    preview->width = 0;
    preview->height = 0;
    preview->nframes = 0;
    preview->channels = 0;
}

/* Order finite samples for a percentile lookup. */
static int cmp_double(const void *a, const void *b)
{
    double da = *(const double *)a;
    double db = *(const double *)b;

    if (da < db) {
        return -1;
    }
    if (da > db) {
        return 1;
    }
    return 0;
}

/* Step so the sampled length is at most max_edge. */
static long step_for(long n, int max_edge)
{
    if (n <= max_edge) {
        return 1;
    }
    return (n - 1) / max_edge + 1;
}

/* Pixel count CFITSIO returns for fpixel=1, lpixel=n, inc=step. */
static long sampled_len(long n, long step)
{
    if (step < 1) {
        step = 1;
    }
    return (n - 1) / step + 1;
}

/* 1-based plane index. A single frame uses the middle plane. */
static long plane_at(int frame, int nframes, long nplanes)
{
    if (nframes <= 1) {
        return nplanes / 2 + 1;
    }
    return 1 + ((long)frame * (nplanes - 1)) / (nframes - 1);
}

/* True when GROUPS is T. Random-groups HDUs are not images. */
static int groups_true(fitsfile *fptr)
{
    int status = 0;
    char value[FLEN_VALUE];

    if (fits_read_key_str(fptr, "GROUPS", value, NULL, &status)) {
        return 0;
    }
    return value[0] == 'T' || value[0] == 't';
}

/* True when CTYPE for this 1-based axis names a color axis. */
static int axis_is_color(fitsfile *fptr, int axis_1based)
{
    int status = 0;
    char key[16];
    char value[FLEN_VALUE];
    char upper[FLEN_VALUE];
    size_t i;

    snprintf(key, sizeof key, "CTYPE%d", axis_1based);
    if (fits_read_key_str(fptr, key, value, NULL, &status)) {
        return 0;
    }
    for (i = 0; value[i] != '\0' && i + 1 < sizeof upper; i++) {
        upper[i] = (char)toupper((unsigned char)value[i]);
    }
    upper[i] = '\0';
    return strstr(upper, "RGB") != NULL || strstr(upper, "COLOR") != NULL;
}

/* Fill naxis, naxes for an image or tile-compressed image HDU. */
static int hdu_is_image(fitsfile *fptr, int *naxis, long *naxes)
{
    int status = 0;
    int hdutype = 0;
    int compressed;

    if (groups_true(fptr)) {
        return 0;
    }
    status = 0;
    if (fits_get_hdu_type(fptr, &hdutype, &status)) {
        return 0;
    }
    status = 0;
    compressed = fits_is_compressed_image(fptr, &status);
    status = 0;
    if (hdutype != IMAGE_HDU && !compressed) {
        return 0;
    }
    if (fits_get_img_dim(fptr, naxis, &status) || *naxis > FITS_PREVIEW_MAX_AXES) {
        return 0;
    }
    if (*naxis < 1) {
        return 0;
    }
    status = 0;
    if (fits_get_img_size(fptr, *naxis, naxes, &status)) {
        return 0;
    }
    return 1;
}

/* Read one subsampled plane. vary_axis < 0 pins every other axis at 1. */
static int read_plane(fitsfile *fptr, int naxis, const long *naxes,
                      int x_axis, long x_step, int y_axis, long y_step,
                      int vary_axis, long vary_pixel, double *dst)
{
    int status = 0;
    int anynul = 0;
    int i;
    double nulval = NAN;
    long fpixel[FITS_PREVIEW_MAX_AXES];
    long lpixel[FITS_PREVIEW_MAX_AXES];
    long inc[FITS_PREVIEW_MAX_AXES];

    for (i = 0; i < naxis; i++) {
        fpixel[i] = 1;
        lpixel[i] = 1;
        inc[i] = 1;
    }
    fpixel[x_axis] = 1;
    lpixel[x_axis] = naxes[x_axis];
    inc[x_axis] = x_step;
    fpixel[y_axis] = 1;
    lpixel[y_axis] = naxes[y_axis];
    inc[y_axis] = y_step;
    if (vary_axis >= 0) {
        fpixel[vary_axis] = vary_pixel;
        lpixel[vary_axis] = vary_pixel;
        inc[vary_axis] = 1;
    }
    fits_read_subset(fptr, TDOUBLE, fpixel, lpixel, inc, &nulval, dst,
                     &anynul, &status);
    return status;
}

/* Value at percentile p of a sorted sample. p is 0..1. */
static double pct_sorted(const double *sorted, int n, double p)
{
    int i;

    if (n < 1) {
        return 0.0;
    }
    i = (int)((double)(n - 1) * p);
    if (i < 0) {
        i = 0;
    }
    if (i >= n) {
        i = n - 1;
    }
    return sorted[i];
}

/* Black, the 1st–99th span, and the white point for one asinh stretch. */
struct stretch_scale {
    double lo;
    double hi;
    double beta;
    double norm;
};

#define STRETCH_SAMPLE 65536

/* Subsample finite pixels and keep the true peak. step is at least 1. */
static int sample_plane(const double *src, int count, long *seen, long step,
                       double *vals, int *n, int cap, double *peak, int *have_peak)
{
    int i;
    int finite = 0;

    if (step < 1) {
        step = 1;
    }
    for (i = 0; i < count; i++) {
        double v = src[i];

        if (!isfinite(v)) {
            continue;
        }
        finite++;
        if (!*have_peak || v > *peak) {
            *peak = v;
            *have_peak = 1;
        }
        if (*seen % step == 0 && *n < cap) {
            vals[*n] = v;
            (*n)++;
        }
        (*seen)++;
    }
    return finite;
}

/* Asinh from the 1st percentile. The 1st–99th span sets the bend, so the
 * noise stays dark. White is the 99.95th percentile, or the peak when that
 * peak is far above it. */
static int scale_ready(double *vals, int n, double peak, int have_peak,
                      struct stretch_scale *scale)
{
    double p99;
    double hi;

    if (n < 1) {
        return 0;
    }
    qsort(vals, (size_t)n, sizeof(double), cmp_double);
    scale->lo = pct_sorted(vals, n, 0.01);
    p99 = pct_sorted(vals, n, 0.99);
    hi = pct_sorted(vals, n, 0.9995);
    if (have_peak && hi > 0.0 && peak > 5.0 * hi) {
        hi = peak;
    }
    scale->hi = hi;
    scale->beta = p99 - scale->lo;
    if (!(scale->beta > 0.0)) {
        scale->beta = hi - scale->lo;
    }
    if (!(scale->hi > scale->lo) || !(scale->beta > 0.0)) {
        scale->norm = 0.0;
    } else {
        scale->norm = asinh((scale->hi - scale->lo) / scale->beta);
    }
    return 1;
}

/* Map one plane with a scale already measured. */
static int apply_scale(const double *src, int count, const struct stretch_scale *scale,
                      unsigned char *dst)
{
    int i;
    int nfin = 0;

    for (i = 0; i < count; i++) {
        double t;

        if (!isfinite(src[i])) {
            dst[i] = 0;
            continue;
        }
        nfin++;
        if (!(scale->norm > 0.0)) {
            dst[i] = 128;
            continue;
        }
        t = asinh((src[i] - scale->lo) / scale->beta) / scale->norm;
        if (t < 0.0) {
            t = 0.0;
        }
        if (t > 1.0) {
            t = 1.0;
        }
        dst[i] = (unsigned char)(t * 255.0 + 0.5);
    }
    return nfin;
}

/* Map finite pixels to 8-bit using this plane's own histogram. */
static int stretch_plane(const double *src, int count, unsigned char *dst)
{
    struct stretch_scale scale;
    double *vals;
    double peak = 0.0;
    long seen = 0;
    long step = 1;
    int n = 0;
    int have_peak = 0;
    int nfin;

    if (count > STRETCH_SAMPLE) {
        step = count / STRETCH_SAMPLE;
    }
    vals = malloc((size_t)STRETCH_SAMPLE * sizeof(double));
    if (vals == NULL) {
        memset(dst, 0, (size_t)count);
        return 0;
    }
    if (sample_plane(src, count, &seen, step, vals, &n, STRETCH_SAMPLE, &peak,
                    &have_peak) == 0 ||
        !scale_ready(vals, n, peak, have_peak, &scale)) {
        free(vals);
        memset(dst, 0, (size_t)count);
        return 0;
    }
    free(vals);
    nfin = apply_scale(src, count, &scale, dst);
    return nfin;
}

/* One scale for every frame, so a bright channel is not stretched on its own noise. */
static int measure_frames(fitsfile *fptr, int naxis, const long *naxes,
                         int x_axis, long x_step, int y_axis, long y_step,
                         int vary_axis, int nframes, long nplanes, int npix,
                         struct stretch_scale *scale)
{
    double *vals;
    double *plane;
    double peak = 0.0;
    long seen = 0;
    long step = 1;
    long total;
    int n = 0;
    int have_peak = 0;
    int frame;
    int ready = 0;

    if (nframes < 1 || npix < 1) {
        return 0;
    }
    total = (long)nframes * (long)npix;
    if (total > STRETCH_SAMPLE) {
        step = total / STRETCH_SAMPLE;
    }
    vals = malloc((size_t)STRETCH_SAMPLE * sizeof(double));
    plane = malloc((size_t)npix * sizeof(double));
    if (vals == NULL || plane == NULL) {
        free(vals);
        free(plane);
        return -1;
    }
    for (frame = 0; frame < nframes; frame++) {
        long which = vary_axis < 0 ? 1 : plane_at(frame, nframes, nplanes);

        if (read_plane(fptr, naxis, naxes, x_axis, x_step, y_axis, y_step,
                       vary_axis, which, plane)) {
            free(vals);
            free(plane);
            return -1;
        }
        sample_plane(plane, npix, &seen, step, vals, &n, STRETCH_SAMPLE, &peak,
                     &have_peak);
    }
    if (n > 0) {
        ready = scale_ready(vals, n, peak, have_peak, scale);
    }
    free(vals);
    free(plane);
    return ready;
}

/* Write every frame with the shared scale. Returns 1 if any pixel was finite. */
static int paint_frames(fitsfile *fptr, int naxis, const long *naxes,
                       int x_axis, long x_step, int y_axis, long y_step,
                       int vary_axis, int nframes, long nplanes, int npix,
                       const struct stretch_scale *scale, unsigned char *pixels)
{
    double *plane;
    int frame;
    int any = 0;

    plane = malloc((size_t)npix * sizeof(double));
    if (plane == NULL) {
        return -1;
    }
    for (frame = 0; frame < nframes; frame++) {
        long which = vary_axis < 0 ? 1 : plane_at(frame, nframes, nplanes);
        unsigned char *dst = pixels + (size_t)frame * (size_t)npix;

        if (read_plane(fptr, naxis, naxes, x_axis, x_step, y_axis, y_step,
                       vary_axis, which, plane)) {
            free(plane);
            return -1;
        }
        if (apply_scale(plane, npix, scale, dst) > 0) {
            any = 1;
        }
    }
    free(plane);
    return any;
}

/* Angular size of one pixel on this original FITS axis. 0 if unknown. */
static double axis_sky_scale(fitsfile *fptr, int axis_1based)
{
    int status = 0;
    int have_cd = 0;
    double cdelt = 0.0;
    double cd1 = 0.0;
    double cd2 = 0.0;
    char key[16];

    snprintf(key, sizeof key, "CDELT%d", axis_1based);
    if (!fits_read_key_dbl(fptr, key, &cdelt, NULL, &status) && cdelt != 0.0) {
        return fabs(cdelt);
    }
    status = 0;
    snprintf(key, sizeof key, "CD1_%d", axis_1based);
    if (!fits_read_key_dbl(fptr, key, &cd1, NULL, &status)) {
        have_cd = 1;
    }
    status = 0;
    snprintf(key, sizeof key, "CD2_%d", axis_1based);
    if (!fits_read_key_dbl(fptr, key, &cd2, NULL, &status)) {
        have_cd = 1;
    }
    if (!have_cd) {
        return 0.0;
    }
    return hypot(cd1, cd2);
}

/* Nearest-neighbour resize so each output pixel covers the same angle. */
static unsigned char *resample_plane(const unsigned char *src, int width, int height,
                                    int channels, int nframes, int new_w, int new_h)
{
    unsigned char *dst;
    size_t plane_in;
    size_t plane_out;
    int frame;
    int y;
    int x;
    int c;

    if (new_w < 1 || new_h < 1 || channels < 1) {
        return NULL;
    }
    if ((size_t)new_w > SIZE_MAX / (size_t)new_h) {
        return NULL;
    }
    plane_out = (size_t)new_w * (size_t)new_h * (size_t)channels;
    if ((size_t)nframes > SIZE_MAX / plane_out) {
        return NULL;
    }
    dst = malloc(plane_out * (size_t)nframes);
    if (dst == NULL) {
        return NULL;
    }
    plane_in = (size_t)width * (size_t)height * (size_t)channels;
    for (frame = 0; frame < nframes; frame++) {
        const unsigned char *in = src + plane_in * (size_t)frame;
        unsigned char *out = dst + plane_out * (size_t)frame;

        for (y = 0; y < new_h; y++) {
            int sy = y * height / new_h;

            if (sy >= height) {
                sy = height - 1;
            }
            for (x = 0; x < new_w; x++) {
                int sx = x * width / new_w;

                if (sx >= width) {
                    sx = width - 1;
                }
                for (c = 0; c < channels; c++) {
                    out[((size_t)y * (size_t)new_w + (size_t)x) * (size_t)channels +
                        (size_t)c] =
                        in[((size_t)sy * (size_t)width + (size_t)sx) * (size_t)channels +
                           (size_t)c];
                }
            }
        }
    }
    return dst;
}

/* Stretch the buffer when CDELT or CD says the pixels are not square. */
static void apply_pixel_scale(fitsfile *fptr, int x_axis, int y_axis,
                             unsigned char **pixels, int *width, int *height,
                             int channels, int nframes)
{
    double sx = axis_sky_scale(fptr, x_axis);
    double sy = axis_sky_scale(fptr, y_axis);
    double ratio;
    int new_w;
    int new_h;
    unsigned char *scaled;

    if (!(sx > 0.0) || !(sy > 0.0) || *width < 1 || *height < 1) {
        return;
    }
    ratio = sy / sx;
    if (ratio > 0.98 && ratio < 1.02) {
        return;
    }
    if (ratio > 20.0 || ratio < 0.05) {
        return;
    }
    if (ratio > 1.0) {
        new_w = *width;
        new_h = (int)((double)(*height) * ratio + 0.5);
    } else {
        new_w = (int)((double)(*width) / ratio + 0.5);
        new_h = *height;
    }
    if (new_w < 1) {
        new_w = 1;
    }
    if (new_h < 1) {
        new_h = 1;
    }
    if (new_w == *width && new_h == *height) {
        return;
    }
    scaled = resample_plane(*pixels, *width, *height, channels, nframes, new_w, new_h);
    if (scaled == NULL) {
        return;
    }
    free(*pixels);
    *pixels = scaled;
    *width = new_w;
    *height = new_h;
}

/* Plot width follows the caller's edge limit. */
static int plot_width(int max_edge)
{
    if (max_edge < 2) {
        return 2;
    }
    return max_edge;
}

/* Plot height is half the width, and at least two pixels. */
static int plot_height(int width)
{
    int height = width / 2;

    if (height < 2) {
        height = 2;
    }
    return height;
}

/* Map v into 0 .. n-1. A flat range sits in the middle. */
static int map_axis(double v, double lo, double hi, int n)
{
    double t;

    if (n < 2 || !(hi > lo)) {
        return n > 1 ? n / 2 : 0;
    }
    t = (v - lo) / (hi - lo);
    if (t < 0.0) {
        t = 0.0;
    }
    if (t > 1.0) {
        t = 1.0;
    }
    return (int)(t * (double)(n - 1) + 0.5);
}

/* Set one pixel white. Out-of-range coordinates are clamped. */
static void put_px(unsigned char *dst, int width, int height, int x, int y)
{
    if (x < 0) {
        x = 0;
    }
    if (y < 0) {
        y = 0;
    }
    if (x >= width) {
        x = width - 1;
    }
    if (y >= height) {
        y = height - 1;
    }
    dst[(size_t)y * (size_t)width + (size_t)x] = 255;
}

/* Bresenham. Coordinates are already inside the canvas. */
static void stroke(unsigned char *dst, int width, int height,
                   int x0, int y0, int x1, int y1)
{
    int dx = abs(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    for (;;) {
        int e2;

        put_px(dst, width, height, x0, y0);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

/* White polyline on black. A non-finite sample breaks the line. */
static int draw_xy(const double *x, const double *y, int n,
                   int width, int height, unsigned char *dst)
{
    int i;
    int nfin = 0;
    int have = 0;
    int px = 0;
    int py = 0;
    double xmin = 0.0;
    double xmax = 0.0;
    double ymin = 0.0;
    double ymax = 0.0;

    for (i = 0; i < n; i++) {
        if (!isfinite(x[i]) || !isfinite(y[i])) {
            continue;
        }
        if (nfin == 0 || x[i] < xmin) {
            xmin = x[i];
        }
        if (nfin == 0 || x[i] > xmax) {
            xmax = x[i];
        }
        if (nfin == 0 || y[i] < ymin) {
            ymin = y[i];
        }
        if (nfin == 0 || y[i] > ymax) {
            ymax = y[i];
        }
        nfin++;
    }
    if (nfin == 0) {
        return 0;
    }
    memset(dst, 0, (size_t)width * (size_t)height);
    for (i = 0; i < n; i++) {
        int ix;
        int iy;

        if (!isfinite(x[i]) || !isfinite(y[i])) {
            have = 0;
            continue;
        }
        ix = map_axis(x[i], xmin, xmax, width);
        iy = height - 1 - map_axis(y[i], ymin, ymax, height);
        if (have) {
            stroke(dst, width, height, px, py, ix, iy);
        } else {
            put_px(dst, width, height, ix, iy);
        }
        px = ix;
        py = iy;
        have = 1;
    }
    return 1;
}

/* Store a line plot as a one-channel still. */
static int finish_plot(const double *x, const double *y, int n, int max_edge,
                      fits_preview *out)
{
    int width = plot_width(max_edge);
    int height = plot_height(width);
    unsigned char *pixels;

    if ((size_t)width > SIZE_MAX / (size_t)height) {
        return 0;
    }
    pixels = calloc((size_t)width * (size_t)height, 1);
    if (pixels == NULL) {
        return 0;
    }
    if (!draw_xy(x, y, n, width, height, pixels)) {
        free(pixels);
        return 0;
    }
    out->width = width;
    out->height = height;
    out->nframes = 1;
    out->channels = 1;
    out->pixels = pixels;
    out->kind = FITS_PREVIEW_IMAGE;
    return 1;
}

/* How many samples to keep from a long vector. */
static int sample_n(long n)
{
    if (n < 2) {
        return 0;
    }
    if (n > FITS_PREVIEW_PLOT_POINTS) {
        return FITS_PREVIEW_PLOT_POINTS;
    }
    return (int)n;
}

/* 1-based index of sample i when keeping nout of n values. */
static long sample_at(int i, int nout, long n)
{
    if (nout <= 1) {
        return 1;
    }
    return 1 + ((n - 1) * (long)i) / (nout - 1);
}

/* Read one squeezed axis into dst. step matches sampled_len. */
static int read_vector(fitsfile *fptr, int naxis, const long *naxes, int axis,
                      long step, double *dst)
{
    int status = 0;
    int anynul = 0;
    int i;
    double nulval = NAN;
    long fpixel[FITS_PREVIEW_MAX_AXES];
    long lpixel[FITS_PREVIEW_MAX_AXES];
    long inc[FITS_PREVIEW_MAX_AXES];

    for (i = 0; i < naxis; i++) {
        fpixel[i] = 1;
        lpixel[i] = 1;
        inc[i] = 1;
    }
    fpixel[axis] = 1;
    lpixel[axis] = naxes[axis];
    inc[axis] = step;
    fits_read_subset(fptr, TDOUBLE, fpixel, lpixel, inc, &nulval, dst, &anynul,
                     &status);
    return status;
}

/* Image that is one long axis after dropping length-1 axes. */
static int render_spectrum(fitsfile *fptr, int max_edge, fits_preview *out)
{
    int naxis = 0;
    int nkept = 0;
    int i;
    int nread;
    int ok;
    long step;
    long naxes[FITS_PREVIEW_MAX_AXES];
    int kept_index[FITS_PREVIEW_MAX_AXES];
    long kept_length[FITS_PREVIEW_MAX_AXES];
    double *x;
    double *y;

    if (!hdu_is_image(fptr, &naxis, naxes)) {
        return 0;
    }
    for (i = 0; i < naxis; i++) {
        if (naxes[i] > 1) {
            kept_index[nkept] = i;
            kept_length[nkept] = naxes[i];
            nkept++;
        }
    }
    if (nkept != 1 || kept_length[0] < 2) {
        return 0;
    }
    step = step_for(kept_length[0], FITS_PREVIEW_PLOT_POINTS);
    nread = (int)sampled_len(kept_length[0], step);
    if (nread < 2) {
        return 0;
    }
    x = malloc((size_t)nread * sizeof(double));
    y = malloc((size_t)nread * sizeof(double));
    if (x == NULL || y == NULL) {
        free(x);
        free(y);
        return 0;
    }
    for (i = 0; i < nread; i++) {
        x[i] = (double)i;
    }
    if (read_vector(fptr, naxis, naxes, kept_index[0], step, y)) {
        free(x);
        free(y);
        return 0;
    }
    ok = finish_plot(x, y, nread, max_edge, out);
    free(x);
    free(y);
    return ok;
}

/* True for an integer or floating column, not a string or bit column. */
static int numeric_col(int typecode)
{
    switch (typecode) {
    case TBYTE:
    case TSBYTE:
    case TSHORT:
    case TUSHORT:
    case TINT:
    case TUINT:
    case TLONG:
    case TULONG:
    case TLONGLONG:
    case TFLOAT:
    case TDOUBLE:
        return 1;
    default:
        return 0;
    }
}

/* Read nout samples. vector selects elements of row 1; otherwise rows. */
static int read_col_samples(fitsfile *fptr, int col, int vector, long n,
                           int nout, double *dst)
{
    int status = 0;
    int anynul = 0;
    int i;
    double nulval = NAN;

    if (nout == (int)n) {
        if (vector) {
            return fits_read_col(fptr, TDOUBLE, col, 1, 1, n, &nulval, dst, &anynul,
                                 &status);
        }
        return fits_read_col(fptr, TDOUBLE, col, 1, 1, n, &nulval, dst, &anynul,
                             &status);
    }
    for (i = 0; i < nout; i++) {
        long at = sample_at(i, nout, n);

        status = 0;
        if (vector) {
            if (fits_read_col(fptr, TDOUBLE, col, 1, at, 1, &nulval, &dst[i],
                              &anynul, &status)) {
                return status;
            }
        } else if (fits_read_col(fptr, TDOUBLE, col, at, 1, 1, &nulval, &dst[i],
                                 &anynul, &status)) {
            return status;
        }
    }
    return 0;
}

/* A short numeric table: one column versus row, or the second versus the first. */
static int render_table(fitsfile *fptr, int max_edge, fits_preview *out)
{
    int status = 0;
    int hdutype = 0;
    int ncols = 0;
    int col;
    int nnum = 0;
    int nout;
    int ok;
    int cols[FITS_PREVIEW_MAX_LINE_COLS];
    long repeat[FITS_PREVIEW_MAX_LINE_COLS];
    long nrows = 0;
    double *x = NULL;
    double *y = NULL;

    if (fits_get_hdu_type(fptr, &hdutype, &status)) {
        return 0;
    }
    if (hdutype != BINARY_TBL && hdutype != ASCII_TBL) {
        return 0;
    }
    status = 0;
    if (fits_get_num_rows(fptr, &nrows, &status) ||
        fits_get_num_cols(fptr, &ncols, &status)) {
        return 0;
    }
    for (col = 1; col <= ncols; col++) {
        int typecode = 0;
        long rep = 0;
        long width = 0;

        status = 0;
        if (fits_get_coltype(fptr, col, &typecode, &rep, &width, &status)) {
            return 0;
        }
        if (typecode < 0 || !numeric_col(typecode)) {
            continue;
        }
        if (nnum == FITS_PREVIEW_MAX_LINE_COLS) {
            return 0;
        }
        cols[nnum] = col;
        repeat[nnum] = rep;
        nnum++;
    }
    if (nnum < 1 || nnum > FITS_PREVIEW_MAX_LINE_COLS) {
        return 0;
    }

    /* One row holding a vector is still a 1D spectrum. */
    if (nrows <= 1 && repeat[0] >= 2) {
        nout = sample_n(repeat[0]);
        if (nout < 2) {
            return 0;
        }
        x = malloc((size_t)nout * sizeof(double));
        y = malloc((size_t)nout * sizeof(double));
        if (x == NULL || y == NULL) {
            free(x);
            free(y);
            return 0;
        }
        if (nnum >= 2 && repeat[1] == repeat[0]) {
            if (read_col_samples(fptr, cols[0], 1, repeat[0], nout, x) ||
                read_col_samples(fptr, cols[1], 1, repeat[0], nout, y)) {
                free(x);
                free(y);
                return 0;
            }
        } else {
            for (col = 0; col < nout; col++) {
                x[col] = (double)col;
            }
            if (read_col_samples(fptr, cols[0], 1, repeat[0], nout, y)) {
                free(x);
                free(y);
                return 0;
            }
        }
        ok = finish_plot(x, y, nout, max_edge, out);
        free(x);
        free(y);
        return ok;
    }

    if (nrows < 2) {
        return 0;
    }
    for (col = 0; col < nnum; col++) {
        if (repeat[col] != 1) {
            return 0;
        }
    }
    nout = sample_n(nrows);
    x = malloc((size_t)nout * sizeof(double));
    y = malloc((size_t)nout * sizeof(double));
    if (x == NULL || y == NULL) {
        free(x);
        free(y);
        return 0;
    }
    if (nnum == 1) {
        for (col = 0; col < nout; col++) {
            x[col] = (double)col;
        }
        if (read_col_samples(fptr, cols[0], 0, nrows, nout, y)) {
            free(x);
            free(y);
            return 0;
        }
    } else if (read_col_samples(fptr, cols[0], 0, nrows, nout, x) ||
               read_col_samples(fptr, cols[1], 0, nrows, nout, y)) {
        free(x);
        free(y);
        return 0;
    }
    ok = finish_plot(x, y, nout, max_edge, out);
    free(x);
    free(y);
    return ok;
}

/* DS9 shows FITS pixel (1,1) at the lower left. The bitmap's first row is the top. */
static void flip_vertical(unsigned char *pixels, int width, int height,
                         int channels, int nframes)
{
    unsigned char *tmp;
    unsigned char *base;
    unsigned char *top;
    unsigned char *bottom;
    size_t row;
    int frame;
    int y;

    if (pixels == NULL || width < 1 || height < 2 || channels < 1 || nframes < 1) {
        return;
    }
    row = (size_t)width * (size_t)channels;
    tmp = malloc(row);
    if (tmp == NULL) {
        return;
    }
    for (frame = 0; frame < nframes; frame++) {
        base = pixels + (size_t)frame * row * (size_t)height;
        for (y = 0; y < height / 2; y++) {
            top = base + (size_t)y * row;
            bottom = base + (size_t)(height - 1 - y) * row;
            memcpy(tmp, top, row);
            memcpy(top, bottom, row);
            memcpy(bottom, tmp, row);
        }
    }
    free(tmp);
}

/* Build the preview for the current HDU. Returns 1 if out was filled. */
static int render_hdu(fitsfile *fptr, int max_edge, int max_frames,
                      fits_preview *out)
{
    int naxis = 0;
    int nkept = 0;
    int i;
    int color;
    int edge;
    int nframes;
    int channels;
    int vary_axis;
    int width;
    int height;
    int saw_finite = 0;
    int ch;
    long naxes[FITS_PREVIEW_MAX_AXES];
    int kept_index[FITS_PREVIEW_MAX_AXES];
    long kept_length[FITS_PREVIEW_MAX_AXES];
    long x_step;
    long y_step;
    size_t npix;
    double *plane = NULL;
    unsigned char *pixels = NULL;

    if (!hdu_is_image(fptr, &naxis, naxes)) {
        return 0;
    }
    for (i = 0; i < naxis; i++) {
        if (naxes[i] > 1) {
            kept_index[nkept] = i;
            kept_length[nkept] = naxes[i];
            nkept++;
        }
    }
    if (nkept < 2) {
        return 0;
    }

    color = nkept >= 3 && kept_length[2] == 3 &&
            axis_is_color(fptr, kept_index[2] + 1);
    edge = max_edge;
    if (!color && nkept >= 3 && edge > FITS_PREVIEW_CUBE_EDGE) {
        edge = FITS_PREVIEW_CUBE_EDGE;
    }
    /* One step for both axes so a 2:1 image stays 2:1. */
    x_step = step_for(kept_length[0] > kept_length[1] ? kept_length[0]
                                                      : kept_length[1],
                      edge);
    y_step = x_step;
    width = (int)sampled_len(kept_length[0], x_step);
    height = (int)sampled_len(kept_length[1], y_step);
    if (width < 1 || height < 1 || width > 100000 || height > 100000) {
        return 0;
    }
    if ((size_t)width > SIZE_MAX / (size_t)height) {
        return 0;
    }
    npix = (size_t)width * (size_t)height;
    if (npix > (size_t)INT_MAX) {
        return 0;
    }

    if (color) {
        channels = 3;
        nframes = 1;
        vary_axis = kept_index[2];
    } else if (nkept >= 3) {
        channels = 1;
        if (kept_length[2] > FITS_PREVIEW_CUBE_FRAMES) {
            nframes = FITS_PREVIEW_CUBE_FRAMES;
        } else {
            nframes = (int)kept_length[2];
        }
        if (nframes > max_frames) {
            nframes = max_frames;
        }
        if (nframes < 1) {
            nframes = 1;
        }
        vary_axis = kept_index[2];
    } else {
        channels = 1;
        nframes = 1;
        vary_axis = -1;
    }

    if (npix > SIZE_MAX / (size_t)channels ||
        (size_t)nframes > SIZE_MAX / (npix * (size_t)channels)) {
        return 0;
    }
    pixels = calloc((size_t)nframes * npix * (size_t)channels, 1);
    plane = malloc(npix * sizeof(double));
    if (pixels == NULL || plane == NULL) {
        free(pixels);
        free(plane);
        return 0;
    }

    if (color) {
        for (ch = 0; ch < 3; ch++) {
            unsigned char *chan = malloc(npix);

            if (chan == NULL ||
                read_plane(fptr, naxis, naxes, kept_index[0], x_step,
                           kept_index[1], y_step, vary_axis, ch + 1, plane)) {
                free(chan);
                free(plane);
                free(pixels);
                return 0;
            }
            if (stretch_plane(plane, (int)npix, chan) > 0) {
                saw_finite = 1;
            }
            for (i = 0; i < (int)npix; i++) {
                pixels[(size_t)i * 3 + (size_t)ch] = chan[i];
            }
            free(chan);
        }
    } else if (nframes == 1) {
        long which = 1;

        if (vary_axis >= 0) {
            which = plane_at(0, 1, kept_length[2]);
        }
        if (read_plane(fptr, naxis, naxes, kept_index[0], x_step,
                       kept_index[1], y_step, vary_axis, which, plane)) {
            free(plane);
            free(pixels);
            return 0;
        }
        if (stretch_plane(plane, (int)npix, pixels) > 0) {
            saw_finite = 1;
        }
    } else {
        struct stretch_scale scale;
        int painted;

        int measured;

        measured = measure_frames(fptr, naxis, naxes, kept_index[0], x_step,
                                  kept_index[1], y_step, vary_axis, nframes,
                                  kept_length[2], (int)npix, &scale);
        if (measured < 0) {
            free(plane);
            free(pixels);
            return 0;
        }
        painted = 0;
        if (measured > 0) {
            painted = paint_frames(fptr, naxis, naxes, kept_index[0], x_step,
                                   kept_index[1], y_step, vary_axis, nframes,
                                   kept_length[2], (int)npix, &scale, pixels);
        }
        if (painted < 0) {
            free(plane);
            free(pixels);
            return 0;
        }
        saw_finite = painted;
    }

    free(plane);
    if (!saw_finite) {
        free(pixels);
        return 0;
    }
    apply_pixel_scale(fptr, kept_index[0] + 1, kept_index[1] + 1, &pixels, &width,
                      &height, channels, nframes);
    flip_vertical(pixels, width, height, channels, nframes);

    out->width = width;
    out->height = height;
    out->nframes = nframes;
    out->channels = channels;
    out->pixels = pixels;
    out->kind = nframes > 1 ? FITS_PREVIEW_CUBE : FITS_PREVIEW_IMAGE;
    return 1;
}

/* Open path and preview the first HDU that still has two axes after squeeze. */
int fits_preview_load(const char *path, int max_edge, int max_frames,
                      fits_preview *out)
{
    fitsfile *fptr = NULL;
    int status = 0;
    int nhdu = 0;
    int h;

    if (out == NULL) {
        return 1;
    }
    memset(out, 0, sizeof(*out));
    out->kind = FITS_PREVIEW_NONE;
    if (path == NULL || path[0] == '\0') {
        return 1;
    }
    if (max_edge < 1) {
        max_edge = 1;
    }
    if (max_frames < 1) {
        max_frames = 1;
    }
    if (fits_open_file(&fptr, path, READONLY, &status)) {
        return status ? status : 1;
    }
    if (fits_get_num_hdus(fptr, &nhdu, &status)) {
        fits_close_file(fptr, &status);
        return 1;
    }
    for (h = 1; h <= nhdu; h++) {
        int hdutype = 0;

        status = 0;
        if (fits_movabs_hdu(fptr, h, &hdutype, &status)) {
            break;
        }
        if (render_hdu(fptr, max_edge, max_frames, out) ||
            render_spectrum(fptr, max_edge, out) ||
            render_table(fptr, max_edge, out)) {
            status = 0;
            fits_close_file(fptr, &status);
            return 0;
        }
        fits_preview_free(out);
        out->kind = FITS_PREVIEW_NONE;
    }
    status = 0;
    fits_close_file(fptr, &status);
    return 0;
}
