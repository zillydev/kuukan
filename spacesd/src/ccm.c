#include "ccm.h"
#define JSMN_STATIC
#include "../vendor/jsmn.h"
#include <dispatch/dispatch.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CCM_MAX_FILE (1 << 20)
#define CCM_MAX_TOK 8192

static int tok_eq(const char* js, const jsmntok_t* t, const char* s) {
  int n = t->end - t->start;
  return t->type == JSMN_STRING && n >= 0 && (int)strlen(s) == n
         && strncmp(js + t->start, s, (size_t)n) == 0;
}

static int tok_skip(const jsmntok_t* t, int ntok, int i) {
  if (i < 0 || i >= ntok) return ntok;
  int need = 1;
  while (need > 0 && i < ntok) {
    need += t[i].size - 1;
    i++;
  }
  return i;
}

int ccm_load(const char* path, struct ccm_session* out, int max) {
  if (!path || !*path) return 0;
  int count = 0;
  FILE* f = fopen(path, "r");
  if (!f) return 0;
  struct stat st;
  size_t cap = CCM_MAX_FILE;
  if (fstat(fileno(f), &st) == 0 && st.st_size >= 0) {
    size_t sz = (size_t)st.st_size;
    cap = (sz < CCM_MAX_FILE - 1) ? sz + 1 : CCM_MAX_FILE;
  }
  char* js = malloc(cap);
  if (!js) { fclose(f); return 0; }
  size_t len = fread(js, 1, cap - 1, f);
  fclose(f);
  js[len] = '\0';

  jsmntok_t* tok = malloc(CCM_MAX_TOK * sizeof(jsmntok_t));
  if (!tok) { free(js); return 0; }
  jsmn_parser p;
  jsmn_init(&p);
  int ntok = jsmn_parse(&p, js, len, tok, CCM_MAX_TOK);
  if (ntok < 1 || tok[0].type != JSMN_OBJECT) {
    free(tok);
    free(js);
    return 0;
  }

  int i = 1;
  for (int k = 0; k < tok[0].size && i + 1 < ntok; k++) {
    int key = i, val = i + 1;
    i = tok_skip(tok, ntok, val);
    if (!tok_eq(js, &tok[key], "sessions") || tok[val].type != JSMN_OBJECT)
      continue;
    int si = val + 1;
    for (int s = 0; s < tok[val].size && si + 1 < ntok; s++) {
      int sval = si + 1;
      si = tok_skip(tok, ntok, sval);
      if (tok[sval].type != JSMN_OBJECT) continue;
      long wid = -1;
      int unread = 0, running = 0;
      int fi = sval + 1;
      for (int fnum = 0; fnum < tok[sval].size && fi + 1 < ntok; fnum++) {
        int fkey = fi, fval = fi + 1;
        fi = tok_skip(tok, ntok, fval);
        if (tok_eq(js, &tok[fkey], "window")
            && tok[fval].type == JSMN_PRIMITIVE) {
          wid = strtol(js + tok[fval].start, NULL, 10);
        } else if (tok_eq(js, &tok[fkey], "state")
                   && tok[fval].type == JSMN_STRING) {
          unread = tok_eq(js, &tok[fval], "unread");
          running = tok_eq(js, &tok[fval], "running");
        }
      }
      if (wid > 0 && (unread || running) && count < max) {
        out[count].wid = (uint32_t)wid;
        out[count].unread = unread;
        out[count].running = running;
        count++;
      }
    }
  }
  free(tok);
  free(js);
  return count;
}

static char* g_path = NULL;
static char* g_dir = NULL;
static void (*g_changed)(void) = NULL;
static dispatch_source_t g_src = NULL;
static bool g_debounce = false;
static int g_retry_ms = 200;

static void ccm_arm(void);

static void ccm_fire(void) {
  if (g_debounce) return;
  g_debounce = true;
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 30 * NSEC_PER_MSEC),
                 dispatch_get_main_queue(), ^{
                   g_debounce = false;
                   ccm_arm();
                   if (g_changed) g_changed();
                 });
}

// Watch the file directly when present; fall back to its parent directory when absent
// so file creation wakes via kqueue instead of polling.
static void ccm_arm(void) {
  if (g_src) {
    dispatch_source_cancel(g_src);
    g_src = NULL;
  }
  int fd = open(g_path, O_EVTONLY);
  unsigned long mask = DISPATCH_VNODE_WRITE | DISPATCH_VNODE_DELETE
                       | DISPATCH_VNODE_RENAME | DISPATCH_VNODE_EXTEND;
  if (fd < 0 && g_dir) {
    fd = open(g_dir, O_EVTONLY);
    mask = DISPATCH_VNODE_WRITE | DISPATCH_VNODE_DELETE | DISPATCH_VNODE_RENAME;
  }
  if (fd < 0) {
    int delay = g_retry_ms;
    g_retry_ms = (g_retry_ms < 5000) ? g_retry_ms * 2 : 5000;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)delay * NSEC_PER_MSEC),
                   dispatch_get_main_queue(), ^{ ccm_arm(); });
    return;
  }
  g_retry_ms = 200;
  g_src = dispatch_source_create(DISPATCH_SOURCE_TYPE_VNODE, (uintptr_t)fd,
                                 mask, dispatch_get_main_queue());
  dispatch_source_set_event_handler(g_src, ^{ ccm_fire(); });
  dispatch_source_set_cancel_handler(g_src, ^{ close(fd); });
  dispatch_resume(g_src);
}

void ccm_watch(const char* path, void (*changed)(void)) {
  g_path = strdup(path);
  const char* slash = strrchr(path, '/');
  if (slash && slash > path) {
    size_t dlen = (size_t)(slash - path);
    g_dir = malloc(dlen + 1);
    if (g_dir) {
      memcpy(g_dir, path, dlen);
      g_dir[dlen] = '\0';
    }
  }
  g_changed = changed;
  ccm_arm();
}
