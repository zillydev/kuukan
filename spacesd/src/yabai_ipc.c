#include "yabai_ipc.h"
#define JSMN_STATIC
#include "../vendor/jsmn.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define FAILURE_BYTE '\x07' // yabai src/misc/macros.h FAILURE_MESSAGE
#define YABAI_JSON_MAX_TOK 2048

static int tok_skip(const jsmntok_t* t, int ntok, int i) {
  if (i < 0 || i >= ntok) return ntok;
  int need = 1;
  while (need > 0 && i < ntok) {
    need += t[i].size - 1;
    i++;
  }
  return i;
}

static bool tok_eq(const char* js, const jsmntok_t* t, const char* s) {
  int n = t->end - t->start;
  return t->type == JSMN_STRING && n >= 0 && (int)strlen(s) == n
         && strncmp(js + t->start, s, (size_t)n) == 0;
}

int yabai_frame(char* out, int cap, int argc, const char** argv) {
  int payload = 1;
  for (int i = 0; i < argc; i++) payload += (int)strlen(argv[i]) + 1;
  if ((int)sizeof(int) + payload > cap) return -1;
  memcpy(out, &payload, sizeof(int));
  char* p = out + sizeof(int);
  for (int i = 0; i < argc; i++) {
    size_t n = strlen(argv[i]);
    memcpy(p, argv[i], n);
    p += n;
    *p++ = '\0';
  }
  *p++ = '\0';
  return (int)(p - out);
}

static bool yabai_exchange(char* out, size_t outsz, char* err, size_t errsz,
                           int argc, const char** argv) {
  if (err && errsz) err[0] = '\0';
  if (out && outsz) out[0] = '\0';
  char buf[4096];
  int total = yabai_frame(buf, sizeof(buf), argc, argv);
  if (total < 0) return false;

  const char* user = getenv("USER");
  if (!user) return false;
  char path[256];
  snprintf(path, sizeof(path), "/tmp/yabai_%s.socket", user);

  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) return false;
  struct sockaddr_un addr = {0};
  addr.sun_family = AF_UNIX;
  snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);
  if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
    if (err) snprintf(err, errsz, "yabai socket unavailable");
    close(fd);
    return false;
  }
  bool ok = send(fd, buf, (size_t)total, 0) == total;
  shutdown(fd, SHUT_WR);

  char scratch[2048];
  char* dst = out ? out : scratch;
  size_t dstsz = out ? outsz : sizeof(scratch);
  size_t n = 0;
  while (ok && dstsz > 1 && n < dstsz - 1) {
    ssize_t r = recv(fd, dst + n, dstsz - 1 - n, 0);
    if (r <= 0) break;
    n += (size_t)r;
  }
  close(fd);
  if (dstsz > 0) dst[n] = '\0';
  if (n > 0 && dst[0] == FAILURE_BYTE) {
    if (err) snprintf(err, errsz, "%s", dst + 1);
    return false;
  }
  return ok;
}

bool yabai_cmd(char* err, size_t errsz, int argc, const char** argv) {
  return yabai_exchange(NULL, 0, err, errsz, argc, argv);
}

bool yabai_cmdf(char* err, size_t errsz, const char* fmt, ...) {
  char line[512];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(line, sizeof(line), fmt, ap);
  va_end(ap);

  const char* argv[16];
  int argc = 0;
  char* save = NULL;
  for (char* tok = strtok_r(line, " ", &save); tok && argc < 16;
       tok = strtok_r(NULL, " ", &save)) {
    argv[argc++] = tok;
  }
  return yabai_cmd(err, errsz, argc, argv);
}

// Scripting-addition direct wire: [int16 LE payload_len][opcode][args] to Dock.app.
static bool sa_send(const char* body, int body_len, uint8_t opcode) {
  char buf[32];
  if (body_len < 0 || body_len > (int)sizeof(buf) - 3) return false;
  int16_t payload = (int16_t)(1 + body_len);
  memcpy(buf, &payload, sizeof(payload));
  buf[2] = (char)opcode;
  if (body_len > 0) memcpy(buf + 3, body, (size_t)body_len);

  const char* user = getenv("USER");
  if (!user) return false;
  char path[256];
  snprintf(path, sizeof(path), "/tmp/yabai-sa_%s.socket", user);

  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) return false;
  struct sockaddr_un addr = {0};
  addr.sun_family = AF_UNIX;
  snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);
  bool ok = connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0
            && send(fd, buf, (size_t)(3 + body_len), 0) == (ssize_t)(3 + body_len);
  close(fd);
  return ok;
}

bool yabai_sa_space_switch(uint64_t sid) {
  return sa_send((const char*)&sid, sizeof(sid), 0x02); // SA_OPCODE_SPACE_FOCUS
}

