/*
 * AppDelegate.m
 *
 * Shows a short note on launch. A FITS file dropped on the Dock icon is
 * written as a PNG or a movie and opened in the default viewer.
 */

#import "AppDelegate.h"

#include <limits.h>
#include <string.h>

#import "FitsPreviewRender.h"

@interface AppDelegate ()
@property (assign) BOOL hideHelp;
@property (assign) BOOL didLaunch;
@property (assign) BOOL opening;
@property (assign) BOOL openedAny;
@property (assign) BOOL openFailed;
@property (assign) NSInteger pendingOpens;
@end

@implementation AppDelegate

/* True for the names this app previews. A plain .gz is not one of them. */
static BOOL fits_name(NSURL *url)
{
    NSString *name = url.lastPathComponent.lowercaseString;

    if ([name hasSuffix:@".gz"]) {
        return [name hasSuffix:@".fits.gz"] || [name hasSuffix:@".fit.gz"] ||
               [name hasSuffix:@".fts.gz"];
    }
    return [name hasSuffix:@".fits"] || [name hasSuffix:@".fit"] ||
           [name hasSuffix:@".fts"];
}

/* The observation name, without .fits or .gz, for the viewer's title. */
static NSString *preview_stem(NSURL *url)
{
    NSString *name = url.lastPathComponent;
    NSString *lower = name.lowercaseString;

    if ([lower hasSuffix:@".gz"]) {
        name = [name stringByDeletingPathExtension];
        lower = name.lowercaseString;
    }
    if ([lower hasSuffix:@".fits"] || [lower hasSuffix:@".fit"] ||
        [lower hasSuffix:@".fts"]) {
        name = [name stringByDeletingPathExtension];
    }
    if (name.length == 0) {
        name = @"FitsPreview";
    }
    return name;
}

/* A short modal note. The help window stays up after this returns. */
static void show_alert(NSString *message)
{
    NSAlert *alert = [[NSAlert alloc] init];

    alert.messageText = message;
    alert.alertStyle = NSAlertStyleWarning;
    [alert addButtonWithTitle:@"OK"];
    [NSApp activateIgnoringOtherApps:YES];
    [alert runModal];
}

/* The host app has no nib, so the menu bar is built here. */
- (void)applicationWillFinishLaunching:(NSNotification *)notification
{
    NSMenu *bar = [NSMenu new];
    NSMenuItem *appItem = [NSMenuItem new];
    NSMenu *appMenu = [[NSMenu alloc] initWithTitle:@"FitsPreviewer"];
    NSMenuItem *quit = [[NSMenuItem alloc] initWithTitle:@"Quit FitsPreviewer"
                                                  action:@selector(terminate:)
                                           keyEquivalent:@"q"];

    (void)notification;
    quit.target = NSApp;
    [appMenu addItem:quit];
    appItem.submenu = appMenu;
    [bar addItem:appItem];
    [NSApp setMainMenu:bar];
}

