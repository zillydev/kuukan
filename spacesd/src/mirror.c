#include "mirror.h"
#include "state.h"
#include "render.h"
#include "bar.h"
#include "ccm.h"
#include "skylight.h"
#include "yabai_ipc.h"
#include <dispatch/dispatch.h>
#include <stdio.h>
#include <string.h>

static uint64_t g_prev_vis[8] = {0};
static bool g_sync_scheduled = false;
static bool g_reconfig_pending = false;
static bool g_connecting = false;

struct mwin { uint32_t wid; char mirror[40]; char primary[40]; };
static struct mwin g_mwins[MAX_WINDOWS];
static int g_nmwins = 0;

static char g_pairs_cache[4096];
static int g_prev_ndisp = 0;
static int g_connect_tries = 0;
static int g_connect_need = 0;
static int g_connect_phase = 0; // 0 = settle (clean/pull), 1 = create
static int g_create_fails = 0;
static bool g_disc_repass = false;
static uint64_t g_reconfig_seq = 0;

void mirror_init_displays(int ndisplays) { g_prev_ndisp = ndisplays; }

static void snapshot_mirror_windows(void) {
  rebuild_windows();
  int n = 0;
  for (int w = 0; w < g_nwin && n < MAX_WINDOWS; w++) {
    int si = sp_by_sid(g_win[w].sid);
    if (si < 0 || g_sp[si].display != 2 || g_sp[si].pair < 0) continue;
    g_mwins[n].wid = g_win[w].wid;
    memcpy(g_mwins[n].mirror, g_sp[si].uuid, 40);
    memcpy(g_mwins[n].primary, g_sp[g_sp[si].pair].uuid, 40);
    n++;
  }
  g_nmwins = n;
}

static void reconcile_stranded(void) {
  rebuild_spaces();
  rebuild_windows();
  for (int k = 0; k < g_nmwins; k++) {
    int cur = -1;
    for (int w = 0; w < g_nwin; w++)
      if (g_win[w].wid == g_mwins[k].wid) { cur = sp_by_sid(g_win[w].sid); break; }
    int mi = sp_by_uuid(g_mwins[k].mirror);
    int ti = (mi >= 0 && g_sp[mi].display == 2) ? mi : sp_by_uuid(g_mwins[k].primary);
    if (cur < 0 || ti < 0 || cur == ti) continue;
    move_window(g_mwins[k].wid, ti);
    logln("reconcile: window %u -> mc%d (%.8s)", g_mwins[k].wid, g_sp[ti].mc, g_sp[ti].uuid);
  }
}

static void sweep_orphan_mirrors(void) {
  if (g_connecting || g_reconfig_pending || g_ndisplays < 2) return;
  int d1g = 0, d2 = 0;
  for (int i = 0; i < g_nsp; i++) {
    if (g_sp[i].display == 1 && g_sp[i].mc > PINNED) d1g++;
    if (g_sp[i].display == 2) d2++;
  }
  if (d2 <= d1g) return;
  rebuild_windows();
  for (int i = 0; i < g_nsp; i++) {
    if (g_sp[i].display == 2 && g_sp[i].pair < 0 && win_on_sid(g_sp[i].sid) == 0) {
      char err[128] = "";
      logln("sweep: destroying orphan D2 space mc%d %.8s", g_sp[i].mc, g_sp[i].uuid);
      destroy_space(i, err, sizeof(err));
      return;
    }
  }
}

// Instant display-content switch on the far display via SA 0x02 (focus-free).
static void mirror_check(void) {
  int d1 = visible_on_display(1), d2 = visible_on_display(2);
  if (d1 < 0 || d2 < 0) return;
  if (g_sp[d1].pair == d2) return;

  uint64_t active = SLSGetActiveSpace(g_cid);
  int ai = sp_by_sid(active);
  int near_display = ai >= 0 ? g_sp[ai].display : 1;
  int near = (near_display == 1) ? d1 : d2;
  if (g_sp[near].pair < 0) return;
  struct sp* far_target = &g_sp[g_sp[near].pair];

  logln("mirror: d%d -> mc%d (sa instant)", near_display == 1 ? 2 : 1,
        far_target->mc);
  if (!yabai_sa_space_switch(far_target->sid))
    logln("mirror: sa switch failed (scripting addition down?)");
}

