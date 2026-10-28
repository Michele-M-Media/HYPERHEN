#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

#include "cr_api.h"
#include "cr_config.h"
#include "cr_http.h"
#include "cr_log.h"
#include "cr_notifications.h"
#include "cr_shutdown.h"

#define MAX_REQ_SIZE (4 * 1024 * 1024)

static volatile int g_http_listen_notified = 0;
char g_listen_ip[64] = "127.0.0.1";
int g_http_listen_fd = -1;

#define HTTP_MAX_CONCURRENT 24
static volatile int  g_active_clients = 0;
static volatile long long g_too_many_requests_count = 0;
static pthread_mutex_t g_active_lock = PTHREAD_MUTEX_INITIALIZER;

int  cr_http_active_clients(void)          { return g_active_clients; }
int  cr_http_max_concurrent(void)          { return HTTP_MAX_CONCURRENT; }
long long cr_http_too_many_requests_count(void) { return g_too_many_requests_count; }

typedef struct { int fd; char ip[64]; } http_client_ctx_t;
static void http_handle_client(int fd, const char *client_ip);

static int
socket_send_all(int fd, const void *buf, size_t len) {
  const uint8_t *p = (const uint8_t *)buf;
  size_t sent = 0;
  while (sent < len) {
    ssize_t n = send(fd, p + sent, len - sent, 0);
    if (n > 0) {
      sent += (size_t)n;
      continue;
    }
    if (n < 0 && errno == EINTR) {
      continue;
    }
    return -1;
  }
  return 0;
}

static void *
http_client_thread(void *arg) {
  http_client_ctx_t *ctx = (http_client_ctx_t *)arg;
  http_handle_client(ctx->fd, ctx->ip);
  shutdown(ctx->fd, SHUT_RDWR);
  close(ctx->fd);
  free(ctx);
  pthread_mutex_lock(&g_active_lock);
  g_active_clients--;
  pthread_mutex_unlock(&g_active_lock);
  return NULL;
}

static int
local_ip(char *out, size_t out_size) {
  struct ifaddrs *ifaddr = NULL;
  char ip[INET_ADDRSTRLEN] = "127.0.0.1";
  if (!out || out_size == 0) {
    return -1;
  }
  if (getifaddrs(&ifaddr) == -1) {
    snprintf(out, out_size, "%s", ip);
    return -1;
  }
  for (struct ifaddrs *ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
    if (!ifa->ifa_addr || !ifa->ifa_name || ifa->ifa_addr->sa_family != AF_INET) {
      continue;
    }
    if (!strncmp("lo", ifa->ifa_name, 2)) {
      continue;
    }
    struct sockaddr_in *in = (struct sockaddr_in *)ifa->ifa_addr;
    inet_ntop(AF_INET, &(in->sin_addr), ip, sizeof(ip));
    if (strncmp(ip, "0.", 2) != 0) {
      break;
    }
  }
  freeifaddrs(ifaddr);
  snprintf(out, out_size, "%s", ip);
  return 0;
}

static size_t
parse_content_length_header(const char *headers) {
  if (!headers || !headers[0]) {
    return 0;
  }
  const char *p = strcasestr(headers, "\nContent-Length:");
  if (!p) {
    if (!strncasecmp(headers, "Content-Length:", 15)) {
      p = headers;
    } else {
      return 0;
    }
  } else {
    p++;
  }
  p += 15;
  while (*p == ' ' || *p == '\t') {
    p++;
  }
  unsigned long long v = 0;
  while (*p >= '0' && *p <= '9') {
    v = v * 10ULL + (unsigned long long)(*p - '0');
    p++;
    if (v > (unsigned long long)MAX_REQ_SIZE) {
      return (size_t)MAX_REQ_SIZE + 1;
    }
  }
  return (size_t)v;
}

static int
request_accepts_gzip(const char *headers) {
  if (!headers || !headers[0]) {
    return 0;
  }
  const char *line = strcasestr(headers, "Accept-Encoding:");
  if (!line) {
    return 0;
  }
  const char *eol = strpbrk(line, "\r\n");
  size_t line_len = eol ? (size_t)(eol - line) : strlen(line);
  for (size_t i = 0; i + 4 <= line_len; i++) {
    if (strncasecmp(line + i, "gzip", 4) == 0) {
      return 1;
    }
  }
  return 0;
}

