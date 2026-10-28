/* Copyright (C) 2026 OnionHEN / LightningMods */

#include "bootstrap_assets.h"

#include <onion/fs.h>
#include <onion/notify.h>

#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>

extern uint8_t sicon_start[];
extern const unsigned int sicon_size;
extern uint8_t klogsrv_start[];
extern const unsigned int klogsrv_size;
extern uint8_t ftpsrv_start[];
extern const unsigned int ftpsrv_size;
extern uint8_t garlic_start[];
extern const unsigned int garlic_size;
extern uint8_t debug_elfldr_start[];
extern const unsigned int debug_elfldr_size;
#if defined(HYPERHEN_EXPERIMENTAL_HYPERSHELL)
extern uint8_t hypershell_engine_start[];
extern const unsigned int hypershell_engine_size;
#endif
extern uint8_t fw_spoof_start[];
extern const unsigned int fw_spoof_size;
extern uint8_t np_fake_signin_start[];
extern const unsigned int np_fake_signin_size;
extern uint8_t nanodns_start[];
extern const unsigned int nanodns_size;
extern uint8_t chukei_start[];
extern const unsigned int chukei_size;
extern uint8_t zftpd_start[];
extern const unsigned int zftpd_size;

#define ONION_ICON(name)                                                       \
  extern uint8_t name##_start[];                                               \
  extern const unsigned int name##_size;
#include "icon_manifest.inc"
#undef ONION_ICON

typedef struct EmbeddedIcon {
  const char *name;
  const uint8_t *data;
  const unsigned int *size;
} EmbeddedIcon;

static const EmbeddedIcon kEmbeddedIcons[] = {
#define ONION_ICON(name) {#name, name##_start, &name##_size},
#include "icon_manifest.inc"
#undef ONION_ICON
};

bool bootstrap_assets_write(void) {
  mkdir("/data/HyperHEN", 0777);
  mkdir("/data/HyperHEN/assets", 0777);
  mkdir("/data/HyperHEN/tools", 0777);
  mkdir("/data/HyperHEN/internal", 0777);
  mkdir("/user/data", 0777);
  mkdir("/user/data/HyperHEN", 0777);

  const bool startup_icon_ready = write_file_atomic(
      ONION_NOTIFY_ICON_PATH, sicon_start, sicon_size);

  for (size_t i = 0; i < sizeof(kEmbeddedIcons) / sizeof(kEmbeddedIcons[0]);
       ++i) {
    char path[256];
    snprintf(path, sizeof(path), "/data/HyperHEN/assets/%s.png",
             kEmbeddedIcons[i].name);
    (void)write_file_atomic(path, kEmbeddedIcons[i].data,
                            *kEmbeddedIcons[i].size);
  }

  /* Fixed paths make the toolbox toggles independent of USB storage. */
  (void)write_file_atomic("/data/HyperHEN/tools/klogsrv.elf",
                          klogsrv_start, klogsrv_size);
  (void)write_file_atomic("/data/HyperHEN/tools/ftpsrv.elf",
                          ftpsrv_start, ftpsrv_size);
  (void)write_file_atomic("/data/HyperHEN/tools/garlic-savemgr.elf",
                          garlic_start, garlic_size);
  (void)write_file_atomic("/data/HyperHEN/tools/hyperhen_debug_elfldr.elf",
                          debug_elfldr_start, debug_elfldr_size);
#if defined(HYPERHEN_EXPERIMENTAL_HYPERSHELL)
  (void)write_file_atomic("/data/HyperHEN/internal/hypershell-engine.elf",
                          hypershell_engine_start, hypershell_engine_size);
#endif
  (void)write_file_atomic("/data/HyperHEN/tools/ps5-fw-spoof.elf",
                          fw_spoof_start, fw_spoof_size);
  (void)write_file_atomic("/data/HyperHEN/tools/np-fake-signin-ps5.elf",
                          np_fake_signin_start, np_fake_signin_size);
  (void)write_file_atomic("/data/HyperHEN/tools/nanodns.elf",
                          nanodns_start, nanodns_size);
  (void)write_file_atomic("/data/HyperHEN/tools/chukei-ps5.elf",
                          chukei_start, chukei_size);
  (void)write_file_atomic(
      "/data/HyperHEN/tools/zftpd-ps5-zhttp-v1.6.0.elf", zftpd_start,
      zftpd_size);

  mkdir("/system_ex/vsh_asset", 0777);
  (void)write_file_atomic("/system_ex/vsh_asset/hyperhen.png", sicon_start,
                          sicon_size);
  return startup_icon_ready;
}
