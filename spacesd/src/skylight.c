#include "skylight.h"
#include "papp.h"
#include <libproc.h>
#include <ApplicationServices/ApplicationServices.h>
#include <CoreServices/CoreServices.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

extern AXError _AXUIElementGetWindow(AXUIElementRef ref, uint32_t* wid);

#define kCPSUserGenerated 0x200
extern CGError SLSGetConnectionPSN(int cid, ProcessSerialNumber* psn);
extern CGError _SLPSSetFrontProcessWithOptions(ProcessSerialNumber* psn,
                                               uint32_t wid, uint32_t mode);
extern CGError _SLPSGetFrontProcess(ProcessSerialNumber* psn);

// Front-process handoff via the exported SkyLight symbol (yabai
// window_manager.c:1320). Not the Dock setFrontWindow offset — this is a
// stable symbol, no version scanning. No synthetic events, no app reopen.
bool sls_window_set_front(uint32_t wid) {
  int wcid = 0;
  if (SLSGetWindowOwner(g_cid, wid, &wcid) != kCGErrorSuccess || !wcid)
    return false;
  ProcessSerialNumber psn = {0};
  if (SLSGetConnectionPSN(wcid, &psn) != kCGErrorSuccess) return false;
  return _SLPSSetFrontProcessWithOptions(&psn, wid, kCPSUserGenerated)
         == kCGErrorSuccess;
}


// Compared by PSN on both sides (yabai window_manager_focused_application), so a PWA's
// helper-vs-app pid ambiguity cannot give a false answer.
bool sls_window_is_front(uint32_t wid) {
  int wcid = 0;
  if (SLSGetWindowOwner(g_cid, wid, &wcid) != kCGErrorSuccess || !wcid) return false;
  ProcessSerialNumber wpsn = {0}, fpsn = {0};
  if (SLSGetConnectionPSN(wcid, &wpsn) != kCGErrorSuccess) return false;
  if (_SLPSGetFrontProcess(&fpsn) != kCGErrorSuccess) return false;
  return wpsn.highLongOfPSN == fpsn.highLongOfPSN
         && wpsn.lowLongOfPSN == fpsn.lowLongOfPSN;
}

// Top-most normal (layer 0) on-screen window id, or 0. Used to tell whether
// the target is actually the visually front window — the pid/active-app check
// is unreliable for PWAs whose window is owned by a helper process.
uint32_t sls_frontmost_window(void) {
  CFArrayRef wl = CGWindowListCopyWindowInfo(
      kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements,
      kCGNullWindowID);
  if (!wl) return 0;
  uint32_t top = 0;
  for (CFIndex i = 0; i < CFArrayGetCount(wl); i++) {
    CFDictionaryRef w = CFArrayGetValueAtIndex(wl, i);
    CFNumberRef lv = CFDictionaryGetValue(w, kCGWindowLayer);
    int layer = 99;
    if (lv) CFNumberGetValue(lv, kCFNumberIntType, &layer);
    if (layer != 0) continue; // skip menubar/bar/overlays
    CFNumberRef n = CFDictionaryGetValue(w, kCGWindowNumber);
    if (n) CFNumberGetValue(n, kCFNumberSInt32Type, &top);
    break;
  }
  CFRelease(wl);
  return top;
}

int g_cid = 0;

static sls_space_cb g_space_cb = NULL;

// Signature per SLSRegisterNotifyProc usage in sketchybar sketchybar.c:187.
static void space_events(uint32_t event, void* data, size_t data_length, void* context) {
  (void)event; (void)data; (void)data_length; (void)context;
  if (g_space_cb) g_space_cb();
}

void sls_init(void) {
  g_cid = SLSMainConnectionID();
}

void sls_register_space_notifications(sls_space_cb cb) {
  g_space_cb = cb;
  SLSRegisterNotifyProc((void*)space_events, 1327, NULL);
  SLSRegisterNotifyProc((void*)space_events, 1328, NULL);
}

// The synthetic key-window events yabai posts (window_manager_make_key_window): set_front alone
// changes only the front process, which apps that gate on real activation show as inactive.
extern CGError SLPSPostEventRecordTo(ProcessSerialNumber* psn, uint8_t* bytes);
void sls_window_make_key(uint32_t wid) {
  int wcid = 0;
  if (SLSGetWindowOwner(g_cid, wid, &wcid) != kCGErrorSuccess || !wcid) return;
  ProcessSerialNumber psn = {0};
  if (SLSGetConnectionPSN(wcid, &psn) != kCGErrorSuccess) return;
  uint8_t bytes[0xf8] = {0};
  bytes[0x04] = 0xf8;
  bytes[0x3a] = 0x10;
  memcpy(bytes + 0x3c, &wid, sizeof(uint32_t));
  memset(bytes + 0x20, 0xff, 0x10);
  bytes[0x08] = 0x01;
  SLPSPostEventRecordTo(&psn, bytes);
  bytes[0x08] = 0x02;
  SLPSPostEventRecordTo(&psn, bytes);
}

