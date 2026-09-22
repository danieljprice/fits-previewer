/*
 * PreviewProvider.m
 *
 * Spacebar preview. Stills are drawn into a bitmap context. Cubes are a
 * short H.264 movie. Anything else is an error, so Finder stays blank.
 */

#import "PreviewProvider.h"

#include <string.h>

#import "FitsPreviewRender.h"

/* Draw one plane at the data pixel size. Quick Look can enlarge the window. */
static CGSize display_size(int width, int height)
{
    double w = width < 1 ? 1.0 : (double)width;
    double h = height < 1 ? 1.0 : (double)height;

    return CGSizeMake(w, h);
}

/* Draw one plane. The pixel buffer outlives the preview struct. */
static void reply_still(NSData *bytes, int width, int height, int channels,
                        void (^handler)(QLPreviewReply *, NSError *))
{
    CGSize size = display_size(width, height);

    handler([[QLPreviewReply alloc] initWithContextSize:size
                                                isBitmap:YES
                                            drawingBlock:^BOOL(CGContextRef context,
                                                               QLPreviewReply *reply,
                                                               NSError **error) {
        CGImageRef image = FitsPreviewCreateImage(bytes.bytes, width, height, channels);
        CGRect canvas = CGRectMake(0, 0, size.width, size.height);

        (void)reply;
        (void)error;
        if (image == NULL || context == NULL) {
            CGImageRelease(image);
            return NO;
        }
        CGContextDrawImage(context, canvas, image);
        CGImageRelease(image);
        return YES;
    }],
            nil);
}

@implementation PreviewProvider

/* Build a movie or a still for the first drawable HDU, or return an error. */
- (void)providePreviewForFileRequest:(QLFilePreviewRequest *)request
                   completionHandler:(void (^)(QLPreviewReply *, NSError *))handler
{
    fits_preview preview;
    int rc;
    NSURL *file = nil;
    int frame;
    int width;
    int height;
    int channels;
    NSData *bytes;

    memset(&preview, 0, sizeof preview);
    rc = FitsPreviewLoadURL(request.fileURL, 16384, 32, &preview);
    if (rc != 0 || preview.kind == FITS_PREVIEW_NONE) {
        fits_preview_free(&preview);
        handler(nil, FitsPreviewNoImageError());
        return;
    }
    if (preview.kind == FITS_PREVIEW_CUBE) {
        file = FitsPreviewWriteMovie(&preview, nil);
    }
    if (file != nil) {
        fits_preview_free(&preview);
        handler([[QLPreviewReply alloc] initWithFileURL:file], nil);
        return;
    }
    frame = preview.nframes / 2;
    if (frame < 0 || frame >= preview.nframes) {
        frame = 0;
    }
    width = preview.width;
    height = preview.height;
    channels = preview.channels;
    bytes = [NSData dataWithBytes:preview.pixels +
                                  (NSUInteger)frame * (NSUInteger)width *
                                      (NSUInteger)height * (NSUInteger)channels
                           length:(NSUInteger)width * (NSUInteger)height *
                                  (NSUInteger)channels];
    fits_preview_free(&preview);
    if (bytes == nil || width < 1 || height < 1) {
        handler(nil, FitsPreviewNoImageError());
        return;
    }
    reply_still(bytes, width, height, channels, handler);
}

@end
