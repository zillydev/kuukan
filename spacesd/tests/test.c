// Unit tests for spacesd's pure modules: framing, JSON parsing, bar buffer, model, ccm, papp, colours.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../src/bar.h"
#include "../src/ccm.h"
#include "../src/colors.h"
#include "../src/config.h"
#include "../src/model.h"
#include "../src/papp.h"
#include "../src/yabai_ipc.h"

static void test_yabai_frame(void) {
  char buf[64];
  const char* argv[] = {"space", "--focus", "7"};
  int total = yabai_frame(buf, sizeof(buf), 3, argv);
  int len = 0;
  memcpy(&len, buf, sizeof(len));
  assert(len == 17);
  assert(total == 4 + 17);
  assert(memcmp(buf + 4, "space\0--focus\0007\0\0", 17) == 0);
  assert(yabai_frame(buf, 10, 3, argv) == -1);
}

static void test_yabai_json(void) {
  const char* d2 = "[{\"id\":1,\"uuid\":\"AAA\",\"label\":\"has \\\"uuid\\\" inside\"},"
                   "{\"id\":2,\"uuid\":\"BBB\"}]";
  assert(yabai_parse_display_count(d2, strlen(d2)) == 2);
  assert(yabai_parse_display_count("[]", 2) == -1);
  assert(yabai_parse_display_count("{bad", 4) == -1);

  const char* sp = "{\"index\":2,\"nested\":{\"uuid\":\"WRONG\"},\"uuid\":\"REAL-UUID-1\"}";
  char uuid[40];
  assert(yabai_parse_space_uuid(sp, strlen(sp), uuid, sizeof(uuid)));
  assert(strcmp(uuid, "REAL-UUID-1") == 0);
  char tiny[5];
  assert(yabai_parse_space_uuid(sp, strlen(sp), tiny, sizeof(tiny)));
  assert(strcmp(tiny, "REAL") == 0);
  assert(!yabai_parse_space_uuid("{\"index\":2}", 11, uuid, sizeof(uuid)));

  // Window title containing fake JSON keys must not shadow real id/space/pid fields.
  const char* win = "{\"title\":\"debug \\\"id\\\":999 \\\"space\\\":1\","
                    "\"id\":42,\"pid\":777,\"space\":3}";
  uint32_t wid = 0;
  int space = -1;
  pid_t pid = 0;
  assert(yabai_parse_window(win, strlen(win), &wid, &space, &pid));
  assert(wid == 42 && space == 3 && pid == 777);
  assert(!yabai_parse_window("{\"title\":\"no id\"}", 17, &wid, &space, &pid));
}

static void test_bar_builder(void) {
  bar_msg_begin();
  bar_msg_argf("--set");
  bar_msg_argf("space.%d", 2);
  assert(bar_msg_count() == 2);
  char snap[32];
  int n = bar_msg_snapshot(snap, sizeof(snap));
  assert(n == 14);
  assert(memcmp(snap, "--set\0space.2\0", 14) == 0);

  // Oversized argument must clamp cleanly without overflowing the internal 64 KiB buffer.
  bar_msg_begin();
  char huge[70000];
  memset(huge, 'A', sizeof(huge) - 1);
  huge[sizeof(huge) - 1] = '\0';
  bar_msg_argf("%s", huge);
  assert(bar_msg_count() == 1);
  bar_msg_argf("dropped-after-full");
  assert(bar_msg_count() == 1);
  bar_msg_begin();
}

