// render.h — diffed SketchyBar rendering for space items and pinned app badges.
#pragma once
#include <stdbool.h>

void render(bool full);
void render_reset(void);
void papp_reset(void);
void papp_poll(void);
void papp_timer_start(void);
