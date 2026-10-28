#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "sm_platform.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>

#include "sm_pkg_server.h"
#include "sm_pkg_stream.h"
#include "sm_pkg_ws.h"
#include "sm_limits.h"
#include "sm_log.h"
#include "sm_time.h"
#include "sm_vfs.h"

#define SM_PKG_SERVER_MAX_CONNECTIONS 16
#define SM_PKG_SERVER_REQUEST_MAX 8192
#define SM_PKG_SERVER_SEND_CHUNK (2u * 1024u * 1024u)
#define SM_PKG_SERVER_SMB_CHUNK (4u * 1024u * 1024u)
#define SM_PKG_SERVER_SOCKET_BUFFER (512 * 1024)
#define SM_PKG_SERVER_IO_TIMEOUT_SECONDS 60
#define SM_PKG_SERVER_COVERAGE_MAX 256

typedef struct {
  uint64_t started_us, reported_us, bytes, read_us, send_us, wait_us;
} sm_pkg_transfer_stats_t;

struct sm_pkg_server {
  pthread_mutex_t mutex;
  pthread_cond_t cond;
  atomic_bool running;
  int listen_fd;
  int pkg_fd;
  sm_pkg_read_fn read;
  void *read_ctx;
  sm_pkg_stream_t *browser;
  int browser_fd;
  int connections[SM_PKG_SERVER_MAX_CONNECTIONS];
  atomic_int worker_count;
  pthread_t listener;
  char directory[MAX_PATH];
  char attempt[64];
  char basename[MAX_PATH];
  char host[INET_ADDRSTRLEN];
  uint16_t port;
  uint64_t total;
  uint64_t last_request_us;
  sm_pkg_transfer_stats_t transfer;
  struct {
    uint64_t start;
    uint64_t end;
  } intervals[SM_PKG_SERVER_COVERAGE_MAX];
  size_t interval_count;
};

typedef struct {
  sm_pkg_server_t *server;
  int fd;
} sm_pkg_server_worker_arg_t;

typedef struct {
  bool get;
  bool head;
  bool keep_alive;
  char target[MAX_PATH];
  char range[128];
  bool has_range;
} sm_pkg_http_request_t;

typedef struct {
  char *data;
  ssize_t size;
  uint64_t started_us, read_us;
} sm_pkg_read_block_t;

/* Owned by one HTTP response; pending includes the block being sent. */
typedef struct {
  sm_pkg_server_t *server;
  pthread_mutex_t mutex;
  pthread_cond_t cond;
  pthread_t thread;
  sm_pkg_read_block_t blocks[2];
  size_t chunk_size;
  uint64_t offset, end;
  unsigned head, pending;
  bool stop, done;
} sm_pkg_read_ahead_t;

static void *read_ahead_worker(void *opaque) {
  sm_pkg_read_ahead_t *state = opaque;
  pthread_mutex_lock(&state->mutex);
  while (!state->stop && state->server->running && state->offset <= state->end) {
    while (state->pending == 2 && !state->stop && state->server->running)
      pthread_cond_wait(&state->cond, &state->mutex);
    if (state->stop || !state->server->running) break;
    sm_pkg_read_block_t *block = &state->blocks[(state->head + state->pending) % 2];
    uint64_t remaining = state->end - state->offset + 1;
    size_t want = remaining < state->chunk_size
                      ? (size_t)remaining : state->chunk_size;
    uint64_t offset = state->offset;
    pthread_mutex_unlock(&state->mutex);
    block->started_us = monotonic_time_us();
    block->size = pread(state->server->pkg_fd, block->data, want, (off_t)offset);
    int error = errno;
    block->read_us = monotonic_time_us() - block->started_us;
    pthread_mutex_lock(&state->mutex);
    if (block->size < 0 && error == EINTR) continue;
    ++state->pending;
    pthread_cond_signal(&state->cond);
    if (block->size <= 0) break;
    state->offset += (uint64_t)block->size;
  }
  state->done = true;
  pthread_cond_broadcast(&state->cond);
  pthread_mutex_unlock(&state->mutex);
  return NULL;
}

static bool start_read_ahead(sm_pkg_read_ahead_t *state) {
  state->blocks[1].data = malloc(state->chunk_size);
  if (!state->blocks[1].data) return false;
  if (pthread_mutex_init(&state->mutex, NULL) != 0) {
    free(state->blocks[1].data);
    return false;
  }
  if (pthread_cond_init(&state->cond, NULL) != 0) {
    pthread_mutex_destroy(&state->mutex);
    free(state->blocks[1].data);
    return false;
  }
  if (pthread_create(&state->thread, NULL, read_ahead_worker, state) == 0) return true;
  pthread_cond_destroy(&state->cond);
  pthread_mutex_destroy(&state->mutex);
  free(state->blocks[1].data);
  return false;
}

