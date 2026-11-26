#include "state.h"
#include "skylight.h"
#include "workspace.h"
#include "yabai_ipc.h"
#include "papp.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

struct kuukan_config g_cfg;
char g_names_path[512];
char g_old_names_path[512];
char g_sock_path[512];
char g_ccm_path[512];
char g_pairs_path[512];
bool g_shadow = false;

struct sp g_sp[MAX_SPACES];
int g_nsp = 0;
int g_ndisplays = 0;

struct win g_win[MAX_WINDOWS];
int g_nwin = 0;

struct name_entry g_names[MAX_NAMES];
int g_nnames = 0;

struct ccm_session g_ccm[MAX_CCM];
int g_nccm = 0;

struct badge g_badges[8];
int g_nbadges = 0;

uint64_t g_gen = 0;
uint64_t g_app_gen = 0;

void logln(const char* fmt, ...) {
  char ts[32];
  time_t t = time(NULL);
  struct tm tm;
  localtime_r(&t, &tm);
  strftime(ts, sizeof(ts), "%H:%M:%S", &tm);
  fprintf(stderr, "[%s] ", ts);
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fputc('\n', stderr);
}

int sp_by_sid(uint64_t sid) {
  for (int i = 0; i < g_nsp; i++) if (g_sp[i].sid == sid) return i;
  return -1;
}

int sp_by_mc(int mc) {
  for (int i = 0; i < g_nsp; i++) if (g_sp[i].mc == mc) return i;
  return -1;
}

int sp_by_uuid(const char* uuid) {
  for (int i = 0; i < g_nsp; i++) if (strcmp(g_sp[i].uuid, uuid) == 0) return i;
  return -1;
}

int win_on_sid(uint64_t sid) {
  int n = 0;
  for (int w = 0; w < g_nwin; w++) if (g_win[w].sid == sid) n++;
  return n;
}

int visible_on_display(int display) {
  for (int i = 0; i < g_nsp; i++)
    if (g_sp[i].display == display && g_sp[i].visible) return i;
  return -1;
}

int active_primary(void) {
  uint64_t active = SLSGetActiveSpace(g_cid);
  int i = sp_by_sid(active);
  if (i < 0) return -1;
  if (g_sp[i].display != 1 && g_sp[i].pair >= 0) return g_sp[i].pair;
  return i;
}

// SLSCopyManagedDisplaySpaces returns per-display dicts in arrangement order.
bool rebuild_spaces(void) {
  CFArrayRef displays = SLSCopyManagedDisplaySpaces(g_cid);
  if (!displays) return false;
  int nsp = 0;
  int ndisp = (int)CFArrayGetCount(displays);
  int mc = 0;
  for (int d = 0; d < ndisp; d++) {
    CFDictionaryRef disp = CFArrayGetValueAtIndex(displays, d);
    CFStringRef ident = CFDictionaryGetValue(disp, CFSTR("Display Identifier"));
    uint64_t cur = ident ? SLSManagedDisplayGetCurrentSpace(g_cid, ident) : 0;
    CFArrayRef spaces = CFDictionaryGetValue(disp, CFSTR("Spaces"));
    if (!spaces) continue;
    int n = (int)CFArrayGetCount(spaces);
    for (int s = 0; s < n && nsp < MAX_SPACES; s++) {
      CFDictionaryRef sd = CFArrayGetValueAtIndex(spaces, s);
      CFNumberRef sid_ref = CFDictionaryGetValue(sd, CFSTR("id64"));
      if (!sid_ref) continue;
      uint64_t sid = 0;
      CFNumberGetValue(sid_ref, kCFNumberSInt64Type, &sid);
      struct sp* e = &g_sp[nsp++];
      memset(e, 0, sizeof(*e));
      e->sid = sid;
      e->display = d + 1;
      e->mc = ++mc;
      e->visible = (sid == cur);
      e->pair = -1;
      CFStringRef uuid_ref = CFDictionaryGetValue(sd, CFSTR("uuid"));
      if (uuid_ref) {
        CFStringGetCString(uuid_ref, e->uuid, sizeof(e->uuid),
                           kCFStringEncodingUTF8);
      } else {
        snprintf(e->uuid, sizeof(e->uuid), "sid-%llu",
                 (unsigned long long)sid);
      }
    }
  }
  CFRelease(displays);
  g_nsp = nsp;
  g_ndisplays = ndisp;
  pair_spaces(g_sp, g_nsp, g_ndisplays);
  return true;
}

