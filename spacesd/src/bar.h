// bar.h — outbound rendering to SketchyBar over the CLI wire protocol:
// NUL-separated args + trailing NUL, one Mach OOL descriptor to bootstrap
// name "git.felix.$BAR_NAME". This is the same protocol the sketchybar
// binary speaks (sketchybar src/sketchybar.c client_send_message) — the
// stable surface — NOT the mach_helper env format.
#pragma once
#include <stdbool.h>

void bar_msg_begin(void);
void bar_msg_argf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
int  bar_msg_count(void);               // args queued so far
// Copies the pending message into out (NUL-separated) for shadow logging.
int  bar_msg_snapshot(char* out, int cap);
bool bar_msg_flush(void);               // false if the bar is unreachable
