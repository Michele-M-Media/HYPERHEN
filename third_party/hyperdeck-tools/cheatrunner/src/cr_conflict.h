#pragma once
#include <stdint.h>
#include <stddef.h>

#define CONFLICT_MAX_PAIRS  256
#define CONFLICT_MAX_MODS    64
#define CONFLICT_MAX_ENTRIES  32

typedef struct {
    int      mod_a;
    int      mod_b;
    uint64_t shared_off;   /* lowest conflicting byte offset */
} conflict_pair_t;

typedef struct {
    conflict_pair_t pairs[CONFLICT_MAX_PAIRS];
    int             pair_count;
    char            mod_names[CONFLICT_MAX_MODS][64];
    int             mod_count;
} cheat_conflict_map_t;

/* Builds the conflict map from parsed JSON text by comparing mod pairs for overlapping offset
 * ranges. Returns 0 on success, -1 on parse error. */
int  conflict_map_build(const char *json_text, cheat_conflict_map_t *out);

/* Returns list of mod indices that conflict with mod_idx; returns count. */
int  conflict_map_get_for_mod(const cheat_conflict_map_t *map, int mod_idx,
                               int *out, int out_max);

/* Returns mod name string (never NULL). */
const char *conflict_map_mod_name(const cheat_conflict_map_t *map, int mod_idx);
