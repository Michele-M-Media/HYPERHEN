#include "pkg_installer.h"

#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <time.h>

#include "smb.h"
#include "websrv.h"
#include "pkg_info.h"
#include "path_util.h"
#include "vfs.h"

/* Keep successful source URLs alive: installation can continue after submission. */
struct pkg_install_source {
  char name[64];
  int fd;
  unsigned int refs;
  struct pkg_install_source *next;
};
static pkg_install_source_t *sources;
static pthread_mutex_t sources_lock = PTHREAD_MUTEX_INITIALIZER;

pkg_install_source_t *pkg_installer_source_acquire(const char *name) {
  pkg_install_source_t *source = NULL;
  const char *extension = strrchr(name, '.');
  if(!extension || (strcmp(extension, ".pkg") && strcmp(extension, ".png"))) return NULL;
  pthread_mutex_lock(&sources_lock);
  for(pkg_install_source_t *item = sources; item; item = item->next) {
    if(strlen(item->name) == (size_t)(extension - name) && !strncmp(item->name, name, (size_t)(extension - name))) {
      item->refs++;
      source = item;
      break;
    }
  }
  pthread_mutex_unlock(&sources_lock);
  return source;
}

int pkg_installer_source_fd(const pkg_install_source_t *source) {
  return source->fd;
}

void pkg_installer_source_release(pkg_install_source_t *source) {
  if(!source) return;
  pthread_mutex_lock(&sources_lock);
  int dispose = --source->refs == 0;
  pthread_mutex_unlock(&sources_lock);
  if(dispose) { close(source->fd); free(source); }
}

#ifndef __linux__

typedef struct pkg_metadata {
  const char *uri;
  const char *ex_uri;
  const char *playgo_scenario_id;
  const char *content_id;
  const char *content_name;
  const char *icon_url;
  uint32_t slot;
  uint32_t is_playgo_enabled;
} pkg_metadata_t;

_Static_assert(offsetof(pkg_metadata_t, slot) == 0x30 &&
               offsetof(pkg_metadata_t, is_playgo_enabled) == 0x34 &&
               sizeof(pkg_metadata_t) == 0x38,
               "sceAppInstUtil metadata ABI mismatch");

typedef struct pkg_info {
  char content_id[48];
  int type;
  int platform;
} pkg_info_t;

typedef struct playgo_info {
  char languages[30][8];
  char scenario_ids[64][3];
  char content_ids[64][48];
  long unknown[810];
} playgo_info_t;

_Static_assert(sizeof(playgo_info_t) == 0x2700,
               "sceAppInstUtil PlayGoInfo ABI mismatch");

int sceAppInstUtilInitialize(void);
int sceAppInstUtilInstallByPackage(const pkg_metadata_t *, pkg_info_t *,
                                   playgo_info_t *);

static pthread_mutex_t installer_lock = PTHREAD_MUTEX_INITIALIZER;
static int installer_initialized;

static int
initialize_locked(void) {
  int result;

  if(!installer_initialized) {
    result = sceAppInstUtilInitialize();
    if(result) return result;
    installer_initialized = 1;
  }
  return 0;
}

int
pkg_installer_initialize(void) {
  int result;

  pthread_mutex_lock(&installer_lock);
  result = initialize_locked();
  pthread_mutex_unlock(&installer_lock);
  return result;
}

int
pkg_installer_install(const char *path) {
  char install_path[PATH_MAX + 128];
  char icon_url[160] = {0};
  pkg_details_t details;
  pkg_install_source_t *source = NULL;
  const char *uri = path;
  pkg_metadata_t metadata = {
    .uri = NULL,
    .ex_uri = "",
    .playgo_scenario_id = "",
    .content_id = "",
    .content_name = "",
    .icon_url = "",
    .slot = 0,
    .is_playgo_enabled = 0
  };
  pkg_info_t pkg_info = {0};
  playgo_info_t playgo_info = {0};
  int result;
  int remote;

  if(!path) return -1;
  remote = smb_path(path);
  pkg_read_details(path, &details);
  metadata.content_name = details.title[0] ? details.title : path_basename(path);
  metadata.content_id = details.content_id;
#if WFM_DEBUG
  fprintf(stderr, "PKG metadata platform=%s content_id=%s title=%s icon=%d\n",
          details.platform == 5 ? "PS5" : details.platform == 4 ? "PS4" : "unknown",
          details.content_id, metadata.content_name, details.has_icon);
#endif
  if(remote || details.has_icon) {
    unsigned short port = websrv_port();
    struct timespec now;
    if(!port || clock_gettime(CLOCK_REALTIME, &now) || !(source = calloc(1, sizeof(*source)))) return -1;
    source->fd = open(path, O_RDONLY);
    if(source->fd < 0) { free(source); return -1; }
    source->refs = 1; /* Registry ownership, also after install submission. */
    static unsigned long serial;
    pthread_mutex_lock(&sources_lock);
    snprintf(source->name, sizeof(source->name), "%llu-%lu-%lu", (unsigned long long)now.tv_sec,
             (unsigned long)now.tv_nsec, ++serial);
    source->next = sources; sources = source;
    pthread_mutex_unlock(&sources_lock);
    if(remote) {
      snprintf(install_path, sizeof(install_path), "http://127.0.0.1:%u/pkg-source/%s.pkg", port, source->name);
      uri = install_path;
    }
    if(details.has_icon) {
      snprintf(icon_url, sizeof(icon_url), "http://127.0.0.1:%u/pkg-source/%s.png", port, source->name);
      metadata.icon_url = icon_url;
    }
  }
  if(!remote && !strncmp(path, "/data/", 6)) {
    snprintf(install_path, sizeof(install_path), "/user%s", path);
    uri = install_path;
  }
  metadata.uri = uri;

  pthread_mutex_lock(&installer_lock);
  result = initialize_locked();
  if(!result) result = sceAppInstUtilInstallByPackage(&metadata, &pkg_info, &playgo_info);
  pthread_mutex_unlock(&installer_lock);
  if(result && source) {
    pthread_mutex_lock(&sources_lock);
    pkg_install_source_t **link = &sources;
    while(*link && *link != source) link = &(*link)->next;
    if(*link) *link = source->next;
    pthread_mutex_unlock(&sources_lock);
    pkg_installer_source_release(source);
  }
  return result;
}

#else

int
pkg_installer_initialize(void) {
  return PKG_INSTALL_UNSUPPORTED;
}

int
pkg_installer_install(const char *path) {
  (void)path;
  return PKG_INSTALL_UNSUPPORTED;
}

#endif
