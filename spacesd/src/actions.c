#include "actions.h"
#include "state.h"
#include "render.h"
#include "mirror.h"
#include "skylight.h"
#include "workspace.h"
#include "yabai_ipc.h"
#include <dispatch/dispatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static uint32_t app_wid_for_pid(pid_t pid, int pinned_idx) {
  uint64_t psid = g_sp[pinned_idx].sid;
  for (int w = 0; w < g_nwin; w++)
    if (g_win[w].pid == pid && g_win[w].sid == psid) return g_win[w].wid;
  for (int w = 0; w < g_nwin; w++)
    if (g_win[w].pid == pid) return g_win[w].wid;
  return 0;
}

// Raise via Dock z-order, then activate through Accessibility (PWA-safe, no yabai window --focus).
static void app_claim(pid_t pid, uint32_t wid) {
  yabai_sa_window_order(wid, 1, 0);
  if (!sls_window_ax_focus(pid, wid)) {
    sls_window_set_front(wid);
    workspace_activate_pid(pid);
    sls_window_make_key(wid);
  }
}

static bool app_settled(uint32_t wid) {
  return sls_window_is_front(wid) && sls_frontmost_window() == wid;
}

// Hold focus across macOS's post-switch re-home; newer presses cancel via g_app_gen.
static void app_hold(pid_t pid, uint32_t wid, uint64_t gen, int tick, int stable) {
  if (gen != g_app_gen) return;
  if (app_settled(wid)) stable++;
  else { app_claim(pid, wid); stable = 0; }
  bool done = (tick >= 40 && stable >= 12) || tick >= 150;
  if (done) return;
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 8 * NSEC_PER_MSEC),
                 dispatch_get_main_queue(), ^{
    app_hold(pid, wid, gen, tick + 1, stable);
  });
}

static void cmd_focus(int mc, char* reply, size_t rsz) {
  g_app_gen++;
  int t = sp_by_mc(mc);
  if (t < 0) { snprintf(reply, rsz, "err: no space with index %d", mc); return; }
  // Dock-switch the windowed half of a pair so macOS does not yank focus to another space's MRU app.
  uint64_t active = SLSGetActiveSpace(g_cid);
  int ai = sp_by_sid(active);
  int near_display = ai >= 0 ? g_sp[ai].display : 1;
  int dock = t;
  if (g_sp[t].display != near_display && g_sp[t].pair >= 0)
    dock = g_sp[t].pair;
  int sa = g_sp[dock].pair;
  if (win_on_sid(g_sp[dock].sid) == 0 && sa >= 0 && win_on_sid(g_sp[sa].sid) > 0) {
    int tmp = dock; dock = sa; sa = tmp;
  }
  g_gen++;
  char err[128] = "";
  bool dock_ok = false;
  if (!yabai_agrees(dock)) {
    snprintf(err, sizeof(err), "yabai out of sync");
  } else {
    dock_ok = yabai_cmdf(err, sizeof(err), "space --focus %d", g_sp[dock].mc);
  }
  if (sa >= 0) yabai_sa_space_switch(g_sp[sa].sid);
  if (!dock_ok) { snprintf(reply, rsz, "ok (%s)", err); return; }
  snprintf(reply, rsz, "ok");
}

static void cmd_app(const char* name, char* reply, size_t rsz) {
  pid_t pid = workspace_pid_for_app(name);
  if (!pid) { snprintf(reply, rsz, "ok: %s not running", name); return; }
  rebuild_spaces();
  int p = sp_by_mc(PINNED);
  if (p < 0) { snprintf(reply, rsz, "err: no pinned space"); return; }
  rebuild_windows();
  uint32_t wid = app_wid_for_pid(pid, p);
  if (!wid) {
    workspace_activate_app(name);
    snprintf(reply, rsz, "ok: %s (no window, activated)", name);
    return;
  }
  g_gen++;
  if (!g_sp[p].visible) yabai_sa_space_switch(g_sp[p].sid);
  app_claim(pid, wid);
  app_hold(pid, wid, ++g_app_gen, 0, 0);
  logln("app: %s (wid=%u)", name, wid);
  snprintf(reply, rsz, "ok");
}

