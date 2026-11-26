// colors.h — space and pinned-app palette plus the visible/unread/running colour policy.
#pragma once
#include <stdbool.h>

#define COLOR_STR_MAX 16

#define COL_SEL_BG   "0xffcba6f7"
#define COL_SEL_FG   "0xff1e1e2e"
#define COL_REST_BG  "0xff313244"
#define COL_UNREAD   "0xfffab387"
#define COL_RUNNING  "0xffa6e3a1"
#define COL_DEFAULT  "0xffcdd6f4"
#define COL_SUBTEXT  "0xffa6adc8"

struct color_palette {
  char sel_bg[COLOR_STR_MAX];
  char sel_fg[COLOR_STR_MAX];
  char rest_bg[COLOR_STR_MAX];
  char unread[COLOR_STR_MAX];
  char running[COLOR_STR_MAX];
  char def[COLOR_STR_MAX];
  char subtext[COLOR_STR_MAX];
};

static inline struct color_palette color_palette_default(void) {
  struct color_palette p = {
    .sel_bg  = COL_SEL_BG,
    .sel_fg  = COL_SEL_FG,
    .rest_bg = COL_REST_BG,
    .unread  = COL_UNREAD,
    .running = COL_RUNNING,
    .def     = COL_DEFAULT,
    .subtext = COL_SUBTEXT,
  };
  return p;
}

// A selected running space uses green fill because green text does not read on mauve.
static inline void space_colors_pal(const struct color_palette* pal,
                                    bool visible, int un, int ru,
                                    const char** bg, const char** fg) {
  if (visible)     { *bg = ru > 0 ? pal->running : pal->sel_bg; *fg = pal->sel_fg; }
  else if (un > 0) { *bg = pal->rest_bg; *fg = pal->unread; }
  else if (ru > 0) { *bg = pal->rest_bg; *fg = pal->running; }
  else             { *bg = pal->rest_bg; *fg = pal->def; }
}

static inline void space_colors(bool visible, int un, int ru,
                                const char** bg, const char** fg) {
  if (visible)     { *bg = ru > 0 ? COL_RUNNING : COL_SEL_BG; *fg = COL_SEL_FG; }
  else if (un > 0) { *bg = COL_REST_BG; *fg = COL_UNREAD; }
  else if (ru > 0) { *bg = COL_REST_BG; *fg = COL_RUNNING; }
  else             { *bg = COL_REST_BG; *fg = COL_DEFAULT; }
}
