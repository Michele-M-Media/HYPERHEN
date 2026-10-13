/* Copyright (C) 2026 OnionHEN / LightningMods */

#include "bootstrap_runtime.h"

#include "bootstrap_notify.h"
#include "elfldr.h"
#include "faulthandler.h"

#include <onion/log.h>
#include <onion/platform.h>

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

enum {
  kLoggerPort = 9088,
  kStdoutFd = 1,
  kStderrFd = 2,
};

static int g_logger_fd = -1;
jmp_buf g_catch_buf;

static void close_logger(void) {
  if (g_logger_fd >= 0) {
    close(g_logger_fd);
    g_logger_fd = -1;
  }
}

void bootstrap_runtime_cleanup(void) {
  close_logger();
  bootstrap_notify("notify.boot.cleaned_up");
  exit(0);
}

bool bootstrap_runtime_prepare(void) {
  signal(SIGCHLD, SIG_IGN);
  fault_handler_init(bootstrap_runtime_cleanup);

  LOG_DEBUG("Jailbreaking the bootstrapper ...");
  if (elfldr_raise_privileges(getpid()) != 0) {
    bootstrap_notify("notify.priv.unable");
    return false;
  }
  LOG_DEBUG("   Success!");
  return true;
}

static int redirect_standard_streams(void) {
  g_logger_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (g_logger_fd < 0) {
    bootstrap_notify("notify.net.socket_create", strerror(errno));
    return -1;
  }

  int value = 1;
  if (setsockopt(g_logger_fd, SOL_SOCKET, SO_REUSEADDR, &value,
                 sizeof(value)) < 0) {
    bootstrap_notify("notify.net.socket_options", strerror(errno));
    close_logger();
    return -1;
  }

  struct sockaddr_in server_addr;
  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(kLoggerPort);
  server_addr.sin_addr.s_addr = 0;

  if (bind(g_logger_fd, (struct sockaddr *)&server_addr,
           sizeof(server_addr)) != 0) {
    bootstrap_notify("notify.net.socket_bind", strerror(errno));
    close_logger();
    return -1;
  }
  if (listen(g_logger_fd, 1) != 0) {
    bootstrap_notify("notify.net.socket_listen", strerror(errno));
    close_logger();
    return -1;
  }

  struct sockaddr client_addr;
  socklen_t addr_len = sizeof(client_addr);
  const int connection = accept(g_logger_fd, &client_addr, &addr_len);
  if (connection < 0) {
    bootstrap_notify("notify.net.socket_accept", strerror(errno));
    close_logger();
    return -1;
  }

  dup2(connection, kStdoutFd);
  dup2(connection, kStderrFd);
  close(connection);
  return 0;
}

void bootstrap_runtime_enable_remote_logging(void) {
  if (!if_exists("/data/I_want_logging_for_hyperhen"))
    return;

  LOG_DEBUG("Redirecting stdout and stderr to logger ...");
  if (redirect_standard_streams() == 0)
    LOG_DEBUG("   Success!");
  else
    LOG_ERROR("   Failed!");
}
