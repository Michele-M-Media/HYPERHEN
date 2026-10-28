/* Copyright (C) 2026 OnionHEN / LightningMods
 *
 * HyperHEN private ELF loader service.
 *
 * The private runtime target listens on 127.0.0.1:9020.  The same source is
 * also built as an opt-in debug service on 0.0.0.0:9025; keeping the two
 * instances separate prevents a toolbox stop action from taking down the
 * loader used by HyperHEN itself.
 */

#include <onion/log.h>
#include <arpa/inet.h>
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <unistd.h>

#include <onion/elfldr.h>
#include <onion/system_tmp.h>
#include <ps5/kernel.h>
#include <ps5/klog.h>

#include "elfldr_remote.h"

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0x20000
#endif

#ifndef SO_NOSIGPIPE
#define SO_NOSIGPIPE 0x1022
#endif

#ifndef EADDRINUSE
#define EADDRINUSE 48
#endif

#define PAYLOAD_MAGIC_ELF      0x464C457F
#define PAYLOAD_MAGIC_URI_FILE 0x656C6966
#define PAYLOAD_MAGIC_PLUGIN   0x676C7068
#define PAYLOAD_MAGIC_ONION    0x6F696E6F

#define PTRACE_AUTHID 0x4800000000010003

#ifdef HYPERHEN_ELFLDR_DEBUG_SERVICE
#define HYPERHEN_ELFLDR_STATE                                                \
  ONION_SYSTEM_TMP_ROOT "/debug_elfldr.state"
#define HYPERHEN_ELFLDR_BUSY                                                 \
  ONION_SYSTEM_TMP_ROOT "/debug_elfldr.busy"
#define HYPERHEN_ELFLDR_PORT 9025
#define HYPERHEN_ELFLDR_ADDRESS INADDR_ANY
#define HYPERHEN_ELFLDR_ADDRESS_TEXT "0.0.0.0"
#define HYPERHEN_ELFLDR_PROCESS_NAME "hyperhen_debug_elfldr"
#define HYPERHEN_ELFLDR_PONG "OK hyperhen_debug_elfldr 9025\n"
#else
#define HYPERHEN_ELFLDR_STATE ONION_SYSTEM_TMP_ELFLDR_STATE
#define HYPERHEN_ELFLDR_BUSY ONION_SYSTEM_TMP_ELFLDR_BUSY
#define HYPERHEN_ELFLDR_PORT ONION_ELFLDR_PORT
#define HYPERHEN_ELFLDR_ADDRESS INADDR_LOOPBACK
#define HYPERHEN_ELFLDR_ADDRESS_TEXT "127.0.0.1"
#define HYPERHEN_ELFLDR_PROCESS_NAME "hyperhen_elfldr.elf"
#define HYPERHEN_ELFLDR_PONG ONION_ELFLDR_PONG
#endif

