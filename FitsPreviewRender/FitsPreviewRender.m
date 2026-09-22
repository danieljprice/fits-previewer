/*
 * FitsPreviewRender.m
 *
 * Encode 8-bit preview planes as PNG or H.264. Grayscale stays one channel
 * in the PNG. Movies are padded to even sizes because H.264 requires it.
 */

#import "FitsPreviewRender.h"

#include <stdio.h>
#include <string.h>

#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>
#import <ImageIO/ImageIO.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

/* First movie was 12 fps. setpts=4.*PTS holds each plane four times longer. */
static const int kMovieTimescale = 12;
static const int kMovieHold = 4;

/* A file in the temporary directory. A stem keeps the viewer's title;
 * nil uses a unique name so two previews do not collide. */
static NSURL *temporary_file(NSString *stem, NSString *extension)
{
    NSString *base;
    NSString *name;
    NSURL *url;

    if (stem.length == 0) {
        base = NSUUID.UUID.UUIDString;
    } else {
        base = [[stem componentsSeparatedByCharactersInSet:
                    [NSCharacterSet characterSetWithCharactersInString:@"/:\\"]]
            componentsJoinedByString:@"-"];
        if (base.length == 0) {
            base = @"FitsPreview";
        }
    }
    name = [base stringByAppendingPathExtension:extension];
    url = [NSURL fileURLWithPath:[NSTemporaryDirectory() stringByAppendingPathComponent:name]];
    /* A second drop of the same file replaces the previous export. */
    if (stem.length > 0) {
        [[NSFileManager defaultManager] removeItemAtURL:url error:nil];
    }
    return url;
}

/* Free a buffer handed to a CGDataProvider. */
static void release_bytes(void *info, const void *data, size_t size)
{
    (void)info;
    (void)size;
    free((void *)data);
}

/* Build a CGImage for one frame. The image owns its pixel copy. */
CGImageRef FitsPreviewCreateImage(const unsigned char *pixels, int width, int height,
                                  int channels)
{
    CGColorSpaceRef space;
    CGBitmapInfo info;
    CGDataProviderRef provider;
    CGImageRef image;
    unsigned char *copy;
    size_t count = (size_t)width * (size_t)height;
    size_t bytesPerRow;
    size_t i;
    int bpp;

    if (channels == 1) {
        copy = malloc(count);
        if (copy == NULL) {
            return NULL;
        }
        memcpy(copy, pixels, count);
        space = CGColorSpaceCreateDeviceGray();
        info = (CGBitmapInfo)kCGImageAlphaNone;
        bytesPerRow = (size_t)width;
        bpp = 8;
    } else {
        copy = malloc(count * 4);
        if (copy == NULL) {
            return NULL;
        }
        for (i = 0; i < count; i++) {
            copy[i * 4] = pixels[i * 3];
            copy[i * 4 + 1] = pixels[i * 3 + 1];
            copy[i * 4 + 2] = pixels[i * 3 + 2];
            copy[i * 4 + 3] = 255;
        }
        space = CGColorSpaceCreateDeviceRGB();
        info = (CGBitmapInfo)kCGImageAlphaNoneSkipLast;
        bytesPerRow = (size_t)width * 4;
        bpp = 32;
    }
    provider = CGDataProviderCreateWithData(NULL, copy, bytesPerRow * (size_t)height,
                                            release_bytes);
    if (provider == NULL) {
        free(copy);
        CGColorSpaceRelease(space);
        return NULL;
    }
    image = CGImageCreate((size_t)width, (size_t)height, 8, bpp, bytesPerRow, space, info,
                          provider, NULL, false, kCGRenderingIntentDefault);
    CGDataProviderRelease(provider);
    CGColorSpaceRelease(space);
    return image;
}

/* Error Quick Look shows as an empty preview rather than a fake page. */
NSError *FitsPreviewNoImageError(void)
{
    return [NSError errorWithDomain:@"com.fitspreviewer"
                               code:1
                           userInfo:@{
                               NSLocalizedDescriptionKey: @"No image in this FITS file"
                           }];
}

