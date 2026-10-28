#include "filemgr_internal.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#ifdef __SCE__
#include <sys/mount.h>
#endif

#include "json_util.h"
#include "path_util.h"
#include "smb.h"

static unsigned long long
vfs_bytes(fsblkcnt_t blocks, unsigned long block_size) {
  return (unsigned long long)blocks * (unsigned long long)block_size;
}

#ifdef __SCE__
static int
path_is_mounted(const char *path, const struct stat *st) {
  char parent[PATH_MAX];
  struct stat parent_st;

  if(!strcmp(path, "/")) {
    return 1;
  }
  if(path_dirname(path, parent, sizeof(parent)) || stat(parent, &parent_st)) {
    return 0;
  }
  return st->st_dev != parent_st.st_dev;
}
#endif

enum MHD_Result
api_space(struct MHD_Connection *conn) {
#ifdef __SCE__
  static const struct {
    const char *label_key;
    const char *path;
  } mounts[] = {
    {"storageInternal", "/data"},
    {"storageUsb", "/mnt/usb0"},
    {"storageUsb", "/mnt/usb1"},
    {"storageUsb", "/mnt/usb2"},
    {"storageUsb", "/mnt/usb3"},
    {"storageUsb", "/mnt/usb4"},
    {"storageUsb", "/mnt/usb5"},
    {"storageUsb", "/mnt/usb6"},
    {"storageUsb", "/mnt/usb7"},
    {"storageM2", "/mnt/ext1"},
    {"storageExtended", "/mnt/ext0"},
  };
#endif
  char *current = fs_path_value(query_value(conn, "path"));
  strbuf_t b = {0};
  int first = 1;

  strbuf_append(&b, "{\"ok\":true,\"spaces\":[");
#ifdef __SCE__
  for(size_t i = 0; i < sizeof(mounts) / sizeof(mounts[0]); i++) {
    struct statvfs vfs;
    struct stat st;
    unsigned long block_size;
    unsigned long long free_bytes;
    unsigned long long total_bytes;
    int is_current = 0;

    if(stat(mounts[i].path, &st) || !path_is_mounted(mounts[i].path, &st) ||
       statvfs(mounts[i].path, &vfs)) {
      continue;
    }
    block_size = vfs.f_frsize ? vfs.f_frsize : vfs.f_bsize;
    free_bytes = vfs_bytes(vfs.f_bavail, block_size);
    total_bytes = vfs_bytes(vfs.f_blocks, block_size);
    if(current) {
      if(!strcmp(mounts[i].path, "/")) {
        is_current = !strcmp(current, "/");
      } else if(!strncmp(current, mounts[i].path, strlen(mounts[i].path)) &&
                (current[strlen(mounts[i].path)] == 0 ||
                 current[strlen(mounts[i].path)] == '/')) {
        is_current = 1;
      }
    }

    if(!first) {
      strbuf_append(&b, ",");
    }
    first = 0;
    strbuf_append(&b, "{\"label_key\":");
    json_escape(&b, mounts[i].label_key);
    strbuf_append(&b, ",\"path\":");
    json_escape(&b, mounts[i].path);
    strbuf_printf(&b, ",\"free\":%llu,\"total\":%llu,\"current\":%s}",
                  free_bytes, total_bytes, is_current ? "true" : "false");
  }
#else
  const char *paths[] = {"/"};
  const char *labels[] = {"storageRoot"};
  for(size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
    struct statvfs vfs;
    unsigned long block_size;
    unsigned long long free_bytes;
    unsigned long long total_bytes;

    if(!paths[i] || statvfs(paths[i], &vfs)) {
      continue;
    }
    block_size = vfs.f_frsize ? vfs.f_frsize : vfs.f_bsize;
    free_bytes = vfs_bytes(vfs.f_bavail, block_size);
    total_bytes = vfs_bytes(vfs.f_blocks, block_size);
    if(!first) {
      strbuf_append(&b, ",");
    }
    first = 0;
    strbuf_append(&b, "{\"label_key\":");
    json_escape(&b, labels[i]);
    strbuf_append(&b, ",\"path\":");
    json_escape(&b, paths[i]);
    strbuf_printf(&b, ",\"free\":%llu,\"total\":%llu,\"current\":%s}",
                  free_bytes, total_bytes, smb_path(current) ? "false" : "true");
  }
#endif
  smb_append_spaces(&b, current, &first);
  free(current);
  strbuf_append(&b, "]}");
  return send_buffer(conn, MHD_HTTP_OK, b.data, "application/json");
}
