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

/* Fit src into canvas without stretching. */
static CGRect aspect_fit(CGRect canvas, int width, int height)
{
    CGFloat scale = MIN(canvas.size.width / (CGFloat)width,
                        canvas.size.height / (CGFloat)height);
    CGSize fitted = CGSizeMake((CGFloat)width * scale, (CGFloat)height * scale);

    return CGRectMake((canvas.size.width - fitted.width) / 2.0,
                      (canvas.size.height - fitted.height) / 2.0,
                      fitted.width, fitted.height);
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

    contextSize = request.maximumSize;
    if (contextSize.width < 1 || contextSize.height < 1) {
        contextSize = CGSizeMake(width, height);
    }
    if (contextSize.width < request.minimumSize.width) {
        contextSize.width = request.minimumSize.width;
    }
    if (contextSize.height < request.minimumSize.height) {
        contextSize.height = request.minimumSize.height;
    }

    handler([QLThumbnailReply replyWithContextSize:contextSize
                                      drawingBlock:^BOOL(CGContextRef context) {
        CGImageRef image = FitsPreviewCreateImage(bytes.bytes, width, height, channels);

        if (image == NULL) {
            return NO;
        }
        CGContextDrawImage(context,
                           aspect_fit(full_canvas(context, contextSize, request.scale),
                                      width, height),
                           image);
        CGImageRelease(image);
        return YES;
    }],
            nil);
}

@end
