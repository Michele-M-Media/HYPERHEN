#include "archive_helper.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#include "vfs.h"

#define WFM_HEADER_SIZE 20U
#define WFM_MAX_PAYLOAD (1024U * 1024U)
#define WFM_MAX_PATH_PAYLOAD (256U * 1024U)
#define WFM_MAX_PASSWORD_PAYLOAD (256U * 1024U)
#define WFM_MAX_RESPONSE (64U * 1024U)
#define WFM_HELPER_ELF "/data/wfm/wfm-7zip-helper.elf"
#define WFM_ELFLDR_PORT 9021
#define WFM_MAX_ELF_SIZE (128LL * 1024LL * 1024LL)
#ifdef __linux__
#define WFM_HELPER_SOCKET "/tmp/wfm-7zip-helper.sock"
#else
#define WFM_HELPER_SOCKET "/system_tmp/wfm-7zip-helper.sock"
#endif

#define WFM_MSG_PING 1U
#define WFM_MSG_PONG 2U
#define WFM_MSG_EXTRACT 10U
#define WFM_MSG_CANCEL 11U
#define WFM_MSG_LIST_TASKS 13U
#define WFM_MSG_ATTACH_TASK 14U
#define WFM_MSG_ACK_TASK 15U
#define WFM_MSG_ACCEPTED 20U
#define WFM_MSG_PROGRESS 21U
#define WFM_MSG_CURRENT_FILE 22U
#define WFM_MSG_PASSWORD_REQUIRED 23U
#define WFM_MSG_DONE 24U
#define WFM_MSG_ERROR 25U
#define WFM_MSG_TASK_SNAPSHOT 26U
#define WFM_MSG_LIST_DONE 27U

typedef struct helper_frame {
  unsigned int type;
  unsigned int flags;
  unsigned long long request_id;
  unsigned int payload_size;
} helper_frame_t;

static unsigned int
get16(const unsigned char *p) {
  return ((unsigned int)p[0] << 8) | p[1];
}

static unsigned int
get32(const unsigned char *p) {
  return ((unsigned int)p[0] << 24) | ((unsigned int)p[1] << 16) |
         ((unsigned int)p[2] << 8) | p[3];
}

static unsigned long long
get64(const unsigned char *p) {
  return ((unsigned long long)get32(p) << 32) | get32(p + 4);
}

static void
put16(unsigned char *p, unsigned int value) {
  p[0] = (unsigned char)(value >> 8);
  p[1] = (unsigned char)value;
}

static void
put32(unsigned char *p, unsigned int value) {
  p[0] = (unsigned char)(value >> 24);
  p[1] = (unsigned char)(value >> 16);
  p[2] = (unsigned char)(value >> 8);
  p[3] = (unsigned char)value;
}

static void
put64(unsigned char *p, unsigned long long value) {
  put32(p, (unsigned int)(value >> 32));
  put32(p + 4, (unsigned int)value);
}

static int
send_all(int fd, const void *data, size_t size) {
  const unsigned char *p = data;
  size_t done = 0;

  while(done < size) {
    ssize_t n = send(fd, p + done, size - done, MSG_NOSIGNAL);
    if(n < 0 && errno == EINTR) continue;
    if(n <= 0) {
      if(!n) errno = EPIPE;
      return -1;
    }
    done += (size_t)n;
  }
  return 0;
}