void mirrors_persist(void) {
  if (g_ndisplays < 2 || g_connecting || g_reconfig_pending) return;
  char buf[4096];
  buf[0] = '\0';
  size_t off = 0;
  for (int i = 0; i < g_nsp && off < sizeof(buf) - 96; i++) {
    if (g_sp[i].display == 1 && g_sp[i].pair >= 0) {
      buf_appendf(buf, sizeof(buf), &off, "%s %s\n",
                  g_sp[g_sp[i].pair].uuid, g_sp[i].uuid);
    }
  }
  if (strcmp(buf, g_pairs_cache) == 0) return;
  snprintf(g_pairs_cache, sizeof(g_pairs_cache), "%s", buf);
  char tmp[560];
  snprintf(tmp, sizeof(tmp), "%s.tmp", g_pairs_path);
  FILE* f = fopen(tmp, "w");
  if (!f) return;
  fputs(buf, f);
  fclose(f);
  rename(tmp, g_pairs_path);
}

void sync_now(void) {
  if (!rebuild_spaces()) return;
  logln("sync");
  uint64_t vis[8] = {0};
  for (int d = 1; d <= g_ndisplays && d < 8; d++) {
    int i = visible_on_display(d);
    vis[d] = i >= 0 ? g_sp[i].sid : 0;
  }
  bool changed = memcmp(vis, g_prev_vis, sizeof(vis)) != 0;
  if (changed) g_gen++;
  memcpy(g_prev_vis, vis, sizeof(vis));

  render(false);
  mirrors_persist();

  if (changed && g_ndisplays >= 2) mirror_check();
  if (g_ndisplays >= 2 && !g_reconfig_pending) {
    snapshot_mirror_windows();
    sweep_orphan_mirrors();
  }
}

void schedule_sync(void) {
  if (g_sync_scheduled) return;
  g_sync_scheduled = true;
  dispatch_async(dispatch_get_main_queue(), ^{
    g_sync_scheduled = false;
    sync_now();
  });
}

void full_resync(const char* why) {
  logln("full resync (%s)", why);
  rebuild_spaces();
  rebuild_windows();
  g_nccm = ccm_load(g_ccm_path, g_ccm, MAX_CCM);
  refresh_badges();
  render_reset();
  uint64_t vis[8] = {0};
  for (int d = 1; d <= g_ndisplays && d < 8; d++) {
    int i = visible_on_display(d);
    vis[d] = i >= 0 ? g_sp[i].sid : 0;
  }
  memcpy(g_prev_vis, vis, sizeof(vis));
  render(true);
  // Trigger sketchybar's space_change so space items recompute display affinity.
  if (!g_shadow) {
    bar_msg_begin();
    bar_msg_argf("--trigger");
    bar_msg_argf("space_change");
    bar_msg_flush();
  }
  papp_reset();
  papp_poll();
  if (g_ndisplays >= 2) mirror_check();
  if (g_ndisplays >= 2 && !g_reconfig_pending) {
    snapshot_mirror_windows();
    sweep_orphan_mirrors();
  }
}

static bool create_d2_space_at_end(void) {
  char snap[MAX_SPACES][40];
  int snap_n = g_nsp;
  for (int i = 0; i < g_nsp; i++) memcpy(snap[i], g_sp[i].uuid, 40);
  char err[128] = "";
  if (!yabai_cmdf(err, sizeof(err), "space --create")) {
    logln("mirror connect: create failed: %s", err);
    return false;
  }
  int nu = -1;
  for (int tries = 0; tries < 15 && nu < 0; tries++) {
    wait_rebuild();
    nu = find_new_space(snap, snap_n);
  }
  if (nu < 0) { logln("mirror connect: new space not found"); return false; }
  char u[40];
  memcpy(u, g_sp[nu].uuid, 40);
  if (g_sp[nu].display != 2) {
    if (!yabai_agrees(nu)) {
      logln("mirror connect: move to d2 failed: yabai out of sync");
    } else if (!yabai_cmdf(err, sizeof(err), "space %d --display 2", g_sp[nu].mc)) {
      logln("mirror connect: move to d2 failed: %s", err);
    }
    wait_rebuild();
    nu = sp_by_uuid(u);
  }
  if (nu < 0 || g_sp[nu].display != 2) {
    if (nu >= 0) {
      logln("mirror connect: stray on d%d, destroying it", g_sp[nu].display);
      destroy_space(nu, err, sizeof(err));
    }
    return false;
  }
  int last = -1;
  for (int i = 0; i < g_nsp; i++)
    if (g_sp[i].display == 2 && (last < 0 || g_sp[i].mc > g_sp[last].mc)) last = i;
  if (last >= 0 && last != nu && yabai_agrees(nu) && yabai_agrees(last))
    yabai_cmdf(err, sizeof(err), "space %d --move %d", g_sp[nu].mc, g_sp[last].mc);
  return true;
}

