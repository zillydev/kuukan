#include "model.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void buf_appendf(char* buf, size_t cap, size_t* off, const char* fmt, ...) {
  if (cap == 0 || *off >= cap - 1) return;
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf + *off, cap - *off, fmt, ap);
  va_end(ap);
  if (n < 0) return;
  size_t rem = cap - *off - 1;
  *off += ((size_t)n < rem) ? (size_t)n : rem;
}

void pair_spaces(struct sp* sp, int nsp, int ndisplays) {
  for (int i = 0; i < nsp; i++) sp[i].pair = -1;
  if (ndisplays < 2) return;
  int d1[MAX_SPACES], d2[MAX_SPACES], n1 = 0, n2 = 0;
  for (int i = 0; i < nsp; i++) {
    if (sp[i].display == 1 && sp[i].mc > PINNED) d1[n1++] = i;
    if (sp[i].display == 2) d2[n2++] = i;
  }
  for (int i = 0; i < n1 && i < n2; i++) {
    sp[d1[i]].pair = d2[i];
    sp[d2[i]].pair = d1[i];
  }
}

int find_new_space_in(const struct sp* sp, int nsp, char snapshot[][40], int snap_n) {
  for (int i = 0; i < nsp; i++) {
    bool known = false;
    for (int j = 0; j < snap_n; j++) {
      if (strcmp(sp[i].uuid, snapshot[j]) == 0) {
        known = true;
        break;
      }
    }
    if (!known) return i;
  }
  return -1;
}

int pick_destroy_neighbor(const struct sp* sp, int nsp, int p) {
  int left = -1, right = -1;
  for (int i = 0; i < nsp; i++) {
    if (sp[i].display != 1 || sp[i].mc <= PINNED || i == p) continue;
    if (sp[i].mc < sp[p].mc) {
      if (left < 0 || sp[i].mc > sp[left].mc) left = i;
    } else if (right < 0 || sp[i].mc < sp[right].mc) {
      right = i;
    }
  }
  return left >= 0 ? left : right;
}
