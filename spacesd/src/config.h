// config.h — JSON configuration loader for spacesd (pinned apps, palette, session status).
#pragma once
#include "colors.h"
#include <stdbool.h>

#define MAX_PAPPS 16

struct papp_cfg {
  char app[64];
  char item[64];
};

struct kuukan_config {
  struct papp_cfg papps[MAX_PAPPS];
  int npapps;
  char ccm_path[512];
  struct color_palette colors;
};

void config_init_defaults(struct kuukan_config* cfg, const char* home);
bool config_apply_file(struct kuukan_config* cfg, const char* path, const char* home);
void config_load(struct kuukan_config* cfg, const char* home);
