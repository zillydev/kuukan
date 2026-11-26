// papp.h — pinned-app badge matching (app.Slack / app.Discord / app.Beeper) from the Dock's
// icon list over Accessibility (sls_dock_badges): lsappinfo stopped reporting StatusLabel on macOS 27.
#pragma once

#define PAPP_LABEL_MAX 32

// One Dock icon: its title and AXStatusLabel badge ("" when none).
struct dock_badge { char title[64]; char label[PAPP_LABEL_MAX]; };

// Fills result[i] for names[i] with "off", "dot" (a bare • label), or the
// badge text. An app without a Dock icon is off.
void papp_match(const struct dock_badge* items, int nitems, int n,
                const char* const* names, char (*result)[PAPP_LABEL_MAX]);
