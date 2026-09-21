/*
 * AppDelegate.m
 *
 * Shows a short note on launch. Previewing happens in Finder, not here.
 */

#import "AppDelegate.h"

@implementation AppDelegate

/* Open a small window that tells the user how to turn the preview on. */
- (void)applicationDidFinishLaunching:(NSNotification *)notification
{
    NSRect frame = NSMakeRect(0, 0, 520, 240);
    NSTextField *label;

    (void)notification;
    self.window = [[NSWindow alloc]
        initWithContentRect:frame
                  styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                            NSWindowStyleMaskMiniaturizable
                    backing:NSBackingStoreBuffered
                      defer:NO];
    self.window.title = @"FitsPreviewer";
    label = [[NSTextField alloc] initWithFrame:NSMakeRect(24, 24, 472, 180)];
    label.editable = NO;
    label.bezeled = NO;
    label.drawsBackground = NO;
    label.selectable = YES;
    label.font = [NSFont systemFontOfSize:14];
    label.stringValue =
        @"FitsPreviewer is running.\n\n"
        @"In Finder, select a .fits, .fit, .fts, or .fits.gz file and press the spacebar. "
        @"The first image is shown with no axes. A data cube plays as a short movie. "
        @"A spectrum or a short table is a plain line.\n\n"
        @"If Finder shows nothing, open System Settings, General, "
        @"Login Items & Extensions, Quick Look, and enable FitsPreviewer.";
    [self.window.contentView addSubview:label];
    [self.window center];
    [self.window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
}

@end
