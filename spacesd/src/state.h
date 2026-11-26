// state.h — live daemon tables, persistence paths, and SLS/yabai state helpers.
#pragma once
#include "model.h"
#include "ccm.h"
#include "config.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

extern struct kuukan_config g_cfg;
extern char g_names_path[512];
extern char g_old_names_path[512];
extern char g_sock_path[512];
extern char g_ccm_path[512];
extern char g_pairs_path[512];
extern bool g_shadow;

extern struct sp g_sp[MAX_SPACES];
extern int g_nsp;
extern int g_ndisplays;

extern struct win g_win[MAX_WINDOWS];
extern int g_nwin;

extern struct name_entry g_names[MAX_NAMES];
extern int g_nnames;

extern struct ccm_session g_ccm[MAX_CCM];
extern int g_nccm;

extern struct badge g_badges[8];
extern int g_nbadges;

extern uint64_t g_gen;
extern uint64_t g_app_gen;

void logln(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

int sp_by_sid(uint64_t sid);
int sp_by_mc(int mc);
int sp_by_uuid(const char* uuid);
int win_on_sid(uint64_t sid);
int visible_on_display(int display);
int active_primary(void);

bool rebuild_spaces(void);
void rebuild_windows(void);
void wait_rebuild(void);
int find_new_space(char snapshot[][40], int snap_n);

bool destroy_space(int i, char* err, size_t errsz);
void move_window(uint32_t wid, int i);
bool yabai_agrees(int i);

const char* name_for_uuid(const char* uuid);
void names_set(const char* uuid, const char* name);
void names_load_or_migrate(void);

bool app_is_running(const char* app);
const char* badge_for_app(const char* app);
void refresh_badges(void);