static void
http_send_json(int fd, int status, const char *body) {
  const char *payload = body ? body : "{\"ok\":false}";
  char header[256];
  int n = snprintf(header, sizeof(header),
                   "HTTP/1.1 %d OK\r\n"
                   "Content-Type: application/json\r\n"
                   "Content-Length: %u\r\n"
                   "Connection: close\r\n"
                   "Access-Control-Allow-Origin: *\r\n"
                   "Cache-Control: no-cache\r\n"
                   "\r\n",
                   status, (unsigned int)strlen(payload));
  if (n > 0) {
    (void)socket_send_all(fd, header, (size_t)n);
    (void)socket_send_all(fd, payload, strlen(payload));
  }
}

/* Grows *buf (cap+1 bytes allocated) so cap >= need, capped at MAX_REQ_SIZE. */
static int
http_grow_buf(char **buf, size_t *cap, size_t need) {
  if (need <= *cap) return 0;
  size_t new_cap = *cap;
  while (new_cap < need) {
    new_cap *= 2;
    if (new_cap >= MAX_REQ_SIZE) { new_cap = MAX_REQ_SIZE; break; }
  }
  char *nb = realloc(*buf, new_cap + 1);
  if (!nb) return -1;
  *buf = nb;
  *cap = new_cap;
  return 0;
}

static void
http_handle_client(int fd, const char *client_ip) {
  /* Most requests are a few KB: start small and grow only for large uploads. */
  size_t cap = 65536;
  char *req = malloc(cap + 1);
  if (!req) {
    http_send_json(fd, 500, "{\"ok\":false,\"error\":\"alloc\"}");
    return;
  }
  size_t off = 0;
  size_t body_len = 0;

  struct timeval tv;
  tv.tv_sec = 10;
  tv.tv_usec = 0;
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

  for (;;) {
    if (off >= MAX_REQ_SIZE) {
      break;
    }
    if (off >= cap && http_grow_buf(&req, &cap, off + 1) != 0) {
      http_send_json(fd, 500, "{\"ok\":false,\"error\":\"alloc\"}");
      free(req);
      return;
    }
    int n = recv(fd, req + off, cap - off, 0);
    if (n <= 0) {
      if (n < 0 && errno == EINTR) {
        continue;
      }
      if (off == 0) {
        free(req);
        return;
      }
      break;
    }
    off += (size_t)n;
    req[off] = '\0';
    if (strstr(req, "\r\n\r\n") || strstr(req, "\n\n")) {
      break;
    }
  }
  if (off == 0) {
    free(req);
    return;
  }

  char *headers_end = strstr(req, "\r\n\r\n");
  size_t sep_len = 4;
  if (!headers_end) {
    headers_end = strstr(req, "\n\n");
    sep_len = 2;
  }
  if (!headers_end) {
    http_send_json(fd, 400, "{\"ok\":false,\"error\":\"bad request\"}");
    free(req);
    return;
  }

  size_t header_bytes = (size_t)(headers_end - req);
  req[header_bytes] = '\0';
  size_t already_body = off > (header_bytes + sep_len) ? (off - (header_bytes + sep_len)) : 0;
  size_t content_len = parse_content_length_header(req);
  size_t used = header_bytes + sep_len;
  if (used >= MAX_REQ_SIZE || content_len > MAX_REQ_SIZE - used - 1) {
    http_send_json(fd, 413, "{\"ok\":false,\"error\":\"payload_too_large\"}");
    free(req);
    return;
  }
  /* Headers parsed: grow to the full size now, since pointers taken before a realloc would dangle. */
  if (http_grow_buf(&req, &cap, used + content_len) != 0) {
    http_send_json(fd, 500, "{\"ok\":false,\"error\":\"alloc\"}");
    free(req);
    return;
  }
  while (already_body < content_len) {
    if (off >= MAX_REQ_SIZE) {
      http_send_json(fd, 413, "{\"ok\":false,\"error\":\"payload_too_large\"}");
      free(req);
      return;
    }
    int n = recv(fd, req + off, cap - off, 0);
    if (n <= 0) {
      if (n < 0 && errno == EINTR) {
        continue;
      }
      break;
    }
    off += (size_t)n;
    already_body += (size_t)n;
  }
  if (already_body < content_len) {
    http_send_json(fd, 400, "{\"ok\":false,\"error\":\"incomplete_body\"}");
    free(req);
    return;
  }
  body_len = content_len;
  char *body_ptr = req + header_bytes + sep_len;
  body_ptr[body_len] = '\0';

  char method[8] = {0};
  char target[1024] = {0};
  char version[16] = {0};
  if (sscanf(req, "%7s %1023s %15s", method, target, version) != 3) {
    http_send_json(fd, 400, "{\"ok\":false,\"error\":\"bad request\"}");
    free(req);
    return;
  }
  char *query = strchr(target, '?');
  if (query) {
    *query = '\0';
    query++;
  } else {
    query = "";
  }
  int accepts_gzip = request_accepts_gzip(req);
  http_route(fd, method, target, query, client_ip ? client_ip : "unknown",
             body_ptr ? body_ptr : "", body_len, accepts_gzip);
  free(req);
}

