#include "config.h"
#define JSMN_STATIC
#include "../vendor/jsmn.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define CFG_MAX_FILE (1 << 18)
#define CFG_MAX_TOK 4096

static int tok_eq(const char* js, const jsmntok_t* t, const char* s) {
  int n = t->end - t->start;
  return t->type == JSMN_STRING && n >= 0 && (int)strlen(s) == n
         && strncmp(js + t->start, s, (size_t)n) == 0;
}

static bool tok_prim_eq(const char* js, const jsmntok_t* t, const char* s) {
  int n = t->end - t->start;
  return t->type == JSMN_PRIMITIVE && n >= 0 && (int)strlen(s) == n
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

static void tok_copy(const char* js, const jsmntok_t* t, char* out, size_t cap) {
  if (cap == 0) return;
  int n = t->end - t->start;
  if (n < 0) n = 0;
  size_t len = (size_t)n;
  if (len >= cap) len = cap - 1;
  memcpy(out, js + t->start, len);
  out[len] = '\0';
}

static void expand_home(const char* in, const char* home, char* out, size_t cap) {
  if (in[0] == '~' && (in[1] == '/' || in[1] == '\0') && home && *home)
    snprintf(out, cap, "%s%s", home, in + 1);
  else
    snprintf(out, cap, "%s", in);
}

void config_init_defaults(struct kuukan_config* cfg, const char* home) {
  memset(cfg, 0, sizeof(*cfg));
  static const struct { const char* app; const char* item; } defs[3] = {
    {"Slack", "app.Slack"}, {"Discord", "app.Discord"}, {"Beeper Desktop", "app.Beeper"},
  };
  cfg->npapps = 3;
  for (int i = 0; i < 3; i++) {
    snprintf(cfg->papps[i].app, sizeof(cfg->papps[i].app), "%s", defs[i].app);
    snprintf(cfg->papps[i].item, sizeof(cfg->papps[i].item), "%s", defs[i].item);
  }
  if (home && *home)
    snprintf(cfg->ccm_path, sizeof(cfg->ccm_path),
             "%s/.config/claude-code-manager/session-status.json", home);
  cfg->colors = color_palette_default();
}

static void parse_colors(struct color_palette* pal, const char* js,
                         const jsmntok_t* tok, int ntok, int obj) {
  int i = obj + 1;
  for (int k = 0; k < tok[obj].size && i + 1 < ntok; k++) {
    int key = i, val = i + 1;
    i = tok_skip(tok, ntok, val);
    if (tok[val].type != JSMN_STRING || tok[val].end <= tok[val].start) continue;
    char* dst = NULL;
    if (tok_eq(js, &tok[key], "mauve"))         dst = pal->sel_bg;
    else if (tok_eq(js, &tok[key], "base"))     dst = pal->sel_fg;
    else if (tok_eq(js, &tok[key], "surface0")) dst = pal->rest_bg;
    else if (tok_eq(js, &tok[key], "peach"))    dst = pal->unread;
    else if (tok_eq(js, &tok[key], "green"))    dst = pal->running;
    else if (tok_eq(js, &tok[key], "text"))     dst = pal->def;
    else if (tok_eq(js, &tok[key], "subtext"))  dst = pal->subtext;
    if (dst) tok_copy(js, &tok[val], dst, COLOR_STR_MAX);
  }
}

static void parse_bar(struct kuukan_config* cfg, const char* js,
                      const jsmntok_t* tok, int ntok, int obj) {
  int i = obj + 1;
  for (int k = 0; k < tok[obj].size && i + 1 < ntok; k++) {
    int key = i, val = i + 1;
    i = tok_skip(tok, ntok, val);
    if (tok_eq(js, &tok[key], "colors") && tok[val].type == JSMN_OBJECT)
      parse_colors(&cfg->colors, js, tok, ntok, val);
  }
}

static void parse_pinned_apps(struct kuukan_config* cfg, const char* js,
                              const jsmntok_t* tok, int ntok, int arr) {
  cfg->npapps = 0;
  int i = arr + 1;
  for (int a = 0; a < tok[arr].size && i < ntok; a++) {
    int elem = i;
    i = tok_skip(tok, ntok, elem);
    if (tok[elem].type != JSMN_OBJECT || cfg->npapps >= MAX_PAPPS) continue;
    char id[56] = "", app[64] = "";
    bool bar = false;
    int fi = elem + 1;
    for (int f = 0; f < tok[elem].size && fi + 1 < ntok; f++) {
      int fkey = fi, fval = fi + 1;
      fi = tok_skip(tok, ntok, fval);
      if (tok_eq(js, &tok[fkey], "id") && tok[fval].type == JSMN_STRING)
        tok_copy(js, &tok[fval], id, sizeof(id));
      else if (tok_eq(js, &tok[fkey], "app") && tok[fval].type == JSMN_STRING)
        tok_copy(js, &tok[fval], app, sizeof(app));
      else if (tok_eq(js, &tok[fkey], "bar"))
        bar = tok_prim_eq(js, &tok[fval], "true");
    }
    if (bar && id[0] && app[0]) {
      snprintf(cfg->papps[cfg->npapps].app, sizeof(cfg->papps[cfg->npapps].app), "%s", app);
      snprintf(cfg->papps[cfg->npapps].item, sizeof(cfg->papps[cfg->npapps].item), "app.%s", id);
      cfg->npapps++;
    }
  }
}

bool config_apply_file(struct kuukan_config* cfg, const char* path, const char* home) {
  if (!path || !*path) return false;
  FILE* f = fopen(path, "r");
  if (!f) return false;
  struct stat st;
  size_t cap = CFG_MAX_FILE;
  if (fstat(fileno(f), &st) == 0 && st.st_size >= 0) {
    size_t sz = (size_t)st.st_size;
    cap = (sz < CFG_MAX_FILE - 1) ? sz + 1 : CFG_MAX_FILE;
  }
  char* js = malloc(cap);
  if (!js) { fclose(f); return false; }
  size_t len = fread(js, 1, cap - 1, f);
  fclose(f);
  js[len] = '\0';

  jsmntok_t* tok = malloc(CFG_MAX_TOK * sizeof(jsmntok_t));
  if (!tok) { free(js); return false; }
  jsmn_parser p;
  jsmn_init(&p);
  int ntok = jsmn_parse(&p, js, len, tok, CFG_MAX_TOK);
  if (ntok < 1 || tok[0].type != JSMN_OBJECT) {
    free(tok);
    free(js);
    return false;
  }

  int i = 1;
  for (int k = 0; k < tok[0].size && i + 1 < ntok; k++) {
    int key = i, val = i + 1;
    i = tok_skip(tok, ntok, val);
    if (tok_eq(js, &tok[key], "pinned_apps") && tok[val].type == JSMN_ARRAY) {
      parse_pinned_apps(cfg, js, tok, ntok, val);
    } else if (tok_eq(js, &tok[key], "bar") && tok[val].type == JSMN_OBJECT) {
      parse_bar(cfg, js, tok, ntok, val);
    } else if (tok_eq(js, &tok[key], "session_status_file")) {
      if (tok[val].type == JSMN_STRING) {
        char raw[512];
        tok_copy(js, &tok[val], raw, sizeof(raw));
        if (raw[0]) expand_home(raw, home, cfg->ccm_path, sizeof(cfg->ccm_path));
        else cfg->ccm_path[0] = '\0';
      } else if (tok_prim_eq(js, &tok[val], "null")) {
        cfg->ccm_path[0] = '\0';
      }
    }
  }
  free(tok);
  free(js);
  return true;
}

void config_load(struct kuukan_config* cfg, const char* home) {
  config_init_defaults(cfg, home);
  char path[512];
  if (home && *home) {
    snprintf(path, sizeof(path), "%s/.config/kuukan/config.default.json", home);
    config_apply_file(cfg, path, home);
  }
  const char* env = getenv("KUUKAN_CONFIG");
  if (env && *env) {
    config_apply_file(cfg, env, home);
  } else if (home && *home) {
    snprintf(path, sizeof(path), "%s/.config/kuukan/config.json", home);
    config_apply_file(cfg, path, home);
  }
}