static void stop_read_ahead(sm_pkg_read_ahead_t *state) {
  pthread_mutex_lock(&state->mutex);
  state->stop = true;
  pthread_cond_broadcast(&state->cond);
  pthread_mutex_unlock(&state->mutex);
  pthread_join(state->thread, NULL);
  pthread_cond_destroy(&state->cond);
  pthread_mutex_destroy(&state->mutex);
  free(state->blocks[1].data);
}

static bool send_all(int fd, const void *buffer, size_t size) {
  const char *data = buffer;
  while (size > 0) {
    ssize_t sent = send(fd, data, size, MSG_NOSIGNAL);
    if (sent < 0 && errno == EINTR)
      continue;
    if (sent <= 0)
      return false;
    data += sent;
    size -= (size_t)sent;
  }
  return true;
}

static int hex_digit(char value) {
  if (value >= '0' && value <= '9')
    return value - '0';
  if (value >= 'a' && value <= 'f')
    return value - 'a' + 10;
  if (value >= 'A' && value <= 'F')
    return value - 'A' + 10;
  return -1;
}

static bool decode_path(const char *encoded, char *decoded, size_t size) {
  size_t used = 0;
  // ShellCore appends product/serverIpAddr/r parameters to package requests.
  // Split at the raw '?' so an encoded %3F in the filename stays intact.
  for (size_t i = 0; encoded[i] != '\0' && encoded[i] != '?'; i++) {
    unsigned char value = (unsigned char)encoded[i];
    if (value == '%') {
      if (encoded[i + 1] == '\0' || encoded[i + 2] == '\0')
        return false;
      int high = hex_digit(encoded[i + 1]);
      int low = hex_digit(encoded[i + 2]);
      if (high < 0 || low < 0)
        return false;
      value = (unsigned char)((high << 4) | low);
      i += 2;
    }
    if (value == '\0' || used + 1 >= size)
      return false;
    decoded[used++] = (char)value;
  }
  decoded[used] = '\0';
  return true;
}

static bool contains_ci(const char *value, const char *needle) {
  size_t needle_size = strlen(needle);
  if (needle_size == 0)
    return true;
  for (const char *cursor = value; cursor && *cursor; cursor++) {
    if (strncasecmp(cursor, needle, needle_size) == 0)
      return true;
  }
  return false;
}

static bool header_value(const char *request, const char *name, char *value,
                         size_t value_size) {
  size_t name_size = strlen(name);
  const char *line = request;
  value[0] = '\0';
  while (line && *line) {
    const char *end = strstr(line, "\r\n");
    size_t length = end ? (size_t)(end - line) : strlen(line);
    if (length > name_size && strncasecmp(line, name, name_size) == 0 &&
        line[name_size] == ':') {
      const char *start = line + name_size + 1;
      const char *limit = line + length;
      while (start < limit && (*start == ' ' || *start == '\t'))
        start++;
      while (limit > start && (limit[-1] == ' ' || limit[-1] == '\t'))
        limit--;
      size_t copy = (size_t)(limit - start);
      if (copy >= value_size)
        copy = value_size - 1;
      memcpy(value, start, copy);
      value[copy] = '\0';
      return true;
    }
    if (!end)
      break;
    line = end + 2;
  }
  return false;
}

static bool parse_request(const char *request, sm_pkg_http_request_t *out) {
  char method[16];
  char version[16];
  char connection[64];
  if (sscanf(request, "%15s %1023s %15s", method, out->target, version) != 3)
    return false;
  out->get = strcasecmp(method, "GET") == 0;
  out->head = strcasecmp(method, "HEAD") == 0;
  if (!out->get && !out->head)
    return false;
  out->keep_alive = strcasecmp(version, "HTTP/1.0") != 0;
  if (header_value(request, "Connection", connection, sizeof(connection))) {
    if (contains_ci(connection, "close"))
      out->keep_alive = false;
    else if (contains_ci(connection, "keep-alive"))
      out->keep_alive = true;
  }
  out->has_range = header_value(request, "Range", out->range,
                                sizeof(out->range));
  return true;
}

