/* HyperHEN contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <onion/system_tmp.h>

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>

#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum Onion5xxBootMode {
  ONION_5XX_BOOT_MODE_INVALID = -1,
  ONION_5XX_BOOT_MODE_DEFAULT = 0,
  ONION_5XX_BOOT_MODE_BYPERVISOR = 1,
  ONION_5XX_BOOT_MODE_HYPERCORE = 2,
  ONION_5XX_BOOT_MODE_PS5HEN = 3,
} Onion5xxBootMode;

#define ONION_5XX_SELECTOR_ACTIVE_PATH                                      \
  ONION_SYSTEM_TMP_ROOT "/selector_5xx_active"
#define ONION_5XX_BOOT_MODE_PATH ONION_SYSTEM_TMP_ROOT "/boot_mode_5xx"
#define ONION_5XX_BOOT_OWNER_PATH ONION_SYSTEM_TMP_ROOT "/boot_5xx_owner"

#define ONION_5XX_BOOT_MODE_BYPERVISOR_TOKEN "bypervisor"
#define ONION_5XX_BOOT_MODE_HYPERCORE_TOKEN "hypercore"
#define ONION_5XX_BOOT_MODE_PS5HEN_TOKEN "ps5hen"

/* The selector writes one exact token without a trailing newline. */
static inline Onion5xxBootMode onion_5xx_boot_mode_read(const char *path) {
  if (!path || !path[0])
    return ONION_5XX_BOOT_MODE_INVALID;

  const int fd = open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
  if (fd < 0)
    return ONION_5XX_BOOT_MODE_INVALID;

  struct stat info;
  if (fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) ||
      (info.st_size !=
           (off_t)(sizeof(ONION_5XX_BOOT_MODE_BYPERVISOR_TOKEN) - 1) &&
       info.st_size !=
           (off_t)(sizeof(ONION_5XX_BOOT_MODE_HYPERCORE_TOKEN) - 1) &&
       info.st_size !=
           (off_t)(sizeof(ONION_5XX_BOOT_MODE_PS5HEN_TOKEN) - 1))) {
    (void)close(fd);
    return ONION_5XX_BOOT_MODE_INVALID;
  }

  char token[16] = {0};
  size_t used = 0;
  while (used < (size_t)info.st_size) {
    const ssize_t chunk = read(fd, token + used, (size_t)info.st_size - used);
    if (chunk > 0) {
      used += (size_t)chunk;
      continue;
    }
    if (chunk < 0 && errno == EINTR)
      continue;
    (void)close(fd);
    return ONION_5XX_BOOT_MODE_INVALID;
  }

  char extra = 0;
  ssize_t trailing = -1;
  do {
    trailing = read(fd, &extra, 1);
  } while (trailing < 0 && errno == EINTR);
  (void)close(fd);
  if (trailing != 0)
    return ONION_5XX_BOOT_MODE_INVALID;

  if (used == sizeof(ONION_5XX_BOOT_MODE_BYPERVISOR_TOKEN) - 1 &&
      memcmp(token, ONION_5XX_BOOT_MODE_BYPERVISOR_TOKEN,
             sizeof(ONION_5XX_BOOT_MODE_BYPERVISOR_TOKEN) - 1) == 0)
    return ONION_5XX_BOOT_MODE_BYPERVISOR;
  if (used == sizeof(ONION_5XX_BOOT_MODE_HYPERCORE_TOKEN) - 1 &&
      memcmp(token, ONION_5XX_BOOT_MODE_HYPERCORE_TOKEN,
             sizeof(ONION_5XX_BOOT_MODE_HYPERCORE_TOKEN) - 1) == 0)
    return ONION_5XX_BOOT_MODE_HYPERCORE;
  if (used == sizeof(ONION_5XX_BOOT_MODE_PS5HEN_TOKEN) - 1 &&
      memcmp(token, ONION_5XX_BOOT_MODE_PS5HEN_TOKEN,
             sizeof(ONION_5XX_BOOT_MODE_PS5HEN_TOKEN) - 1) == 0)
    return ONION_5XX_BOOT_MODE_PS5HEN;
  return ONION_5XX_BOOT_MODE_INVALID;
}

static inline const char *onion_5xx_boot_mode_name(Onion5xxBootMode mode) {
  switch (mode) {
  case ONION_5XX_BOOT_MODE_BYPERVISOR:
    return ONION_5XX_BOOT_MODE_BYPERVISOR_TOKEN;
  case ONION_5XX_BOOT_MODE_HYPERCORE:
    return ONION_5XX_BOOT_MODE_HYPERCORE_TOKEN;
  case ONION_5XX_BOOT_MODE_PS5HEN:
    return ONION_5XX_BOOT_MODE_PS5HEN_TOKEN;
  default:
    return "invalid";
  }
}

#ifdef __cplusplus
}
#endif