/* Open a small window that tells the user how to turn the preview on. */
- (void)showHelp
{
    NSRect frame = NSMakeRect(0, 0, 520, 280);
    NSTextField *label;
    NSButton *ok;
    NSImageView *logo;
    NSImage *icon;
    CGFloat iconSide = 48;
    CGFloat textWidth = 472;
    NSSize textSize;
    CGFloat labelY;
    CGFloat contentHeight;

    if (self.window != nil) {
        [self.window makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];
        return;
    }
    self.window = [[NSWindow alloc]
        initWithContentRect:frame
                  styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                            NSWindowStyleMaskMiniaturizable
                    backing:NSBackingStoreBuffered
                      defer:NO];
    self.window.title = @"FitsPreviewer";
    label = [NSTextField labelWithString:@""];
    label.selectable = YES;
    label.font = [NSFont systemFontOfSize:14];
    label.cell.wraps = YES;
    label.cell.scrollable = NO;
    label.stringValue =
        @"FitsPreviewer is running.\n\n"
        @"In Finder, select a .fits, .fit, .fts, or .fits.gz file and press the spacebar. "
        @"If Finder shows nothing, open System Settings → General → "
        @"Login Items & Extensions → Quick Look, and enable FitsPreviewer.\n\n"
        @"Copyright (c) 2026 Daniel Price\n\n"
        @"Please report bugs to https://github.com/danieljprice/fits-previewer\n"
        @"and give a star on Github or buy me a coffee if you like it!";
    textSize = [label.cell cellSizeForBounds:NSMakeRect(0, 0, textWidth, CGFLOAT_MAX)];

    /* OK ends the app. The Quick Look extension stays registered. */
    ok = [NSButton buttonWithTitle:@"OK" target:NSApp action:@selector(terminate:)];
    ok.keyEquivalent = @"\r";
    [ok sizeToFit];
    ok.frame = NSMakeRect(NSWidth(frame) - MAX(NSWidth(ok.frame), 80) - 20,
                          16,
                          MAX(NSWidth(ok.frame), 80),
                          NSHeight(ok.frame));
    [self.window.contentView addSubview:ok];

    labelY = NSMaxY(ok.frame) + 12;
    label.frame = NSMakeRect(24, labelY, textWidth, textSize.height);
    [self.window.contentView addSubview:label];

    /* The icon sits centred above the note. */
    icon = [[NSImage imageNamed:NSImageNameApplicationIcon] copy];
    logo = [NSImageView imageViewWithImage:icon];
    logo.imageScaling = NSImageScaleProportionallyUpOrDown;
    logo.frame = NSMakeRect((NSWidth(frame) - iconSide) / 2.0,
                            NSMaxY(label.frame) + 12,
                            iconSide,
                            iconSide);
    [self.window.contentView addSubview:logo];
    contentHeight = NSMaxY(logo.frame) + 16;
    [self.window setContentSize:NSMakeSize(NSWidth(frame), contentHeight)];
    [self.window center];
    [self.window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
}

/* Load one file and write a PNG or a movie of every cube plane. */
- (NSURL *)exportPreview:(NSURL *)url
{
    fits_preview preview;
    NSURL *file = nil;
    int rc;
    int frame;

    if (!fits_name(url)) {
        show_alert(@"FitsPreviewer opens .fits, .fit, .fts, and .fits.gz files.");
        return nil;
    }
    memset(&preview, 0, sizeof preview);
    rc = FitsPreviewLoadURL(url, 16384, INT_MAX, &preview);
    if (rc != 0 || preview.kind == FITS_PREVIEW_NONE || preview.pixels == NULL) {
        fits_preview_free(&preview);
        show_alert(@"No image in this FITS file");
        return nil;
    }
    if (preview.kind == FITS_PREVIEW_CUBE) {
        file = FitsPreviewWriteMovie(&preview, preview_stem(url));
    }
    if (file == nil) {
        frame = preview.nframes / 2;
        if (frame < 0 || frame >= preview.nframes) {
            frame = 0;
        }
        file = FitsPreviewWritePNG(&preview, frame, preview_stem(url));
    }
    fits_preview_free(&preview);
    if (file == nil) {
        show_alert(@"Could not write a preview of this FITS file");
    }
    return file;
}

/* Quit after every drop has opened. A failure leaves the help window up. */
- (void)finishDrop
{
    if (self.pendingOpens > 0) {
        return;
    }
    if (self.openFailed || !self.openedAny) {
        if (self.didLaunch) {
            [self showHelp];
        }
        return;
    }
    self.hideHelp = YES;
    if (self.window != nil) {
        [self.window close];
    } else if (self.didLaunch) {
        [NSApp terminate:nil];
    }
}

/* Ask the default viewer to open the export. The temp file stays put. */
- (void)openInViewer:(NSURL *)file
{
    NSWorkspaceOpenConfiguration *config = [NSWorkspaceOpenConfiguration configuration];

    self.pendingOpens += 1;
    [[NSWorkspace sharedWorkspace] openURL:file
                              configuration:config
                          completionHandler:^(NSRunningApplication *app, NSError *error) {
        (void)app;
        if (error != nil) {
            self.openFailed = YES;
            show_alert(@"Could not open the preview");
        } else {
            self.openedAny = YES;
        }
        self.pendingOpens -= 1;
        [self finishDrop];
    }];
}

- (void)applicationDidFinishLaunching:(NSNotification *)notification
{
    (void)notification;
    self.didLaunch = YES;
    if (self.opening) {
        [self finishDrop];
        return;
    }
    [self showHelp];
}

/* A drop or Open. A successful export skips the help window. */
- (void)application:(NSApplication *)sender openURLs:(NSArray<NSURL *> *)urls
{
    (void)sender;
    self.opening = YES;
    if (self.pendingOpens == 0) {
        self.openedAny = NO;
        self.openFailed = NO;
    }
    for (NSURL *url in urls) {
        NSURL *file = [self exportPreview:url];

        if (file == nil) {
            self.openFailed = YES;
        } else {
            [self openInViewer:file];
        }
    }
    [self finishDrop];
}

/* Closing the note exits. There is nothing else for the host app to do. */
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender
{
    (void)sender;
    return YES;
}

@end
