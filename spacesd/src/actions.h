// actions.h — control-socket command handlers.
#pragma once
#include <stddef.h>

void handle_command(int argc, char** argv, char* reply, size_t rsz);