static bool parse_range(const char *value, uint64_t total, uint64_t *start,
                        uint64_t *end) {
  const char *dash;
  char *endptr;
  uint64_t first;
  uint64_t last;
  if (total == 0 || strncasecmp(value, "bytes=", 6) != 0)
    return false;
  dash = strchr(value + 6, '-');
  if (!dash || strchr(dash + 1, ',') != NULL)
    return false;
  if (dash == value + 6) {
    if (!isdigit((unsigned char)dash[1])) return false;
    errno = 0;
    uint64_t suffix = strtoull(dash + 1, &endptr, 10);
    if (errno == ERANGE || endptr == dash + 1 || *endptr != '\0' || suffix == 0)
      return false;
    if (suffix > total)
      suffix = total;
    *start = total - suffix;
    *end = total - 1;
    return true;
  }
  if (!isdigit((unsigned char)value[6])) return false;
  errno = 0;
  first = strtoull(value + 6, &endptr, 10);
  if (errno == ERANGE || endptr != dash || first >= total)
    return false;
  if (dash[1] == '\0') {
    last = total - 1;
  } else {
    if (!isdigit((unsigned char)dash[1])) return false;
    errno = 0;
    last = strtoull(dash + 1, &endptr, 10);
    if (errno == ERANGE || *endptr != '\0' || last < first)
      return false;
    if (last >= total)
      last = total - 1;
  }
  *start = first;
  *end = last;
  return true;
}

static void add_coverage_locked(sm_pkg_server_t *server, uint64_t start,
                                uint64_t end) {
  size_t position = 0;
  if (end < start)
    return;
  while (position < server->interval_count &&
         server->intervals[position].end + 1 < start)
    position++;
  size_t after = position;
  while (after < server->interval_count &&
         server->intervals[after].start <= end + 1) {
    if (server->intervals[after].start < start)
      start = server->intervals[after].start;
    if (server->intervals[after].end > end)
      end = server->intervals[after].end;
    ++after;
  }
  if (after > position) {
    server->intervals[position].start = start;
    server->intervals[position].end = end;
    memmove(&server->intervals[position + 1], &server->intervals[after],
            (server->interval_count - after) * sizeof(server->intervals[0]));
    server->interval_count -= after - position - 1;
    return;
  }
  if (server->interval_count >= SM_PKG_SERVER_COVERAGE_MAX)
    return;
  memmove(&server->intervals[position + 1], &server->intervals[position],
          (server->interval_count - position) * sizeof(server->intervals[0]));
  server->intervals[position].start = start;
  server->intervals[position].end = end;
  server->interval_count++;
}

static uint64_t covered_bytes_locked(const sm_pkg_server_t *server) {
  uint64_t total = 0;
  for (size_t i = 0; i < server->interval_count; i++)
    total += server->intervals[i].end - server->intervals[i].start + 1;
  return total;
}

static bool complete_locked(const sm_pkg_server_t *server) {
  return server->total == 0 ||
         (server->interval_count == 1 && server->intervals[0].start == 0 &&
          server->intervals[0].end == server->total - 1);
}

static int header_404(char *buffer, size_t size, bool keep_alive) {
  return snprintf(buffer, size,
                  "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n"
                  "Connection: %s\r\n\r\n",
                  keep_alive ? "keep-alive" : "close");
}

static int header_416(char *buffer, size_t size, uint64_t total,
                      bool keep_alive) {
  return snprintf(buffer, size,
                  "HTTP/1.1 416 Range Not Satisfiable\r\nContent-Length: 0\r\n"
                  "Content-Range: bytes */%llu\r\nConnection: %s\r\n\r\n",
                  (unsigned long long)total,
                  keep_alive ? "keep-alive" : "close");
}

static int header_content(char *buffer, size_t size, uint64_t start, uint64_t end,
                           uint64_t total, bool ranged, bool keep_alive) {
  char date[40];
  char range[128] = "";
  time_t now = time(NULL);
  struct tm utc;
  if (!gmtime_r(&now, &utc) ||
      !strftime(date, sizeof(date), "%a, %d %b %Y %H:%M:%S GMT", &utc))
    strlcpy(date, "Wed, 01 Jan 2025 00:00:00 GMT", sizeof(date));
  if (ranged)
    snprintf(range, sizeof(range), "Content-Range: bytes %llu-%llu/%llu\r\n",
             (unsigned long long)start, (unsigned long long)end,
             (unsigned long long)total);
  // ShellCore needs cacheable responses and a stable validator when advancing
  // ranges. Each attempt already has its own immutable source URI.
  return snprintf(buffer, size,
                  "HTTP/1.1 %s\r\nDate: %s\r\n"
                  "Last-Modified: Wed, 01 Jan 2025 00:00:00 GMT\r\n"
                  "Content-Type: application/octet-stream\r\n"
                  "Content-Length: %llu\r\n%s"
                  "Accept-Ranges: bytes\r\nConnection: %s\r\n\r\n",
                  ranged ? "206 Partial Content" : "200 OK", date,
                  (unsigned long long)(ranged ? end - start + 1 : total),
                  range, keep_alive ? "keep-alive" : "close");
}

