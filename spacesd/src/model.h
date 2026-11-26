// model.h — pure data structures and algorithms shared by spacesd and unit tests.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define PINNED 1       // space 1 = messaging space: excluded from numbering/pairing
#define ITEM_MAX_MC 22 // sketchybarrc creates space.2..space.22
#define MAX_SPACES 64
#define MAX_WINDOWS 512
#define MAX_NAMES 64
#define MAX_CCM 256

struct sp {
  uint64_t sid;
  char uuid[40];
  int display; // arrangement index, 1-based
  int mc;      // global mission-control index
  bool visible;
  int pair;    // partner index in space table, or -1
};

struct win {
  uint32_t wid;
  uint64_t sid;
  pid_t pid;
  char app[48];
};

struct name_entry {
  char uuid[40];
  char name[64];
};

struct badge {
  char app[48];
  char label[24];
};

void buf_appendf(char* buf, size_t cap, size_t* off, const char* fmt, ...)
    __attribute__((format(printf, 4, 5)));
void pair_spaces(struct sp* sp, int nsp, int ndisplays);
int find_new_space_in(const struct sp* sp, int nsp, char snapshot[][40], int snap_n);
int pick_destroy_neighbor(const struct sp* sp, int nsp, int p);
