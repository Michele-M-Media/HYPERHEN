/* HyperHEN contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "bootstrap_hyperdeck.h"
#include <onion/elfldr.h>
#include <onion/log.h>
#include <ps5/kernel.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define HYPERDECK_LOG "/data/HyperHEN/HyperDeck.log"
#define HYPERDECK_PTRACE_AUTHID 0x4800000000010003ULL
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0x20000
#endif

void bootstrap_hyperdeck_record(const char *stage, int result) {
  LOG_INFO("HyperDeck r3 stage=%s result=0x%08x", stage, (unsigned)result);
  int fd = open(HYPERDECK_LOG, O_WRONLY | O_CREAT | O_APPEND, 0666);
  if (fd < 0) {
    LOG_ERROR("HyperDeck parent log unavailable: %s", strerror(errno));
    return;
  }
  char line[192];
  const int size = snprintf(line, sizeof(line),
      "[HyperDeck bootstrap r3 pid=%d] stage=%s result=0x%08x\n",
      (int)getpid(), stage, (unsigned)result);
  if (size > 0 && (size_t)size < sizeof(line))
    (void)write(fd, line, (size_t)size);
  close(fd);
}

pid_t bootstrap_hyperdeck_spawn(const uint8_t *elf, size_t size) {
  // Match the reference's direct embedded spawn, including SIGCHLD handling.
  // Bootstrap runtime normally ignores SIGCHLD; ptrace/waitpid needs it reset.
  struct sigaction saved, normal;
  memset(&normal, 0, sizeof(normal));
  normal.sa_handler = SIG_DFL;
  sigemptyset(&normal.sa_mask);
  const uint64_t previous = kernel_get_ucred_authid(-1);
  if (!previous) return -1;
  const int fd = open(HYPERDECK_LOG, O_RDWR | O_CREAT | O_APPEND, 0666);
  if (fd < 0) return -1;
  if (sigaction(SIGCHLD, &normal, &saved) != 0) {
    close(fd);return -1;
  }
  pid_t pid = -1;
  if (kernel_set_ucred_authid(-1, HYPERDECK_PTRACE_AUTHID) == 0) {
    char name[] = "hyperdeck.elf";
    char *argv[] = {name, NULL};
    pid = elfldr_spawn(fd, argv, (uint8_t *)elf, size);
    if (kernel_set_ucred_authid(-1, previous) != 0) {
      LOG_ERROR("HyperDeck spawn context restoration failed");
      pid = -1;
    }
  }
  if (sigaction(SIGCHLD, &saved, NULL) != 0) {
    LOG_ERROR("HyperDeck SIGCHLD restoration failed");
    pid = -1;
  }
  close(fd);
  return pid;
}

static bool hyperdeck_http_probe_expected(const char *expected_body) {
  const int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return false;
  const int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
    close(fd);return false;
  }
  struct sockaddr_in address;
  memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_port = htons(8088);
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
#if defined(__SCE__) || defined(PS5)
  address.sin_len = sizeof(address);
#endif
  if (connect(fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
    struct pollfd pending = {.fd=fd, .events=POLLOUT};
    int error = 0;
    socklen_t error_size = sizeof(error);
    if (errno != EINPROGRESS || poll(&pending, 1, 200) <= 0 ||
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &error_size) != 0 || error) {
      close(fd);return false;
    }
  }
  (void)fcntl(fd, F_SETFL, flags);
  const struct timeval timeout = {.tv_sec=0, .tv_usec=200000};
  (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  (void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
  static const char request[] =
      "GET /api/health HTTP/1.0\r\nHost: 127.0.0.1:8088\r\nConnection: close\r\n\r\n";
  size_t sent = 0;
  while (sent < sizeof(request)-1) {
    const ssize_t n = send(fd, request+sent, sizeof(request)-1-sent, MSG_NOSIGNAL);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) {close(fd);return false;}
    sent += (size_t)n;
  }
  char response[2048];
  size_t used = 0;
  bool valid = false;
  while (used < sizeof(response)-1) {
    const ssize_t n = recv(fd, response+used, sizeof(response)-1-used, 0);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) break;
    used += (size_t)n;
    response[used] = '\0';
    const char *body = strstr(response, "\r\n\r\n");
    if (body && (!strncmp(response,"HTTP/1.0 200 ",13) ||
                 !strncmp(response,"HTTP/1.1 200 ",13)) &&
        !strcmp(body+4,expected_body)) {
      valid = true;break;
    }
  }
  close(fd);
  return valid;
}

bool bootstrap_hyperdeck_http_probe(void) {
  return hyperdeck_http_probe_expected(
      "{\"service\":\"hyperdeck-websrv\",\"protocol\":1}");
}

bool bootstrap_hyperdeck_http_probe_selector(void) {
  return hyperdeck_http_probe_expected(
      "{\"service\":\"hyperdeck-websrv\",\"protocol\":2,"
      "\"mode\":\"5xx-selector\"}");
}
