// workspace.m — NSWorkspace observers + app-name lookup (ARC).
// Pattern: sketchybar src/workspace.m.
#include "workspace.h"
#include "proc.h"
#import <AppKit/AppKit.h>

static void (*g_space_changed)(void) = NULL;
static void (*g_woke)(void) = NULL;

@interface SpacesdObserver : NSObject
@end

@implementation SpacesdObserver
- (void)spaceChanged:(NSNotification*)note { if (g_space_changed) g_space_changed(); }
- (void)displayChanged:(NSNotification*)note { if (g_space_changed) g_space_changed(); }
- (void)didWake:(NSNotification*)note { if (g_woke) g_woke(); }
@end

static SpacesdObserver* g_observer = nil;

void workspace_observe(void (*space_changed)(void), void (*woke)(void)) {
  // Without this, a bare daemon's runloop never services the AppKit/CGS
  // notification ports and neither NSWorkspace nor SLSRegisterNotifyProc
  // callbacks are delivered. Same call yabai makes (src/yabai.c:139).
  NSApplicationLoad();
  g_space_changed = space_changed;
  g_woke = woke;
  g_observer = [SpacesdObserver new];
  NSNotificationCenter* nc = [[NSWorkspace sharedWorkspace] notificationCenter];
  [nc addObserver:g_observer selector:@selector(spaceChanged:)
             name:NSWorkspaceActiveSpaceDidChangeNotification object:nil];
  [nc addObserver:g_observer selector:@selector(displayChanged:)
             name:@"NSWorkspaceActiveDisplayDidChangeNotification" object:nil];
  [nc addObserver:g_observer selector:@selector(didWake:)
             name:NSWorkspaceDidWakeNotification object:nil];
}

// Match display name ("Beeper") or bundle name ("Beeper Desktop") — callers
// hold one or the other. nil guards matter: messaging nil returns
// 0 == NSOrderedSame and would match an arbitrary app.
static NSRunningApplication* find_app(const char* name) {
  NSString* want = [NSString stringWithUTF8String:name];
  if (!want) return nil;
  for (NSRunningApplication* app in
       [[NSWorkspace sharedWorkspace] runningApplications]) {
    if (app.activationPolicy != NSApplicationActivationPolicyRegular) continue;
    NSString* ln = app.localizedName;
    NSString* bn = [[app.bundleURL lastPathComponent] stringByDeletingPathExtension];
    if ((ln && [ln caseInsensitiveCompare:want] == NSOrderedSame)
        || (bn && [bn caseInsensitiveCompare:want] == NSOrderedSame))
      return app;
  }
  return nil;
}

bool workspace_activate_app(const char* name) {
  @autoreleasepool {
    NSRunningApplication* app = find_app(name);
    if (!app) return false;
    // LaunchServices activation (open -a semantics: reopen + activate), not
    // -[NSRunningApplication activate]: the latter silently no-ops for PWA
    // and tray-style Electron apps whose window sits on a hidden space.
    if (app.bundleURL) {
      [[NSWorkspace sharedWorkspace]
          openApplicationAtURL:app.bundleURL
                 configuration:[NSWorkspaceOpenConfiguration configuration]
             completionHandler:nil];
      return true;
    }
    return [app activateWithOptions:0]; // options are no-ops on macOS 14+
  }
}



bool workspace_activate_pid(pid_t pid) {
  @autoreleasepool {
    NSRunningApplication* app =
        [NSRunningApplication runningApplicationWithProcessIdentifier:pid];
    return app && [app activateWithOptions:0]; // options are no-ops on macOS 14+
  }
}

pid_t workspace_pid_for_app(const char* name) {
  @autoreleasepool {
    NSRunningApplication* app = find_app(name);
    if (!app) return 0;
    pid_t pid = app.processIdentifier;
    // LaunchServices can register a launch without its pid (-1, seen with Discord on macOS 27).
    if (pid <= 0) {
      pid_t found = proc_pid_for_executable(app.executableURL.path.fileSystemRepresentation);
      if (found) pid = found;
    }
    return pid;
  }
}


char* workspace_copy_app_name(pid_t pid) {
  @autoreleasepool {
    NSRunningApplication* app =
        [NSRunningApplication runningApplicationWithProcessIdentifier:pid];
    const char* name = [[app localizedName] UTF8String];
    return name ? strdup(name) : NULL;
  }
}