static void
close_fd_safe(int *fd) {
  if (!fd || *fd < 0) {
    return;
  }
  shutdown(*fd, SHUT_RDWR);
  close(*fd);
  *fd = -1;
}

void *
http_server_thread(void *arg) {
  (void)arg;
  int http_port = CHEATRUNNER_HTTP_PORT;
  int bind_fails = 0;
  pthread_mutex_lock(&g_cfg_lock);
  http_port = g_cfg.http_port;
  pthread_mutex_unlock(&g_cfg_lock);
  while (!g_shutdown_requested) {
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
      log_msg("[HTTP] socket() failed");
      notify("CheatRunner HTTP socket failed");
      if (g_shutdown_requested) {
        break;
      }
      sleep(3);
      continue;
    }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR,  &opt, sizeof(opt));
    /* Must be set before bind()/listen(); too late after accept() on ProsperoOS. */
    { int rcvbuf = 4 * 1024 * 1024; setsockopt(listen_fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf)); }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(http_port);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
      int be = errno;
      close(listen_fd);
      bind_fails++;
      /* EADDRINUSE almost always means a previous CheatRunner still holds the port (SO_REUSEADDR cannot
       * reclaim a live listener); warn once, then keep retrying. */
      if (bind_fails <= 5) {
        log_msg("[HTTP] bind() failed on %d: %d", http_port, be);
      }
      if (bind_fails == 5) {
        if (be == EADDRINUSE) {
          log_msg("[HTTP] port %d still in use after retries — is a previous CheatRunner running? Reboot the PS5 or close it.", http_port);
          notify("CheatRunner: port %d busy — a previous instance is still running. Reboot the PS5.", http_port);
        } else {
          notify("CheatRunner HTTP bind failed on %d (errno %d)", http_port, be);
        }
      }
      if (g_shutdown_requested) {
        break;
      }
      sleep(bind_fails < 5 ? 3 : 10);
      continue;
    }
    bind_fails = 0;
    if (listen(listen_fd, 32) != 0) {
      log_msg("[HTTP] listen() failed: %d", errno);
      notify("CheatRunner HTTP listen failed on %d", http_port);
      close(listen_fd);
      if (g_shutdown_requested) {
        break;
      }
      sleep(3);
      continue;
    }
    g_http_listen_fd = listen_fd;

    {
      char ip[64];
      local_ip(ip, sizeof(ip));
      /* local_ip() can transiently return 127.0.0.1 after a socket reopen; keep the last real LAN address for dashboard links. */
      if (strcmp(ip, "127.0.0.1") != 0)
        snprintf(g_listen_ip, sizeof(g_listen_ip), "%s", ip);
      log_msg("Listening on: %s:%d", g_listen_ip, http_port);
      if (!g_http_listen_notified) {
        notify("Listening on: %s:%d", g_listen_ip, http_port);
        g_http_listen_notified = 1;
      }
    }

    while (!g_shutdown_requested) {
      struct sockaddr_in client_addr;
      socklen_t client_len = sizeof(client_addr);
      int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
      if (client_fd < 0) {
        if (g_shutdown_requested) {
          break;
        }
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
          continue;
        }
        /* Non-transient error (e.g. network dropped in sleep mode): break out so the socket is recreated. */
        log_msg("[HTTP] accept() failed: %d — reopening socket", errno);
        break;
      }
      { int nd = 1; setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &nd, sizeof(nd)); }
      /* The ProsperoOS default SNDBUF caps outbound throughput; set it on each accepted socket. */
      { int sndbuf = 4 * 1024 * 1024; setsockopt(client_fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf)); }
      char client_ip[64] = {0};
      inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));

      pthread_mutex_lock(&g_active_lock);
      int over = g_active_clients >= HTTP_MAX_CONCURRENT;
      if (!over) { g_active_clients++; }
      pthread_mutex_unlock(&g_active_lock);

      if (over) {
        pthread_mutex_lock(&g_active_lock);
        g_too_many_requests_count++;
        pthread_mutex_unlock(&g_active_lock);
        http_send_json(client_fd, 503, "{\"ok\":false,\"error\":\"too_many_requests\"}");
        shutdown(client_fd, SHUT_RDWR);
        close(client_fd);
        continue;
      }

      http_client_ctx_t *ctx = malloc(sizeof(*ctx));
      if (!ctx) {
        pthread_mutex_lock(&g_active_lock);
        g_active_clients--;
        pthread_mutex_unlock(&g_active_lock);
        shutdown(client_fd, SHUT_RDWR);
        close(client_fd);
        continue;
      }
      ctx->fd = client_fd;
      snprintf(ctx->ip, sizeof(ctx->ip), "%s", client_ip[0] ? client_ip : "unknown");

      pthread_attr_t tattr;
      pthread_attr_init(&tattr);
      pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_DETACHED);
      pthread_t tid;
      if (pthread_create(&tid, &tattr, http_client_thread, ctx) != 0) {
        pthread_mutex_lock(&g_active_lock);
        g_active_clients--;
        pthread_mutex_unlock(&g_active_lock);
        shutdown(client_fd, SHUT_RDWR);
        close(client_fd);
        free(ctx);
      }
      pthread_attr_destroy(&tattr);
    }

    close_fd_safe(&g_http_listen_fd);
    if (!g_shutdown_requested) {
      sleep(1);
    }
  }
  cr_log("info", "http", "server stopped");
  return NULL;
}

