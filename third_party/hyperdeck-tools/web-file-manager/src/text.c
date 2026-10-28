#include "filemgr_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "json_util.h"
#include "path_util.h"
#include "websrv.h"
#include "vfs.h"
#include "transfer.h"

#define TEXT_FILE_MAX_SIZE (1024 * 1024)

typedef enum text_newline {
  TEXT_NEWLINE_LF,
  TEXT_NEWLINE_CRLF,
  TEXT_NEWLINE_CR,
} text_newline_t;

static enum MHD_Result
send_json_version(struct MHD_Connection *conn, unsigned long long version) {
  char *data;

  if(asprintf(&data, "{\"ok\":true,\"version\":\"%016llx\"}", version) < 0) {
    return MHD_NO;
  }
  return send_buffer(conn, MHD_HTTP_OK, data, "application/json");
}

static int
text_extension_allowed(const char *path) {
  static const char *extensions[] = {
    ".txt", ".json", ".xml", ".ini", ".cfg", ".conf", ".md",
    ".log", ".lua", ".js", ".css", ".html", ".htm", ".c", ".h",
    ".cpp", ".hpp", ".sh", ".csv", ".yaml", ".yml", ".shn"
  };
  const char *extension = strrchr(path, '.');
  size_t i;

  if(!extension) {
    return 0;
  }
  for(i = 0; i < sizeof(extensions) / sizeof(extensions[0]); i++) {
    if(!strcasecmp(extension, extensions[i])) {
      return 1;
    }
  }
  return 0;
}

static int
valid_utf8(const unsigned char *data, size_t size) {
  size_t i = 0;

  while(i < size) {
    unsigned char c = data[i++];
    size_t trailing;
    unsigned int codepoint;

    if(!c) return 0;
    if(c < 0x80) continue;
    if(c >= 0xc2 && c <= 0xdf) {
      trailing = 1;
      codepoint = c & 0x1f;
    } else if(c >= 0xe0 && c <= 0xef) {
      trailing = 2;
      codepoint = c & 0x0f;
    } else if(c >= 0xf0 && c <= 0xf4) {
      trailing = 3;
      codepoint = c & 0x07;
    } else {
      return 0;
    }
    if(trailing > size - i) return 0;
    while(trailing--) {
      unsigned char next = data[i++];
      if((next & 0xc0) != 0x80) return 0;
      codepoint = (codepoint << 6) | (next & 0x3f);
    }
    if((codepoint >= 0xd800 && codepoint <= 0xdfff) || codepoint > 0x10ffff ||
       (codepoint < 0x800 && c >= 0xe0) ||
       (codepoint < 0x10000 && c >= 0xf0)) {
      return 0;
    }
  }
  return 1;
}

static unsigned long long
text_version(const unsigned char *data, size_t size) {
  unsigned long long hash = 1469598103934665603ULL;
  size_t i;

  for(i = 0; i < size; i++) {
    hash ^= data[i];
    hash *= 1099511628211ULL;
  }
  return hash;
}

static text_newline_t
detect_text_newline(const unsigned char *data, size_t size) {
  size_t crlf = 0;
  size_t lf = 0;
  size_t cr = 0;
  size_t i;

  for(i = 0; i < size; i++) {
    if(data[i] == '\r') {
      if(i + 1 < size && data[i + 1] == '\n') {
        crlf++;
        i++;
      } else {
        cr++;
      }
    } else if(data[i] == '\n') {
      lf++;
    }
  }
  if(crlf > lf && crlf >= cr) return TEXT_NEWLINE_CRLF;
  if(cr > lf && cr > crlf) return TEXT_NEWLINE_CR;
  return TEXT_NEWLINE_LF;
}

static int
format_text_for_save(const char *body, size_t body_size,
                     const unsigned char *current, size_t current_size,
                     char **output, size_t *output_size) {
  static const unsigned char bom[] = {0xef, 0xbb, 0xbf};
  int keep_bom = current_size >= sizeof(bom) &&
                 !memcmp(current, bom, sizeof(bom));
  text_newline_t newline = detect_text_newline(
    current + (keep_bom ? sizeof(bom) : 0),
    current_size - (keep_bom ? sizeof(bom) : 0));
  const unsigned char *input = (const unsigned char *)(body ? body : "");
  size_t input_size = body_size;
  size_t capacity = body_size * (newline == TEXT_NEWLINE_CRLF ? 2 : 1) +
                    sizeof(bom) + 1;
  char *formatted;
  size_t i;
  size_t len = 0;

  if(input_size >= sizeof(bom) && !memcmp(input, bom, sizeof(bom))) {
    input += sizeof(bom);
    input_size -= sizeof(bom);
  }
  if(!(formatted = malloc(capacity))) {
    errno = ENOMEM;
    return -1;
  }
  if(keep_bom) {
    memcpy(formatted + len, bom, sizeof(bom));
    len += sizeof(bom);
  }
  for(i = 0; i < input_size; i++) {
    unsigned char c = input[i];

    if(c != '\r' && c != '\n') {
      formatted[len++] = (char)c;
      continue;
    }
    if(c == '\r' && i + 1 < input_size && input[i + 1] == '\n') {
      i++;
    }
    if(newline == TEXT_NEWLINE_CRLF) {
      formatted[len++] = '\r';
      formatted[len++] = '\n';
    } else {
      formatted[len++] = newline == TEXT_NEWLINE_CR ? '\r' : '\n';
    }
  }
  if(len > TEXT_FILE_MAX_SIZE) {
    free(formatted);
    errno = EFBIG;
    return -1;
  }
  formatted[len] = 0;
  *output = formatted;
  *output_size = len;
  return 0;
}