// Per-space window sweep matching sketchybar app_windows.c:177-222.
void rebuild_windows(void) {
  int nwin = 0;
  for (int i = 0; i < g_nsp && nwin < MAX_WINDOWS; i++) {
    uint64_t sid = g_sp[i].sid;
    CFNumberRef num = CFNumberCreate(NULL, kCFNumberSInt64Type, &sid);
    CFArrayRef space_list = CFArrayCreate(NULL, (const void**)&num, 1,
                                          &kCFTypeArrayCallBacks);
    CFRelease(num);
    uint64_t set_tags = 1, clear_tags = 0;
    CFArrayRef window_list = SLSCopyWindowsWithOptionsAndTags(
        g_cid, 0, space_list, 0x2, &set_tags, &clear_tags);
    if (window_list) {
      if (CFArrayGetCount(window_list) > 0) {
        CFTypeRef query = SLSWindowQueryWindows(g_cid, window_list, 0x0);
        if (query) {
          CFTypeRef it = SLSWindowQueryResultCopyWindows(query);
          if (it) {
            while (SLSWindowIteratorAdvance(it) && nwin < MAX_WINDOWS) {
              if (!sls_iterator_window_suitable(it)) continue;
              uint32_t wid = SLSWindowIteratorGetWindowID(it);
              int wcid = 0;
              pid_t pid = 0;
              SLSGetWindowOwner(g_cid, wid, &wcid);
              SLSConnectionGetPID(wcid, &pid);
              struct win* w = &g_win[nwin++];
              w->wid = wid;
              w->sid = sid;
              w->pid = pid;
              w->app[0] = '\0';
              char* name = workspace_copy_app_name(pid);
              if (name) {
                snprintf(w->app, sizeof(w->app), "%s", name);
                free(name);
              }
            }
            CFRelease(it);
          }
          CFRelease(query);
        }
      }
      CFRelease(window_list);
    }
    CFRelease(space_list);
  }
  g_nwin = nwin;
}

void wait_rebuild(void) { usleep(120000); rebuild_spaces(); }

int find_new_space(char snapshot[][40], int snap_n) {
  return find_new_space_in(g_sp, g_nsp, snapshot, snap_n);
}

// Destroy by sid inside the Dock; avoids yabai's lagging space index map.
bool destroy_space(int i, char* err, size_t errsz) {
  uint64_t sid = g_sp[i].sid;
  int on_display = 0;
  for (int j = 0; j < g_nsp; j++) if (g_sp[j].display == g_sp[i].display) on_display++;
  if (SLSSpaceGetType(g_cid, sid) != 0 || on_display < 2) {
    snprintf(err, errsz, "refused: not a user space or last on its display");
    return false;
  }
  for (int t = 0; t < 3; t++) {
    int w = 0;
    while (!yabai_sa_space_destroy(sid)) {
      if (++w > 20) { snprintf(err, errsz, "sa socket"); return false; }
      usleep(100000);
    }
    usleep(150000);
    rebuild_spaces();
    if (sp_by_sid(sid) < 0) return true;
  }
  snprintf(err, errsz, "sid %llu survived destroy", (unsigned long long)sid);
  return false;
}

void move_window(uint32_t wid, int i) {
  yabai_sa_window_to_space(g_sp[i].sid, wid);
}

// Confirm yabai's index maps to g_sp[i].uuid before issuing an index-based call.
bool yabai_agrees(int i) {
  char idx[16], out[8192], got[40];
  snprintf(idx, sizeof(idx), "%d", g_sp[i].mc);
  const char* qv[] = {"query", "--spaces", "--space", idx};
  for (int t = 0; t < 8; t++) {
    if (yabai_query(out, sizeof(out), 4, qv)
        && yabai_parse_space_uuid(out, strlen(out), got, sizeof(got))
        && strcmp(got, g_sp[i].uuid) == 0)
      return true;
    usleep(100000);
  }
  logln("yabai index %d is not %.8s, refusing", g_sp[i].mc, g_sp[i].uuid);
  return false;
}

const char* name_for_uuid(const char* uuid) {
  for (int i = 0; i < g_nnames; i++)
    if (strcmp(g_names[i].uuid, uuid) == 0) return g_names[i].name;
  return NULL;
}

