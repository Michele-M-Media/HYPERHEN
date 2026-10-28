#include "app_installer.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>

#include <ps5/kernel.h>

#include "asset.h"
#include "pkg_installer.h"

#define INCASSET(name, file)                    \
  __asm__(".section .rodata\n"                  \
          ".global " #name "\n"                 \
          ".global " #name "_end\n"             \
          ".global " #name "_size\n"            \
          ".align 16\n"                         \
          #name ":\n"                           \
          ".incbin \"" file "\"\n"              \
          #name "_end:\n"                       \
          #name "_size:\n"                      \
          ".quad " #name "_end - " #name "\n"   \
          ".previous\n");                       \
  extern const uint8_t name[];                   \
  extern const size_t name##_size;

INCASSET(param_json, "assets/param.json");
INCASSET(icon0_png, "assets/icon0.png");

int sceAppInstUtilAppInstallAll(void *);

static int
install_file(const char *path, const uint8_t *data, size_t size) {
  struct stat st;
  FILE *f;

  if(!stat(path, &st)) {
    return 0;
  }
  if(errno != ENOENT) {
    return -1;
  }
  if(!(f = fopen(path, "wb"))) {
    return -1;
  }
  if(fwrite(data, size, 1, f) != 1) {
    fclose(f);
    return -1;
  }
  fclose(f);
  return 0;
}

static int
install_app(const char *title_id, const char *dir) {
  int (*sceAppInstUtilAppInstallTitleDir)(const char *, const char *, void *) = 0;
  const char *nid = "Wudg3Xe3heE";
  uint32_t handle;

  if(!kernel_dynlib_handle(-1, "libSceAppInstUtil.sprx", &handle)) {
    sceAppInstUtilAppInstallTitleDir =
      (void *)kernel_dynlib_resolve(-1, handle, nid);
  }

  if(sceAppInstUtilAppInstallTitleDir) {
    return sceAppInstUtilAppInstallTitleDir(title_id, dir, 0);
  }

  return sceAppInstUtilAppInstallAll(0);
}

/* Preserve existing names/settings; only change our loopback launcher URL. */
static int
update_launcher_port(const char *path, unsigned short port) {
  struct stat st;
  FILE *file = NULL;
  char *data = NULL;
  char temp[512];
  char uri[64];
  char *value, *end;
  int result = -1;

  if(stat(path, &st)) return -1;
  if(st.st_size <= 0 || st.st_size > 65536) { errno = EINVAL; return -1; }
  data = malloc((size_t)st.st_size + 1);
  if(!data || !(file = fopen(path, "rb"))) goto done;
  if(fread(data, 1, (size_t)st.st_size, file) != (size_t)st.st_size) {
    errno = EIO;
    goto done;
  }
  fclose(file);
  file = NULL;
  data[st.st_size] = 0;
  errno = EINVAL;
  if(!(value = strstr(data, "\"deeplinkUri\""))) goto done;
  value += strlen("\"deeplinkUri\"");
  while(isspace((unsigned char)*value)) value++;
  if(*value++ != ':') goto done;
  while(isspace((unsigned char)*value)) value++;
  if(*value++ != '"' || strncmp(value, "http://127.0.0.1:", 17)) goto done;
  value += 17;
  end = value;
  while(isdigit((unsigned char)*end)) end++;
  if(end == value || (*end != '/' && *end != '"')) goto done;
  snprintf(uri, sizeof(uri), "%u", port);
  if((size_t)(end - value) == strlen(uri) && !memcmp(value, uri, strlen(uri))) {
    result = 0;
    goto done;
  }
  snprintf(temp, sizeof(temp), "%s.tmp", path);
  if(!(file = fopen(temp, "wb"))) goto done;
  if(fwrite(data, 1, (size_t)(value - data), file) != (size_t)(value - data) ||
     fwrite(uri, 1, strlen(uri), file) != strlen(uri) ||
     fwrite(end, 1, (size_t)st.st_size - (size_t)(end - data), file) !=
       (size_t)st.st_size - (size_t)(end - data)) {
    fclose(file);
    file = NULL;
    remove(temp);
    goto done;
  }
  result = fclose(file);
  file = NULL;
  if(!result) result = rename(temp, path);
  if(result) remove(temp);
done:
  if(file) fclose(file);
  free(data);
  return result;
}

int
app_install_if_needed(unsigned short port) {
  const char *title_id = TITLE_ID;
  char base_dir[256];
  char sce_sys_dir[256];
  char param_path[256];
  char icon_path[256];
  int err;
  static int icon_registered;

  if(!icon_registered) {
    asset_register("/icon0.png", icon0_png, icon0_png_size, "image/png", 0);
    icon_registered = 1;
  }

  snprintf(base_dir, sizeof(base_dir), "/user/app/%s", title_id);
  snprintf(sce_sys_dir, sizeof(sce_sys_dir), "/user/app/%s/sce_sys", title_id);
  snprintf(param_path, sizeof(param_path), "%s/param.json", sce_sys_dir);
  snprintf(icon_path, sizeof(icon_path), "%s/icon0.png", sce_sys_dir);

  printf("Installing launcher app %s\n", title_id);

  if((err = pkg_installer_initialize())) {
    printf("sceAppInstUtilInitialize: error 0x%08X\n", err);
    return -1;
  }
  if(mkdir("/user/app", 0700) && errno != EEXIST) {
    perror("mkdir app dir");
    return -1;
  }
  if(mkdir(base_dir, 0755) && errno != EEXIST) {
    perror("mkdir title dir");
    return -1;
  }
  if(mkdir(sce_sys_dir, 0755) && errno != EEXIST) {
    perror("mkdir sce_sys dir");
    return -1;
  }
  if(install_file(param_path, param_json, param_json_size) ||
     install_file(icon_path, icon0_png, icon0_png_size) ||
     update_launcher_port(param_path, port)) {
    perror("install launcher assets");
    return -1;
  }
  if((err = install_app(title_id, "/user/app/"))) {
    printf("install_app: error 0x%08X\n", err);
    return -1;
  }

  return 0;
}