bool yabai_sa_space_destroy(uint64_t sid) {
  return sa_send((const char*)&sid, sizeof(sid), 0x04); // SA_OPCODE_SPACE_DESTROY
}

bool yabai_sa_window_to_space(uint64_t sid, uint32_t wid) {
  char body[12];
  memcpy(body, &sid, 8);
  memcpy(body + 8, &wid, 4);
  return sa_send(body, sizeof(body), 0x13); // SA_OPCODE_WINDOW_TO_SPACE
}

bool yabai_sa_window_order(uint32_t wid, int32_t order, uint32_t rel_wid) {
  char body[12];
  memcpy(body + 0, &wid, 4);
  memcpy(body + 4, &order, 4);
  memcpy(body + 8, &rel_wid, 4);
  return sa_send(body, sizeof(body), 0x10); // SA_OPCODE_WINDOW_ORDER
}

bool yabai_query(char* out, size_t outsz, int argc, const char** argv) {
  return yabai_exchange(out, outsz, NULL, 0, argc, argv);
}

int yabai_parse_display_count(const char* js, size_t len) {
  if (!js || len == 0) return -1;
  jsmntok_t* tok = malloc(YABAI_JSON_MAX_TOK * sizeof(jsmntok_t));
  if (!tok) return -1;
  jsmn_parser p;
  jsmn_init(&p);
  int ntok = jsmn_parse(&p, js, len, tok, YABAI_JSON_MAX_TOK);
  int count = -1;
  if (ntok >= 1 && tok[0].type == JSMN_ARRAY) {
    int n = 0;
    int i = 1;
    for (int e = 0; e < tok[0].size && i < ntok; e++) {
      if (tok[i].type == JSMN_OBJECT) n++;
      i = tok_skip(tok, ntok, i);
    }
    if (n > 0) count = n;
  }
  free(tok);
  return count;
}

bool yabai_parse_space_uuid(const char* js, size_t len, char* out, size_t outsz) {
  if (!js || len == 0 || !out || outsz == 0) return false;
  out[0] = '\0';
  jsmntok_t* tok = malloc(YABAI_JSON_MAX_TOK * sizeof(jsmntok_t));
  if (!tok) return false;
  jsmn_parser p;
  jsmn_init(&p);
  int ntok = jsmn_parse(&p, js, len, tok, YABAI_JSON_MAX_TOK);
  bool found = false;
  if (ntok >= 1 && tok[0].type == JSMN_OBJECT) {
    int i = 1;
    for (int k = 0; k < tok[0].size && i + 1 < ntok; k++) {
      int key = i, val = i + 1;
      i = tok_skip(tok, ntok, val);
      if (tok_eq(js, &tok[key], "uuid") && tok[val].type == JSMN_STRING) {
        int vlen = tok[val].end - tok[val].start;
        if (vlen > 0) {
          size_t copy = (size_t)vlen < outsz - 1 ? (size_t)vlen : outsz - 1;
          memcpy(out, js + tok[val].start, copy);
          out[copy] = '\0';
          found = true;
        }
        break;
      }
    }
  }
  free(tok);
  return found;
}

bool yabai_parse_window(const char* js, size_t len, uint32_t* wid, int* space, pid_t* pid) {
  if (!js || len == 0) return false;
  jsmntok_t* tok = malloc(YABAI_JSON_MAX_TOK * sizeof(jsmntok_t));
  if (!tok) return false;
  jsmn_parser p;
  jsmn_init(&p);
  int ntok = jsmn_parse(&p, js, len, tok, YABAI_JSON_MAX_TOK);
  long id_val = -1, sp_val = -1, pid_val = 0;
  if (ntok >= 1 && tok[0].type == JSMN_OBJECT) {
    int i = 1;
    for (int k = 0; k < tok[0].size && i + 1 < ntok; k++) {
      int key = i, val = i + 1;
      i = tok_skip(tok, ntok, val);
      if (tok[val].type != JSMN_PRIMITIVE) continue;
      if (tok_eq(js, &tok[key], "id"))
        id_val = strtol(js + tok[val].start, NULL, 10);
      else if (tok_eq(js, &tok[key], "space"))
        sp_val = strtol(js + tok[val].start, NULL, 10);
      else if (tok_eq(js, &tok[key], "pid"))
        pid_val = strtol(js + tok[val].start, NULL, 10);
    }
  }
  free(tok);
  if (id_val <= 0) return false;
  if (wid) *wid = (uint32_t)id_val;
  if (space) *space = (int)sp_val;
  if (pid) *pid = (pid_t)pid_val;
  return true;
}

int yabai_display_count(void) {
  char out[16384];
  const char* argv[] = {"query", "--displays"};
  if (!yabai_query(out, sizeof(out), 2, argv)) return -1;
  return yabai_parse_display_count(out, strlen(out));
}
