// ccm.h — claude-code-manager session-status ingestion.
// File: ~/.config/claude-code-manager/session-status.json, replaced by rename.
// Only "unread" and "running" states matter (matches the retired jq).
#pragma once
#include <stdint.h>

struct ccm_session {
  uint32_t wid;   // CG window id — same id space as SLSWindowIteratorGetWindowID
  int unread;     // 1 if state == "unread"
  int running;    // 1 if state == "running"
};

// Parses path; returns session count (windowless/neutral sessions excluded),
// 0 on missing/malformed file. Never fatal.
int ccm_load(const char* path, struct ccm_session* out, int max);

// Watches path via a vnode dispatch source on the main queue; survives the
// rename-replace pattern by reopening after every event; debounced 80ms.
void ccm_watch(const char* path, void (*changed)(void));