// Since macOS 14 only Accessibility drives an app's AppKit activation (coloured traffic lights,
// keyboard input); the window-server calls do not. False without the Accessibility grant.
bool sls_window_ax_focus(pid_t pid, uint32_t wid) {
  if (!AXIsProcessTrusted()) return false;
  AXUIElementRef app = AXUIElementCreateApplication(pid);
  if (!app) return false;
  AXUIElementSetAttributeValue(app, kAXFrontmostAttribute, kCFBooleanTrue);
  CFArrayRef windows = NULL;
  bool ok = false;
  if (AXUIElementCopyAttributeValue(app, kAXWindowsAttribute,
                                    (CFTypeRef*)&windows) == kAXErrorSuccess
      && windows) {
    for (CFIndex i = 0; i < CFArrayGetCount(windows); i++) {
      AXUIElementRef w = (AXUIElementRef)CFArrayGetValueAtIndex(windows, i);
      uint32_t wwid = 0;
      if (_AXUIElementGetWindow(w, &wwid) == kAXErrorSuccess && wwid == wid) {
        AXUIElementSetAttributeValue(w, kAXMainAttribute, kCFBooleanTrue);
        AXUIElementPerformAction(w, kAXRaiseAction);
        ok = true;
        break;
      }
    }
    CFRelease(windows);
  }
  CFRelease(app);
  return ok;
}

// True if this process holds Accessibility permission.
bool sls_ax_trusted(void) { return AXIsProcessTrusted(); }

// Register this process in the Accessibility list and show the system prompt
// pointing the user to System Settings to enable it. Safe to call repeatedly.
bool sls_ax_prompt(void) {
  const void* keys[] = {kAXTrustedCheckOptionPrompt};
  const void* vals[] = {kCFBooleanTrue};
  CFDictionaryRef opts = CFDictionaryCreate(NULL, keys, vals, 1,
      &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
  bool t = AXIsProcessTrustedWithOptions(opts);
  CFRelease(opts);
  return t;
}

static pid_t dock_pid(void) {
  static _Atomic pid_t cached = 0;
  char name[64];
  pid_t cur = atomic_load_explicit(&cached, memory_order_relaxed);
  if (cur > 0 && proc_name(cur, name, sizeof(name)) > 0
      && strcmp(name, "Dock") == 0)
    return cur;
  pid_t found = 0;
  pid_t pids[4096];
  int n = proc_listallpids(pids, sizeof(pids));
  for (int i = 0; i < n; i++) {
    if (proc_name(pids[i], name, sizeof(name)) > 0 && strcmp(name, "Dock") == 0) {
      struct proc_bsdshortinfo si;
      if (proc_pidinfo(pids[i], PROC_PIDT_SHORTBSDINFO, 0, &si, sizeof(si)) == sizeof(si)
          && si.pbsi_uid == getuid()) {
        found = pids[i];
        break;
      }
    }
  }
  atomic_store_explicit(&cached, found, memory_order_relaxed);
  return found;
}

static void ax_copy_string(AXUIElementRef el, CFStringRef attr, char* buf, size_t sz) {
  buf[0] = '\0';
  CFTypeRef v = NULL;
  if (AXUIElementCopyAttributeValue(el, attr, &v) != kAXErrorSuccess || !v) return;
  if (CFGetTypeID(v) == CFStringGetTypeID())
    CFStringGetCString((CFStringRef)v, buf, (CFIndex)sz, kCFStringEncodingUTF8);
  CFRelease(v);
}

int sls_dock_badges(struct dock_badge* out, int max) {
  if (!AXIsProcessTrusted()) return -1;
  pid_t pid = dock_pid();
  if (pid <= 0) return -1;
  AXUIElementRef dock = AXUIElementCreateApplication(pid);
  if (!dock) return -1;
  // A wedged Dock must not stall the caller (the 5s sweep runs on main).
  AXUIElementSetMessagingTimeout(dock, 0.5f);
  int n = 0;
  CFArrayRef top = NULL;
  if (AXUIElementCopyAttributeValue(dock, kAXChildrenAttribute, (CFTypeRef*)&top)
          == kAXErrorSuccess && top) {
    for (CFIndex i = 0; i < CFArrayGetCount(top); i++) {
      AXUIElementRef list = (AXUIElementRef)CFArrayGetValueAtIndex(top, i);
      CFArrayRef items = NULL;
      if (AXUIElementCopyAttributeValue(list, kAXChildrenAttribute, (CFTypeRef*)&items)
              != kAXErrorSuccess || !items)
        continue;
      for (CFIndex j = 0; j < CFArrayGetCount(items) && n < max; j++) {
        AXUIElementRef it = (AXUIElementRef)CFArrayGetValueAtIndex(items, j);
        ax_copy_string(it, kAXTitleAttribute, out[n].title, sizeof(out[n].title));
        if (!out[n].title[0]) continue;
        ax_copy_string(it, CFSTR("AXStatusLabel"), out[n].label, sizeof(out[n].label));
        n++;
      }
      CFRelease(items);
    }
    CFRelease(top);
  }
  CFRelease(dock);
  return n;
}