/* macOS only looks at the last suffix, so file.fits.gz is typed as gzip.
 * Refuse every other .gz name here, before the file is opened. */
static BOOL refused_gzip(NSURL *url)
{
    NSString *name = url.lastPathComponent.lowercaseString;

    if (![name hasSuffix:@".gz"]) {
        return NO;
    }
    return !([name hasSuffix:@".fits.gz"] || [name hasSuffix:@".fit.gz"] ||
             [name hasSuffix:@".fts.gz"]);
}

/* Load pixels, holding a security scope only for the read. */
int FitsPreviewLoadURL(NSURL *url, int maxEdge, int maxFrames, fits_preview *out)
{
    BOOL scoped;
    int rc;

    if (out == NULL || refused_gzip(url)) {
        if (out != NULL) {
            memset(out, 0, sizeof(*out));
        }
        return 1;
    }
    scoped = [url startAccessingSecurityScopedResource];
    rc = fits_preview_load(url.fileSystemRepresentation, maxEdge, maxFrames, out);

    if (scoped) {
        [url stopAccessingSecurityScopedResource];
    }
    return rc;
}

/* Write one frame as a PNG. The buffer is copied into the image first. */
NSURL *FitsPreviewWritePNG(const fits_preview *preview, int frame, NSString *name)
{
    NSURL *url;
    CGImageRef image;
    CGImageDestinationRef dest;
    const unsigned char *plane;
    size_t planeBytes;
    int index = frame;

    if (preview == NULL || preview->pixels == NULL || preview->width < 1) {
        return nil;
    }
    if (index < 0 || index >= preview->nframes) {
        index = 0;
    }
    planeBytes = (size_t)preview->width * (size_t)preview->height *
                 (size_t)preview->channels;
    plane = preview->pixels + planeBytes * (size_t)index;
    image = FitsPreviewCreateImage(plane, preview->width, preview->height,
                                  preview->channels);
    if (image == NULL) {
        return nil;
    }
    url = temporary_file(name, @"png");
    dest = CGImageDestinationCreateWithURL((__bridge CFURLRef)url,
                                           (__bridge CFStringRef)UTTypePNG.identifier,
                                           1, NULL);
    if (dest == NULL) {
        CGImageRelease(image);
        return nil;
    }
    CGImageDestinationAddImage(dest, image, NULL);
    if (!CGImageDestinationFinalize(dest)) {
        CFRelease(dest);
        CGImageRelease(image);
        return nil;
    }
    CFRelease(dest);
    CGImageRelease(image);
    return url;
}

/* Round up to the next even size, and never below 2. */
static int even_size(int n)
{
    if (n < 2) {
        return 2;
    }
    if (n % 2) {
        return n + 1;
    }
    return n;
}

/* Copy one plane into a BGRA buffer. An odd axis is padded with black. */
static void fill_bgra(CVPixelBufferRef buffer, const fits_preview *preview, int frame,
                      int outWidth, int outHeight)
{
    uint8_t *base;
    size_t bytesPerRow;
    size_t planeBytes;
    const unsigned char *src;
    int y;
    int x;
    int channels = preview->channels;

    CVPixelBufferLockBaseAddress(buffer, 0);
    base = CVPixelBufferGetBaseAddress(buffer);
    bytesPerRow = CVPixelBufferGetBytesPerRow(buffer);
    memset(base, 0, bytesPerRow * (size_t)outHeight);
    planeBytes = (size_t)preview->width * (size_t)preview->height * (size_t)channels;
    src = preview->pixels + planeBytes * (size_t)frame;
    for (y = 0; y < preview->height && y < outHeight; y++) {
        uint8_t *row = base + bytesPerRow * (size_t)y;

        for (x = 0; x < preview->width && x < outWidth; x++) {
            const unsigned char *pix = src + ((size_t)y * (size_t)preview->width +
                                              (size_t)x) * (size_t)channels;
            uint8_t *dst = row + (size_t)x * 4;

            if (channels == 1) {
                dst[0] = pix[0];
                dst[1] = pix[0];
                dst[2] = pix[0];
            } else {
                dst[0] = pix[2];
                dst[1] = pix[1];
                dst[2] = pix[0];
            }
            dst[3] = 255;
        }
    }
    CVPixelBufferUnlockBaseAddress(buffer, 0);
}