static void log_transfer_stats(const sm_pkg_server_t *server, const sm_pkg_transfer_stats_t *stats,
                                uint64_t now, bool final) {
  const char *source = server->read ? "stream" : "disk";
#ifdef SM_VFS_ENABLED
  if (!server->read && sm_vfs_remote_fd(server->pkg_fd)) source = "smb";
#endif
  // Read/send times can overlap and are summed across workers; elapsed is wall time.
  log_debug("  [PKG] HTTP transfer: source=%s bytes=%llu elapsed_ms=%llu "
            "read_ms=%llu send_ms=%llu wait_ms=%llu final=%u",
            source, (unsigned long long)stats->bytes,
            (unsigned long long)((now - stats->started_us) / 1000),
            (unsigned long long)(stats->read_us / 1000),
            (unsigned long long)(stats->send_us / 1000),
            (unsigned long long)(stats->wait_us / 1000), (unsigned)final);
}

static bool serve_file(sm_pkg_server_t *server, int fd,
                       const sm_pkg_http_request_t *request) {
  char decoded[MAX_PATH];
  char expected[MAX_PATH + 80];
  char sidecar[MAX_PATH];
  char prefix[80];
  char header[1024];
  uint64_t start = 0;
  uint64_t end;
  bool ranged = false;
  bool invalid_range = false;

  if (!decode_path(request->target, decoded, sizeof(decoded))) {
    int size = header_404(header, sizeof(header), false);
    return size > 0 && send_all(fd, header, (size_t)size) && false;
  }
  pthread_mutex_lock(&server->mutex);
  server->last_request_us = monotonic_time_us();
  snprintf(expected, sizeof(expected), "/%s/%s", server->attempt,
           server->basename);
  snprintf(prefix, sizeof(prefix), "/%s/", server->attempt);
  uint64_t total = server->total;
  pthread_mutex_unlock(&server->mutex);

  if (strcmp(decoded, expected) != 0) {
    if (strncmp(decoded, prefix, strlen(prefix)) != 0) {
      int size = header_404(header, sizeof(header), request->keep_alive);
      return size > 0 && send_all(fd, header, (size_t)size) &&
             request->keep_alive;
    }
    const char *name = decoded + strlen(prefix);
    size_t length = strlen(name);
    if (strchr(name, '/') != NULL || length <= 4 ||
        strcasecmp(name + length - 4, ".crc") != 0) {
      int size = header_404(header, sizeof(header), request->keep_alive);
      return size > 0 && send_all(fd, header, (size_t)size) &&
             request->keep_alive;
    }
    int path_length = snprintf(sidecar, sizeof(sidecar), "%s/%s", server->directory, name);
    struct stat st;
    if (path_length < 0 || (size_t)path_length >= sizeof(sidecar) || server->read ||
        lstat(sidecar, &st) != 0 || !S_ISREG(st.st_mode)) {
      int size = header_404(header, sizeof(header), request->keep_alive);
      return size > 0 && send_all(fd, header, (size_t)size) &&
             request->keep_alive;
    }
    int size = snprintf(header, sizeof(header),
                        "HTTP/1.1 200 OK\r\nContent-Length: %llu\r\n"
                        "Connection: close\r\n\r\n",
                        (unsigned long long)st.st_size);
    if (size <= 0 || (size_t)size >= sizeof(header) ||
        !send_all(fd, header, (size_t)size))
      return false;
    if (!request->head) {
      int side_fd = open(sidecar, O_RDONLY | O_NOFOLLOW);
      if (side_fd < 0)
        return false;
      char *buffer = malloc(SM_PKG_SERVER_SEND_CHUNK);
      bool ok = buffer != NULL;
      while (ok) {
        ssize_t count = read(side_fd, buffer, SM_PKG_SERVER_SEND_CHUNK);
        if (count == 0)
          break;
        if (count < 0 || !send_all(fd, buffer, (size_t)count))
          ok = false;
      }
      free(buffer);
      close(side_fd);
      return false;
    }
    return false;
  }

  if (request->has_range) {
    if (parse_range(request->range, total, &start, &end))
      ranged = true;
    else
      invalid_range = true;
  }
  int header_size;
  if (invalid_range) {
    header_size = header_416(header, sizeof(header), total,
                             request->keep_alive);
  } else {
    if (!ranged) end = total == 0 ? 0 : total - 1;
    header_size = header_content(header, sizeof(header), start, end, total,
                                  ranged, request->keep_alive);
  }
  if (header_size <= 0 || (size_t)header_size >= sizeof(header) ||
      !send_all(fd, header, (size_t)header_size))
    return false;
  if (invalid_range || request->head || total == 0)
    return request->keep_alive && !invalid_range;

  size_t chunk_size = SM_PKG_SERVER_SEND_CHUNK;
  if (server->read == sm_pkg_stream_read) chunk_size = SM_PKG_STREAM_CHUNK;
#ifdef SM_VFS_ENABLED
  // Larger SMB batches keep four requests in flight instead of two. Preserve
  // the native-file size and keep two buffers useful even for shorter ranges.
  if (!server->read && sm_vfs_remote_fd(server->pkg_fd) &&
      end - start + 1 >= 2u * SM_PKG_SERVER_SMB_CHUNK)
    chunk_size = SM_PKG_SERVER_SMB_CHUNK;
#endif
  if (end - start + 1 < chunk_size) chunk_size = (size_t)(end - start + 1);
  char *buffer = malloc(chunk_size);
  if (!buffer)
    return false;
  uint64_t offset = start;
  sm_pkg_read_ahead_t ahead = {.server = server, .offset = start, .end = end,
                               .chunk_size = chunk_size,
                               .blocks = {{.data = buffer}}};
  /* Browser sources already prefetch in their range cache. File sources use
   * two bounded buffers, with no extra thread for single-block HTTP ranges. */
  bool pipelined = !server->read && end - start + 1 >= 2u * chunk_size &&
                   start_read_ahead(&ahead);
  bool ok = true;
  while (offset <= end && server->running) {
    uint64_t remaining = end - offset + 1;
    size_t want = remaining > chunk_size
                      ? chunk_size
                      : (size_t)remaining;
    if (server->read == sm_pkg_stream_read) {
      size_t available = SM_PKG_STREAM_CHUNK - (size_t)(offset % SM_PKG_STREAM_CHUNK);
      if (want > available) want = available;
    }
    uint64_t read_started_us = monotonic_time_us();
    uint64_t read_us, wait_us;
    char *data = buffer;
    ssize_t count;
    if (pipelined) {
      pthread_mutex_lock(&ahead.mutex);
      while (!ahead.pending && !ahead.done)
        pthread_cond_wait(&ahead.cond, &ahead.mutex);
      if (!ahead.pending) {
        pthread_mutex_unlock(&ahead.mutex);
        ok = false;
        break;
      }
      wait_us = monotonic_time_us() - read_started_us;
      sm_pkg_read_block_t block = ahead.blocks[ahead.head];
      count = block.size;
      data = block.data;
      read_started_us = block.started_us;
      read_us = block.read_us;
      pthread_mutex_unlock(&ahead.mutex);
    } else {
      count = server->read
        ? ((server->read)(server->read_ctx, offset, buffer, want) == 0 ? (ssize_t)want : -1)
        : pread(server->pkg_fd, buffer, want, (off_t)offset);
      read_us = monotonic_time_us() - read_started_us;
      wait_us = read_us;
    }
    int read_error = count < 0 ? errno : 0;
    uint64_t send_started_us = monotonic_time_us();
    if (!pipelined && !server->read && count < 0 && read_error == EINTR) continue;
    bool sent = count > 0 && send_all(fd, data, (size_t)count);
    uint64_t sent_us = monotonic_time_us();
    sm_pkg_transfer_stats_t snapshot = {0};
    pthread_mutex_lock(&server->mutex);
    sm_pkg_transfer_stats_t *stats = &server->transfer;
    if (!stats->started_us || read_started_us < stats->started_us) stats->started_us = read_started_us;
    if (!stats->reported_us) stats->reported_us = read_started_us;
    stats->read_us += read_us;
    stats->wait_us += wait_us;
    stats->send_us += sent_us - send_started_us;
    if (sent) {
      stats->bytes += (uint64_t)count;
      add_coverage_locked(server, offset, offset + (uint64_t)count - 1);
    }
    // Rate-limit across all ranges and connections, including short requests.
    if (sent_us >= stats->reported_us && sent_us - stats->reported_us >= 30000000ull) {
      stats->reported_us = sent_us;
      snapshot = *stats;
    }
    pthread_mutex_unlock(&server->mutex);
    if (snapshot.started_us) log_transfer_stats(server, &snapshot, sent_us, false);
    if (!sent) { ok = false; break; }
    if (pipelined) {
      pthread_mutex_lock(&ahead.mutex);
      ahead.head = (ahead.head + 1) % 2;
      --ahead.pending;
      pthread_cond_signal(&ahead.cond);
      pthread_mutex_unlock(&ahead.mutex);
    }
    offset += (uint64_t)count;
  }
  if (pipelined) stop_read_ahead(&ahead);
  free(buffer);
  return ok && request->keep_alive;
}

