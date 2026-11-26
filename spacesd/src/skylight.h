// skylight.h — private SkyLight externs + minimal wrappers.
//
// Every declaration below is copied from a maintained reference so future
// macOS-drift fixes can be diffed mechanically:
//   sketchybar src/misc/extern.h, src/sketchybar.c (notification codes)
//   yabai      src/misc/extern.h, src/space.c (window query pattern)
#pragma once
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>

extern int g_cid; // main SLS connection, set by sls_init()

extern int SLSMainConnectionID(void);
extern CGError SLSRegisterNotifyProc(void* callback, uint32_t event, void* context);
extern CFArrayRef SLSCopyManagedDisplaySpaces(int cid);
extern uint64_t SLSManagedDisplayGetCurrentSpace(int cid, CFStringRef display_uuid);
extern uint64_t SLSGetActiveSpace(int cid);
extern int SLSSpaceGetType(int cid, uint64_t sid);
extern CFArrayRef SLSCopyWindowsWithOptionsAndTags(int cid, uint32_t owner, CFArrayRef spaces, uint32_t options, uint64_t* set_tags, uint64_t* clear_tags);
extern CFTypeRef SLSWindowQueryWindows(int cid, CFArrayRef windows, int count);
extern CFTypeRef SLSWindowQueryResultCopyWindows(CFTypeRef query);
extern bool SLSWindowIteratorAdvance(CFTypeRef iterator);
extern uint32_t SLSWindowIteratorGetParentID(CFTypeRef iterator);
extern uint32_t SLSWindowIteratorGetWindowID(CFTypeRef iterator);
extern uint64_t SLSWindowIteratorGetTags(CFTypeRef iterator);
extern uint64_t SLSWindowIteratorGetAttributes(CFTypeRef iterator);
extern CGError SLSGetWindowOwner(int cid, uint32_t wid, int* window_cid);
extern CGError SLSConnectionGetPID(int cid, pid_t* pid);

void sls_init(void);

// Front-process handoff (stable SkyLight symbol). Returns success.
bool sls_window_set_front(uint32_t wid);
// Top-most normal on-screen window id (0 if none).
uint32_t sls_frontmost_window(void);
// True when wid's process is the window server's front process (PSN compare).
bool sls_window_is_front(uint32_t wid);
// Deliver real keyboard focus by posting synthetic window-key events.
void sls_window_make_key(uint32_t wid);
// Activate app + make window key via Accessibility (needs the permission).
bool sls_window_ax_focus(pid_t pid, uint32_t wid);
// True if this process holds Accessibility permission.
bool sls_ax_trusted(void);
// Add this process to the Accessibility list and show the grant prompt.
bool sls_ax_prompt(void);
// Dock icon titles + badges via Accessibility. Returns the count, or -1
// without the permission or a Dock process.
struct dock_badge;
int sls_dock_badges(struct dock_badge* out, int max);

// Space-change notifications. Codes 1327/1328 are what sketchybar itself
// registers for space changes on macOS 13+ (sketchybar.c:223-226). The
// callback fires on the run loop of the registering thread (main).
typedef void (*sls_space_cb)(void);
void sls_register_space_notifications(sls_space_cb cb);

// Window filter copied verbatim from sketchybar app_windows.c
// iterator_window_suitable — the battle-tested tag constants.
static inline bool sls_iterator_window_suitable(CFTypeRef iterator) {
  uint64_t tags = SLSWindowIteratorGetTags(iterator);
  uint64_t attributes = SLSWindowIteratorGetAttributes(iterator);
  uint32_t parent_wid = SLSWindowIteratorGetParentID(iterator);
  if (((parent_wid == 0)
        && ((attributes & 0x2)
          || (tags & 0x400000000000000))
        && (((tags & 0x1))
          || ((tags & 0x2)
            && (tags & 0x80000000))))) {
    return true;
  }
  return false;
}
