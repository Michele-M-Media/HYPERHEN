#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <ps5/klog.h>

#include "cr_hotkey_signal.h"
#include "cr_plog.h"

void
cr_plog(const char *fmt, ...) {
  char msg[CR_LOG_SIGNAL_MAX];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);
  klog_puts(msg);

  size_t n = strlen(msg);
  while (n > 0 && (msg[n - 1] == '\n' || msg[n - 1] == '\r')) {
    msg[--n] = 0;
  }

  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) {
    return;
  }
  char pkt[CR_LOG_SIGNAL_MAX + CR_LOG_SIGNAL_PREFIX_LEN];
  memcpy(pkt, CR_LOG_SIGNAL_PREFIX, CR_LOG_SIGNAL_PREFIX_LEN);
  memcpy(pkt + CR_LOG_SIGNAL_PREFIX_LEN, msg, n);

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(CR_HOTKEY_SIGNAL_PORT);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  sendto(fd, pkt, n + CR_LOG_SIGNAL_PREFIX_LEN, 0, (struct sockaddr *)&addr, sizeof(addr));
  close(fd);
}