static void names_save(void) {
  char tmp[560];
  snprintf(tmp, sizeof(tmp), "%s.tmp", g_names_path);
  FILE* f = fopen(tmp, "w");
  if (!f) { logln("names: cannot write %s", tmp); return; }
  for (int i = 0; i < g_nnames; i++)
    fprintf(f, "%s=%s\n", g_names[i].uuid, g_names[i].name);
  fclose(f);
  rename(tmp, g_names_path);
}

void names_set(const char* uuid, const char* name) {
  for (int i = 0; i < g_nnames; i++) {
    if (strcmp(g_names[i].uuid, uuid) == 0) {
      if (name && *name)
        snprintf(g_names[i].name, sizeof(g_names[i].name), "%s", name);
      else
        g_names[i] = g_names[--g_nnames];
      names_save();
      return;
    }
  }
  if (name && *name && g_nnames < MAX_NAMES) {
    snprintf(g_names[g_nnames].uuid, sizeof(g_names[g_nnames].uuid), "%s", uuid);
    snprintf(g_names[g_nnames].name, sizeof(g_names[g_nnames].name), "%s", name);
    g_nnames++;
    names_save();
  }
}

void names_load_or_migrate(void) {
  FILE* f = fopen(g_names_path, "r");
  if (f) {
    char line[128];
    while (g_nnames < MAX_NAMES && fgets(line, sizeof(line), f)) {
      char* eq = strchr(line, '=');
      char* nl = strchr(line, '\n');
      if (nl) *nl = '\0';
      if (!eq || eq - line >= 40) continue;
      *eq = '\0';
      snprintf(g_names[g_nnames].uuid, sizeof(g_names[g_nnames].uuid), "%s", line);
      snprintf(g_names[g_nnames].name, sizeof(g_names[g_nnames].name), "%s", eq + 1);
      g_nnames++;
    }
    fclose(f);
    logln("names: loaded %d from %s", g_nnames, g_names_path);
    return;
  }
  f = fopen(g_old_names_path, "r");
  if (!f) return;
  char line[128];
  int migrated = 0;
  while (g_nnames < MAX_NAMES && fgets(line, sizeof(line), f)) {
    int idx;
    char nm[64];
    if (sscanf(line, "%d=%63[^\n]", &idx, nm) != 2) continue;
    int i = sp_by_mc(idx);
    if (i < 0 || g_sp[i].display != 1 || g_sp[i].mc <= PINNED) continue;
    snprintf(g_names[g_nnames].uuid, sizeof(g_names[g_nnames].uuid), "%s", g_sp[i].uuid);
    snprintf(g_names[g_nnames].name, sizeof(g_names[g_nnames].name), "%s", nm);
    g_nnames++;
    migrated++;
  }
  fclose(f);
  if (migrated) {
    names_save();
    logln("names: migrated %d entries from %s", migrated, g_old_names_path);
  }
}

bool app_is_running(const char* app) {
  for (int i = 0; i < g_nwin; i++)
    if (strcmp(g_win[i].app, app) == 0) return true;
  return false;
}

const char* badge_for_app(const char* app) {
  for (int i = 0; i < g_nbadges; i++)
    if (strcmp(g_badges[i].app, app) == 0) return g_badges[i].label;
  return "";
}

void refresh_badges(void) {
  struct badge next[8];
  memset(next, 0, sizeof(next));
  int n = 0;
  struct dock_badge dock[64];
  int nd = sls_dock_badges(dock, 64);
  for (int i = 0; i < g_nsp && n < 8; i++) {
    if (g_sp[i].display != 1 || g_sp[i].mc <= PINNED) continue;
    const char* nm = name_for_uuid(g_sp[i].uuid);
    if (!nm || !app_is_running(nm)) continue;
    bool seen = false;
    for (int j = 0; j < n; j++) if (strcmp(next[j].app, nm) == 0) seen = true;
    if (seen) continue;
    snprintf(next[n].app, sizeof(next[n].app), "%s", nm);
    for (int j = 0; j < nd; j++) {
      if (strcmp(dock[j].title, nm) == 0) {
        snprintf(next[n].label, sizeof(next[n].label), "%s", dock[j].label);
        break;
      }
    }
    n++;
  }
  memcpy(g_badges, next, sizeof(next[0]) * (size_t)n);
  g_nbadges = n;
}