static bool create_space_on(int d, char out[40], char* err, size_t errsz) {
  rebuild_spaces();
  char snap[MAX_SPACES][40];
  int snap_n = g_nsp;
  for (int i = 0; i < g_nsp; i++) memcpy(snap[i], g_sp[i].uuid, 40);
  if (!yabai_cmdf(err, errsz, "space --create %d", d)) return false;
  int nu = -1;
  for (int tries = 0; tries < 15 && nu < 0; tries++) {
    wait_rebuild();
    nu = find_new_space(snap, snap_n);
  }
  if (nu < 0) { snprintf(err, errsz, "new space not found"); return false; }
  if (g_sp[nu].display != d) {
    char derr[128] = "";
    snprintf(err, errsz, "new space landed on d%d", g_sp[nu].display);
    destroy_space(nu, derr, sizeof(derr));
    return false;
  }
  memcpy(out, g_sp[nu].uuid, 40);
  int last = -1;
  for (int i = 0; i < g_nsp; i++)
    if (g_sp[i].display == d && (last < 0 || g_sp[i].mc > g_sp[last].mc)) last = i;
  if (last >= 0 && last != nu && yabai_agrees(nu) && yabai_agrees(last))
    yabai_cmdf(err, errsz, "space %d --move %d", g_sp[nu].mc, g_sp[last].mc);
  return true;
}

static void cmd_create(const char* name, char* reply, size_t rsz) {
  char err[128] = "", p_uuid[40], m_uuid[40];
  if (!create_space_on(1, p_uuid, err, sizeof(err))) {
    snprintf(reply, rsz, "err: create: %s", err);
    return;
  }
  if (name && *name) names_set(p_uuid, name);
  if (g_ndisplays >= 2 && !create_space_on(2, m_uuid, err, sizeof(err)))
    logln("create: mirror: %s", err);
  full_resync("create");
  int p = sp_by_uuid(p_uuid);
  if (p >= 0) {
    char freply[128] = "";
    cmd_focus(g_sp[p].mc, freply, sizeof(freply));
    if (strncmp(freply, "err", 3) == 0) logln("create: focus: %s", freply);
  }
  snprintf(reply, rsz, "ok: created%s%s", name && *name ? " " : "",
           name ? name : "");
}

static void cmd_destroy(char* reply, size_t rsz) {
  rebuild_spaces();
  int p = active_primary();
  if (p < 0) { snprintf(reply, rsz, "err: no active space"); return; }
  if (g_sp[p].mc <= PINNED) { snprintf(reply, rsz, "refused: pinned"); return; }
  char p_uuid[40], s_uuid[40] = "";
  memcpy(p_uuid, g_sp[p].uuid, 40);
  if (g_sp[p].pair >= 0) memcpy(s_uuid, g_sp[g_sp[p].pair].uuid, 40);
  char err[128] = "";

  if (s_uuid[0]) {
    rebuild_windows();
    uint64_t s_sid = g_sp[g_sp[p].pair].sid;
    for (int w = 0; w < g_nwin; w++)
      if (g_win[w].sid == s_sid) move_window(g_win[w].wid, p);
  }
  int neighbor = pick_destroy_neighbor(g_sp, g_nsp, p);
  g_gen++;
  if (neighbor >= 0 && yabai_agrees(neighbor))
    yabai_cmdf(err, sizeof(err), "space --focus %d", g_sp[neighbor].mc);

  for (int pass = 0; pass < 2; pass++) {
    const char* uuid = pass == 0 ? s_uuid : p_uuid;
    if (!uuid[0]) continue;
    wait_rebuild();
    int idx = sp_by_uuid(uuid);
    if (idx >= 0 && !destroy_space(idx, err, sizeof(err)))
      logln("destroy: %s", err);
  }
  names_set(p_uuid, NULL);
  full_resync("destroy");
  snprintf(reply, rsz, "ok");
}

