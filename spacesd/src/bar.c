#include "bar.h"
#include <bootstrap.h>
#include <mach/mach.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Mach OOL send matching SketchyBar's src/mach.c mach_send_message.
struct mach_message {
  mach_msg_header_t header;
  mach_msg_size_t msgh_descriptor_count;
  mach_msg_ool_descriptor_t descriptor;
};

static mach_port_t g_bar_port = MACH_PORT_NULL;

static mach_port_t bar_lookup(void) {
  mach_port_t bs_port;
  if (task_get_special_port(mach_task_self(), TASK_BOOTSTRAP_PORT, &bs_port)
      != KERN_SUCCESS) return MACH_PORT_NULL;
  char* name = getenv("BAR_NAME");
  if (!name) name = "sketchybar";
  char buffer[256];
  snprintf(buffer, sizeof(buffer), "git.felix.%s", name);
  mach_port_t port;
  if (bootstrap_look_up(bs_port, buffer, &port) != KERN_SUCCESS) return MACH_PORT_NULL;
  return port;
}

static bool mach_send(mach_port_t port, char* message, uint32_t len) {
  if (!message || !port) return false;
  struct mach_message msg = {0};
  msg.header.msgh_remote_port = port;
  msg.header.msgh_local_port = 0;
  msg.header.msgh_id = 0;
  msg.header.msgh_bits = MACH_MSGH_BITS_SET(MACH_MSG_TYPE_COPY_SEND,
                                            MACH_MSG_TYPE_MAKE_SEND,
                                            0, MACH_MSGH_BITS_COMPLEX);
  msg.header.msgh_size = sizeof(struct mach_message);
  msg.msgh_descriptor_count = 1;
  msg.descriptor.address = message;
  msg.descriptor.size = len;
  msg.descriptor.copy = MACH_MSG_VIRTUAL_COPY;
  msg.descriptor.deallocate = false;
  msg.descriptor.type = MACH_MSG_OOL_DESCRIPTOR;
  return mach_msg(&msg.header, MACH_SEND_MSG, sizeof(struct mach_message), 0,
                  MACH_PORT_NULL, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL)
         == KERN_SUCCESS;
}

#define BAR_BUF_MAX 65536
static char g_buf[BAR_BUF_MAX];
static int g_len = 0;
static int g_args = 0;

void bar_msg_begin(void) { g_len = 0; g_args = 0; }

void bar_msg_argf(const char* fmt, ...) {
  int avail = BAR_BUF_MAX - g_len - 2; // reserve arg NUL + message-terminating NUL
  if (avail <= 0) return;
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(g_buf + g_len, (size_t)avail, fmt, ap);
  va_end(ap);
  if (n < 0) return;
  if (n >= avail) n = avail - 1;
  g_len += n;
  g_buf[g_len++] = '\0';
  g_args++;
}

int bar_msg_count(void) { return g_args; }

int bar_msg_snapshot(char* out, int cap) {
  if (cap <= 0) return 0;
  int n = g_len < cap - 1 ? g_len : cap - 1;
  memcpy(out, g_buf, (size_t)n);
  out[n] = '\0';
  return n;
}

bool bar_msg_flush(void) {
  if (g_args == 0) return true;
  g_buf[g_len++] = '\0'; // trailing empty arg terminates the message
  bool ok = false;
  if (!g_bar_port) g_bar_port = bar_lookup();
  if (g_bar_port) ok = mach_send(g_bar_port, g_buf, (uint32_t)g_len);
  if (!ok) {
    if (g_bar_port) {
      mach_port_deallocate(mach_task_self(), g_bar_port);
      g_bar_port = MACH_PORT_NULL;
    }
    g_bar_port = bar_lookup();
    if (g_bar_port) ok = mach_send(g_bar_port, g_buf, (uint32_t)g_len);
  }
  bar_msg_begin();
  return ok;
}
