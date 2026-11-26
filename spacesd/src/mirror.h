// mirror.h — multi-display mirroring, sync/resync, and display reconfiguration lifecycle.
#pragma once
#include <CoreGraphics/CoreGraphics.h>

void sync_now(void);
void schedule_sync(void);
void full_resync(const char* why);
void mirrors_persist(void);
void mirror_connect_manual(void);
void mirror_disconnect(void);
void mirror_init_displays(int ndisplays);
void on_display_reconfig(CGDirectDisplayID did, CGDisplayChangeSummaryFlags flags, void* user);