/* SIGCONT means wake from standby; accept() could otherwise block on a stale socket for a while. */
static volatile sig_atomic_t g_net_resume_flag = 0;

static void
net_resume_sighandler(int sig) {
  (void)sig;
  g_net_resume_flag = 1;
}

/* Watches for IP changes and console resume, so the accept() loop recreates its socket early
 * instead of waiting for an error. */
void *
http_net_watchdog_thread(void *arg) {
  (void)arg;
  signal(SIGCONT, net_resume_sighandler);
  char last_ip[64] = "";
  while (!g_shutdown_requested) {
    if (g_net_resume_flag) {
      g_net_resume_flag = 0;
      cr_log("info", "http", "console resumed from standby — forcing listener refresh");
      if (g_http_listen_fd >= 0) {
        shutdown(g_http_listen_fd, SHUT_RDWR);
      }
    }

    char ip[64];
    if (local_ip(ip, sizeof(ip)) == 0 && strcmp(ip, last_ip) != 0) {
      if (last_ip[0]) {
        int port;
        pthread_mutex_lock(&g_cfg_lock);
        port = g_cfg.http_port;
        pthread_mutex_unlock(&g_cfg_lock);
        cr_log("info", "http", "network address changed: %s -> %s", last_ip, ip);
        notify("CheatRunner: address changed\nNew: http://%s:%d", ip, port);
      }
      snprintf(last_ip, sizeof(last_ip), "%s", ip);
    }

    sleep(2);
  }
  return NULL;
}
