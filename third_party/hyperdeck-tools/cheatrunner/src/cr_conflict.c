#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <stdlib.h>

#include "third_party/cJSON.h"
#include "cr_memory.h"
#include "cr_conflict.h"

typedef struct {
    uint64_t off;
    size_t   len;
    int      is_abs;
    int      section;        /* per-entry "section" (dynlib handle index), 0 = none */
    char     module_name[64]; /* per-mod "module_name", "" = eboot/main */
} cmap_entry_t;

int
conflict_map_build(const char *json_text, cheat_conflict_map_t *out) {
    if (!json_text || !out) return -1;
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_Parse(json_text);
    if (!root) return -1;

    cJSON *mods = cJSON_GetObjectItem(root, "mods");
    if (!cJSON_IsArray(mods)) { cJSON_Delete(root); return -1; }

    int is_name_mc[CONFLICT_MAX_MODS] = {0}; /* from the full name; mod_names is cut at 63 chars */
    int mod_count = cJSON_GetArraySize(mods);
    if (mod_count > CONFLICT_MAX_MODS) mod_count = CONFLICT_MAX_MODS;
    out->mod_count = mod_count;

    /* Collect address ranges per mod — heap-allocated to avoid ~48 KB stack frame. */
    typedef cmap_entry_t entry_grid_t[CONFLICT_MAX_MODS][CONFLICT_MAX_ENTRIES];
    entry_grid_t *entries_p = (entry_grid_t *)calloc(1, sizeof(entry_grid_t));
    int entry_n[CONFLICT_MAX_MODS];
    memset(entry_n, 0, sizeof(entry_n));
    if (!entries_p) { cJSON_Delete(root); return -1; }

    for (int mi = 0; mi < mod_count; mi++) {
        cJSON *mod = cJSON_GetArrayItem(mods, mi);
        cJSON *name_j = cJSON_GetObjectItem(mod, "name");
        if (cJSON_IsString(name_j) && name_j->valuestring)
            snprintf(out->mod_names[mi], 64, "%s", name_j->valuestring);
        if (cJSON_IsString(name_j) && name_j->valuestring)
            is_name_mc[mi] = strcasestr(name_j->valuestring, "mastercode") || strcasestr(name_j->valuestring, "master code");

        cJSON *mem = cJSON_GetObjectItem(mod, "memory");
        if (!cJSON_IsArray(mem)) mem = cJSON_GetObjectItem(mod, "patches");
        if (!cJSON_IsArray(mem)) continue;

        cJSON *mname_j = cJSON_GetObjectItem(mod, "module_name");
        const char *module_name = (cJSON_IsString(mname_j) && mname_j->valuestring) ? mname_j->valuestring : "";

        cJSON *e = NULL;
        cJSON_ArrayForEach(e, mem) {
            if (entry_n[mi] >= CONFLICT_MAX_ENTRIES) break;
            cJSON *off_j = cJSON_GetObjectItem(e, "offset");
            cJSON *on_j  = cJSON_GetObjectItem(e, "on");
            cJSON *abs_j = cJSON_GetObjectItem(e, "absolute");
            cJSON *sec_j = cJSON_GetObjectItem(e, "section");
            if (!cJSON_IsString(off_j) || !cJSON_IsString(on_j)) continue;
            uint64_t off_u = 0;
            if (parse_offset_hex_checked(off_j->valuestring, &off_u) != 0) continue;
            size_t on_len = 0;
            size_t need = hex_bytes_need(on_j->valuestring);
            uint8_t *tmp CR_AUTOFREE = malloc(need);
            if (!tmp || parse_hex_bytes_checked(on_j->valuestring, tmp, need, &on_len) != 0 || on_len == 0) continue;
            cmap_entry_t *ce = &(*entries_p)[mi][entry_n[mi]++];
            ce->off    = off_u;
            ce->len    = on_len;
            ce->is_abs = cJSON_IsTrue(abs_j) ? 1 : 0;
            ce->section = (cJSON_IsNumber(sec_j) && sec_j->valuedouble > 0) ? (int)sec_j->valuedouble : 0;
            snprintf(ce->module_name, sizeof(ce->module_name), "%s", module_name);
        }
    }

    /* group_id: nearest preceding mastercode index (-1 none, -2 is one); the name alone is not enough,
     * it also needs a cave-sized (>=16 byte) entry, like mod_has_cave_entry(). */
    int mod_group[CONFLICT_MAX_MODS];
    for (int mi = 0; mi < mod_count; mi++) mod_group[mi] = -1;
    {
        int last_mc = -1;
        for (int mi = 0; mi < mod_count; mi++) {
            int has_cave = 0;
            for (int ei = 0; ei < entry_n[mi]; ei++) {
                if ((*entries_p)[mi][ei].len >= 16) { has_cave = 1; break; }
            }
            if (is_name_mc[mi] && has_cave) {
                mod_group[mi] = -2; /* IS a mastercode */
                last_mc = mi;
            } else {
                mod_group[mi] = last_mc; /* -1 = before any mastercode (global) */
            }
        }
    }

    /* O(n²) pair comparison: find overlapping address ranges */
    for (int i = 0; i < mod_count; i++) {
        for (int j = i + 1; j < mod_count; j++) {
            if (out->pair_count >= CONFLICT_MAX_PAIRS) goto done;
            /* A mastercode and its dependents do not conflict; a dependent needs the mastercode active. */
            if (mod_group[i] == -2 && mod_group[j] == i) continue;
            /* Skip: different mastercode groups can be on simultaneously, so they don't conflict. */
            if (mod_group[i] >= 0 && mod_group[j] >= 0 && mod_group[i] != mod_group[j]) continue;
            int found = 0;
            for (int ei = 0; ei < entry_n[i] && !found; ei++) {
                for (int ej = 0; ej < entry_n[j] && !found; ej++) {
                    /* Don't compare abs vs relative — can't resolve without base */
                    if ((*entries_p)[i][ei].is_abs != (*entries_p)[j][ej].is_abs) continue;
                    /* Different module base — same offset, different real address. */
                    if ((*entries_p)[i][ei].section != (*entries_p)[j][ej].section) continue;
                    if (strcmp((*entries_p)[i][ei].module_name, (*entries_p)[j][ej].module_name) != 0) continue;
                    uint64_t a0 = (*entries_p)[i][ei].off;
                    uint64_t a1 = a0 + (uint64_t)(*entries_p)[i][ei].len;
                    uint64_t b0 = (*entries_p)[j][ej].off;
                    uint64_t b1 = b0 + (uint64_t)(*entries_p)[j][ej].len;
                    if (a0 < b1 && b0 < a1) {
                        conflict_pair_t *p = &out->pairs[out->pair_count++];
                        p->mod_a      = i;
                        p->mod_b      = j;
                        p->shared_off = (a0 < b0) ? a0 : b0;
                        found = 1;
                    }
                }
            }
        }
    }
done:
    free(entries_p);
    cJSON_Delete(root);
    return 0;
}

int
conflict_map_get_for_mod(const cheat_conflict_map_t *map, int mod_idx,
                          int *out, int out_max) {
    if (!map || !out || out_max <= 0) return 0;
    int n = 0;
    for (int i = 0; i < map->pair_count && n < out_max; i++) {
        if (map->pairs[i].mod_a == mod_idx) {
            out[n++] = map->pairs[i].mod_b;
        } else if (map->pairs[i].mod_b == mod_idx) {
            out[n++] = map->pairs[i].mod_a;
        }
    }
    return n;
}

const char *
conflict_map_mod_name(const cheat_conflict_map_t *map, int mod_idx) {
    if (!map || mod_idx < 0 || mod_idx >= map->mod_count) return "";
    return map->mod_names[mod_idx];
}
