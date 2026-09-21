/*
 * PreviewProvider.h
 *
 * Quick Look data preview for FITS files. Images are drawn directly,
 * cubes become a short H.264 movie, and everything else is no preview.
 */

#import <QuickLookUI/QuickLookUI.h>

@interface PreviewProvider : QLPreviewProvider <QLPreviewingController>
@end
