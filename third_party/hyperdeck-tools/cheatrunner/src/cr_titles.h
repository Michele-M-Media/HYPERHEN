#ifndef CR_TITLES_H
#define CR_TITLES_H

#include <stddef.h>
#include <stdint.h>

typedef struct game_entry {
  char     title_id[16];
  char     title_name[256];
  char     content_id[96];
  char     name_source[32];
  int      name_resolved;
  uint64_t play_time_seconds; /* reserved — tbl_appbrowse.playTime if ever present (0 otherwise) */
  uint64_t last_access_time; /* unix timestamp from tbl_contentinfo.lastAccessTime (0 if unavailable) */
} game_entry_t;

int is_valid_title_id(const char *title_id);
int is_game_title_id(const char *title_id);
int cr_title_name_is_unresolved(const char *title_id, const char *name);
int cr_title_id_normalize(const char *in, char out[10]);
int title_id_normalize(const char *in, char out[10]);
int find_title_id_in_string(const char *s, char out[10]);
int extract_title_id_prefix(const char *filename, char *out, size_t out_size);
int case_contains(const char *haystack, const char *needle);

const char *platform_for_title_id(const char *title_id);
int cr_title_is_known_media_app(const char *title_id, const char *name);

int read_param_value_by_title_id(const char *title_id, const char *key, char *out, size_t out_size);
/* Scans /user/appmeta (and external) for a content dir matching title_id and reads key from the sandbox
 * mount (patch0 before app0). Slow: use it only when the faster paths have failed. */
int read_param_value_from_appmeta(const char *title_id, const char *key, char *out, size_t out_size);
int read_param_value_from_sfo(const char *sfo_path, const char *key, char *out, size_t out_size);
/* Read a param from a game directory — tries sce_sys/param.sfo then sce_sys/param.json */
int read_param_value_from_dir(const char *dir, const char *key, char *out, size_t out_size);
int load_title_meta(const char *dir_path, const char *fallback_id, char *title_id, size_t id_size,
                    char *title_name, size_t name_size);

#endif /* CR_TITLES_H */