static void test_model(void) {
  char buf[10];
  buf[0] = '\0';
  size_t off = 0;
  buf_appendf(buf, sizeof(buf), &off, "hello");
  assert(off == 5 && strcmp(buf, "hello") == 0);
  buf_appendf(buf, sizeof(buf), &off, " world!!!");
  assert(off == 9 && strcmp(buf, "hello wor") == 0);
  buf_appendf(buf, sizeof(buf), &off, "more");
  assert(off == 9 && strcmp(buf, "hello wor") == 0);

  struct sp sp[6] = {
    {.sid = 1, .uuid = "u1", .display = 1, .mc = 1},
    {.sid = 2, .uuid = "u2", .display = 1, .mc = 2},
    {.sid = 3, .uuid = "u3", .display = 1, .mc = 3},
    {.sid = 4, .uuid = "u4", .display = 2, .mc = 4},
    {.sid = 5, .uuid = "u5", .display = 2, .mc = 5},
    {.sid = 6, .uuid = "u6", .display = 2, .mc = 6},
  };
  pair_spaces(sp, 6, 1);
  for (int i = 0; i < 6; i++) assert(sp[i].pair == -1);

  pair_spaces(sp, 6, 2);
  assert(sp[0].pair == -1);
  assert(sp[1].pair == 3 && sp[3].pair == 1);
  assert(sp[2].pair == 4 && sp[4].pair == 2);
  assert(sp[5].pair == -1);

  char snap[2][40] = {"u1", "u2"};
  assert(find_new_space_in(sp, 3, snap, 2) == 2);
  assert(find_new_space_in(sp, 2, snap, 2) == -1);

  assert(pick_destroy_neighbor(sp, 6, 2) == 1); // prefers left
  assert(pick_destroy_neighbor(sp, 6, 1) == 2); // falls back to right
  assert(pick_destroy_neighbor(sp, 2, 1) == -1); // only general space
}

static void test_ccm(void) {
  char tmpl[] = "/tmp/spacesd_test_ccm_XXXXXX";
  int fd = mkstemp(tmpl);
  assert(fd >= 0);
  FILE* f = fdopen(fd, "w");
  assert(f != NULL);
  fprintf(f, "{\"sessions\":{"
             "\"a\":{\"state\":\"unread\",\"window\":101,\"cwd\":\"/x\"},"
             "\"b\":{\"state\":\"running\",\"window\":202},"
             "\"c\":{\"state\":\"idle\",\"window\":303},"
             "\"d\":{\"state\":\"unread\",\"window\":null}}}");
  fclose(f);
  struct ccm_session s[8];
  int n = ccm_load(tmpl, s, 8);
  assert(n == 2);
  int unread = 0, running = 0;
  for (int i = 0; i < n; i++) { unread += s[i].unread; running += s[i].running; }
  assert(unread == 1 && running == 1);
  assert(ccm_load("/nonexistent", s, 8) == 0);

  // Deeply nested JSON exercises iterative tok_skip without stack overflow.
  f = fopen(tmpl, "w");
  assert(f != NULL);
  fprintf(f, "{\"junk\":");
  for (int i = 0; i < 400; i++) fputc('[', f);
  for (int i = 0; i < 400; i++) fputc(']', f);
  fprintf(f, ",\"sessions\":{\"x\":{\"state\":\"running\",\"window\":55}}}");
  fclose(f);
  n = ccm_load(tmpl, s, 8);
  assert(n == 1 && s[0].wid == 55 && s[0].running == 1);
  unlink(tmpl);
}

