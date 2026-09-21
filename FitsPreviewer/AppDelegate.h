/*
 * AppDelegate.h
 *
 * The host app exists so macOS will load the Quick Look extensions.
 */

#import <Cocoa/Cocoa.h>

@interface AppDelegate : NSObject <NSApplicationDelegate>
@property (strong) NSWindow *window;
@end
