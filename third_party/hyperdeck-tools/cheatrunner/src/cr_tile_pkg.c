#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "cr_config.h"
#include "cr_log.h"
#include "cr_notifications.h"
#include "cr_paths.h"
#include "cr_tile_pkg.h"
#include "jb.h"

/* ----------------------------------------------------------------------- */
/* sceAppInstUtil ABI structs. */

#define HB_CONTENTID_SIZE        0x30
#define HB_PLAYGOSCENARIOID_SIZE 3
#define HB_LANGUAGE_SIZE         8
#define HB_NUM_LANGUAGES         30
#define HB_NUM_IDS               64

typedef char hb_content_id_t[HB_CONTENTID_SIZE];
typedef char hb_playgo_scenario_id_t[HB_PLAYGOSCENARIOID_SIZE];
typedef char hb_language_t[HB_LANGUAGE_SIZE];

typedef struct {
  hb_content_id_t content_id;
  int             content_type;
  int             content_platform;
} hb_pkg_info_t;

typedef struct {
  const char *uri;
  const char *ex_uri;
  const char *playgo_scenario_id;
  const char *content_id;
  const char *content_name;
  const char *icon_url;
} hb_meta_info_t;

typedef struct {
  hb_language_t            languages[HB_NUM_LANGUAGES];
  hb_playgo_scenario_id_t  playgo_scenario_ids[HB_NUM_IDS];
  hb_content_id_t          content_ids[HB_NUM_IDS];
  long                     unknown[810];
} hb_playgo_info_t;

extern int sceAppInstUtilInitialize(void);
extern int sceAppInstUtilInstallByPackage(hb_meta_info_t   *meta,
                                          hb_pkg_info_t    *pkg_info,
                                          hb_playgo_info_t *play);
extern int sceAppInstUtilAppInstallPkg(const char        *path,
                                       hb_pkg_info_t     *pkg_info);

/* ----------------------------------------------------------------------- */
/* Embedded PKG data (generated at build time from dist/CheatRunner.pkg)   */

#ifdef CHEATRUNNER_HAVE_TILE_PKG
#include "cheatrunner_tile_pkg.h"  /* g_cheatrunner_tile_pkg[], _len */
#endif

/* ----------------------------------------------------------------------- */

#define TILE_PKG_PATH  CHEATRUNNER_DATA_DIR "/cheatrunner-tile.pkg"
#define TILE_TITLE_ID  "CHTR09999"
#define TILE_APPMETA   "/user/appmeta/" TILE_TITLE_ID

static int
install_pkg_at_path(const char *path) {
  /* Initialize once: repeated calls reset the IPMI state. */
  static pthread_mutex_t ai_mtx  = PTHREAD_MUTEX_INITIALIZER;
  static int             ai_done = 0;
  pthread_mutex_lock(&ai_mtx);
  if (!ai_done) { sceAppInstUtilInitialize(); ai_done = 1; }
  pthread_mutex_unlock(&ai_mtx);

  /* /data/... → /user/data/... so the install service sandbox can see it. */
  char sdk_path[1024];
  if (strncmp(path, "/data/", 6) == 0)
    snprintf(sdk_path, sizeof(sdk_path), "/user%s", path);
  else
    snprintf(sdk_path, sizeof(sdk_path), "%s", path);

  /* Primary: direct install. */
  hb_pkg_info_t pkg = {0};
  int rc = sceAppInstUtilAppInstallPkg(sdk_path, &pkg);
  if (rc == 0) return 0;

  cr_log("warn", "tile_pkg",
         "sceAppInstUtilAppInstallPkg rc=0x%08x — trying InstallByPackage",
         (unsigned)rc);

  /* Fallback: InstallByPackage with a file:// URI. */
  char file_uri[1024];
  snprintf(file_uri, sizeof(file_uri), "file://%s", sdk_path);

  hb_meta_info_t   meta   = {0};
  hb_pkg_info_t    pkg2   = {0};
  hb_playgo_info_t playgo = {0};
  meta.uri                = file_uri;
  meta.ex_uri             = "";
  meta.playgo_scenario_id = "";
  meta.content_id         = "";
  meta.content_name       = "CheatRunner";
  meta.icon_url           = "";

  int rc2 = sceAppInstUtilInstallByPackage(&meta, &pkg2, &playgo);
  return (rc2 == 0) ? 0 : rc;
}

static void *
tile_autoinstall_thread(void *arg) {
  (void)arg;

#ifndef CHEATRUNNER_HAVE_TILE_PKG
  cr_log("info", "tile_pkg", "no embedded PKG at build time — skipping");
  return NULL;
#else
  pthread_mutex_lock(&g_cfg_lock);
  int autoinstall_enabled = g_cfg.tile_autoinstall_enabled;
  pthread_mutex_unlock(&g_cfg_lock);
  if (!autoinstall_enabled) {
    cr_log("info", "tile_pkg", "auto-install disabled in config — skipping");
    return NULL;
  }

  /* Already installed: skip, since AppPrepareOverwriteByPackage on every boot crashes the SceShellCore
   * installer queue on some firmwares. */
  struct stat st;
  if (stat(TILE_APPMETA, &st) == 0) {
    cr_log("info", "tile_pkg", TILE_TITLE_ID " already installed, skipping");
    return NULL;
  }

  /* Notify immediately so the user sees feedback before the disk write. */
  notify("CheatRunner: installing PKG...");
  cr_log("info", "tile_pkg", "tile not yet installed — starting install");

  /* 3 s lets the SceShellCore installer queue settle; only a cold first boot needs the full 30 s. */
  sleep(3);

  if (jb_escalate_pid(getpid()) != 0) {
    cr_log("warn", "tile_pkg", "jb_escalate_pid failed; PKG install may fail");
  }

  /* Write the embedded PKG bytes to disk. */
  cr_log("info", "tile_pkg", "writing %s (%zu bytes)",
         TILE_PKG_PATH, g_cheatrunner_tile_pkg_len);
  if (write_file_atomic(TILE_PKG_PATH,
                        g_cheatrunner_tile_pkg,
                        g_cheatrunner_tile_pkg_len) != 0) {
    cr_log("error", "tile_pkg", "write failed — errno %d", errno);
    notify("CheatRunner: tile PKG write failed");
    return NULL;
  }

  int rc = install_pkg_at_path(TILE_PKG_PATH);
  if (rc == 0) {
    cr_log("info", "tile_pkg", "home-screen tile installed");
    notify("CheatRunner: PKG installed!");
  } else {
    cr_log("warn", "tile_pkg", "install failed rc=0x%08x", (unsigned)rc);
    notify("CheatRunner: tile install failed (0x%08x)", (unsigned)rc);
  }
  return NULL;
#endif
}

void
cr_tile_autoinstall_init(void) {
  pthread_t      t;
  pthread_attr_t a;
  pthread_attr_init(&a);
  pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
  if (pthread_create(&t, &a, tile_autoinstall_thread, NULL) != 0)
    cr_log("warn", "tile_pkg", "could not spawn tile-install thread");
  pthread_attr_destroy(&a);
}