static void cmd_move(const char* dir, char* reply, size_t rsz) {
  rebuild_spaces();
  int p = active_primary();
  if (p < 0 || g_sp[p].mc <= PINNED) { snprintf(reply, rsz, "refused"); return; }
  int d1max = 0;
  for (int i = 0; i < g_nsp; i++)
    if (g_sp[i].display == 1 && g_sp[i].mc > d1max) d1max = g_sp[i].mc;
  int target;
  if (strcmp(dir, "left") == 0 && g_sp[p].mc > PINNED + 1)
    target = g_sp[p].mc - 1;
  else if (strcmp(dir, "right") == 0 && g_sp[p].mc < d1max)
    target = g_sp[p].mc + 1;
  else { snprintf(reply, rsz, "refused: boundary"); return; }

  int s_mc = -1, s_target_mc = -1;
  if (g_sp[p].pair >= 0) {
    s_mc = g_sp[g_sp[p].pair].mc;
    int tp = sp_by_mc(target);
    if (tp >= 0 && g_sp[tp].pair >= 0) s_target_mc = g_sp[g_sp[tp].pair].mc;
  }
  char err[128] = "";
  g_gen++;
  int ti = sp_by_mc(target);
  if (ti < 0 || !yabai_agrees(p) || !yabai_agrees(ti)) {
    snprintf(reply, rsz, "err: yabai out of sync");
    return;
  }
  yabai_cmdf(err, sizeof(err), "space --focus %d", g_sp[p].mc);
  if (!yabai_cmdf(err, sizeof(err), "space --move %d", target)) {
    snprintf(reply, rsz, "err: move: %s", err);
    return;
  }
  rebuild_spaces();
  int si = sp_by_mc(s_mc), sti = sp_by_mc(s_target_mc);
  if (si >= 0 && sti >= 0 && yabai_agrees(si) && yabai_agrees(sti))
    yabai_cmdf(err, sizeof(err), "space %d --move %d", s_mc, s_target_mc);
  full_resync("move");
  snprintf(reply, rsz, "ok");
}

static void cmd_rename(const char* name, char* reply, size_t rsz) {
  rebuild_spaces();
  int p = active_primary();
  if (p < 0 || g_sp[p].mc <= PINNED) { snprintf(reply, rsz, "refused"); return; }
  names_set(g_sp[p].uuid, name);
  render(false);
  snprintf(reply, rsz, "ok: %d=%s", g_sp[p].mc - PINNED, name ? name : "");
}

static void cmd_send(int mc, char* reply, size_t rsz) {
  g_app_gen++;
  rebuild_spaces();
  int t = sp_by_mc(mc);
  if (t < 0) { snprintf(reply, rsz, "err: no space %d", mc); return; }
  int p = (g_sp[t].display == 1) ? t : (g_sp[t].pair >= 0 ? g_sp[t].pair : t);

  uint32_t wid = 0;
  int cur_mc = -1;
  pid_t pid = 0;
  char out[8192];
  for (int tries = 0; tries < 4 && wid == 0; tries++) {
    const char* qv[] = {"query", "--windows", "--window"};
    if (yabai_query(out, sizeof(out), 3, qv))
      yabai_parse_window(out, strlen(out), &wid, &cur_mc, &pid);
    if (wid == 0) usleep(120000);
  }
  if (wid == 0) { snprintf(reply, rsz, "err: no focused window"); return; }

  int dest = p;
  if (cur_mc == g_sp[p].mc && g_sp[p].pair >= 0) dest = g_sp[p].pair;

  char err[128] = "";
  if (!yabai_agrees(dest)) { snprintf(reply, rsz, "err: yabai out of sync"); return; }
  if (!yabai_cmdf(err, sizeof(err), "window %u --space %d", wid, g_sp[dest].mc)) {
    snprintf(reply, rsz, "err: move: %s", err);
    return;
  }
  g_gen++;
  yabai_cmdf(err, sizeof(err), "space --focus %d", g_sp[dest].mc);
  if (g_sp[dest].pair >= 0)
    yabai_sa_space_switch(g_sp[g_sp[dest].pair].sid);
  if (pid > 0) app_claim(pid, wid);
  snprintf(reply, rsz, "ok: window %u -> mc%d%s", wid, g_sp[dest].mc,
           dest == p ? "" : " (mirror)");
}