/* Encode every frame as H.264. Nil means the caller should fall back to a still. */
NSURL *FitsPreviewWriteMovie(const fits_preview *preview, NSString *name)
{
    NSURL *url;
    NSError *error = nil;
    AVAssetWriter *writer;
    AVAssetWriterInput *input;
    AVAssetWriterInputPixelBufferAdaptor *adaptor;
    dispatch_semaphore_t done;
    int outWidth;
    int outHeight;
    int frame = 0;
    int spins = 0;
    int fps;
    NSDictionary *settings;
    NSDictionary *bufferAttrs;

    if (preview == NULL || preview->nframes < 2 || preview->pixels == NULL) {
        return nil;
    }
    outWidth = even_size(preview->width);
    outHeight = even_size(preview->height);
    fps = kMovieTimescale / kMovieHold;
    url = temporary_file(name, @"mp4");
    writer = [AVAssetWriter assetWriterWithURL:url fileType:AVFileTypeMPEG4 error:&error];
    if (writer == nil) {
        return nil;
    }
    settings = @{
        AVVideoCodecKey: AVVideoCodecTypeH264,
        AVVideoWidthKey: @(outWidth),
        AVVideoHeightKey: @(outHeight),
        AVVideoCompressionPropertiesKey: @{
            AVVideoAverageBitRateKey: @(2000000),
            AVVideoExpectedSourceFrameRateKey: @(fps)
        }
    };
    input = [AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeVideo
                                                 outputSettings:settings];
    input.expectsMediaDataInRealTime = NO;
    bufferAttrs = @{
        (id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA),
        (id)kCVPixelBufferWidthKey: @(outWidth),
        (id)kCVPixelBufferHeightKey: @(outHeight)
    };
    adaptor = [AVAssetWriterInputPixelBufferAdaptor
        assetWriterInputPixelBufferAdaptorWithAssetWriterInput:input
                                   sourcePixelBufferAttributes:bufferAttrs];
    if (![writer canAddInput:input]) {
        return nil;
    }
    [writer addInput:input];
    if (![writer startWriting]) {
        return nil;
    }
    [writer startSessionAtSourceTime:kCMTimeZero];
    while (frame < preview->nframes) {
        CVPixelBufferRef buffer = NULL;
        CMTime when = CMTimeMake((int64_t)frame * kMovieHold, kMovieTimescale);

        if (![input isReadyForMoreMediaData]) {
            if (++spins > 500) {
                [writer cancelWriting];
                return nil;
            }
            [NSThread sleepForTimeInterval:0.01];
            continue;
        }
        spins = 0;
        if (CVPixelBufferPoolCreatePixelBuffer(NULL, adaptor.pixelBufferPool, &buffer) !=
                kCVReturnSuccess ||
            buffer == NULL) {
            [writer cancelWriting];
            return nil;
        }
        fill_bgra(buffer, preview, frame, outWidth, outHeight);
        if (![adaptor appendPixelBuffer:buffer withPresentationTime:when]) {
            CVPixelBufferRelease(buffer);
            [writer cancelWriting];
            return nil;
        }
        CVPixelBufferRelease(buffer);
        frame++;
    }
    /* The last sample's timestamp is not its end. Hold that plane too. */
    [writer endSessionAtSourceTime:CMTimeMake((int64_t)preview->nframes * kMovieHold,
                                             kMovieTimescale)];
    [input markAsFinished];
    done = dispatch_semaphore_create(0);
    [writer finishWritingWithCompletionHandler:^{
        dispatch_semaphore_signal(done);
    }];
    dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
    if (writer.status != AVAssetWriterStatusCompleted) {
        return nil;
    }
    return url;
}