static void test_papp_and_colors(void) {
  const char* names[] = {"Slack", "Discord", "Beeper"};
  char r[3][PAPP_LABEL_MAX];
  struct dock_badge d[] = {
    {"Finder", "2"}, {"Slack", "\xe2\x80\xa2"}, {"Discord", "3"}, {"Beeper", "1"},
  };
  papp_match(d, 4, 3, names, r);
  assert(strcmp(r[0], "dot") == 0 && strcmp(r[1], "3") == 0 && strcmp(r[2], "1") == 0);

  struct dock_badge e[] = {{"Finder", "2"}, {"Slack", "7"}, {"Beeper", ""}};
  papp_match(e, 3, 3, names, r);
  assert(strcmp(r[0], "7") == 0 && strcmp(r[1], "off") == 0 && strcmp(r[2], "off") == 0);

  papp_match(NULL, 0, 3, names, r);
  assert(strcmp(r[0], "off") == 0 && strcmp(r[1], "off") == 0 && strcmp(r[2], "off") == 0);

  const char *bg, *fg;
  space_colors(false, 0, 0, &bg, &fg); assert(!strcmp(bg, COL_REST_BG) && !strcmp(fg, COL_DEFAULT));
  space_colors(false, 0, 2, &bg, &fg); assert(!strcmp(bg, COL_REST_BG) && !strcmp(fg, COL_RUNNING));
  space_colors(false, 1, 2, &bg, &fg); assert(!strcmp(bg, COL_REST_BG) && !strcmp(fg, COL_UNREAD));
  space_colors(true,  0, 0, &bg, &fg); assert(!strcmp(bg, COL_SEL_BG)  && !strcmp(fg, COL_SEL_FG));
  space_colors(true,  1, 0, &bg, &fg); assert(!strcmp(bg, COL_SEL_BG)  && !strcmp(fg, COL_SEL_FG));
  space_colors(true,  0, 1, &bg, &fg); assert(!strcmp(bg, COL_RUNNING) && !strcmp(fg, COL_SEL_FG));
  space_colors(true,  1, 1, &bg, &fg); assert(!strcmp(bg, COL_RUNNING) && !strcmp(fg, COL_SEL_FG));
}

static void test_config(void) {
  struct kuukan_config cfg;
  config_init_defaults(&cfg, "/Users/test");
  assert(cfg.npapps == 3);
  assert(strcmp(cfg.papps[0].app, "Slack") == 0 && strcmp(cfg.papps[0].item, "app.Slack") == 0);
  assert(strcmp(cfg.ccm_path, "/Users/test/.config/claude-code-manager/session-status.json") == 0);
  assert(strcmp(cfg.colors.sel_bg, COL_SEL_BG) == 0);

  char tmpl[] = "/tmp/spacesd_test_cfg_XXXXXX";
  int fd = mkstemp(tmpl);
  assert(fd >= 0);
  FILE* f = fdopen(fd, "w");
  assert(f != NULL);
  fprintf(f, "{\"bar\":{\"colors\":{\"mauve\":\"0xff112233\",\"text\":\"0xff445566\"}},"
             "\"pinned_apps\":["
             "{\"id\":\"Mail\",\"app\":\"Apple Mail\",\"bar\":true},"
             "{\"id\":\"Notes\",\"app\":\"Notes\",\"bar\":false}"
             "],"
             "\"session_status_file\":\"~/custom/status.json\"}");
  fclose(f);
  assert(config_apply_file(&cfg, tmpl, "/Users/test"));
  assert(cfg.npapps == 1);
  assert(strcmp(cfg.papps[0].app, "Apple Mail") == 0 && strcmp(cfg.papps[0].item, "app.Mail") == 0);
  assert(strcmp(cfg.ccm_path, "/Users/test/custom/status.json") == 0);
  assert(strcmp(cfg.colors.sel_bg, "0xff112233") == 0);
  assert(strcmp(cfg.colors.def, "0xff445566") == 0);
  assert(strcmp(cfg.colors.rest_bg, COL_REST_BG) == 0);

  const char *bg, *fg;
  space_colors_pal(&cfg.colors, true, 0, 0, &bg, &fg);
  assert(strcmp(bg, "0xff112233") == 0 && strcmp(fg, COL_SEL_FG) == 0);

  // Explicit empty pinned_apps and null session_status_file disable both features.
  f = fopen(tmpl, "w");
  assert(f != NULL);
  fprintf(f, "{\"pinned_apps\":[],\"session_status_file\":null}");
  fclose(f);
  assert(config_apply_file(&cfg, tmpl, "/Users/test"));
  assert(cfg.npapps == 0);
  assert(cfg.ccm_path[0] == '\0');
  assert(!config_apply_file(&cfg, "/nonexistent", "/Users/test"));
  unlink(tmpl);
}

int main(void) {
  test_yabai_frame();
  test_yabai_json();
  test_bar_builder();
  test_model();
  test_ccm();
  test_papp_and_colors();
  test_config();
  printf("all tests passed\n");
  return 0;
}