static int
read_text_file(const char *path, char **data, size_t *size,
               struct stat *st) {
  int fd;
  size_t read_size = 0;

  *data = NULL;
  *size = 0;
  if(lstat(path, st) || !S_ISREG(st->st_mode)) {
    return -1;
  }
  if(st->st_size < 0 || (unsigned long long)st->st_size > TEXT_FILE_MAX_SIZE) {
    errno = EFBIG;
    return -1;
  }
  if((fd = open(path, O_RDONLY)) < 0) {
    return -1;
  }
  if(!(*data = malloc((size_t)st->st_size + 1))) {
    close(fd);
    errno = ENOMEM;
    return -1;
  }
  while(read_size < (size_t)st->st_size) {
    ssize_t count = read(fd, *data + read_size, (size_t)st->st_size - read_size);
    if(count <= 0) {
      free(*data);
      *data = NULL;
      close(fd);
      if(!count) errno = EIO;
      return -1;
    }
    read_size += (size_t)count;
  }
  close(fd);
  (*data)[read_size] = 0;
  *size = read_size;
  return 0;
}

static enum MHD_Result
send_text_file(struct MHD_Connection *conn, char *data, size_t size,
               unsigned long long version) {
  struct MHD_Response *resp;
  enum MHD_Result ret;
  char version_text[24];

  if(!(resp = MHD_create_response_from_buffer(size, data,
                                               MHD_RESPMEM_MUST_FREE))) {
    free(data);
    return MHD_NO;
  }
  snprintf(version_text, sizeof(version_text), "%016llx", version);
  MHD_add_response_header(resp, MHD_HTTP_HEADER_CONTENT_TYPE,
                          "text/plain; charset=utf-8");
  MHD_add_response_header(resp, "X-Text-Version", version_text);
  ret = websrv_queue_response(conn, MHD_HTTP_OK, resp);
  MHD_destroy_response(resp);
  return ret;
}

enum MHD_Result
api_text(struct MHD_Connection *conn) {
  char *path = fs_path_value(query_value(conn, "path"));
  char *data;
  size_t size;
  struct stat st;
  unsigned long long version;

  if(has_active_task()) {
    free(path);
    return send_json_error(conn, MHD_HTTP_CONFLICT, "another task is running");
  }
  if(!path || !text_extension_allowed(path)) {
    free(path);
    return send_json_error(conn, MHD_HTTP_BAD_REQUEST,
                           "file type is not editable");
  }
  if(read_text_file(path, &data, &size, &st)) {
    int error = errno;
    free(path);
    if(error == EFBIG) {
      return send_json_error(conn, MHD_HTTP_CONTENT_TOO_LARGE,
                             "text file is too large");
    }
    errno = error;
    return send_json_error(conn, MHD_HTTP_NOT_FOUND, "file not found");
  }
  free(path);
  if(!valid_utf8((const unsigned char *)data, size)) {
    free(data);
    return send_json_error(conn, MHD_HTTP_UNSUPPORTED_MEDIA_TYPE,
                           "file is not valid UTF-8");
  }
  version = text_version((const unsigned char *)data, size);
  return send_text_file(conn, data, size, version);
}

enum MHD_Result
api_text_create(struct MHD_Connection *conn) {
  char *path = fs_path_value(query_value(conn, "path"));
  char *name = fs_path_value(query_value(conn, "name"));
  char target[PATH_MAX];
  int fd = -1;
  int ret = -1;
  int error = 0;
  int created = 0;

  if(has_active_task()) {
    free(path); free(name);
    return send_json_error(conn, MHD_HTTP_CONFLICT, "another task is running");
  }
  if(!path || !name || path_join(target, sizeof(target), path, name)) {
    free(path); free(name);
    return send_json_error(conn, MHD_HTTP_BAD_REQUEST, "invalid path");
  }
  if(mode_access(path, W_OK | X_OK)) {
    free(path); free(name);
    return send_json_error(conn, MHD_HTTP_FORBIDDEN,
                           "text file is not writable");
  }
  if((fd = open(target, O_WRONLY | O_CREAT | O_EXCL, 0777)) >= 0) {
    created = 1;
    ret = fchmod_0777(fd);
    if(close(fd) && !ret) ret = -1;
    fd = -1;
  }
  if(ret) {
    error = errno;
    if(fd >= 0) close(fd);
    if(created) unlink(target);
  }
  free(path); free(name);
  if(!ret) {
    return send_json_version(conn,
                             text_version((const unsigned char *)"", 0));
  }
  errno = error;
  return error == EEXIST ?
    send_json_error(conn, MHD_HTTP_CONFLICT, "file already exists") :
    send_json_error(conn, MHD_HTTP_INTERNAL_SERVER_ERROR, NULL);
}

