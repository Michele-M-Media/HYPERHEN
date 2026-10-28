#ifndef SM_PKG_NATIVE_H
#define SM_PKG_NATIVE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include "sm_storage.h"

// AppInstUtil output sizes verified against the console's 9.40 library.
typedef struct { char content_id[48]; int type, platform; } sm_pkg_native_info_t;
// InstallByPackage task types; independent of the source parser's category.
enum { SM_PKG_NATIVE_BASE = 0, SM_PKG_NATIVE_ADDCONT = 1, SM_PKG_NATIVE_PATCH = 2 };
typedef struct {
  int32_t code, version;
  char description[512], type[9];
} sm_pkg_native_error_t;
typedef struct {
  char status[16], source[8];
  float remaining_seconds;
  uint64_t downloaded, initial_chunk, total;
  uint32_t promotion;
  unsigned char reserved[100];
  sm_pkg_native_error_t error;
  char patch_version[12];
  int32_t local_copy_percent;
  bool copy_only;
} sm_pkg_native_status_t;

typedef struct {
  const char *uri, *ex_uri, *scenario, *content_id, *name, *icon;
  uint64_t reserved[8];
} sm_pkg_native_metadata_t;
typedef struct {
  char languages[30][8], scenarios[64][3], content_ids[64][48];
  unsigned char reserved[6488];
} sm_pkg_native_playgo_t;

// GetAddcontInfoList2 record layout
typedef struct {
  char content_id[48], title_name[129];
  unsigned char reserved0[7];
  uint64_t size;
  char icon_path[257];
  unsigned char reserved1[10];
  char version[13];
  unsigned char reserved2[16];
} sm_pkg_native_addcont_t;
_Static_assert(sizeof(sm_pkg_native_addcont_t) == 0x1e8, "AppInstUtil addcont ABI");
_Static_assert(offsetof(sm_pkg_native_addcont_t, size) == 0xb8, "AppInstUtil addcont size offset");
_Static_assert(offsetof(sm_pkg_native_addcont_t, version) == 0x1cb, "AppInstUtil addcont version offset");
_Static_assert(sizeof(sm_pkg_native_metadata_t) == 0x70, "AppInstUtil metadata ABI");
_Static_assert(sizeof(sm_pkg_native_info_t) == 0x38, "AppInstUtil info ABI");
_Static_assert(sizeof(sm_pkg_native_playgo_t) == 0x2708, "AppInstUtil PlayGo ABI");
_Static_assert(sizeof(sm_pkg_native_status_t) == 0x2c8, "AppInstUtil status ABI");
_Static_assert(offsetof(sm_pkg_native_status_t, downloaded) == 0x20, "AppInstUtil download offset");
_Static_assert(offsetof(sm_pkg_native_status_t, error) == 0xa0, "AppInstUtil error offset");
_Static_assert(offsetof(sm_pkg_native_status_t, local_copy_percent) == 0x2c0, "AppInstUtil copy offset");
int sceAppInstUtilInstallByPackage(sm_pkg_native_metadata_t *metadata,
    sm_pkg_native_info_t *info, sm_pkg_native_playgo_t *playgo);
int sceAppInstUtilGetInstallStatus(const char *content_id, sm_pkg_native_status_t *status);
int sceAppInstUtilGetPatchInstallStatus(const char *content_id, sm_pkg_native_status_t *status);
// This API writes one 32-bit state, not a status struct. Verified on firmware 9.40.
enum { SM_PKG_ADDCONT_INSTALLED = 4 };
int sceAppInstUtilGetAddcontInstalledStatus(const char *content_id, int *installed);
int sceAppInstUtilAppGetSize(const char *title_id, uint64_t *size);
int sceAppInstUtilAppGetAddcontNum(const char *title_id, uint64_t *count);
// total is the complete matching count, not the number written into this page.
int sceAppInstUtilGetAddcontInfoList2(const char *title_id, int sort,
    int offset, int capacity, sm_pkg_native_addcont_t *entries, uint64_t *total);
int sceAppInstUtilCancelInstall(const char *content_id);
int sceAppInstUtilAppUnInstallPat(const char *title_id);
// dir_name is the content ID suffix; the IPC request reads a full 32-byte buffer.
int sceAppInstUtilAppUnInstallAddcont(const char *title_id, const char dir_name[32]);
#endif
