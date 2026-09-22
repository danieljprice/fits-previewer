/*
 * FitsPreviewRender.h
 *
 * Turn preview pixels into a PNG or an H.264 movie Quick Look can show.
 * FITS reading stays in fits_preview; this file only encodes.
 */

#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>

#include "fits_preview.h"

/* Image Quick Look and the thumbnail share. Caller releases it. */
CGImageRef FitsPreviewCreateImage(const unsigned char *pixels, int width, int height,
                                  int channels);
NSError *FitsPreviewNoImageError(void);

/* Read url with CFITSIO. Starts security-scoped access when the URL allows it. */
int FitsPreviewLoadURL(NSURL *url, int maxEdge, int maxFrames, fits_preview *out);

/* Write one still. frame selects a cube plane. name is the temp file stem,
 * or nil for a unique name. Returns nil on failure. */
NSURL *FitsPreviewWritePNG(const fits_preview *preview, int frame, NSString *name);

/* Write an H.264 movie. Each plane is held four times longer than 12 fps.
 * name is the temp file stem, or nil for a unique name. */
NSURL *FitsPreviewWriteMovie(const fits_preview *preview, NSString *name);