static int
write_text_atomic(const char *path, const char *body, size_t body_size,
                  mode_t mode) {
  struct timespec now;
  char temp[PATH_MAX];
  int fd = -1;
  int ret = -1;
  int n;

  clock_gettime(CLOCK_MONOTONIC, &now);
  n = snprintf(temp, sizeof(temp), "%s.wfm-%ld-%ld.tmp", path,
               (long)getpid(), now.tv_nsec);
  if(n < 0 || (size_t)n >= sizeof(temp)) {
    errno = ENAMETOOLONG;
    return -1;
  }
  if((fd = open(temp, O_WRONLY | O_CREAT | O_EXCL, 0600)) < 0) {
    return -1;
  }
  if(transfer_write_all(NULL, fd, path, body, body_size, NULL)) goto done;
  if(fchmod(fd, mode & 07777) && !ignore_chmod_error(errno)) {
    goto done;
  }
  if(fsync(fd)) {
    goto done;
  }
  if(close(fd)) {
    fd = -1;
    goto done;
  }
  fd = -1;
  if(rename(temp, path)) {
    goto done;
  }
  ret = 0;

done:
  if(fd >= 0) close(fd);
  if(ret) unlink(temp);
  return ret;
}

enum MHD_Result
api_text_save(struct MHD_Connection *conn, const char *body,
              size_t body_size) {
  char *path = fs_path_value(query_value(conn, "path"));
  char *expected = query_value(conn, "version");
  char *current = NULL;
  size_t current_size = 0;
  struct stat st;
  char version_text[24];
  char parent[PATH_MAX];
  char *formatted = NULL;
  size_t formatted_size = 0;
  int ret;

  if(has_active_task()) {
    free(path); free(expected);
    return send_json_error(conn, MHD_HTTP_CONFLICT, "another task is running");
  }
  if(!path || !expected) {
    free(path); free(expected);
    return send_json_error(conn, MHD_HTTP_BAD_REQUEST, "invalid path");
  }
  if(body_size > TEXT_FILE_MAX_SIZE) {
    free(path); free(expected);
    return send_json_error(conn, MHD_HTTP_CONTENT_TOO_LARGE,
                           "text file is too large");
  }
  if(!valid_utf8((const unsigned char *)(body ? body : ""), body_size)) {
    free(path); free(expected);
    return send_json_error(conn, MHD_HTTP_UNSUPPORTED_MEDIA_TYPE,
                           "file is not valid UTF-8");
  }
  if(read_text_file(path, &current, &current_size, &st)) {
    free(path); free(expected);
    return send_json_error(conn, MHD_HTTP_NOT_FOUND, "file not found");
  }
  snprintf(version_text, sizeof(version_text), "%016llx",
           text_version((const unsigned char *)current, current_size));
  if(strcmp(expected, version_text)) {
    free(current);
    free(path); free(expected);
    return send_json_error(conn, MHD_HTTP_CONFLICT,
                           "file changed since it was opened");
  }
  if(path_dirname(path, parent, sizeof(parent)) ||
     mode_access(path, W_OK) || mode_access(parent, W_OK | X_OK)) {
    free(current);
    free(path); free(expected);
    return send_json_error(conn, MHD_HTTP_FORBIDDEN,
                           "text file is not writable");
  }
  if(format_text_for_save(body, body_size, (const unsigned char *)current,
                          current_size, &formatted, &formatted_size)) {
    int error = errno;
    free(current);
    free(path); free(expected);
    errno = error;
    return error == EFBIG ?
      send_json_error(conn, MHD_HTTP_CONTENT_TOO_LARGE,
                      "text file is too large") :
      send_json_error(conn, MHD_HTTP_INTERNAL_SERVER_ERROR, NULL);
  }
  free(current);
  ret = write_text_atomic(path, formatted, formatted_size, st.st_mode);
  free(formatted);
  free(path); free(expected);
  return ret ? send_json_error(conn, MHD_HTTP_INTERNAL_SERVER_ERROR, NULL)
             : send_json_ok(conn);
}
