#ifndef SM_ICON_THUMB_H
#define SM_ICON_THUMB_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/stat.h>

#include "sm_limits.h"

#define SM_ICON_THUMB_SIZE 128u
#define SM_ICON_CARD_SIZE 256u
#define SM_ICON_COVER_SIZE 512u
#define SM_ICON_CACHE_VERSION 3u

uint64_t sm_icon_source_fingerprint(const char *source_path,
                                    const struct stat *source_st);

// Return a cached thumbnail path, creating it atomically when necessary.
bool sm_icon_thumbnail_path(const char *title_id, const char *source_path,
                            const struct stat *source_st, uint32_t edge,
                            char out[MAX_PATH]);

#endif
