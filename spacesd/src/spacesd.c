// spacesd — native space-policy daemon entry point, control socket, and event wiring.
#include "actions.h"
#include "ccm.h"
#include "mirror.h"
#include "render.h"
#include "skylight.h"
#include "state.h"
#include "workspace.h"
#include "yabai_ipc.h"

#include <dispatch/dispatch.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

// Carbon loop delivers CGS/AppKit notifications; bare CFRunLoopRun does not.
extern int RunApplicationEventLoop(void);

static void serve_client(int fd) {
  struct timeval tv = {.tv_sec = 0, .tv_usec = 500000};
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

  int len = 0;
  if (recv(fd, &len, sizeof(int), MSG_WAITALL) != sizeof(int)
      || len <= 0 || len > 4096) {
    close(fd);
    return;
  }
  char buf[4100];
  if (recv(fd, buf, (size_t)len, MSG_WAITALL) != len) {
    close(fd);
    return;
  }
  buf[len] = '\0';
  char* argv[16];
  int argc = 0;
  char* p = buf;
  while (p < buf + len && *p && argc < 16) {
    argv[argc++] = p;
    p += strlen(p) + 1;
  }
  static char reply[8192];
  handle_command(argc, argv, reply, sizeof(reply));
  send(fd, reply, strlen(reply), 0);
  close(fd);
}

static void control_server_start(void) {
  unlink(g_sock_path);
  int lfd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (lfd < 0) {
    logln("control: socket failed: %s", strerror(errno));
    return;
  }
  struct sockaddr_un addr = {0};
  addr.sun_family = AF_UNIX;
  snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", g_sock_path);
  if (bind(lfd, (struct sockaddr*)&addr, sizeof(addr)) != 0
      || listen(lfd, 8) != 0) {
    logln("control: cannot bind %s: %s", g_sock_path, strerror(errno));
    close(lfd);
    return;
  }
  dispatch_source_t src = dispatch_source_create(
      DISPATCH_SOURCE_TYPE_READ, (uintptr_t)lfd, 0, dispatch_get_main_queue());
  dispatch_source_set_event_handler(src, ^{
    int fd = accept(lfd, NULL, NULL);
    if (fd >= 0) serve_client(fd);
  });
  dispatch_resume(src);
  logln("control: listening on %s", g_sock_path);
}

static int client_main(int argc, char** argv) {
  char buf[4200];
  int total = yabai_frame(buf, sizeof(buf), argc, (const char**)argv);
  if (total < 0) { fprintf(stderr, "message too long\n"); return 1; }
  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) { fprintf(stderr, "socket failed\n"); return 1; }
  struct sockaddr_un addr = {0};
  addr.sun_family = AF_UNIX;
  snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", g_sock_path);
  if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
    fprintf(stderr, "spacesd not running (%s)\n", g_sock_path);
    close(fd);
    return 1;
  }
  send(fd, buf, (size_t)total, 0);
  shutdown(fd, SHUT_WR);
  char rsp[8192];
  int n = 0;
  ssize_t r;
  while (n < (int)sizeof(rsp) - 1 && (r = recv(fd, rsp + n, sizeof(rsp) - 1 - (size_t)n, 0)) > 0)
    n += (int)r;
  close(fd);
  rsp[n] = '\0';
  if (n) printf("%s\n", rsp);
  return strncmp(rsp, "err", 3) == 0 ? 1 : 0;
}

static void on_space_event(void) { logln("event: space"); schedule_sync(); }
static void on_wake(void) { full_resync("wake"); }
static void on_ccm_changed(void) {
  g_nccm = ccm_load(g_ccm_path, g_ccm, MAX_CCM);
  logln("ccm: reload n=%d", g_nccm);
  render(false);
}

static void sweep_timer_start(void) {
  dispatch_source_t t = dispatch_source_create(
      DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_main_queue());
  dispatch_source_set_timer(t, DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC, NSEC_PER_SEC / 2);
  dispatch_source_set_event_handler(t, ^{
    rebuild_windows();
    refresh_badges();
    render(false);
  });
  dispatch_resume(t);
}

int main(int argc, char** argv) {
  const char* home = getenv("HOME");
  if (!home) { fprintf(stderr, "HOME unset\n"); return 1; }
  snprintf(g_names_path, sizeof(g_names_path), "%s/.config/spacesd/names", home);
  snprintf(g_old_names_path, sizeof(g_old_names_path),
           "%s/.config/sketchybar/space_names", home);
  snprintf(g_sock_path, sizeof(g_sock_path), "%s/.config/spacesd/sock", home);
  snprintf(g_pairs_path, sizeof(g_pairs_path), "%s/.config/spacesd/mirrors", home);

  if (argc >= 2 && strcmp(argv[1], "-m") == 0)
    return client_main(argc - 2, argv + 2);
  for (int i = 1; i < argc; i++)
    if (strcmp(argv[i], "--shadow") == 0) g_shadow = true;

  config_load(&g_cfg, home);
  snprintf(g_ccm_path, sizeof(g_ccm_path), "%s", g_cfg.ccm_path);

  signal(SIGPIPE, SIG_IGN);
  char dir[560];
  snprintf(dir, sizeof(dir), "%s/.config/spacesd", home);
  mkdir(dir, 0755);

  sls_init();
  rebuild_spaces();
  names_load_or_migrate();
  full_resync("startup");
  mirror_init_displays(g_ndisplays);
  mirrors_persist();

  sls_register_space_notifications(on_space_event);
  workspace_observe(on_space_event, on_wake);
  CGDisplayRegisterReconfigurationCallback(on_display_reconfig, NULL);
  if (g_ccm_path[0]) ccm_watch(g_ccm_path, on_ccm_changed);
  control_server_start();
  sweep_timer_start();
  papp_timer_start();

  logln("spacesd up: displays=%d spaces=%d windows=%d names=%d ccm=%d papps=%d shadow=%d",
        g_ndisplays, g_nsp, g_nwin, g_nnames, g_nccm, g_cfg.npapps, g_shadow);
  RunApplicationEventLoop();
  return 0;
}
