/*
 * encode_check.m
 *
 * Confirm a still becomes a PNG and a cube becomes an H.264 movie. Run after
 * the C driver has written tests/test_images.
 */

#import "FitsPreviewRender.h"

#include <stdio.h>

#import <AVFoundation/AVFoundation.h>

/* Fail the process unless path is a non-empty file. */
static int require_file(NSURL *url, const char *label)
{
    NSNumber *size = nil;

    if (url == nil) {
        fprintf(stderr, "FAIL %s: no file\n", label);
        return 1;
    }
    if (![url.scheme isEqualToString:@"file"]) {
        fprintf(stderr, "FAIL %s: not a file url\n", label);
        return 1;
    }
    if (![url checkResourceIsReachableAndReturnError:nil]) {
        fprintf(stderr, "FAIL %s: missing %s\n", label, url.path.UTF8String);
        return 1;
    }
    [url getResourceValue:&size forKey:NSURLFileSizeKey error:nil];
    if (size.unsignedLongLongValue < 32) {
        fprintf(stderr, "FAIL %s: too small\n", label);
        return 1;
    }
    return 0;
}

int main(void)
{
    fits_preview preview;
    NSURL *png;
    NSURL *movie;
    int failed = 0;

    @autoreleasepool {
        if (fits_preview_load("tests/test_images/ramp.fits", 128, 1, &preview) != 0 ||
            preview.kind != FITS_PREVIEW_IMAGE) {
            fprintf(stderr, "FAIL encode ramp load\n");
            return 1;
        }
        png = FitsPreviewWritePNG(&preview, 0);
        fits_preview_free(&preview);
        failed |= require_file(png, "png");

        if (fits_preview_load("tests/test_images/cube.fits", 64, 8, &preview) != 0 ||
            preview.kind != FITS_PREVIEW_CUBE) {
            fprintf(stderr, "FAIL encode cube load\n");
            return 1;
        }
        movie = FitsPreviewWriteMovie(&preview);
        fits_preview_free(&preview);
        failed |= require_file(movie, "movie");
        if (movie != nil && ![movie.pathExtension isEqualToString:@"mp4"]) {
            fprintf(stderr, "FAIL movie: extension %s\n", movie.pathExtension.UTF8String);
            failed = 1;
        }
        if (movie != nil) {
            AVURLAsset *asset = [AVURLAsset URLAssetWithURL:movie options:nil];
            double seconds = CMTimeGetSeconds(asset.duration);

            /* Five planes, each held 4/12 s, the same as setpts=4.*PTS. */
            if (!(seconds > 1.5 && seconds < 1.9)) {
                fprintf(stderr, "FAIL movie duration %g\n", seconds);
                failed = 1;
            }
        }
    }
    if (failed) {
        return 1;
    }
    printf("encode checks passed\n");
    return 0;
}
