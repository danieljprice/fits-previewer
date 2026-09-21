/*
 * fits_preview.h
 *
 * Read the first drawable image in a FITS file and scale it to 8-bit
 * preview pixels. A one-axis image or a short numeric table becomes a
 * line plot. Callers pass limits in; this module talks to CFITSIO
 * and does not draw axes or labels.
 */

#ifndef FITS_PREVIEW_H
#define FITS_PREVIEW_H

typedef enum {
    FITS_PREVIEW_NONE = 0,
    FITS_PREVIEW_IMAGE = 1,
    FITS_PREVIEW_CUBE = 2
} fits_preview_kind;

typedef struct {
    fits_preview_kind kind;
    int width;
    int height;
    int nframes;
    int channels;
    unsigned char *pixels;
} fits_preview;

/* Load a preview. Returns 0 after the file is classified, including when
 * kind is FITS_PREVIEW_NONE. Returns non-zero if the path cannot be opened.
 * On an image or cube, pixels is malloc'd and owned by the caller. */
int fits_preview_load(const char *path, int max_edge, int max_frames,
                      fits_preview *out);

/* Release pixels from a successful load. Safe on a zeroed preview. */
void fits_preview_free(fits_preview *preview);

#endif