static int user_space_on_d2(void) {
  char known[MAX_SPACES][40];
  int nk = 0;
  FILE* f = fopen(g_pairs_path, "r");
  if (f) {
    char pr[40];
    while (nk < MAX_SPACES && fscanf(f, "%39s %39s", known[nk], pr) == 2) nk++;
    fclose(f);
  }
  rebuild_windows();
  for (int i = 0; i < g_nsp; i++) {
    if (g_sp[i].display != 2) continue;
    if (name_for_uuid(g_sp[i].uuid)) return i;
    bool mirror = false;
    for (int k = 0; k < nk; k++)
      if (strcmp(known[k], g_sp[i].uuid) == 0) mirror = true;
    if (!mirror && win_on_sid(g_sp[i].sid) > 0) return i;
  }
  return -1;
}

static void mirror_connect_step(void);
static void schedule_connect_step(int ms) {
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)ms * NSEC_PER_MSEC),
                 dispatch_get_main_queue(), ^{ mirror_connect_step(); });
}

static void mirror_connect_step(void) {
  g_connecting = true;
  rebuild_spaces();
  if (g_connect_tries++ > 120) {
    logln("mirror connect: gave up");
    g_connect_need = 0;
    g_connect_phase = 0;
    g_connecting = false;
    full_resync("connect-gaveup");
    return;
  }
  if (g_ndisplays < 2) { schedule_connect_step(500); return; }

  int ydc = yabai_display_count();
  if (ydc < 2) {
    logln("mirror connect: waiting for yabai (%d displays)", ydc);
    schedule_connect_step(500);
    return;
  }

  FILE* f = fopen(g_pairs_path, "r");
  if (f) {
    char m[40], pr[40];
    while (fscanf(f, "%39s %39s", m, pr) == 2) {
      int mi = sp_by_uuid(m), pi = sp_by_uuid(pr);
      if (mi < 0 || g_sp[mi].display != 1 || name_for_uuid(m)) continue;
      char err[128] = "";
      rebuild_windows();
      if (pi >= 0) {
        for (int w = 0; w < g_nwin; w++)
          if (g_win[w].sid == g_sp[mi].sid) move_window(g_win[w].wid, pi);
      }
      logln("mirror connect: cleaning leftover mirror mc%d", g_sp[mi].mc);
      destroy_space(mi, err, sizeof(err));
      fclose(f);
      schedule_connect_step(250);
      return;
    }
    fclose(f);
  }

  int d1 = 0, d2 = 0;
  for (int i = 0; i < g_nsp; i++) {
    if (g_sp[i].display == 1) d1++;
    if (g_sp[i].display == 2) d2++;
  }
  // Phase-gated so created mirrors are never pulled back to D1.
  if (g_connect_phase == 0 && d2 > 1) {
    int pull = user_space_on_d2();
    if (pull >= 0) {
      char err[128] = "";
      logln("mirror connect: pulling d2 mc%d home (d2=%d)", g_sp[pull].mc, d2);
      if (!yabai_agrees(pull)) {
        logln("mirror connect: pull failed: yabai out of sync");
      } else if (!yabai_cmdf(err, sizeof(err), "space %d --display 1", g_sp[pull].mc)) {
        logln("mirror connect: pull failed: %s", err);
      }
      schedule_connect_step(250);
      return;
    }
  }
  if (g_connect_phase == 0) {
    g_connect_phase = 1;
    g_connect_need = d1 - PINNED;
    if (g_connect_need > 16) {
      logln("mirror connect: need=%d exceeds sanity cap, aborting", g_connect_need);
      g_connect_need = 0;
      g_connect_phase = 0;
      g_connecting = false;
      full_resync("connect-abort");
      return;
    }
    if (g_connect_need <= 0) {
      g_connecting = false;
      full_resync("mirror connect");
      return;
    }
  }
  if (d2 < g_connect_need) {
    logln("mirror connect: creating mirror %d/%d", d2 + 1, g_connect_need);
    if (!create_d2_space_at_end() && ++g_create_fails >= 5) {
      logln("mirror connect: %d consecutive create failures, aborting", g_create_fails);
      g_connect_need = 0;
      g_connect_phase = 0;
      g_create_fails = 0;
      g_connecting = false;
      full_resync("connect-abort");
      return;
    }
    schedule_connect_step(300);
    return;
  }
  logln("mirror connect: complete (%d pairs)", g_connect_need);
  g_connect_need = 0;
  g_connect_phase = 0;
  g_create_fails = 0;
  g_connecting = false;
  full_resync("mirror connect");
  mirrors_persist();
}

