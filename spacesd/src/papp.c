// papp.c — see papp.h.
#include "papp.h"
#include <stdio.h>
#include <string.h>

void papp_match(const struct dock_badge* items, int nitems, int n,
                const char* const* names, char (*result)[PAPP_LABEL_MAX]) {
  for (int i = 0; i < n; i++) {
    snprintf(result[i], PAPP_LABEL_MAX, "off");
    for (int j = 0; j < nitems; j++) {
      if (strcmp(items[j].title, names[i]) != 0 || !items[j].label[0]) continue;
      if (strcmp(items[j].label, "\xe2\x80\xa2") == 0)
        snprintf(result[i], PAPP_LABEL_MAX, "dot");
      else
        snprintf(result[i], PAPP_LABEL_MAX, "%s", items[j].label);
      break;
    }
  }
}
