/*
 * ThumbnailProvider.m
 *
 * Draw one plane into the thumbnail context. A cube contributes its
 * brightest plane. Files with no image produce no thumbnail.
 */

#import "ThumbnailProvider.h"

#import "FitsPreviewRender.h"

#include <string.h>

/* Largest side of the thumbnail request, used as the read limit. */
static int edge_for_request(QLFileThumbnailRequest *request)
{
    CGFloat edge = MAX(request.maximumSize.width, request.maximumSize.height);

    if (edge < 1) {
        edge = 256;
    }
    if (edge > 512) {
        edge = 512;
    }
    return (int)edge;
}

/* Round a request edge. A missing edge stays 0 so the fitter can ignore it. */
static int thumb_edge(CGFloat edge)
{
    if (edge < 1.0) {
        return 0;
    }
    return (int)(edge + 0.5);
}

/* Finder's bitmap is contextSize times request.scale, in raw pixels.
 * Drawing only contextSize leaves the picture in the lower left. */
static CGRect full_canvas(CGContextRef context, CGSize contextSize, CGFloat scale)
{
    size_t pixels_wide = CGBitmapContextGetWidth(context);
    size_t pixels_high = CGBitmapContextGetHeight(context);
    CGFloat sx = scale > 1.0 ? scale : 1.0;

    if (pixels_wide > 0 && pixels_high > 0) {
        return CGRectMake(0, 0, (CGFloat)pixels_wide, (CGFloat)pixels_high);
    }
    return CGRectMake(0, 0, contextSize.width * sx, contextSize.height * sx);
}

@implementation ThumbnailProvider

/* Draw one still. One frame means a cube contributes its brightest plane. */
- (void)provideThumbnailForFileRequest:(QLFileThumbnailRequest *)request
                     completionHandler:(void (^)(QLThumbnailReply *, NSError *))handler
{
    fits_preview preview;
    int rc;
    CGSize contextSize;
    NSData *bytes;
    int width;
    int height;
    int channels;
    int thumb_w;
    int thumb_h;

    memset(&preview, 0, sizeof preview);
    rc = FitsPreviewLoadURL(request.fileURL, edge_for_request(request), 1, &preview);
    if (rc != 0 || preview.kind == FITS_PREVIEW_NONE || preview.pixels == NULL) {
        fits_preview_free(&preview);
        handler(nil, FitsPreviewNoImageError());
        return;
    }
    width = preview.width;
    height = preview.height;
    channels = preview.channels;
    bytes = [NSData dataWithBytes:preview.pixels
                           length:(NSUInteger)width * (NSUInteger)height * (NSUInteger)channels];
    fits_preview_free(&preview);

    /* The bitmap matches the picture, so the short side is not padded. */
    thumb_w = width;
    thumb_h = height;
    fits_preview_thumb_size(width, height,
                            thumb_edge(request.maximumSize.width),
                            thumb_edge(request.maximumSize.height),
                            thumb_edge(request.minimumSize.width),
                            thumb_edge(request.minimumSize.height),
                            &thumb_w, &thumb_h);
    contextSize = CGSizeMake(thumb_w, thumb_h);

    handler([QLThumbnailReply replyWithContextSize:contextSize
                                      drawingBlock:^BOOL(CGContextRef context) {
        CGImageRef image = FitsPreviewCreateImage(bytes.bytes, width, height, channels);

        if (image == NULL) {
            return NO;
        }
        CGContextDrawImage(context,
                           full_canvas(context, contextSize, request.scale),
                           image);
        CGImageRelease(image);
        return YES;
    }],
            nil);
}

@end