static void cmd_status(char* reply, size_t rsz) {
  if (rsz == 0) return;
  reply[0] = '\0';
  size_t off = 0;
  buf_appendf(reply, rsz, &off,
              "displays=%d spaces=%d windows=%d ccm=%d gen=%llu shadow=%d ax=%d\n",
              g_ndisplays, g_nsp, g_nwin, g_nccm,
              (unsigned long long)g_gen, g_shadow, sls_ax_trusted() ? 1 : 0);
  for (int i = 0; i < g_nsp && off + 160 < rsz; i++) {
    struct sp* s = &g_sp[i];
    const char* nm = NULL;
    if (s->display == 1) nm = name_for_uuid(s->uuid);
    else if (s->pair >= 0) nm = name_for_uuid(g_sp[s->pair].uuid);
    buf_appendf(reply, rsz, &off,
                "mc=%-2d d%d sid=%-5llu vis=%d pair=%-2d %s %s\n",
                s->mc, s->display, (unsigned long long)s->sid, s->visible,
                s->pair >= 0 ? g_sp[s->pair].mc : -1, s->uuid, nm ? nm : "");
  }
}

void handle_command(int argc, char** argv, char* reply, size_t rsz) {
  reply[0] = '\0';
  if (argc < 1) { snprintf(reply, rsz, "err: empty"); return; }
  if (strcmp(argv[0], "focus") == 0 && argc >= 2)
    cmd_focus(atoi(argv[1]), reply, rsz);
  else if (strcmp(argv[0], "send") == 0 && argc >= 2)
    cmd_send(atoi(argv[1]), reply, rsz);
  else if (strcmp(argv[0], "app") == 0 && argc >= 2)
    cmd_app(argv[1], reply, rsz);
  else if (strcmp(argv[0], "axprompt") == 0)
    snprintf(reply, rsz, sls_ax_prompt() ? "ok: already trusted"
                                          : "ok: prompt shown, enable spacesd in Settings");
  else if (strcmp(argv[0], "create") == 0)
    cmd_create(argc >= 2 ? argv[1] : NULL, reply, rsz);
  else if (strcmp(argv[0], "destroy") == 0)
    cmd_destroy(reply, rsz);
  else if (strcmp(argv[0], "move") == 0 && argc >= 2)
    cmd_move(argv[1], reply, rsz);
  else if (strcmp(argv[0], "rename") == 0 && argc >= 2)
    cmd_rename(argv[1], reply, rsz);
  else if (strcmp(argv[0], "repaint") == 0) {
    config_load(&g_cfg, getenv("HOME"));
    full_resync("repaint");
    papp_poll();
    snprintf(reply, rsz, "ok");
  } else if (strcmp(argv[0], "status") == 0)
    cmd_status(reply, rsz);
  else if (strcmp(argv[0], "shadow") == 0 && argc >= 2) {
    g_shadow = strcmp(argv[1], "on") == 0;
    if (!g_shadow) full_resync("shadow off");
    snprintf(reply, rsz, "ok: shadow=%d", g_shadow);
  } else if (strcmp(argv[0], "mirror") == 0 && argc >= 2) {
    if (strcmp(argv[1], "connect") == 0) {
      mirror_connect_manual();
      snprintf(reply, rsz, "ok: connect running");
    } else if (strcmp(argv[1], "disconnect") == 0) {
      mirror_disconnect();
      snprintf(reply, rsz, "ok");
    } else
      snprintf(reply, rsz, "err: mirror connect|disconnect");
  } else if (strcmp(argv[0], "version") == 0)
    snprintf(reply, rsz, "spacesd 0.1");
  else
    snprintf(reply, rsz, "err: unknown command '%s'", argv[0]);
}
