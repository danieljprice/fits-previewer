/*
 * main.m
 *
 * Standard AppKit entry. The delegate is kept for the life of the run loop.
 */

#import <Cocoa/Cocoa.h>

#import "AppDelegate.h"

int main(int argc, const char *argv[])
{
    @autoreleasepool {
        NSApplication *app = [NSApplication sharedApplication];
        AppDelegate *delegate = [AppDelegate new];

        (void)argc;
        (void)argv;
        app.delegate = delegate;
        [app run];
    }
    return 0;
}