static void retire_worker(sm_pkg_server_t *server) {
  pthread_mutex_lock(&server->mutex);
  atomic_fetch_sub(&server->worker_count, 1);
  pthread_cond_signal(&server->cond);
  pthread_mutex_unlock(&server->mutex);
}

static void *worker(void *opaque) {
  sm_pkg_server_worker_arg_t *argument = opaque;
  sm_pkg_server_t *server = argument->server;
  int fd = argument->fd;
  int slot = -1;
  free(argument);
  pthread_mutex_lock(&server->mutex);
  for (int i = 0; i < SM_PKG_SERVER_MAX_CONNECTIONS; i++) {
    if (server->connections[i] < 0) {
      server->connections[i] = fd;
      slot = i;
      break;
    }
  }
  pthread_mutex_unlock(&server->mutex);
  if (slot < 0) {
    close(fd);
    retire_worker(server);
    return NULL;
  }
  struct timeval timeout = {SM_PKG_SERVER_IO_TIMEOUT_SECONDS, 0};
  (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  (void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

  char request[SM_PKG_SERVER_REQUEST_MAX + 1];
  size_t used = 0;
  request[0] = 0;
  while (server->running && used < SM_PKG_SERVER_REQUEST_MAX) {
    char *end = strstr(request, "\r\n\r\n");
    if (!end) {
      ssize_t count = recv(fd, request + used,
                         SM_PKG_SERVER_REQUEST_MAX - used, 0);
      if (count < 0 && errno == EINTR) continue;
      if (count <= 0) break;
      used += (size_t)count;
      request[used] = '\0';
      end = strstr(request, "\r\n\r\n");
      if (!end) continue;
    }
    size_t request_size = (size_t)(end + 4 - request);
    char next = request[request_size];
    request[request_size] = 0;
    sm_pkg_http_request_t parsed;
    bool keep_alive = false;
    if (parse_request(request, &parsed)) {
      char endpoint[80];
      snprintf(endpoint, sizeof(endpoint), "/%s/ws", server->attempt);
      if (server->browser && parsed.get && !strcmp(parsed.target, endpoint)) {
        char key[80], upgrade[32], connection[80], version[16], accept[32];
        bool valid = header_value(request, "Sec-WebSocket-Key", key, sizeof(key)) &&
            header_value(request, "Upgrade", upgrade, sizeof(upgrade)) && !strcasecmp(upgrade, "websocket") &&
            header_value(request, "Connection", connection, sizeof(connection)) && contains_ci(connection, "upgrade") &&
            header_value(request, "Sec-WebSocket-Version", version, sizeof(version)) && !strcmp(version, "13") &&
            !sm_pkg_ws_accept_key(key, accept);
        pthread_mutex_lock(&server->mutex);
        bool owner = valid && server->browser_fd < 0 && server->running;
        if (owner) server->browser_fd = fd;
        pthread_mutex_unlock(&server->mutex);
        if (owner) {
          request[request_size] = next;
          sm_pkg_ws_serve(fd, key, request + request_size, used - request_size, server->browser, &server->running);
          pthread_mutex_lock(&server->mutex);
          server->browser_fd = -1;
          pthread_mutex_unlock(&server->mutex);
        } else (void)send_all(fd, "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\nConnection: close\r\n\r\n", 64);
        break;
      }
      keep_alive = serve_file(server, fd, &parsed) && parsed.keep_alive;
    }
    else {
      char header[256];
      int header_size = header_404(header, sizeof(header), false);
      if (header_size > 0)
        (void)send_all(fd, header, (size_t)header_size);
    }
    if (!keep_alive)
      break;
    request[request_size] = next;
    memmove(request, request + request_size, used - request_size);
    used -= request_size;
    request[used] = '\0';
  }

  pthread_mutex_lock(&server->mutex);
  if (slot >= 0)
    server->connections[slot] = -1;
  pthread_mutex_unlock(&server->mutex);
  close(fd);
  retire_worker(server);
  return NULL;
}

static void *listener(void *opaque) {
  sm_pkg_server_t *server = opaque;
  while (server->running) {
    struct pollfd incoming = {.fd = server->listen_fd, .events = POLLIN};
    int ready = poll(&incoming, 1, 250);
    if (!server->running) break;
    if (ready < 0) {
      if (errno == EINTR) continue;
      break;
    }
    if (!ready) continue;
    if (!(incoming.revents & POLLIN)) break;
    struct sockaddr_in address;
    socklen_t address_size = sizeof(address);
    int fd = accept(server->listen_fd, (struct sockaddr *)&address,
                    &address_size);
    if (fd < 0) {
      if ((errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) && server->running)
        continue;
      break;
    }
    // BSD may inherit the listener's O_NONBLOCK flag on accepted sockets.
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) != 0) {
      close(fd);
      continue;
    }
    int one = 1;
    (void)setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    int send_buffer = SM_PKG_SERVER_SOCKET_BUFFER;
    (void)setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &send_buffer, sizeof(send_buffer));
    if (atomic_fetch_add(&server->worker_count, 1) + 1 >
        SM_PKG_SERVER_MAX_CONNECTIONS) {
      retire_worker(server);
      close(fd);
      continue;
    }
    sm_pkg_server_worker_arg_t *argument = malloc(sizeof(*argument));
    if (!argument) {
      retire_worker(server);
      close(fd);
      continue;
    }
    argument->server = server;
    argument->fd = fd;
    pthread_t thread;
    if (pthread_create(&thread, NULL, worker, argument) != 0) {
      retire_worker(server);
      free(argument);
      close(fd);
      continue;
    }
    (void)pthread_detach(thread);
  }
  return NULL;
}

