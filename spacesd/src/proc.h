// proc.h — process lookups through libproc.
#pragma once
#include <unistd.h>

// pid of a running process whose executable is path, or 0.
pid_t proc_pid_for_executable(const char* path);