static void write_state_file(void) {
  mkdir(ONION_SYSTEM_TMP_ROOT, 0777);
  mkdir(ONION_SYSTEM_TMP_PID_ROOT, 0777);
  int fd = open(HYPERHEN_ELFLDR_STATE, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if(fd < 0) {
    return;
  }
  char buf[32];
  int len = snprintf(buf, sizeof(buf), "%d", getpid());
  if(len > 0) {
    (void)write(fd, buf, (size_t)len);
  }
  close(fd);
}

static void set_launch_busy(bool busy) {
  if(!busy) {
    unlink(HYPERHEN_ELFLDR_BUSY);
    return;
  }

  mkdir(ONION_SYSTEM_TMP_ROOT, 0777);
  mkdir(ONION_SYSTEM_TMP_PID_ROOT, 0777);
  int fd = open(HYPERHEN_ELFLDR_BUSY,
                O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if(fd < 0) {
    return;
  }
  char buf[32];
  int len = snprintf(buf, sizeof(buf), "%d", getpid());
  if(len > 0) {
    (void)write(fd, buf, (size_t)len);
  }
  close(fd);
}

static int send_text(int fd, const char *text) {
  if(fd < 0 || !text) {
    return -1;
  }
  size_t len = strlen(text);
  size_t off = 0;
  while(off < len) {
    ssize_t n = send(fd, text + off, len - off, MSG_NOSIGNAL);
    if(n <= 0) {
      return -1;
    }
    off += (size_t)n;
  }
  return 0;
}

static int hexval(char c) {
  if(c >= '0' && c <= '9') {
    return c - '0';
  }
  if(c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if(c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

static char *uri_decode(const char *src, size_t len) {
  char *out = (char *)malloc(len + 1);
  if(!out) {
    return NULL;
  }

  char *dst = out;
  for(size_t i = 0; i < len; i++) {
    if(src[i] == '%' && i + 2 < len) {
      int hi = hexval(src[i + 1]);
      int lo = hexval(src[i + 2]);
      if(hi >= 0 && lo >= 0) {
        *dst++ = (char)((hi << 4) | lo);
        i += 2;
        continue;
      }
    }
    *dst++ = src[i];
  }
  *dst = '\0';
  return out;
}

static char *uri_get_path(const char *uri) {
  if(!uri || strncmp(uri, "file:", 5) != 0) {
    return NULL;
  }

  const char *begin = uri + 5;
  const char *end = begin + strlen(begin);
  for(const char *p = begin; *p; p++) {
    if(*p == '?' || *p == '#') {
      end = p;
      break;
    }
  }

  if(*begin != '/') {
    return NULL;
  }
  return uri_decode(begin, (size_t)(end - begin));
}

static char *uri_get_filename(const char *uri) {
  if(!uri || !uri[0]) {
    return NULL;
  }

  const char *end = uri + strlen(uri);
  for(const char *p = uri; *p; p++) {
    if(*p == '?' || *p == '#') {
      end = p;
      break;
    }
  }

  const char *begin = uri;
  for(const char *p = uri; p < end; p++) {
    if(*p == '/') {
      begin = p + 1;
    }
  }
  if(begin >= end) {
    return NULL;
  }
  return uri_decode(begin, (size_t)(end - begin));
}

static char *uri_get_param(const char *uri, const char *name) {
  const char *p;
  size_t len;

  if(!uri || !name || !(p = strchr(uri, '?'))) {
    return NULL;
  }
  p++;
  len = strlen(name);

  while(*p && *p != '#') {
    if(!strncmp(p, name, len) && p[len] == '=') {
      const char *begin = p + len + 1;
      const char *end = begin + strlen(begin);
      for(const char *q = begin; *q; q++) {
        if(*q == '&' || *q == '#') {
          end = q;
          break;
        }
      }
      return uri_decode(begin, (size_t)(end - begin));
    }
    while(*p && *p != '&' && *p != '#') {
      p++;
    }
    if(*p == '&') {
      p++;
    }
  }

  return NULL;
}

static char *args_decode(const char *s) {
  size_t length = strlen(s);
  char *arg = (char *)malloc(length + 1);
  if(!arg) {
    return NULL;
  }

  size_t off = 0;
  int escape = 0;
  for(size_t i = 0; i < length; i++) {
    if(s[i] == '\\' && !escape) {
      escape = 1;
    } else {
      arg[off++] = s[i];
      escape = 0;
    }
  }
  arg[off] = '\0';
  return arg;
}

static int args_split(const char *args, char **argv, size_t size) {
  if(!argv || size == 0) {
    return 0;
  }
  memset(argv, 0, size * sizeof(char *));
  if(!args || !args[0]) {
    return 0;
  }

  char *buf = strdup(args);
  if(!buf) {
    return 0;
  }

  size_t len = strlen(buf);
  int escape = 0;
  int argc = 0;
  for(size_t i = 0; i < len && (size_t)argc < size; i++) {
    if(escape) {
      escape = 0;
      continue;
    }
    if(buf[i] == '\\') {
      escape = 1;
      continue;
    }
    if(buf[i] == ' ') {
      buf[i] = '\0';
      continue;
    }
    if(buf[i] && (i == 0 || !buf[i - 1])) {
      argv[argc++] = buf + i;
    }
  }

  for(int i = 0; i < argc; i++) {
    argv[i] = args_decode(argv[i]);
  }
  free(buf);
  return argc;
}

static int payload_readuri(int fd, char *uri, size_t size) {
  if(!uri || size == 0) {
    return -1;
  }

  for(size_t i = 0; i + 1 < size; i++) {
    char c;
    int n = (int)read(fd, &c, 1);
    if(n < 0) {
      return -1;
    }
    if(n == 0 || c == '\n') {
      uri[i] = '\0';
      return 0;
    }
    if(c == '\r') {
      i--;
      continue;
    }
    uri[i] = c;
  }

  uri[size - 1] = '\0';
  return -1;
}

static int path_read(const char *path, uint8_t **content, size_t *content_size,
                      bool plugin) {
  struct stat st;
  uint8_t *buf;
  int fd;

  if(!path || !content || !content_size) {
    return -1;
  }
  *content = NULL;
  *content_size = 0;

  fd = open(path, O_RDONLY | O_NOFOLLOW);
  if(fd < 0) {
    return -1;
  }
  if(fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0 ||
     st.st_size > 128 * 1024 * 1024) {
    close(fd);
    return -1;
  }

  buf = (uint8_t *)malloc((size_t)st.st_size);
  if(!buf) {
    close(fd);
    return -1;
  }

  size_t off = 0;
  while(off < (size_t)st.st_size) {
    ssize_t n = read(fd, buf + off, (size_t)st.st_size - off);
    if(n <= 0) {
      free(buf);
      close(fd);
      return -1;
    }
    off += (size_t)n;
  }
  close(fd);

  if(!plugin && elfldr_sanity_check(buf, (size_t)st.st_size)) {
    free(buf);
    errno = ENOEXEC;
    return -1;
  }

  *content = buf;
  *content_size = (size_t)st.st_size;
  return 0;
}

static pid_t payload_spawn(char *filename, char *args, uint8_t *payload,
                           size_t payload_size, bool plugin) {
  char *argv[255 + 2] = {0};
  pid_t pid;

  argv[0] = filename && filename[0] ? filename : "payload.elf";
  args_split(args, argv + 1, 255);
  struct sigaction previous;
  bool changed = false;
  if(plugin) {
    struct sigaction action;
    memset(&action, 0, sizeof(action)); sigemptyset(&action.sa_mask);
    action.sa_handler = SIG_DFL;
    changed = sigaction(SIGCHLD, &action, &previous) == 0;
    if(!changed) {
      for(int i=1;argv[i];++i) free(argv[i]);
      return -1;
    }
  }
  pid = plugin ? elfldr_spawn_plugin(-1, argv, payload, payload_size)
               : elfldr_spawn(-1, argv, payload, payload_size);
  if(changed) (void)sigaction(SIGCHLD, &previous, NULL);

  for(int i = 1; argv[i]; i++) {
    free(argv[i]);
  }
  return pid;
}

static void on_connection(int fd) {
  char uri[1024] = {0};
  char *filename = NULL;
  char *args = NULL;
  uint8_t *buf = NULL;
  size_t len = 0;
  int optval = 1;
  int magic = 0;
  pid_t pid = -1;
  struct timeval timeout;

  (void)setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &optval, sizeof(optval));
  timeout.tv_sec = 10;
  timeout.tv_usec = 0;
  (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

  if(recv(fd, &magic, sizeof(magic), MSG_PEEK | MSG_WAITALL) != sizeof(magic)) {
    send_text(fd, "ERR unknown payload format\n");
    return;
  }

  if(magic == PAYLOAD_MAGIC_ONION) {
    if(payload_readuri(fd, uri, sizeof(uri)) == 0 &&
       strcmp(uri, ONION_ELFLDR_PING) == 0) {
      send_text(fd, HYPERHEN_ELFLDR_PONG);
    } else {
      send_text(fd, "ERR unknown onion command\n");
    }
    return;
  } else if(magic == PAYLOAD_MAGIC_URI_FILE || magic == PAYLOAD_MAGIC_PLUGIN) {
    char *path = NULL;
    if(payload_readuri(fd, uri, sizeof(uri))) {
      send_text(fd, "ERR incomplete URI\n");
      return;
    }
    if(magic == PAYLOAD_MAGIC_PLUGIN) memcpy(uri, "file", 4);
    if(!(path = uri_get_path(uri)) ||
       path_read(path, &buf, &len, magic == PAYLOAD_MAGIC_PLUGIN)) {
      LOG_ERROR("error reading URI payload: %s", strerror(errno));
      send_text(fd, "ERR uri read failed\n");
      free(path);
      return;
    }
    free(path);
  } else if(magic == PAYLOAD_MAGIC_ELF) {
    if(elfldr_read(fd, &buf, &len)) {
      LOG_ERROR("error reading raw ELF payload: %s", strerror(errno));
      send_text(fd, "ERR elf read failed\n");
      return;
    }
  } else {
    send_text(fd, "ERR unsupported payload format\n");
    return;
  }

  filename = uri_get_filename(uri);
  if(!filename) {
    filename = strdup("payload.elf");
  }
  args = uri_get_param(uri, "args");
  if(!args) {
    args = strdup("");
  }

  LOG_DEBUG("spawning %s (%zu bytes)", filename, len);
  /* The runtime supervisor must not mistake a synchronous, long-running spawn
   * for a wedged loader just because ping is queued behind this connection. */
  set_launch_busy(true);
  pid = payload_spawn(filename, args, buf, len, magic == PAYLOAD_MAGIC_PLUGIN);
  if(pid > 1) {
    char out[64];
    snprintf(out, sizeof(out), "OK %d\n", (int)pid);
    send_text(fd, out);
    LOG_DEBUG("spawned pid=%d name=%s", (int)pid, filename);
  } else {
    send_text(fd, "ERR spawn failed\n");
    LOG_ERROR("spawn failed for %s", filename);
  }
  set_launch_busy(false);

  free(filename);
  free(args);
  free(buf);
}

static int serve_elfldr(uint16_t port) {
  struct sockaddr_in srvaddr;
  int srvfd;

  srvfd = socket(AF_INET, SOCK_STREAM, 0);
  if(srvfd < 0) {
    LOG_ERROR("socket failed: %s", strerror(errno));
    return -1;
  }

  if(setsockopt(srvfd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0) {
    LOG_ERROR("setsockopt failed: %s", strerror(errno));
    close(srvfd);
    return -1;
  }

  memset(&srvaddr, 0, sizeof(srvaddr));
  srvaddr.sin_family = AF_INET;
  srvaddr.sin_addr.s_addr = htonl(HYPERHEN_ELFLDR_ADDRESS);
  srvaddr.sin_port = htons(port);

  if(bind(srvfd, (struct sockaddr*)&srvaddr, sizeof(srvaddr)) != 0) {
    int err = errno;
    LOG_ERROR("bind %s:%u failed: %s", HYPERHEN_ELFLDR_ADDRESS_TEXT, port,
              strerror(err));
    close(srvfd);
    return err == EADDRINUSE ? -2 : -1;
  }

  if(listen(srvfd, 8) != 0) {
    LOG_ERROR("listen failed: %s", strerror(errno));
    close(srvfd);
    return -1;
  }

  LOG_DEBUG("serving on %s:%u", HYPERHEN_ELFLDR_ADDRESS_TEXT, port);
  write_state_file();

  while(1) {
    struct pollfd pfd;
    memset(&pfd, 0, sizeof(pfd));
    pfd.fd = srvfd;
    pfd.events = POLLIN;

    const int pr = poll(&pfd, 1, 1000);
    if (pr < 0) {
      if (errno == EINTR)
        continue;
      LOG_ERROR("poll failed: %s", strerror(errno));
      break;
    }
    if (pr == 0)
      continue;
    if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
      LOG_WARN("listen socket dead; re-binding :%u", port);
      close(srvfd);
      return 0;
    }

    struct sockaddr_in cliaddr;
    socklen_t socklen = sizeof(cliaddr);
    int connfd = accept(srvfd, (struct sockaddr*)&cliaddr, &socklen);
    if(connfd < 0) {
      if (errno == EINTR)
        continue;
      LOG_ERROR("accept failed: %s", strerror(errno));
      close(srvfd);
      return 0;
    }
    on_connection(connfd);
    close(connfd);
  }

  close(srvfd);
  unlink(HYPERHEN_ELFLDR_STATE);
  unlink(HYPERHEN_ELFLDR_BUSY);
  return -1;
}

int main(void) {
  signal(SIGCHLD, SIG_IGN);
  signal(SIGPIPE, SIG_IGN);
  syscall(SYS_thr_set_name, -1, HYPERHEN_ELFLDR_PROCESS_NAME);
  unlink(HYPERHEN_ELFLDR_BUSY);

  if(chdir("/") != 0) {
    LOG_ERROR("chdir failed: %s", strerror(errno));
  }
  if(elfldr_raise_privileges(getpid()) != 0) {
    LOG_ERROR("self privilege raise failed");
    return -1;
  }
  if(kernel_set_ucred_authid(getpid(), PTRACE_AUTHID) != 0) {
    LOG_ERROR("ptrace authid raise failed");
    return -1;
  }

  while(1) {
    int rc = serve_elfldr(HYPERHEN_ELFLDR_PORT);
    if(rc == -2) {
      return 0;
    }
    if(rc != 0) {
      sleep(3);
    }
  }

  return 0;
}
