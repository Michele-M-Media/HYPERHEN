#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include "cr_paths.h"

static volatile uint32_t g_write_tmp_seq = 0;

void
str_trim(char *s) {
  if (!s || !*s) {
    return;
  }
  char *p = s;
  while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) {
    p++;
  }
  if (p != s) {
    memmove(s, p, strlen(p) + 1);
  }
  size_t n = strlen(s);
  while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) {
    s[n - 1] = '\0';
    n--;
  }
}

uint64_t
now_ms(void) {
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
    return (uint64_t)time(NULL) * 1000ULL;
  }
  return ((uint64_t)ts.tv_sec * 1000ULL) + ((uint64_t)ts.tv_nsec / 1000000ULL);
}

static int
write_file_impl(const char *path, const uint8_t *data, size_t len, int do_sync) {
  char tmp[512];
  int fd = -1;
  int saved;
  uint32_t seq = __sync_fetch_and_add(&g_write_tmp_seq, 1u);
  snprintf(tmp, sizeof(tmp), "%s.%u.tmp", path, (unsigned int)seq);
  fd = open(tmp, O_CREAT | O_TRUNC | O_WRONLY, 0644);
  if (fd < 0) {
    return -1;
  }
  if (len > 0 && write(fd, data, len) != (ssize_t)len) {
    saved = errno;
    close(fd);
    unlink(tmp);
    errno = saved;
    return -1;
  }
  if (do_sync) {
    fsync(fd);
  }
  close(fd);
  if (rename(tmp, path) != 0) {
    saved = errno;
    unlink(tmp);
    errno = saved;
    return -1;
  }
  return 0;
}

int
write_file_atomic(const char *path, const uint8_t *data, size_t len) {
  return write_file_impl(path, data, len, 1);
}

/* Same atomic rename, no fsync: for files that can be downloaded again. */
int
write_file_fast(const char *path, const uint8_t *data, size_t len) {
  return write_file_impl(path, data, len, 0);
}

int
read_file_bytes(const char *path, uint8_t **out, size_t *out_len) {
  struct stat st;
  int fd;
  ssize_t n;
  uint8_t *buf;

  *out = NULL;
  *out_len = 0;

  if (stat(path, &st) != 0 || st.st_size <= 0 || st.st_size > (32 * 1024 * 1024)) {
    return -1;
  }
  fd = open(path, O_RDONLY);
  if (fd < 0) {
    return -1;
  }
  buf = malloc((size_t)st.st_size);
  if (!buf) {
    close(fd);
    return -1;
  }
  n = read(fd, buf, (size_t)st.st_size);
  close(fd);
  if (n != st.st_size) {
    free(buf);
    return -1;
  }
  *out = buf;
  *out_len = (size_t)st.st_size;
  return 0;
}

int
read_file_text(const char *path, char **out) {
  uint8_t *buf = NULL;
  size_t len = 0;
  if (read_file_bytes(path, &buf, &len) != 0) {
    return -1;
  }
  char *txt = malloc(len + 1);
  if (!txt) {
    free(buf);
    return -1;
  }
  memcpy(txt, buf, len);
  txt[len] = '\0';
  free(buf);
  *out = txt;
  return 0;
}

void
ensure_data_dirs(void) {
  mkdir(CHEATRUNNER_DATA_DIR, 0755);
  mkdir(CHEATRUNNER_CHEATS_DIR, 0755);
  mkdir(CHEATRUNNER_CHEATS_JSON_DIR, 0755);
  mkdir(CHEATRUNNER_CHEATS_SHN_DIR, 0755);
  mkdir(CHEATRUNNER_CHEATS_MC4_DIR, 0755);
  mkdir(CHEATRUNNER_PATCHES_DIR,      0755);
  mkdir(CHEATRUNNER_PATCHES_XML_DIR,  0755);
  mkdir(CHEATRUNNER_PATCHES_PS5_DIR,  0755);
  chmod(CHEATRUNNER_PATCHES_DIR,      0777);
  chmod(CHEATRUNNER_PATCHES_XML_DIR,  0777);
  chmod(CHEATRUNNER_PATCHES_PS5_DIR,  0777);
  mkdir(CHEATRUNNER_CACHE_DIR, 0755);
  mkdir(CHEATRUNNER_CACHE_ICON_DIR, 0755);
  mkdir(CHEATRUNNER_CACHE_PIC0_DIR, 0755);
  mkdir(CHEATRUNNER_CACHE_REPO_DIR, 0755);
  mkdir(CHEATRUNNER_CACHE_APPDB_DIR, 0755);
  mkdir(CHEATRUNNER_CACHE_SOURCES_DIR, 0755);
  mkdir(CHEATRUNNER_CACHE_TITLE_NAMES_DIR, 0755);
  chmod(CHEATRUNNER_DATA_DIR, 0777);
  chmod(CHEATRUNNER_CHEATS_DIR, 0777);
  chmod(CHEATRUNNER_CHEATS_JSON_DIR, 0777);
  chmod(CHEATRUNNER_CHEATS_SHN_DIR, 0777);
  chmod(CHEATRUNNER_CHEATS_MC4_DIR, 0777);
  chmod(CHEATRUNNER_CACHE_DIR, 0777);
  chmod(CHEATRUNNER_CACHE_ICON_DIR, 0777);
  chmod(CHEATRUNNER_CACHE_PIC0_DIR, 0777);
  chmod(CHEATRUNNER_CACHE_REPO_DIR, 0777);
  chmod(CHEATRUNNER_CACHE_APPDB_DIR, 0777);
  chmod(CHEATRUNNER_CACHE_SOURCES_DIR, 0777);
  chmod(CHEATRUNNER_CACHE_TITLE_NAMES_DIR, 0777);
}

int
ensure_dir_recursive(const char *path) {
  if (!path || !path[0]) {
    return -1;
  }
  char tmp[512];
  snprintf(tmp, sizeof(tmp), "%s", path);
  size_t n = strlen(tmp);
  if (n == 0 || n >= sizeof(tmp)) {
    return -1;
  }
  for (size_t i = 1; i < n; i++) {
    if (tmp[i] == '/') {
      tmp[i] = '\0';
      if (tmp[0]) {
        mkdir(tmp, 0755);
        chmod(tmp, 0777);
      }
      tmp[i] = '/';
    }
  }
  mkdir(tmp, 0755);
  chmod(tmp, 0777);
  return 0;
}

int
is_safe_filename(const char *name) {
  if (!name || !*name || strstr(name, "..")) {
    return 0;
  }
  for (const char *p = name; *p; p++) {
    if (!isalnum((unsigned char)*p) && *p != '.' && *p != '_' && *p != '-') {
      return 0;
    }
  }
  return 1;
}

int
is_safe_repo_rel_path(const char *path) {
  if (!path || !path[0] || path[0] == '/' || path[0] == '\\' || strstr(path, "..")) {
    return 0;
  }
  for (const char *p = path; *p; p++) {
    unsigned char c = (unsigned char)*p;
    if (isalnum(c) || c == '/' || c == '.' || c == '_' || c == '-') {
      continue;
    }
    return 0;
  }
  return 1;
}

int
ends_with_case(const char *s, const char *suffix) {
  if (!s || !suffix) {
    return 0;
  }
  size_t sn = strlen(s), tn = strlen(suffix);
  if (sn < tn) {
    return 0;
  }
  return strcasecmp(s + sn - tn, suffix) == 0;
}

const char *
path_basename_ptr(const char *path) {
  if (!path) {
    return "";
  }
  const char *slash = strrchr(path, '/');
  return slash ? slash + 1 : path;
}
