#include "filemgr.h"

#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "filemgr_internal.h"
#include "mime.h"
#include "path_util.h"
#include "websrv.h"
#include "vfs.h"
#include "pkg_installer.h"
#include "pkg_info.h"
#include "transfer.h"

typedef struct file_stream {
  int fd;
  uint64_t start, size, file_size;
  transfer_reader_t *reader;
  int reader_initialized;
  pkg_install_source_t *source;
} file_stream_t;

static ssize_t
file_read(void *cls, uint64_t pos, char *buf, size_t max) {
  file_stream_t *stream = cls;
  if(pos >= stream->size) return MHD_CONTENT_READER_END_OF_STREAM;
  if(max > stream->size - pos) max = (size_t)(stream->size - pos);
  if(!stream->reader_initialized) {
    stream->reader = transfer_http_reader_start(stream->fd, stream->start + pos, stream->size - pos, stream->file_size, NULL);
    stream->reader_initialized = 1;
  }
  ssize_t len = transfer_read_at(stream->reader, stream->fd, buf, max, stream->start + pos);
  return len <= 0 ? MHD_CONTENT_READER_END_WITH_ERROR : len;
}

static void
file_close(void *cls) {
  file_stream_t *stream = cls;
  transfer_reader_close(stream->reader);
  if(stream->source) pkg_installer_source_release(stream->source);
  else close(stream->fd);
  free(stream);
}

/* One byte range, including open-ended and suffix forms; no signed/overflow values. */
static int
file_range(const char *range, uint64_t size, uint64_t *start, uint64_t *length) {
  char *end;
  uint64_t first, last;
  if(strncmp(range, "bytes=", 6) || !size) return -1;
  range += 6;
  int suffix = *range == '-';
  if(suffix) range++;
  if(*range < '0' || *range > '9') return -1;
  errno = 0;
  first = strtoull(range, &end, 10);
  if(errno) return -1;
  if(suffix) {
    if(*end || !first) return -1;
    *length = first < size ? first : size;
    *start = size - *length;
    return 0;
  }
  if(*end++ != '-' || first >= size) return -1;
  last = size - 1;
  if(*end) {
    if(*end < '0' || *end > '9') return -1;
    errno = 0;
    last = strtoull(end, &end, 10);
    if(errno || *end || last < first) return -1;
    if(last >= size) last = size - 1;
  }
  *start = first;
  *length = last - first + 1;
  return 0;
}

static enum MHD_Result
file_response_open(struct MHD_Connection *conn, const char *path, int fd,
                    pkg_install_source_t *source) {
  struct stat st;
  struct MHD_Response *resp;
  if(fd < 0) {
    if(source) pkg_installer_source_release(source);
    return send_json_error(conn, MHD_HTTP_NOT_FOUND, "file not found");
  }
  if(fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size < 0) {
    if(source) pkg_installer_source_release(source);
    else close(fd);
    return send_json_error(conn, MHD_HTTP_NOT_FOUND, "file not found");
  }
  uint64_t start = 0, length = (uint64_t)st.st_size;
  const char *range = MHD_lookup_connection_value(conn, MHD_HEADER_KIND, MHD_HTTP_HEADER_RANGE);
  unsigned int status = MHD_HTTP_OK;
  char content_range[100];
  if(range && file_range(range, (uint64_t)st.st_size, &start, &length)) {
    if(source) pkg_installer_source_release(source);
    else close(fd);
    resp = MHD_create_response_from_buffer(0, NULL, MHD_RESPMEM_PERSISTENT);
    if(!resp) return MHD_NO;
    snprintf(content_range, sizeof(content_range), "bytes */%llu", (unsigned long long)st.st_size);
    MHD_add_response_header(resp, MHD_HTTP_HEADER_CONTENT_RANGE, content_range);
    MHD_add_response_header(resp, MHD_HTTP_HEADER_ACCEPT_RANGES, "bytes");
    enum MHD_Result ret = websrv_queue_response(conn, MHD_HTTP_RANGE_NOT_SATISFIABLE, resp);
    MHD_destroy_response(resp);
    return ret;
  }
  file_stream_t *stream = malloc(sizeof(*stream));
  if(!stream) {
    if(source) pkg_installer_source_release(source);
    else close(fd);
    return MHD_NO;
  }
  *stream = (file_stream_t){.fd = fd, .start = start, .size = length,
    .file_size = (uint64_t)st.st_size, .source = source};
  size_t buffer_size = transfer_use_pipeline(stream->file_size) ? TRANSFER_HTTP_BUFFER_SIZE : 512 * 1024;
  if(length < buffer_size) buffer_size = length ? (size_t)length : 1;
  resp = MHD_create_response_from_callback(length, buffer_size, file_read, stream, file_close);
  if(!resp) { file_close(stream); return MHD_NO; }
  const char *mime = mime_get_type(path);
  if(mime) MHD_add_response_header(resp, MHD_HTTP_HEADER_CONTENT_TYPE, mime);
  MHD_add_response_header(resp, MHD_HTTP_HEADER_ACCEPT_RANGES, "bytes");
  if(range) {
    status = MHD_HTTP_PARTIAL_CONTENT;
    snprintf(content_range, sizeof(content_range), "bytes %llu-%llu/%llu",
             (unsigned long long)start, (unsigned long long)(start + length - 1),
             (unsigned long long)st.st_size);
    MHD_add_response_header(resp, MHD_HTTP_HEADER_CONTENT_RANGE, content_range);
  }
  enum MHD_Result ret = websrv_queue_response(conn, status, resp);
  MHD_destroy_response(resp);
  return ret;
}

enum MHD_Result
filemgr_fs_request(struct MHD_Connection *conn) {
  if(has_active_task()) return send_json_error(conn, MHD_HTTP_CONFLICT, "another task is running");
  char *path = fs_path_value(query_value(conn, "path"));
  enum MHD_Result ret = path ? file_response_open(conn, path, open(path, O_RDONLY), NULL) :
    send_json_error(conn, MHD_HTTP_NOT_FOUND, "file not found");
  free(path);
  return ret;
}

enum MHD_Result
filemgr_pkg_source_request(struct MHD_Connection *conn, const char *url, const char *method) {
  const union MHD_ConnectionInfo *info = MHD_get_connection_info(conn, MHD_CONNECTION_INFO_CLIENT_ADDRESS);
  const struct sockaddr_in *peer = info ? (const struct sockaddr_in *)info->client_addr : NULL;
  if(!peer || peer->sin_family != AF_INET || ntohl(peer->sin_addr.s_addr) != INADDR_LOOPBACK)
    return send_json_error(conn, MHD_HTTP_FORBIDDEN, "package source is only available locally");
  if(strcmp(method, MHD_HTTP_METHOD_GET) && strcmp(method, MHD_HTTP_METHOD_HEAD))
    return send_json_error(conn, MHD_HTTP_METHOD_NOT_ALLOWED, "invalid method");
  const char *name = url + strlen("/pkg-source/");
  pkg_install_source_t *source = pkg_installer_source_acquire(name);
  if(source) {
    const char *extension = strrchr(name, '.');
    int fd = pkg_installer_source_fd(source);
    if(!strcmp(extension, ".png")) {
      enum MHD_Result ret = pkg_icon_response_fd(conn, fd);
      pkg_installer_source_release(source);
      return ret;
    }
    return file_response_open(conn, name, fd, source);
  }
  return send_json_error(conn, MHD_HTTP_NOT_FOUND, "file not found");
}
