/*
 * AppDelegate.m
 *
 * Shows a short note on launch. Previewing happens in Finder, not here.
 */

#import "AppDelegate.h"

@implementation AppDelegate

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
- (void)applicationDidFinishLaunching:(NSNotification *)notification
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

    (void)notification;
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

/* Closing the note exits. There is nothing else for the host app to do. */
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender
{
    (void)sender;
    return YES;
}

@end