static void log_layout(const char* tag) {
  rebuild_spaces();
  rebuild_windows();
  for (int i = 0; i < g_nsp; i++)
    logln("layout[%s] mc=%d d%d %.8s win=%d", tag, g_sp[i].mc, g_sp[i].display,
          g_sp[i].uuid, win_on_sid(g_sp[i].sid));
}

void mirror_disconnect(void) {
  FILE* f = fopen(g_pairs_path, "r");
  if (!f) {
    logln("mirror disconnect: no pair file, leaving spaces");
    full_resync("disconnect");
    return;
  }
  log_layout("disc-before");
  char m[40], pr[40];
  char err[128] = "";
  int destroyed = 0, deferred = 0;
  char keep[MAX_SPACES][82];
  int nkeep = 0;
  while (fscanf(f, "%39s %39s", m, pr) == 2) {
    if (sp_by_uuid(m) < 0) {
      deferred++;
      if (nkeep < MAX_SPACES) snprintf(keep[nkeep++], 82, "%s %s", m, pr);
      continue;
    }
    for (int pass = 0; pass < 6; pass++) {
      rebuild_windows();
      int mi = sp_by_uuid(m), pi = sp_by_uuid(pr);
      if (mi < 0 || pi < 0) break;
      uint64_t msid = g_sp[mi].sid;
      int moved = 0;
      for (int w = 0; w < g_nwin; w++) {
        if (g_win[w].sid == msid) {
          move_window(g_win[w].wid, pi);
          moved++;
        }
      }
      if (!moved) break;
      wait_rebuild();
    }
    rebuild_windows();
    int mi = sp_by_uuid(m);
    if (mi < 0) continue;
    if (win_on_sid(g_sp[mi].sid) > 0) {
      logln("mirror disconnect: %.8s still holds windows, deferring", m);
      deferred++;
      if (nkeep < MAX_SPACES) snprintf(keep[nkeep++], 82, "%s %s", m, pr);
      continue;
    }
    if (destroy_space(mi, err, sizeof(err))) {
      destroyed++;
    } else {
      logln("mirror disconnect: destroy failed: %s", err);
      if (nkeep < MAX_SPACES) snprintf(keep[nkeep++], 82, "%s %s", m, pr);
    }
  }
  fclose(f);
  char tmp[560];
  snprintf(tmp, sizeof(tmp), "%s.tmp", g_pairs_path);
  FILE* o = fopen(tmp, "w");
  if (o) {
    for (int i = 0; i < nkeep; i++) fprintf(o, "%s\n", keep[i]);
    fclose(o);
    rename(tmp, g_pairs_path);
    g_pairs_cache[0] = '\0';
  }
  logln("mirror disconnect: destroyed %d, deferred %d", destroyed, deferred);
  log_layout("disc-after");
  if (deferred > 0 && !g_disc_repass) {
    g_disc_repass = true;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 2500 * NSEC_PER_MSEC),
                   dispatch_get_main_queue(), ^{
                     if (g_ndisplays < 2) mirror_disconnect();
                     g_disc_repass = false;
                   });
  }
  reconcile_stranded();
  full_resync("mirror disconnect");
}

static void start_connect(const char* why) {
  if (g_connecting) { logln("%s: connect already running", why); return; }
  logln("%s: starting mirror connect", why);
  g_connect_tries = 0;
  g_connect_need = 0;
  g_connect_phase = 0;
  g_create_fails = 0;
  mirror_connect_step();
}

void mirror_connect_manual(void) {
  g_connect_tries = 0;
  g_connect_need = 0;
  g_connect_phase = 0;
  g_create_fails = 0;
  mirror_connect_step();
}

// Trailing 1.5s debounce coalesces display cable/power-on flaps into one decision.
void on_display_reconfig(CGDirectDisplayID did,
                         CGDisplayChangeSummaryFlags flags,
                         void* user) {
  (void)did; (void)flags; (void)user;
  g_reconfig_pending = true;
  uint64_t seq = ++g_reconfig_seq;
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 1500 * NSEC_PER_MSEC),
                 dispatch_get_main_queue(), ^{
                   if (seq != g_reconfig_seq) return;
                   g_reconfig_pending = false;
                   rebuild_spaces();
                   if (g_ndisplays < 2 && g_prev_ndisp >= 2) {
                     logln("display removed: starting mirror disconnect");
                     mirror_disconnect();
                   } else if (g_ndisplays >= 2) {
                     if (g_prev_ndisp >= 2) reconcile_stranded();
                     start_connect(g_prev_ndisp < 2 ? "display connected"
                                                    : "display reconfigured");
                   } else {
                     full_resync("display reconfig");
                   }
                   g_prev_ndisp = g_ndisplays;
                 });
}