/* Returns 1 on success, 0 on disconnect, -1 on error, -2 on timeout. */
static int
recv_all(int fd, void *data, size_t size) {
  unsigned char *p = data;
  size_t done = 0;

  while(done < size) {
    ssize_t n = recv(fd, p + done, size - done, 0);
    if(n < 0 && errno == EINTR) continue;
    if(n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return -2;
    if(n < 0) return -1;
    if(!n) return 0;
    done += (size_t)n;
  }
  return 1;
}

static int
send_frame(int fd, unsigned int type, unsigned long long request_id,
           const void *payload, unsigned int payload_size) {
  unsigned char header[WFM_HEADER_SIZE];

  if(payload_size > WFM_MAX_PAYLOAD || (payload_size && !payload)) {
    errno = EINVAL;
    return -1;
  }
  memcpy(header, "W7HP", 4);
  put16(header + 4, type);
  put16(header + 6, 0);
  put32(header + 8, payload_size);
  put64(header + 12, request_id);
  if(send_all(fd, header, sizeof(header))) return -1;
  return payload_size ? send_all(fd, payload, payload_size) : 0;
}

static int
recv_frame(int fd, helper_frame_t *frame, char *payload, size_t capacity) {
  unsigned char header[WFM_HEADER_SIZE];
  int ret = recv_all(fd, header, sizeof(header));

  if(ret <= 0) return ret;
  if(memcmp(header, "W7HP", 4)) {
    errno = EPROTO;
    return -1;
  }
  frame->type = get16(header + 4);
  frame->flags = get16(header + 6);
  frame->payload_size = get32(header + 8);
  frame->request_id = get64(header + 12);
  if(frame->payload_size > WFM_MAX_PAYLOAD || frame->payload_size >= capacity) {
    errno = EMSGSIZE;
    return -1;
  }
  if(frame->payload_size) {
    ret = recv_all(fd, payload, frame->payload_size);
    if(ret <= 0) return ret;
  }
  payload[frame->payload_size] = 0;
  return 1;
}

static int
connect_helper(void) {
  struct sockaddr_un addr;
  struct timeval timeout = {0, 250000};
  const char *path = WFM_HELPER_SOCKET;
  size_t path_len = strlen(path);
  socklen_t addr_len;
  int fd;

  if(path_len >= sizeof(addr.sun_path)) {
    errno = ENAMETOOLONG;
    return -1;
  }
  if((fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) return -1;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  memcpy(addr.sun_path, path, path_len + 1);
#ifndef __linux__
  addr.sun_len = (unsigned char)SUN_LEN(&addr);
  addr_len = addr.sun_len;
#else
  addr_len = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + path_len + 1);
#endif
  if(connect(fd, (struct sockaddr *)&addr, addr_len)) {
    int error = errno;
    close(fd);
    errno = error;
    return -1;
  }
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
  return fd;
}

static int
wait_readable(int fd, int timeout_ms) {
  struct pollfd item;
  int ret;

  memset(&item, 0, sizeof(item));
  item.fd = fd;
  item.events = POLLIN;
  do {
    ret = poll(&item, 1, timeout_ms);
  } while(ret < 0 && errno == EINTR);
  if(ret <= 0) return ret;
  return item.revents & (POLLIN | POLLHUP | POLLERR) ? 1 : -1;
}

static int
ping_helper(int fd) {
  helper_frame_t frame;
  char payload[4097];
  int ret;

  if(send_frame(fd, WFM_MSG_PING, 0, NULL, 0) ||
     wait_readable(fd, 1000) != 1) return -1;
  ret = recv_frame(fd, &frame, payload, sizeof(payload));
  return ret == 1 && !frame.flags && frame.type == WFM_MSG_PONG &&
         frame.request_id == 0 && frame.payload_size == 0 ? 0 : -1;
}

int
archive_helper_send_elf(const char *path) {
#ifdef __linux__
  (void)path;
  errno = ENOTSUP;
  return -1;
#else
  struct sockaddr_in address;
  struct timeval timeout = {10, 0};
  struct stat st;
  unsigned char buffer[16384];
  int file_fd = -1;
  int socket_fd = -1;
  int result = -1;
  int error;
  size_t header_size = 0;
  off_t remaining;
  const char *stage = "open ELF";

  if(!path) {
    errno = EINVAL;
    return -1;
  }
  if((file_fd = open(path, O_RDONLY)) < 0) goto done;
  stage = "validate ELF";
  if(fstat(file_fd, &st)) goto done;
  if(!S_ISREG(st.st_mode) || st.st_size < 4 ||
     st.st_size > WFM_MAX_ELF_SIZE) {
    errno = ENOEXEC;
    goto done;
  }
  while(header_size < 4) {
    ssize_t count = read(file_fd, buffer + header_size, 4 - header_size);
    if(count < 0 && errno == EINTR) continue;
    if(count < 0) goto done;
    if(!count) { errno = ENOEXEC; goto done; }
    header_size += (size_t)count;
  }
  if(memcmp(buffer, "\x7f" "ELF", 4)) {
    errno = ENOEXEC;
    goto done;
  }
  stage = "rewind ELF";
  if(lseek(file_fd, 0, SEEK_SET) < 0) goto done;
  stage = "connect to ELF loader at 127.0.0.1:9021";
  if((socket_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) goto done;
  setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
  memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_port = htons(WFM_ELFLDR_PORT);
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if(connect(socket_fd, (struct sockaddr *)&address, sizeof(address))) goto done;

  remaining = st.st_size;
  while(remaining > 0) {
    stage = "read ELF";
    size_t size = remaining < (off_t)sizeof(buffer) ?
      (size_t)remaining : sizeof(buffer);
    ssize_t count = read(file_fd, buffer, size);
    if(count < 0 && errno == EINTR) continue;
    if(count < 0) goto done;
    if(!count) { errno = EIO; goto done; }
    stage = "send ELF to loader";
    if(send_all(socket_fd, buffer, (size_t)count)) goto done;
    remaining -= count;
  }
  shutdown(socket_fd, SHUT_WR);
  result = 0;

done:
  error = errno;
#if WFM_DEBUG
  if(result) fprintf(stderr, "ELF launch: %s: %s: %s\n", path, stage, strerror(error));
#else
  (void)stage;
#endif
  if(socket_fd >= 0) close(socket_fd);
  if(file_fd >= 0) close(file_fd);
  errno = error;
  return result;
#endif
}

int
archive_helper_autostart(void) {
#ifdef __linux__
  return 0;
#else
  /* Never replace a connected daemon, even if it is temporarily slow. */
  int probe = archive_helper_probe();
  if(probe == 0) {
    puts("7zip helper: already running");
    return 0;
  }
  if(probe == -2) {
    fprintf(stderr, "7zip helper: socket connected but no valid PONG; keeping existing process\n");
    errno = EBUSY;
    return -1;
  }
  printf("7zip helper: launching %s via 127.0.0.1:9021\n", WFM_HELPER_ELF);
  /* Use the actual open/read result; access() checks real rather than effective credentials. */
  if(archive_helper_send_elf(WFM_HELPER_ELF)) return errno == ENOENT ? 0 : -1;
  for(int i = 0; i < 50; i++) {
    usleep(100000);
    probe = archive_helper_probe();
    if(probe == 0) {
      puts("7zip helper: ready");
      return 0;
    }
    if(probe == -2) break;
  }
  fprintf(stderr, "7zip helper: ELF sent, but helper did not become ready\n");
  errno = ETIMEDOUT;
  return -1;
#endif
}

int
archive_helper_probe(void) {
  int fd = connect_helper();
  int ret;

  if(fd < 0) return -1;
  ret = ping_helper(fd);
  close(fd);
  return ret ? -2 : 0;
}

static int
extensions_valid(const unsigned char *payload, size_t payload_size,
                 size_t offset) {
  while(offset < payload_size) {
    unsigned int length;
    if(payload_size - offset < 8) return 0;
    length = get32(payload + offset + 4);
    offset += 8;
    if(length > payload_size - offset) return 0;
    offset += length;
  }
  return 1;
}

static void
set_result(archive_helper_result_t *result, const char *code,
           const char *message) {
  if(!result) return;
  snprintf(result->code, sizeof(result->code), "%s", code ? code : "");
  snprintf(result->message, sizeof(result->message), "%s",
           message ? message : "");
}

static int
dispatch_progress(const unsigned char *payload, size_t size,
                  const archive_helper_callbacks_t *callbacks) {
  unsigned long long total, done;

  if(size < 16 || !extensions_valid(payload, size, 16)) return -1;
  total = get64(payload);
  done = get64(payload + 8);
  if(total && done > total) return -1;
  if(callbacks && callbacks->progress)
    callbacks->progress(callbacks->arg, done, total);
  return 0;
}

static int
dispatch_current_file(const unsigned char *payload, size_t size,
                      const archive_helper_callbacks_t *callbacks) {
  unsigned int length;
  char path[PATH_MAX];

  if(size < 4) return -1;
  length = get32(payload);
  if(length >= sizeof(path) || length > size - 4 ||
     memchr(payload + 4, 0, length) ||
     !extensions_valid(payload, size, 4U + length)) return -1;
  memcpy(path, payload + 4, length);
  path[length] = 0;
  if(callbacks && callbacks->current_file)
    callbacks->current_file(callbacks->arg, path);
  return 0;
}

int
archive_helper_extract(unsigned long job_id, char *const *sources,
                       char *const *destinations, size_t count,
                       const char *password, int overwrite,
                       const archive_helper_callbacks_t *callbacks,
                       archive_helper_result_t *result) {
  helper_frame_t frame;
  /* PS5 worker threads have a small default stack. Extraction events are
     bounded to progress, PATH_MAX current paths, and short errors. */
  char response[8193];
  char *request;
  size_t password_len = password ? strlen(password) : 0;
  size_t request_size = 16 + password_len + 9;
  size_t offset;
  size_t i;
  int cancel_sent = 0;
  int fd;

  if(result) memset(result, 0, sizeof(*result));
  if(!sources || !destinations || !count || count > 1024 ||
     password_len > WFM_MAX_PASSWORD_PAYLOAD) {
    set_result(result, "invalid_request", "invalid archive list");
    return -1;
  }
  for(i = 0; i < count; i++) {
    size_t source_len = strlen(sources[i]);
    size_t destination_len = strlen(destinations[i]);
    if(source_len > WFM_MAX_PATH_PAYLOAD ||
       destination_len > WFM_MAX_PATH_PAYLOAD ||
       request_size > WFM_MAX_PAYLOAD - 8 ||
       source_len > WFM_MAX_PAYLOAD - request_size - 8 ||
       destination_len > WFM_MAX_PAYLOAD - request_size - 8 - source_len) {
      set_result(result, "request_too_large", "archive request is too large");
      return -1;
    }
    request_size += 8 + source_len + destination_len;
  }
  if(request_size > WFM_MAX_PAYLOAD || !(request = malloc(request_size))) {
    set_result(result, "out_of_memory", "could not create helper request");
    return -1;
  }
  put32((unsigned char *)request, 0);
  put32((unsigned char *)request + 4, (unsigned int)password_len);
  put32((unsigned char *)request + 8, (unsigned int)count);
  put32((unsigned char *)request + 12, 0);
  memcpy(request + 16, password ? password : "", password_len);
  offset = 16 + password_len;
  for(i = 0; i < count; i++) {
    size_t source_len = strlen(sources[i]);
    size_t destination_len = strlen(destinations[i]);
    put32((unsigned char *)request + offset, (unsigned int)source_len);
    put32((unsigned char *)request + offset + 4,
          (unsigned int)destination_len);
    offset += 8;
    memcpy(request + offset, sources[i], source_len);
    offset += source_len;
    memcpy(request + offset, destinations[i], destination_len);
    offset += destination_len;
  }
  put32((unsigned char *)request + offset, 1);
  put32((unsigned char *)request + offset + 4, 1);
  request[offset + 8] = overwrite ? 1 : 0;
  offset += 9;

  fd = connect_helper();
  if(fd < 0 || ping_helper(fd) ||
     send_frame(fd, WFM_MSG_EXTRACT, job_id, request,
                (unsigned int)request_size)) {
    if(fd >= 0) close(fd);
    memset(request, 0, request_size);
    free(request);
    set_result(result, "archive_helper_not_running",
               "WFM 7zip helper is not running");
    return -1;
  }
  memset(request, 0, request_size);
  free(request);

  while(1) {
    int ret;
    if(!cancel_sent && callbacks && callbacks->cancel_requested &&
       callbacks->cancel_requested(callbacks->arg)) {
      if(send_frame(fd, WFM_MSG_CANCEL, job_id, NULL, 0)) break;
      cancel_sent = 1;
    }
    int ready = wait_readable(fd, 250);
    if(!ready) continue;
    if(ready < 0) {
      set_result(result, "archive_helper_disconnected",
                 "WFM 7zip helper disconnected");
      close(fd);
      return -1;
    }
    ret = recv_frame(fd, &frame, response, sizeof(response));
    if(ret <= 0 || frame.flags || frame.request_id != job_id) {
      set_result(result, "archive_helper_disconnected",
                 "WFM 7zip helper disconnected");
      close(fd);
      return -1;
    }
    if(frame.type == WFM_MSG_ACCEPTED &&
       extensions_valid((unsigned char *)response, frame.payload_size, 0)) {
      continue;
    }
    if(frame.type == WFM_MSG_PROGRESS) {
      if(dispatch_progress((unsigned char *)response, frame.payload_size,
                           callbacks)) {
        set_result(result, "archive_protocol_error", "invalid helper progress");
        close(fd);
        return -1;
      }
      continue;
    }
    if(frame.type == WFM_MSG_CURRENT_FILE) {
      if(dispatch_current_file((unsigned char *)response, frame.payload_size,
                               callbacks)) {
        set_result(result, "archive_protocol_error", "invalid current path");
        close(fd);
        return -1;
      }
      continue;
    }
    if(frame.type == WFM_MSG_PASSWORD_REQUIRED &&
       extensions_valid((unsigned char *)response, frame.payload_size, 0)) {
      set_result(result, "archive_password_required", sources[0]);
      (void)send_frame(fd, WFM_MSG_ACK_TASK, job_id, NULL, 0);
      close(fd);
      return -1;
    }
    if(frame.type == WFM_MSG_DONE &&
       extensions_valid((unsigned char *)response, frame.payload_size, 0)) {
      (void)send_frame(fd, WFM_MSG_ACK_TASK, job_id, NULL, 0);
      close(fd);
      return 0;
    }
    if(frame.type == WFM_MSG_ERROR) {
      unsigned int code_len;
      unsigned int message_len;
      if(frame.payload_size < 8) {
        set_result(result, "archive_protocol_error", "invalid helper error");
      } else {
        code_len = get32((unsigned char *)response);
        message_len = get32((unsigned char *)response + 4);
        if(code_len > 63 || message_len > 159 ||
           8ULL + code_len + message_len > frame.payload_size ||
           memchr(response + 8, 0, code_len + message_len) ||
           !extensions_valid((unsigned char *)response, frame.payload_size,
                             8U + code_len + message_len)) {
          set_result(result, "archive_protocol_error", "invalid helper error");
        } else {
          char code[64];
          char message[160];
          memcpy(code, response + 8, code_len);
          code[code_len] = 0;
          memcpy(message, response + 8 + code_len, message_len);
          message[message_len] = 0;
          set_result(result, code[0] ? code : "archive_extract_failed",
                     message[0] ? message : "archive extraction failed");
        }
      }
      (void)send_frame(fd, WFM_MSG_ACK_TASK, job_id, NULL, 0);
      close(fd);
      return -1;
    }
    /* New informational event types are optional and safely ignored. */
    continue;
  }

  set_result(result, "archive_helper_disconnected",
             "could not cancel archive helper task");
  close(fd);
  return -1;
}

static void
free_snapshot(archive_helper_snapshot_t *snapshot) {
  if(!snapshot) return;
  free(snapshot->current);
  free(snapshot->error_code);
  free(snapshot->error_message);
  for(size_t i = 0; i < snapshot->count; i++) {
    free(snapshot->sources ? snapshot->sources[i] : NULL);
    free(snapshot->destinations ? snapshot->destinations[i] : NULL);
  }
  free(snapshot->sources);
  free(snapshot->destinations);
  memset(snapshot, 0, sizeof(*snapshot));
}

void
archive_helper_free_snapshots(archive_helper_snapshot_t *snapshots,
                              size_t count) {
  size_t i;
  for(i = 0; i < count; i++) free_snapshot(&snapshots[i]);
  free(snapshots);
}

static char *
copy_field(const unsigned char *data, size_t length) {
  char *text;
  if(length && memchr(data, 0, length)) return NULL;
  if(!(text = malloc(length + 1))) return NULL;
  memcpy(text, data, length);
  text[length] = 0;
  return text;
}

static int
parse_snapshot(const helper_frame_t *frame, const unsigned char *data,
               archive_helper_snapshot_t *snapshot) {
  unsigned int current_len, code_len, message_len;
  size_t offset = 44;
  size_t i;

  memset(snapshot, 0, sizeof(*snapshot));
  if(frame->payload_size < 44) return -1;
  snapshot->job_id = (unsigned long)frame->request_id;
  snapshot->state = get32(data);
  snapshot->count = get32(data + 4);
  snapshot->current_index = get32(data + 8);
  snapshot->completed_count = get32(data + 12);
  snapshot->total = get64(data + 16);
  snapshot->done = get64(data + 24);
  current_len = get32(data + 32);
  code_len = get32(data + 36);
  message_len = get32(data + 40);
  if(!snapshot->count || snapshot->count > 1024 ||
     (unsigned long long)offset + current_len + code_len + message_len >
       frame->payload_size) return -1;
  snapshot->current = copy_field(data + offset, current_len); offset += current_len;
  snapshot->error_code = copy_field(data + offset, code_len); offset += code_len;
  snapshot->error_message = copy_field(data + offset, message_len); offset += message_len;
  snapshot->sources = calloc(snapshot->count, sizeof(*snapshot->sources));
  snapshot->destinations = calloc(snapshot->count, sizeof(*snapshot->destinations));
  if(!snapshot->current || !snapshot->error_code || !snapshot->error_message ||
     !snapshot->sources || !snapshot->destinations) goto fail;
  for(i = 0; i < snapshot->count; i++) {
    unsigned int source_len, destination_len;
    if(frame->payload_size - offset < 8) goto fail;
    source_len = get32(data + offset);
    destination_len = get32(data + offset + 4);
    offset += 8;
    if(!source_len || !destination_len ||
       (unsigned long long)source_len + destination_len >
         frame->payload_size - offset) goto fail;
    snapshot->sources[i] = copy_field(data + offset, source_len); offset += source_len;
    snapshot->destinations[i] = copy_field(data + offset, destination_len); offset += destination_len;
    if(!snapshot->sources[i] || !snapshot->destinations[i]) goto fail;
  }
  return extensions_valid(data, frame->payload_size, offset) ? 0 : -1;
fail:
  free_snapshot(snapshot);
  return -1;
}

int
archive_helper_list_tasks(archive_helper_snapshot_t **out, size_t *out_count) {
  archive_helper_snapshot_t *items = NULL;
  char *payload = malloc(WFM_MAX_RESPONSE + 1);
  size_t count = 0;
  int fd = -1;

  *out = NULL;
  *out_count = 0;
  if(!payload || (fd = connect_helper()) < 0 || ping_helper(fd) ||
     send_frame(fd, WFM_MSG_LIST_TASKS, 0, NULL, 0)) goto fail;
  for(;;) {
    helper_frame_t frame;
    archive_helper_snapshot_t *grown;
    if(wait_readable(fd, 1000) != 1 ||
       recv_frame(fd, &frame, payload, WFM_MAX_RESPONSE + 1) != 1 ||
       frame.flags) goto fail;
    if(frame.type == WFM_MSG_LIST_DONE && frame.request_id == 0) break;
    if(frame.type != WFM_MSG_TASK_SNAPSHOT) goto fail;
    if(!(grown = realloc(items, (count + 1) * sizeof(*items)))) goto fail;
    items = grown;
    if(parse_snapshot(&frame, (unsigned char *)payload, &items[count])) goto fail;
    count++;
  }
  close(fd);
  free(payload);
  *out = items;
  *out_count = count;
  return 0;
fail:
  if(fd >= 0) close(fd);
  free(payload);
  archive_helper_free_snapshots(items, count);
  return -1;
}

int
archive_helper_attach(unsigned long job_id,
                      const archive_helper_callbacks_t *callbacks,
                      archive_helper_result_t *result) {
  int fd = -1;
  int cancel_sent = 0;
  char *payload = malloc(WFM_MAX_RESPONSE + 1);

  if(result) memset(result, 0, sizeof(*result));
  if(!payload || (fd = connect_helper()) < 0 || ping_helper(fd) ||
     send_frame(fd, WFM_MSG_ATTACH_TASK, job_id, NULL, 0)) {
    if(fd >= 0) close(fd);
    free(payload);
    set_result(result, "archive_helper_not_running",
               "WFM 7zip helper is not running");
    return -1;
  }
  for(;;) {
    helper_frame_t frame;
    if(!cancel_sent && callbacks && callbacks->cancel_requested &&
       callbacks->cancel_requested(callbacks->arg)) {
      if(send_frame(fd, WFM_MSG_CANCEL, job_id, NULL, 0)) break;
      cancel_sent = 1;
    }
    int ready = wait_readable(fd, 250);
    if(!ready) continue;
    if(ready < 0 || recv_frame(fd, &frame, payload,
                               WFM_MAX_RESPONSE + 1) != 1 ||
       frame.flags || frame.request_id != job_id) break;
    if(frame.type == WFM_MSG_TASK_SNAPSHOT) {
      archive_helper_snapshot_t snapshot;
      int terminal;
      if(parse_snapshot(&frame, (unsigned char *)payload, &snapshot)) break;
      if(callbacks && callbacks->progress)
        callbacks->progress(callbacks->arg, snapshot.done, snapshot.total);
      if(callbacks && callbacks->current_file && snapshot.current[0])
        callbacks->current_file(callbacks->arg, snapshot.current);
      terminal = snapshot.state != ARCHIVE_HELPER_TASK_RUNNING;
      if(terminal) {
        set_result(result, snapshot.error_code, snapshot.error_message);
        (void)send_frame(fd, WFM_MSG_ACK_TASK, job_id, NULL, 0);
        close(fd);
        if(snapshot.state == ARCHIVE_HELPER_TASK_DONE) {
          free_snapshot(&snapshot);
          free(payload);
          return 0;
        }
        free_snapshot(&snapshot);
        free(payload);
        return -1;
      }
      free_snapshot(&snapshot);
      continue;
    }
    if(frame.type == WFM_MSG_PROGRESS) {
      if(dispatch_progress((unsigned char *)payload, frame.payload_size,
                           callbacks)) break;
      continue;
    }
    if(frame.type == WFM_MSG_CURRENT_FILE) {
      if(dispatch_current_file((unsigned char *)payload, frame.payload_size,
                               callbacks)) break;
      continue;
    }
    if(frame.type == WFM_MSG_DONE) {
      (void)send_frame(fd, WFM_MSG_ACK_TASK, job_id, NULL, 0);
      close(fd);
      free(payload);
      return 0;
    }
    if(frame.type == WFM_MSG_ERROR && frame.payload_size >= 8) {
      unsigned int code_len = get32((unsigned char *)payload);
      unsigned int message_len = get32((unsigned char *)payload + 4);
      if(code_len <= 63 && message_len <= 159 &&
         8ULL + code_len + message_len <= frame.payload_size) {
        char code[64], message[160];
        memcpy(code, payload + 8, code_len); code[code_len] = 0;
        memcpy(message, payload + 8 + code_len, message_len);
        message[message_len] = 0;
        set_result(result, code, message);
      } else set_result(result, "archive_protocol_error", "invalid helper error");
      (void)send_frame(fd, WFM_MSG_ACK_TASK, job_id, NULL, 0);
      close(fd);
      free(payload);
      return -1;
    }
  }
  close(fd);
  free(payload);
  set_result(result, "archive_helper_disconnected",
             "WFM 7zip helper disconnected");
  return -1;
}
