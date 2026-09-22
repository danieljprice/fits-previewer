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

/* True when this launch was asked to open files (Dock drop or Open With). */
static BOOL launch_opens_documents(void)
{
    NSAppleEventDescriptor *event =
        [[NSAppleEventManager sharedAppleEventManager] currentAppleEvent];

    if (event == nil) {
        return NO;
    }
    return [event eventClass] == kCoreEventClass && [event eventID] == kAEOpenDocuments;
}

/* Ask Quick Look to reload extensions after a fresh launch. */
static void refresh_quicklook(void)
{
    NSTask *task = [[NSTask alloc] init];

    task.executableURL = [NSURL fileURLWithPath:@"/usr/bin/qlmanage"];
    task.arguments = @[@"-r"];
    task.standardOutput = [NSFileHandle fileHandleWithNullDevice];
    task.standardError = [NSFileHandle fileHandleWithNullDevice];
    @try {
        [task launch];
    } @catch (NSException *exception) {
        (void)exception;
    }
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
    /* Mark drops before didFinishLaunching, or the help window races ahead. */
    if (launch_opens_documents()) {
        self.opening = YES;
    }
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

    if (self.hideHelp || self.opening) {
        return;
    }
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
        @"Drop a FITS file on the Dock icon to open the preview image or movie.\n\n"
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
    /* Native size and every cube plane. The spacebar preview stays smaller. */
    rc = FitsPreviewLoadURL(url, INT_MAX, INT_MAX, &preview);
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
        self.opening = NO;
        if (self.didLaunch) {
            [self showHelp];
        }
        return;
    }
    self.hideHelp = YES;
    self.opening = NO;
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
        dispatch_async(dispatch_get_main_queue(), ^{
            if (error != nil) {
                self.openFailed = YES;
                show_alert(@"Could not open the preview");
            } else {
                self.openedAny = YES;
            }
            self.pendingOpens -= 1;
            [self finishDrop];
        });
    }];
}

/* Export each dropped FITS file and open the PNG or movie. */
- (void)handleOpenURLs:(NSArray<NSURL *> *)urls
{
    self.opening = YES;
    self.hideHelp = YES;
    if (self.window != nil) {
        [self.window close];
        self.window = nil;
    }
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

- (void)applicationDidFinishLaunching:(NSNotification *)notification
{
    (void)notification;
    self.didLaunch = YES;
    if (self.opening || self.hideHelp) {
        [self finishDrop];
        return;
    }
    /* openURLs can arrive just after this method; wait one turn. */
    dispatch_async(dispatch_get_main_queue(), ^{
        if (self.opening || self.hideHelp || self.pendingOpens > 0) {
            return;
        }
        [self showHelp];
        /* Only a plain launch refreshes Quick Look, not a Dock drop. */
        refresh_quicklook();
    });
}

- (void)application:(NSApplication *)sender openURLs:(NSArray<NSURL *> *)urls
{
    (void)sender;
    [self handleOpenURLs:urls];
}

/*
 * Older Open events still call openFile:. Returning YES stops AppKit from
 * showing "cannot open files in the FITS file format".
 */
- (BOOL)application:(NSApplication *)sender openFile:(NSString *)filename
{
    [self handleOpenURLs:@[ [NSURL fileURLWithPath:filename] ]];
    return YES;
}

- (void)application:(NSApplication *)sender openFiles:(NSArray<NSString *> *)filenames
{
    NSMutableArray<NSURL *> *urls = [NSMutableArray arrayWithCapacity:filenames.count];

    for (NSString *filename in filenames) {
        [urls addObject:[NSURL fileURLWithPath:filename]];
    }
    [self handleOpenURLs:urls];
    [sender replyToOpenOrPrint:NSApplicationDelegateReplySuccess];
}

/* Closing the note exits. There is nothing else for the host app to do. */
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender
{
    (void)sender;
    return YES;
}

@end
