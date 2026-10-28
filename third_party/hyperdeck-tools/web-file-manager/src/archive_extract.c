#include "archive_extract.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <strings.h>

#include "path_util.h"

static const char *const g_archive_suffixes[] = {
  ".7z", ".001", ".zip", ".zipx", ".rar", ".arj", ".bz2",
  ".bzip2", ".tbz", ".tbz2", ".cab", ".gz", ".gzip", ".tgz",
  ".tpz", ".lzh", ".lha", ".tar", ".xz", ".txz", ".z", ".taz",
  ".zst", ".tzst", ".xar", ".xip", ".cpio", ".lzma", ".pmd"
};

static int
suffix_equal(const char *text, const char *suffix) {
  size_t text_len = strlen(text);
  size_t suffix_len = strlen(suffix);

  return text_len >= suffix_len &&
         !strcasecmp(text + text_len - suffix_len, suffix);
}

int
archive_path_supported(const char *path) {
  const char *dot;
  const char *part;
  size_t i;

  if(!path || !(dot = strrchr(path, '.')) || !dot[1]) return 0;
  for(part = path; (part = strchr(part, '.')); part++) {
    if(!strncasecmp(part, ".part", 5)) {
      const char *number = part + 5;
      const char *end = number;
      unsigned long value = 0;
      while(isdigit((unsigned char)*end)) {
        if(value <= 1) value = value * 10 + (unsigned int)(*end - '0');
        end++;
      }
      if(end > number && !strcasecmp(end, ".rar")) return value == 1;
    }
  }
  for(i = 0; i < sizeof(g_archive_suffixes) /
                 sizeof(g_archive_suffixes[0]); i++) {
    if(!strcasecmp(dot, g_archive_suffixes[i])) return 1;
  }
  return 0;
}

static void
remove_suffix(char *name, const char *suffix) {
  if(suffix_equal(name, suffix)) name[strlen(name) - strlen(suffix)] = 0;
}

static void
archive_folder_name(const char *path, char *name, size_t size) {
  static const char *const compound[] = {
    ".tar.gz", ".tar.bz2", ".tar.xz", ".tar.zst", ".tbz", ".tbz2",
    ".tgz", ".tpz", ".txz", ".taz", ".tzst"
  };
  char *part;
  size_t i;

  path_basename_copy(path, name, size);
  part = name;
  while((part = strchr(part, '.'))) {
    if(!strncasecmp(part, ".part", 5)) {
      char *p = part + 5;
      while(isdigit((unsigned char)*p)) p++;
      if(p > part + 5 && !strcasecmp(p, ".rar")) {
        *part = 0;
        break;
      }
    }
    part++;
  }
  remove_suffix(name, ".001");
  for(i = 0; i < sizeof(compound) / sizeof(compound[0]); i++) {
    if(suffix_equal(name, compound[i])) {
      remove_suffix(name, compound[i]);
      return;
    }
  }
  for(i = 0; i < sizeof(g_archive_suffixes) /
                 sizeof(g_archive_suffixes[0]); i++) {
    if(suffix_equal(name, g_archive_suffixes[i])) {
      remove_suffix(name, g_archive_suffixes[i]);
      return;
    }
  }
  {
    size_t len = strlen(name);
    if(len >= 4 && name[len - 4] == '.' &&
       (name[len - 3] == 'r' || name[len - 3] == 'R' ||
        name[len - 3] == 'z' || name[len - 3] == 'Z') &&
       isdigit((unsigned char)name[len - 2]) &&
       isdigit((unsigned char)name[len - 1])) name[len - 4] = 0;
  }
  if(!name[0]) path_basename_copy(path, name, size);
}

int
archive_output_path(const char *archive, const char *base, int separate,
                    char *out, size_t out_size) {
  char name[PATH_MAX];

  if(!archive || !base || base[0] != '/') {
    errno = EINVAL;
    return -1;
  }
  if(!separate) {
    if(strlen(base) >= out_size) {
      errno = ENAMETOOLONG;
      return -1;
    }
    strcpy(out, base);
    return 0;
  }
  archive_folder_name(archive, name, sizeof(name));
  return path_join(out, out_size, base, name);
}
