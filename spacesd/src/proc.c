// proc.c — see proc.h.
#include "proc.h"
#include <libproc.h>
#include <stdlib.h>
#include <string.h>

pid_t proc_pid_for_executable(const char* path) {
  if (!path || !*path) return 0;
  int n = proc_listallpids(NULL, 0);
  if (n <= 0) return 0;
  n += 64; // headroom for processes started between the two calls
  pid_t* pids = calloc((size_t)n, sizeof(pid_t));
  if (!pids) return 0;
  n = proc_listallpids(pids, n * (int)sizeof(pid_t));
  pid_t found = 0;
  char buf[PROC_PIDPATHINFO_MAXSIZE];
  for (int i = 0; i < n && !found; i++)
    if (pids[i] > 0 && proc_pidpath(pids[i], buf, sizeof(buf)) > 0 && strcmp(buf, path) == 0)
      found = pids[i];
  free(pids);
  return found;
}
