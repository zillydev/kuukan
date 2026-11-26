#include "render.h"
#include "state.h"
#include "bar.h"
#include "colors.h"
#include "papp.h"
#include "skylight.h"
#include <dispatch/dispatch.h>
#include <stdio.h>
#include <string.h>

static char g_papp_state[MAX_PAPPS][32];
static bool g_papp_inflight = false;
static char g_papp_result[MAX_PAPPS][PAPP_LABEL_MAX];
static uint64_t g_papp_gen = 0;

static void render_papps(bool force) {
  bar_msg_begin();
  for (int i = 0; i < g_cfg.npapps; i++) {
    const char* st = g_papp_result[i];
    if (!force && strcmp(st, g_papp_state[i]) == 0) continue;
    bar_msg_argf("--set");
    bar_msg_argf("%s", g_cfg.papps[i].item);
    if (strcmp(st, "off") == 0) {
      bar_msg_argf("icon.color=%s", g_cfg.colors.subtext);
      bar_msg_argf("icon.padding_right=6");
      bar_msg_argf("label.drawing=off");
    } else if (strcmp(st, "dot") == 0) {
      bar_msg_argf("icon.color=%s", g_cfg.colors.unread);
      bar_msg_argf("icon.padding_right=6");
      bar_msg_argf("label.drawing=off");
    } else {
      bar_msg_argf("icon.color=%s", g_cfg.colors.unread);
      bar_msg_argf("icon.padding_right=2");
      bar_msg_argf("label.drawing=on");
      bar_msg_argf("label.color=%s", g_cfg.colors.unread);
      bar_msg_argf("label=%s", st);
    }
    snprintf(g_papp_state[i], sizeof(g_papp_state[i]), "%s", st);
  }
  if (bar_msg_count() == 0) return;
  if (g_shadow) { bar_msg_begin(); return; }
  if (!bar_msg_flush()) {
    for (int i = 0; i < MAX_PAPPS; i++) g_papp_state[i][0] = '\0';
  }
}

void papp_reset(void) {
  g_papp_gen++;
  for (int i = 0; i < MAX_PAPPS; i++) g_papp_state[i][0] = '\0';
}

// Polls Dock AX badges on a background queue; macOS exposes no push channel for StatusLabel.
void papp_poll(void) {
  if (g_papp_inflight || g_cfg.npapps <= 0) return;
  g_papp_inflight = true;
  int n = g_cfg.npapps;
  uint64_t gen = g_papp_gen;
  struct { struct papp_cfg items[MAX_PAPPS]; } snap;
  memcpy(snap.items, g_cfg.papps, sizeof(snap.items[0]) * (size_t)n);
  dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
    struct dock_badge dock[64];
    int nd = sls_dock_badges(dock, 64);
    const char* names[MAX_PAPPS];
    struct { char labels[MAX_PAPPS][PAPP_LABEL_MAX]; } res;
    for (int i = 0; i < n; i++) names[i] = snap.items[i].app;
    papp_match(dock, nd < 0 ? 0 : nd, n, names, res.labels);
    dispatch_async(dispatch_get_main_queue(), ^{
      g_papp_inflight = false;
      if (gen != g_papp_gen) return;
      memcpy(g_papp_result, res.labels, sizeof(res.labels[0]) * (size_t)n);
      render_papps(false);
    });
  });
}

void papp_timer_start(void) {
  dispatch_source_t t = dispatch_source_create(
      DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_main_queue());
  dispatch_source_set_timer(t, DISPATCH_TIME_NOW, NSEC_PER_SEC, NSEC_PER_SEC / 4);
  dispatch_source_set_event_handler(t, ^{ papp_poll(); });
  dispatch_resume(t);
}

struct item_state { bool valid; char label[128]; char bg[12]; char fg[12]; };
static struct item_state g_last[ITEM_MAX_MC + 1];

void render_reset(void) {
  for (int mc = 0; mc <= ITEM_MAX_MC; mc++) g_last[mc].valid = false;
}

void render(bool full) {
  int unread[MAX_SPACES] = {0}, running[MAX_SPACES] = {0};
  for (int c = 0; c < g_nccm; c++) {
    for (int w = 0; w < g_nwin; w++) {
      if (g_win[w].wid != g_ccm[c].wid) continue;
      int i = sp_by_sid(g_win[w].sid);
      if (i >= 0) {
        unread[i] += g_ccm[c].unread;
        running[i] += g_ccm[c].running;
      }
      break;
    }
  }

  bool present[ITEM_MAX_MC + 1] = {false};
  bar_msg_begin();
  for (int i = 0; i < g_nsp; i++) {
    struct sp* s = &g_sp[i];
    if (s->mc <= PINNED || s->mc > ITEM_MAX_MC) continue;
    present[s->mc] = true;

    struct sp* primary = (s->display == 1) ? s
                        : (s->pair >= 0 ? &g_sp[s->pair] : s);
    int num = primary->mc - PINNED;
    const char* nm = name_for_uuid(primary->uuid);

    char label[128];
    label[0] = '\0';
    size_t off = 0;
    if (nm) buf_appendf(label, sizeof(label), &off, "%d %s", num, nm);
    else    buf_appendf(label, sizeof(label), &off, "%d", num);

    if (nm && app_is_running(nm)) {
      const char* b = badge_for_app(nm);
      if (strcmp(b, "\xe2\x80\xa2") == 0)
        buf_appendf(label, sizeof(label), &off, "  \xf3\xb0\x8d\xa1");
      else if (b[0])
        buf_appendf(label, sizeof(label), &off, " (%s)", b);
    }
    int un = unread[i], ru = running[i];
    if (s->pair >= 0) { un += unread[s->pair]; ru += running[s->pair]; }
    if (un > 0)
      buf_appendf(label, sizeof(label), &off, " \xf3\xb0\x9a\xa9");

    const char *bg, *fg;
    space_colors_pal(&g_cfg.colors, s->visible, un, ru, &bg, &fg);

    struct item_state* st = &g_last[s->mc];
    if (full || !st->valid || strcmp(st->label, label) != 0
        || strcmp(st->bg, bg) != 0 || strcmp(st->fg, fg) != 0) {
      bar_msg_argf("--set");
      bar_msg_argf("space.%d", s->mc);
      bar_msg_argf("icon=%s", label);
      bar_msg_argf("icon.color=%s", fg);
      bar_msg_argf("background.color=%s", bg);
      bar_msg_argf("background.drawing=on");
      st->valid = true;
      snprintf(st->label, sizeof(st->label), "%s", label);
      snprintf(st->bg, sizeof(st->bg), "%s", bg);
      snprintf(st->fg, sizeof(st->fg), "%s", fg);
    }
  }
  for (int mc = PINNED + 1; mc <= ITEM_MAX_MC; mc++)
    if (!present[mc]) g_last[mc].valid = false;

  if (bar_msg_count() == 0) return;
  if (g_shadow) {
    char snap[4096];
    int n = bar_msg_snapshot(snap, sizeof(snap));
    for (int i = 0; i < n; i++) if (snap[i] == '\0') snap[i] = ' ';
    logln("SHADOW: %s", snap);
    bar_msg_begin();
    return;
  }
  static bool down_logged = false;
  if (!bar_msg_flush()) {
    if (!down_logged) logln("bar: unreachable, will repaint on recovery");
    down_logged = true;
    render_reset();
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC),
                   dispatch_get_main_queue(), ^{ render(true); });
  } else if (down_logged) {
    down_logged = false;
    logln("bar: recovered");
  }
}
