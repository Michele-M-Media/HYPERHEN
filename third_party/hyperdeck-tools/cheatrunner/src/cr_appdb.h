#ifndef CR_APPDB_H
#define CR_APPDB_H

#include <stddef.h>
#include "cr_titles.h"

/* Required capacity of entries[] passed to appdb_collect_games. */
#define CR_APPDB_MAX_GAMES 1024

int appdb_collect_games(game_entry_t *entries, size_t *count);

int resolve_icon_path(const char *title_id, char *out, size_t out_size);
int resolve_pic0_path(const char *title_id, char *out, size_t out_size);
int cache_media_path(const char *title_id, int is_pic0, char *out, size_t out_size);
int cache_media_ensure(const char *title_id, int is_pic0, char *cached_path, size_t cached_size);

void appdb_diag_get(char *mode, size_t mode_size, char *reason, size_t reason_size, size_t *last_count);

#endif /* CR_APPDB_H */