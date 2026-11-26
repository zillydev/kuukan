// yabai_ipc.h — yabai unix-socket client, SA sender, and JSON response parsers.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

int yabai_frame(char* out, int cap, int argc, const char** argv);
bool yabai_cmd(char* err, size_t errsz, int argc, const char** argv);
bool yabai_cmdf(char* err, size_t errsz, const char* fmt, ...)
    __attribute__((format(printf, 3, 4)));

bool yabai_sa_space_switch(uint64_t sid);
bool yabai_sa_space_destroy(uint64_t sid);
bool yabai_sa_window_to_space(uint64_t sid, uint32_t wid);
bool yabai_sa_window_order(uint32_t wid, int32_t order, uint32_t rel_wid);

bool yabai_query(char* out, size_t outsz, int argc, const char** argv);
int yabai_display_count(void);

// Pure JSON parsers for yabai query replies (jsmn-backed).
int yabai_parse_display_count(const char* js, size_t len);
bool yabai_parse_space_uuid(const char* js, size_t len, char* out, size_t outsz);
bool yabai_parse_window(const char* js, size_t len, uint32_t* wid, int* space, pid_t* pid);
