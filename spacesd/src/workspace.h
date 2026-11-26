// workspace.h — public-API fallbacks and helpers via AppKit (pattern:
// sketchybar src/workspace.m).
#pragma once
#include <stdbool.h>
#include <unistd.h>

// Registers NSWorkspace observers. space_changed doubles as the public-API
// backstop for the private 1327/1328 notifications; woke fires after sleep.
// Callbacks arrive on the main thread.
void workspace_observe(void (*space_changed)(void), void (*woke)(void));

// malloc'd localized app name for pid, or NULL.
char* workspace_copy_app_name(pid_t pid);

// Activates the running app whose display or bundle name matches (case-
// insensitive); false if no running app matched.
bool workspace_activate_app(const char* name);

// pid of the running app matching name (display or bundle name), or 0.
pid_t workspace_pid_for_app(const char* name);

// Cooperative AppKit activation of a running app. Only reliably honored once
// the app is already the window-server front process (see cmd_app).
bool workspace_activate_pid(pid_t pid);