static int start_server(sm_pkg_server_t **out, const char *pkg_path, uint64_t size,
    sm_pkg_read_fn read, void *ctx, const char *attempt, uint16_t *port_out,
    sm_pkg_stream_t *browser, const char *bind_address) {
  struct stat st;
  struct sockaddr_in address;
  socklen_t address_size = sizeof(address);
  sm_pkg_server_t *server;
  if (!out || !pkg_path || !attempt || !port_out ||
      (!read && (stat(pkg_path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0)) ||
      (read && (size == 0 || size > INT64_MAX)))
    return EINVAL;
  server = calloc(1, sizeof(*server));
  if (!server)
    return ENOMEM;
  int initialized = pthread_mutex_init(&server->mutex, NULL);
  if (initialized) { free(server); return initialized; }
  initialized = pthread_cond_init(&server->cond, NULL);
  if (initialized) { pthread_mutex_destroy(&server->mutex); free(server); return initialized; }
  atomic_init(&server->running, false);
  atomic_init(&server->worker_count, 0);
  server->listen_fd = -1;
  server->pkg_fd = -1;
  server->read = read;
  server->read_ctx = ctx;
  server->browser = browser;
  server->browser_fd = -1;
  if (read) server->total = size;
  else {
    server->pkg_fd = open(pkg_path, O_RDONLY | O_NOFOLLOW);
    if (server->pkg_fd < 0) {
      pthread_cond_destroy(&server->cond);
      pthread_mutex_destroy(&server->mutex);
      free(server);
      return errno;
    }
    if (fstat(server->pkg_fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0) {
      if (server->pkg_fd >= 0) close(server->pkg_fd);
      pthread_cond_destroy(&server->cond);
      pthread_mutex_destroy(&server->mutex);
      free(server);
      return EINVAL;
    }
    server->total = (uint64_t)st.st_size;
  }
  (void)strlcpy(server->attempt, attempt, sizeof(server->attempt));
  (void)strlcpy(server->basename, strrchr(pkg_path, '/')
                                  ? strrchr(pkg_path, '/') + 1
                                  : pkg_path,
                sizeof(server->basename));
  (void)strlcpy(server->directory, pkg_path, sizeof(server->directory));
  char *slash = strrchr(server->directory, '/');
  if (slash)
    *slash = '\0';
  else
    (void)strlcpy(server->directory, ".", sizeof(server->directory));
  for (int i = 0; i < SM_PKG_SERVER_MAX_CONNECTIONS; i++)
    server->connections[i] = -1;
  server->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server->listen_fd < 0) {
    int error = errno;
    if (server->pkg_fd >= 0) close(server->pkg_fd);
    pthread_cond_destroy(&server->cond);
    pthread_mutex_destroy(&server->mutex);
    free(server);
    return error;
  }
  int one = 1;
  (void)setsockopt(server->listen_fd, SOL_SOCKET, SO_REUSEADDR, &one,
                   sizeof(one));
  if (browser) {
    // Set before listen so uploads inherit the receive window during the
    // handshake, rather than growing it only after the WebSocket upgrade.
    int receive_buffer = SM_PKG_SERVER_SOCKET_BUFFER;
    (void)setsockopt(server->listen_fd, SOL_SOCKET, SO_RCVBUF,
                     &receive_buffer, sizeof(receive_buffer));
  }
  memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  bool valid_bind = !bind_address || inet_pton(AF_INET, bind_address, &address.sin_addr) == 1;
  address.sin_port = 0;
  int flags = fcntl(server->listen_fd, F_GETFL, 0);
  if (!valid_bind || flags < 0 || fcntl(server->listen_fd, F_SETFL, flags | O_NONBLOCK) != 0 ||
      bind(server->listen_fd, (struct sockaddr *)&address, sizeof(address)) != 0 ||
      listen(server->listen_fd, SM_PKG_SERVER_MAX_CONNECTIONS) != 0 ||
      getsockname(server->listen_fd, (struct sockaddr *)&address,
                  &address_size) != 0) {
    int error = valid_bind ? errno : EINVAL;
    close(server->listen_fd);
    if (server->pkg_fd >= 0) close(server->pkg_fd);
    pthread_cond_destroy(&server->cond);
    pthread_mutex_destroy(&server->mutex);
    free(server);
    return error;
  }
  server->port = ntohs(address.sin_port);
  struct in_addr host = address.sin_addr;
  if (host.s_addr == htonl(INADDR_ANY)) host.s_addr = htonl(INADDR_LOOPBACK);
  (void)inet_ntop(AF_INET, &host, server->host, sizeof(server->host));
  server->running = true;
  server->last_request_us = monotonic_time_us();
  int thread_result = pthread_create(&server->listener, NULL, listener, server);
  if (thread_result != 0) {
    close(server->listen_fd);
    if (server->pkg_fd >= 0) close(server->pkg_fd);
    pthread_cond_destroy(&server->cond);
    pthread_mutex_destroy(&server->mutex);
    free(server);
    return thread_result;
  }
  *out = server;
  *port_out = server->port;
  return 0;
}

int sm_pkg_server_start(sm_pkg_server_t **out, const char *path,
                        const char *attempt, uint16_t *port_out) {
  return start_server(out, path, 0, NULL, NULL, attempt, port_out, NULL, NULL);
}
int sm_pkg_server_start_reader(sm_pkg_server_t **out, const char *name, uint64_t size,
    sm_pkg_read_fn read, void *ctx, const char *attempt, uint16_t *port_out) {
  if (!read) return EINVAL;
  return start_server(out, name, size, read, ctx, attempt, port_out, NULL, NULL);
}

int sm_pkg_server_start_browser(sm_pkg_server_t **out, const char *name, uint64_t size,
    sm_pkg_stream_t *stream, const char *bind_address, const char *attempt, uint16_t *port_out) {
  if (!stream) return EINVAL;
  return start_server(out, name, size, sm_pkg_stream_read, stream, attempt, port_out, stream, bind_address);
}

const char *sm_pkg_server_host(sm_pkg_server_t *server) {
  return server->host;
}

uint64_t sm_pkg_server_bytes_served(sm_pkg_server_t *server) {
  if (!server)
    return 0;
  pthread_mutex_lock(&server->mutex);
  uint64_t result = covered_bytes_locked(server);
  pthread_mutex_unlock(&server->mutex);
  return result;
}

bool sm_pkg_server_source_matches(sm_pkg_server_t *server, const sm_pkg_info_t *info) {
  if (!server || server->read || !info) return false;
  struct stat st;
  return fstat(server->pkg_fd, &st) == 0 && sm_pkg_source_matches(info, &st);
}

uint64_t sm_pkg_server_total(sm_pkg_server_t *server) {
  if (!server)
    return 0;
  pthread_mutex_lock(&server->mutex);
  uint64_t result = server->total;
  pthread_mutex_unlock(&server->mutex);
  return result;
}

bool sm_pkg_server_complete(sm_pkg_server_t *server) {
  if (!server)
    return false;
  pthread_mutex_lock(&server->mutex);
  bool result = complete_locked(server);
  pthread_mutex_unlock(&server->mutex);
  return result;
}

uint64_t sm_pkg_server_last_request_us(sm_pkg_server_t *server) {
  if (!server)
    return 0;
  pthread_mutex_lock(&server->mutex);
  uint64_t result = server->last_request_us;
  pthread_mutex_unlock(&server->mutex);
  return result;
}

void sm_pkg_server_stop(sm_pkg_server_t *server) {
  int listen_fd;
  if (!server)
    return;
  if (server->browser) sm_pkg_stream_stop(server->browser);

  pthread_mutex_lock(&server->mutex);
  server->running = 0;
  listen_fd = server->listen_fd;
  pthread_mutex_unlock(&server->mutex);

  if (listen_fd >= 0) {
    (void)shutdown(listen_fd, SHUT_RDWR);
  }
  // Join the listener before taking the connection snapshot. This prevents
  // a just-accepted worker from escaping the shutdown socket sweep.
  (void)pthread_join(server->listener, NULL);
  if (listen_fd >= 0)
    close(listen_fd);

  pthread_mutex_lock(&server->mutex);
  for (int i = 0; i < SM_PKG_SERVER_MAX_CONNECTIONS; i++) {
    if (server->connections[i] >= 0)
      (void)shutdown(server->connections[i], SHUT_RDWR);
  }
  while (atomic_load(&server->worker_count) != 0)
    pthread_cond_wait(&server->cond, &server->mutex);
  pthread_mutex_unlock(&server->mutex);
  if (server->transfer.bytes >= 16u * 1024u * 1024u)
    log_transfer_stats(server, &server->transfer, monotonic_time_us(), true);
  if (server->pkg_fd >= 0) close(server->pkg_fd);
  pthread_cond_destroy(&server->cond);
  pthread_mutex_destroy(&server->mutex);
  free(server);
}
