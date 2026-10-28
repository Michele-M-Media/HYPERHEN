#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "third_party/cJSON.h"
#include "cr_api_internal.h"
#include "cr_api_patches.h"
#include "cr_patch_parser.h"
#include "cr_game_monitor.h"
#include "cr_titles.h"
#include "cr_version.h"
#include "cr_log.h"
#include "cr_paths.h"

/* Maximum XML source files to scan per title */
#define PATCHES_MAX_SOURCES 12

/* Max entries across all source files for one title. */
#define PATCHES_ALL_MAX 128

static const char *
source_kind_str(cr_patch_source_kind_t k) {
    switch (k) {
    case CR_PATCH_SOURCE_CHEATRUNNER_PS5: return "cheatrunner_ps5";
    case CR_PATCH_SOURCE_CHEATRUNNER_XML: return "cheatrunner_xml";
    case CR_PATCH_SOURCE_ELF_ARSENAL_XML: return "elf_arsenal_xml";
    default: return "unknown";
    }
}

static const char *
source_kind_file_label(cr_patch_source_kind_t k) {
    switch (k) {
    case CR_PATCH_SOURCE_CHEATRUNNER_PS5: return "xml_prospero";
    case CR_PATCH_SOURCE_CHEATRUNNER_XML: return "xml";
    case CR_PATCH_SOURCE_ELF_ARSENAL_XML: return "elf-arsenal";
    default: return "";
    }
}

/* ── Build a deterministic merged entry list for a title ─────────────────── */

typedef struct {
    patch_entry_t *entries;  /* heap-allocated array */
    int            count;
    char           game_title[128];
} merged_patches_t;

/* Caller must free the result; prefers files named after title_id over cross-compat files that merely list it. */
static merged_patches_t *
build_merged_patches(const char *title_id) {
    char paths[PATCHES_MAX_SOURCES][384];
    cr_patch_source_kind_t kinds[PATCHES_MAX_SOURCES];
    int nsrc = patch_find_xmls_for_title(title_id, paths, kinds, PATCHES_MAX_SOURCES);

    merged_patches_t *m = calloc(1, sizeof(*m));
    if (!m) return NULL;

    if (nsrc == 0) return m;

    /* Prefer basenames starting with title_id; fall back to all files only if none match. */
    int use[PATCHES_MAX_SOURCES];
    int nuse = 0;
    size_t tid_len = strlen(title_id);
    for (int i = 0; i < nsrc; i++) {
        const char *bn = path_basename_ptr(paths[i]);
        if (strncmp(bn, title_id, tid_len) == 0)
            use[nuse++] = i;
    }
    if (nuse == 0) {
        /* No file named for this title — show all (cross-compat fallback) */
        for (int i = 0; i < nsrc; i++) use[nuse++] = i;
    }

    m->entries = calloc((size_t)PATCHES_ALL_MAX, sizeof(patch_entry_t));
    if (!m->entries) { free(m); return NULL; }

    patch_doc_t *doc = calloc(1, sizeof(*doc));
    if (!doc) { free(m->entries); free(m); return NULL; }

    for (int ui = 0; ui < nuse && m->count < PATCHES_ALL_MAX; ui++) {
        int si = use[ui];
        memset(doc, 0, sizeof(*doc));
        int n = patch_parse_xml_file(paths[si], title_id, kinds[si], doc);
        if (n <= 0) continue;
        if (!m->game_title[0] && doc->game_title[0])
            snprintf(m->game_title, sizeof(m->game_title), "%s", doc->game_title);
        for (int j = 0; j < doc->count && m->count < PATCHES_ALL_MAX; j++)
            m->entries[m->count++] = doc->entries[j];
    }

    free(doc);
    return m;
}

static void
free_merged(merged_patches_t *m) {
    if (!m) return;
    free(m->entries);
    free(m);
}

int
patch_apply_by_entry_id(const char *title_id, const char *entry_id, int force,
                        char *err, size_t err_size) {
    merged_patches_t *m = build_merged_patches(title_id);
    if (!m) { snprintf(err, err_size, "oom"); return -1; }
    if (m->count == 0) {
        free_merged(m);
        snprintf(err, err_size, "patch_not_found");
        return -1;
    }

    patch_entry_t *e = NULL;
    for (int i = 0; i < m->count; i++) {
        if (strcmp(m->entries[i].entry_id, entry_id) == 0) { e = &m->entries[i]; break; }
    }
    if (!e) {
        free_merged(m);
        snprintf(err, err_size, "entry_not_found");
        return -1;
    }
    if (e->line_count == 0) {
        free_merged(m);
        snprintf(err, err_size, "no_supported_lines");
        return -1;
    }
    if (e->has_unsupported) {
        free_merged(m);
        snprintf(err, err_size, "unsupported_line_type");
        return -1;
    }

    if (!force && !e->is_mask_ver && e->app_ver[0]) {
        running_game_state_t apply_st;
        running_state_get(&apply_st);
        char game_ver[32] = {0};
        if (apply_st.running) {
            const char *ver = apply_st.content_version[0] ? apply_st.content_version
                                                           : apply_st.app_version;
            if (ver[0]) snprintf(game_ver, sizeof(game_ver), "%s", ver);
        }
        if (game_ver[0] && !cr_version_equal(e->app_ver, game_ver)) {
            free_merged(m);
            snprintf(err, err_size, "version_mismatch");
            return -1;
        }
    }

    patch_apply_result_t res;
    int rc = patch_apply_entry_ex(title_id, e, &res);
    if (rc != 0) {
        snprintf(err, err_size, "%s", res.error[0] ? res.error : "apply_failed");
        free_merged(m);
        return -1;
    }
    patch_mark_applied(title_id, e->entry_id, res.pid);
    free_merged(m);
    return 0;
}

/* ── /api/patches?titleId=CUSA00900 ─────────────────────────────────────── */

static void
handle_patches_list(int fd, const char *query) {
    char title_id[16] = {0};
    if (!query || query_value(query, "titleId", title_id, sizeof(title_id)) < 0 || !title_id[0]) {
        http_send_json(fd, 400, "{\"ok\":false,\"error\":\"missing titleId\"}");
        return;
    }

    merged_patches_t *m = build_merged_patches(title_id);
    if (!m) { http_send_oom(fd); return; }

    /* Detect running game version */
    char game_ver[32] = {0};
    running_game_state_t cached_st;
    running_state_get(&cached_st);
    int game_running = cached_st.running && strcmp(cached_st.title_id, title_id) == 0;
    pid_t pid = game_running ? cached_st.pid : -1;
    if (game_running) {
        const char *ver = cached_st.content_version[0] ? cached_st.content_version
                                                        : cached_st.app_version;
        if (ver[0]) snprintf(game_ver, sizeof(game_ver), "%s", ver);
    }

    cJSON *out = cJSON_CreateObject();
    cJSON_AddBoolToObject(out, "ok",                  1);
    cJSON_AddStringToObject(out, "titleId",           title_id);
    cJSON_AddStringToObject(out, "gameTitle",         m->game_title);
    cJSON_AddBoolToObject(out,  "hasPatchFile",       m->count > 0);
    cJSON_AddStringToObject(out, "detectedGameVersion", game_ver);
    cJSON_AddBoolToObject(out,  "gameRunning",        game_running ? 1 : 0);

    cJSON *arr = cJSON_AddArrayToObject(out, "entries");

    for (int i = 0; i < m->count; i++) {
        patch_entry_t *e = &m->entries[i];

        /* Version status */
        const char *ver_status;
        int ver_match;
        if (e->is_mask_ver) {
            ver_status = "mask"; ver_match = 1;
        } else if (!game_ver[0] || !e->app_ver[0]) {
            ver_status = "unknown"; ver_match = 0;
        } else {
            int eq = cr_version_equal(e->app_ver, game_ver);
            ver_status = eq ? "match" : "mismatch";
            ver_match  = eq;
        }

        int applied = game_running && patch_is_applied(title_id, e->entry_id, pid);

        /* Partial-overwrite warning (correct condition per spec) */
        int has_partial_ow = 0;
        for (int li = 0; li < e->line_count; li++) {
            if (cr_patch_line_has_trailing_pattern_bytes(&e->lines[li])) {
                has_partial_ow = 1; break;
            }
        }

        int has_mask = 0;
        for (int li = 0; li < e->line_count; li++) {
            if (e->lines[li].type == PATCH_LINE_MASK) { has_mask = 1; break; }
        }

        /* canApply and blockReason */
        int can_apply = 0;
        const char *block_reason = "";

        if (!game_running) {
            block_reason = "missing_game";
        } else if (e->line_count == 0) {
            block_reason = "no_supported_lines";
        } else if (e->has_unsupported) {
            block_reason = "unsupported_line_type";
        } else if (!ver_match) {
            block_reason = "version_mismatch";
        } else if (applied) {
            /* applied — no blockReason, canApply=false because already done */
        } else {
            can_apply = 1;
        }

        /* warnings array */
        cJSON *warnings = cJSON_CreateArray();
        if (has_mask)        cJSON_AddItemToArray(warnings, cJSON_CreateString("mask_scan"));
        if (has_partial_ow)  cJSON_AddItemToArray(warnings, cJSON_CreateString("partial_overwrite"));
        if (!ver_match && !e->is_mask_ver && e->app_ver[0])
            cJSON_AddItemToArray(warnings, cJSON_CreateString("force_required_for_version_mismatch"));

        /* unsupportedTypes array */
        cJSON *unsup_arr = cJSON_CreateArray();
        for (int k = 0; k < e->unsupported_type_count; k++)
            cJSON_AddItemToArray(unsup_arr, cJSON_CreateString(e->unsupported_types[k]));

        /* source file basename */
        const char *src_file = e->source_path[0] ? path_basename_ptr(e->source_path) : "";

        cJSON *ej = cJSON_CreateObject();
        cJSON_AddNumberToObject(ej, "index",               i);
        cJSON_AddStringToObject(ej, "entryId",             e->entry_id);
        cJSON_AddStringToObject(ej, "name",                e->name);
        cJSON_AddStringToObject(ej, "author",              e->author);
        cJSON_AddStringToObject(ej, "note",                e->note);
        cJSON_AddStringToObject(ej, "titleId",             title_id);
        cJSON_AddStringToObject(ej, "appVer",              e->app_ver);
        cJSON_AddBoolToObject(ej,   "appVerMatches",       ver_match);
        cJSON_AddStringToObject(ej, "versionStatus",       ver_status);
        cJSON_AddNumberToObject(ej, "lineCount",           e->line_count);
        cJSON_AddNumberToObject(ej, "supportedLineCount",  e->line_count);
        cJSON_AddNumberToObject(ej, "unsupportedCount",    e->unsupported_count);
        cJSON_AddItemToObject(ej,   "unsupportedTypes",    unsup_arr);
        cJSON_AddBoolToObject(ej,   "hasUnsupported",      e->has_unsupported);
        cJSON_AddBoolToObject(ej,   "hasMask",             has_mask);
        cJSON_AddBoolToObject(ej,   "hasPartialOverwriteWarning", has_partial_ow);
        cJSON_AddBoolToObject(ej,   "canApply",            can_apply);
        cJSON_AddStringToObject(ej, "blockReason",         block_reason);
        cJSON_AddItemToObject(ej,   "warnings",            warnings);
        cJSON_AddBoolToObject(ej,   "applied",             applied);
        cJSON_AddStringToObject(ej, "sourcePath",          e->source_path);
        cJSON_AddStringToObject(ej, "sourceFile",          src_file);
        cJSON_AddStringToObject(ej, "sourceKind",          source_kind_str(e->source_kind));
        cJSON_AddStringToObject(ej, "sourceKindLabel",     source_kind_file_label(e->source_kind));
        cJSON_AddNumberToObject(ej, "metadataIndex",       e->metadata_index);
        cJSON_AddItemToArray(arr, ej);
    }

    char *txt = cJSON_PrintUnformatted(out);
    cJSON_Delete(out);
    free_merged(m);
    http_send_json(fd, 200, txt ? txt : "{\"ok\":false}");
    free(txt);
}

/* ── /api/patches/apply ──────────────────────────────────────────────────── */

static void
handle_patches_apply(int fd, const char *query) {
    char title_id[16]  = {0};
    char entry_id[32]  = {0};
    char idx_str[16]   = {0};
    char force_str[8]  = {0};

    if (!query || query_value(query, "titleId", title_id, sizeof(title_id)) < 0 || !title_id[0]) {
        http_send_json(fd, 400, "{\"ok\":false,\"error\":\"missing titleId\"}");
        return;
    }

    int have_entry_id = (query_value(query, "entryId", entry_id, sizeof(entry_id)) == 0 && entry_id[0]);
    int have_index    = (query_value(query, "index",   idx_str,  sizeof(idx_str))  == 0 && idx_str[0]);

    if (!have_entry_id && !have_index) {
        http_send_json(fd, 400, "{\"ok\":false,\"error\":\"missing entryId or index\"}");
        return;
    }

    int force = (query_value(query, "force", force_str, sizeof(force_str)) == 0 &&
                 strcmp(force_str, "1") == 0);

    merged_patches_t *m = build_merged_patches(title_id);
    if (!m) { http_send_oom(fd); return; }
    if (m->count == 0) {
        free_merged(m);
        http_send_json(fd, 404, "{\"ok\":false,\"error\":\"patch_not_found\"}");
        return;
    }

    /* Find entry by entryId (preferred) or index (fallback) */
    patch_entry_t *e = NULL;
    int matched_idx = -1;
    if (have_entry_id) {
        for (int i = 0; i < m->count; i++) {
            if (strcmp(m->entries[i].entry_id, entry_id) == 0) {
                e = &m->entries[i]; matched_idx = i; break;
            }
        }
    }
    if (!e && have_index) {
        int idx = atoi(idx_str);
        if (idx >= 0 && idx < m->count) {
            e = &m->entries[idx]; matched_idx = idx;
        }
    }
    if (!e) {
        free_merged(m);
        http_send_json(fd, 404, "{\"ok\":false,\"error\":\"entry_not_found\"}");
        return;
    }

    /* Block: no supported lines */
    if (e->line_count == 0) {
        cJSON *ej = cJSON_CreateObject();
        cJSON_AddBoolToObject(ej, "ok", 0);
        cJSON_AddBoolToObject(ej, "applied", 0);
        cJSON_AddStringToObject(ej, "error", "no_supported_lines");
        cJSON_AddStringToObject(ej, "message", "Entry has no supported patch lines");
        char *txt = cJSON_PrintUnformatted(ej); cJSON_Delete(ej);
        free_merged(m);
        http_send_json(fd, 409, txt ? txt : "{\"ok\":false}");
        free(txt); return;
    }

    /* Block: unsupported line types — force=1 cannot bypass this */
    if (e->has_unsupported) {
        cJSON *ej = cJSON_CreateObject();
        cJSON_AddBoolToObject(ej, "ok", 0);
        cJSON_AddBoolToObject(ej, "applied", 0);
        cJSON_AddStringToObject(ej, "error", "unsupported_line_type");
        cJSON_AddStringToObject(ej, "message",
            "Patch contains unsupported line types and was not applied");
        cJSON *ut = cJSON_CreateArray();
        for (int k = 0; k < e->unsupported_type_count; k++)
            cJSON_AddItemToArray(ut, cJSON_CreateString(e->unsupported_types[k]));
        cJSON_AddItemToObject(ej, "unsupportedTypes", ut);
        char *txt = cJSON_PrintUnformatted(ej); cJSON_Delete(ej);
        free_merged(m);
        http_send_json(fd, 409, txt ? txt : "{\"ok\":false}");
        free(txt); return;
    }

    /* Version guard — force=1 can bypass */
    running_game_state_t apply_st;
    running_state_get(&apply_st);
    if (!force && !e->is_mask_ver && e->app_ver[0]) {
        char game_ver[32] = {0};
        if (apply_st.running) {
            const char *ver = apply_st.content_version[0] ? apply_st.content_version
                                                           : apply_st.app_version;
            if (ver[0]) snprintf(game_ver, sizeof(game_ver), "%s", ver);
        }
        if (game_ver[0] && !cr_version_equal(e->app_ver, game_ver)) {
            cJSON *ej = cJSON_CreateObject();
            cJSON_AddBoolToObject(ej, "ok", 0);
            cJSON_AddBoolToObject(ej, "applied", 0);
            cJSON_AddStringToObject(ej, "error", "version_mismatch");
            cJSON_AddStringToObject(ej, "patchVer", e->app_ver);
            cJSON_AddStringToObject(ej, "gameVer",  game_ver);
            char msg[128];
            snprintf(msg, sizeof(msg),
                     "Patch requires v%s but game is v%s. Use force=1 to override.",
                     e->app_ver, game_ver);
            cJSON_AddStringToObject(ej, "message", msg);
            char *txt = cJSON_PrintUnformatted(ej); cJSON_Delete(ej);
            free_merged(m);
            http_send_json(fd, 409, txt ? txt : "{\"ok\":false}");
            free(txt); return;
        }
    }

    /* Apply */
    patch_apply_result_t res;
    int rc = patch_apply_entry_ex(title_id, e, &res);

    if (rc != 0) {
        cJSON *ej = cJSON_CreateObject();
        cJSON_AddBoolToObject(ej, "ok",              0);
        cJSON_AddBoolToObject(ej, "applied",         0);
        cJSON_AddStringToObject(ej, "entryId",       e->entry_id);
        cJSON_AddStringToObject(ej, "error",         res.error);
        cJSON_AddStringToObject(ej, "message",       res.message);
        cJSON_AddBoolToObject(ej, "rolledBack",      res.rolled_back);
        cJSON_AddNumberToObject(ej, "rollbackErrors", res.rollback_errors);
        cJSON_AddNumberToObject(ej, "verifyFailCount", res.verify_fail_count);
        char *txt = cJSON_PrintUnformatted(ej); cJSON_Delete(ej);
        free_merged(m);
        http_send_json(fd, 500, txt ? txt : "{\"ok\":false}");
        free(txt); return;
    }

    /* Mark applied with the pid patch_apply_entry_ex used; apply_st.pid can be stale right after launch. */
    patch_mark_applied(title_id, e->entry_id, res.pid);

    /* Partial-overwrite warning (informational) */
    int had_partial_ow = 0;
    for (int li = 0; li < e->line_count; li++) {
        if (cr_patch_line_has_trailing_pattern_bytes(&e->lines[li])) {
            had_partial_ow = 1; break;
        }
    }

    cJSON *ej = cJSON_CreateObject();
    cJSON_AddBoolToObject(ej, "ok",            1);
    cJSON_AddBoolToObject(ej, "applied",       1);
    cJSON_AddStringToObject(ej, "entryId",     e->entry_id);
    cJSON_AddStringToObject(ej, "name",        e->name);
    cJSON_AddNumberToObject(ej, "lineCount",   e->line_count);
    cJSON_AddNumberToObject(ej, "verifyFailCount", 0);
    cJSON_AddNumberToObject(ej, "skippedLines", res.skipped_lines);
    cJSON_AddBoolToObject(ej, "rolledBack",    0);
    cJSON_AddBoolToObject(ej, "hadPartialOverwrite", had_partial_ow);
    if (had_partial_ow)
        cJSON_AddStringToObject(ej, "warning",
            "Mask patch value is shorter than the scanned pattern. "
            "Original bytes remain after the write site and may cause a SIGILL/crash. "
            "Check klog for mask_partial_overwrite details.");

    char *txt = cJSON_PrintUnformatted(ej);
    cJSON_Delete(ej);
    free_merged(m);
    http_send_json(fd, 200, txt ? txt : "{\"ok\":true}");
    free(txt);
    (void)matched_idx;
}

/* ── /api/patches/restore ────────────────────────────────────────────────── */

static void
handle_patches_restore(int fd, const char *query) {
    char title_id[16] = {0};
    char entry_id[32] = {0};

    if (!query || query_value(query, "titleId", title_id, sizeof(title_id)) < 0 || !title_id[0]) {
        http_send_json(fd, 400, "{\"ok\":false,\"error\":\"missing titleId\"}");
        return;
    }
    if (query_value(query, "entryId", entry_id, sizeof(entry_id)) != 0 || !entry_id[0]) {
        http_send_json(fd, 400, "{\"ok\":false,\"error\":\"missing entryId\"}");
        return;
    }

    /* Live query, same as patch_apply_entry_ex; the cached running state lags by up to 500 ms. */
    pid_t pid = -1;
    char running_title[16] = {0};
    intptr_t base = 0;
    int app_id = 0;
    if (get_running_game_ex(&pid, running_title, sizeof(running_title), &base, &app_id) != 0) {
        http_send_json(fd, 409, "{\"ok\":false,\"error\":\"missing_game\","
                                "\"message\":\"No game is currently running\"}");
        return;
    }

    if (!patch_is_applied(title_id, entry_id, pid)) {
        http_send_json(fd, 409, "{\"ok\":false,\"error\":\"not_applied\","
                                "\"message\":\"Patch is not currently applied\"}");
        return;
    }

    patch_apply_result_t res;
    int rc = patch_restore_entry(title_id, entry_id, pid, &res);

    if (rc != 0) {
        cJSON *ej = cJSON_CreateObject();
        cJSON_AddBoolToObject(ej,   "ok",      0);
        cJSON_AddStringToObject(ej, "error",   res.error[0]   ? res.error   : "restore_failed");
        cJSON_AddStringToObject(ej, "message", res.message[0] ? res.message : "Restore failed");
        char *txt = cJSON_PrintUnformatted(ej); cJSON_Delete(ej);
        http_send_json(fd, 500, txt ? txt : "{\"ok\":false}");
        free(txt);
        return;
    }

    cJSON *ej = cJSON_CreateObject();
    cJSON_AddBoolToObject(ej,   "ok",      1);
    cJSON_AddStringToObject(ej, "entryId", entry_id);
    cJSON_AddStringToObject(ej, "message", "Patch restored to original bytes");
    char *txt = cJSON_PrintUnformatted(ej);
    cJSON_Delete(ej);
    http_send_json(fd, 200, txt ? txt : "{\"ok\":true}");
    free(txt);
}

/* ── /api/patches/files — XML patch file management ─────────────────────── */

static void
handle_patches_files_list(int fd) {
    char *json = patch_files_list_json();
    if (!json) { http_send_oom(fd); return; }
    http_send_json(fd, 200, json);
    free(json);
}

static void
handle_patches_files_toggle(int fd, const char *query) {
    char name[256] = {0};
    char dir_s[4]  = {0};
    char on_s[4]   = {0};
    if (!query ||
        query_value(query, "name", name, sizeof(name)) != 0 || !name[0] ||
        query_value(query, "on",   on_s, sizeof(on_s))  != 0 || !on_s[0]) {
        http_send_json(fd, 400, "{\"ok\":false,\"error\":\"missing name or on\"}");
        return;
    }
    int dir_idx = (query_value(query, "dir", dir_s, sizeof(dir_s)) == 0) ? atoi(dir_s) : 0;
    int rc = patch_file_toggle(name, atoi(on_s), dir_idx);
    if (rc == 0) patch_index_invalidate();
    http_send_json(fd, 200,
        rc == 0 ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"rename failed\"}");
}

static void
handle_patches_files_delete(int fd, const char *query) {
    char name[256] = {0};
    char dir_s[4]  = {0};
    if (!query || query_value(query, "name", name, sizeof(name)) != 0 || !name[0]) {
        http_send_json(fd, 400, "{\"ok\":false,\"error\":\"missing name\"}");
        return;
    }
    int dir_idx = (query_value(query, "dir", dir_s, sizeof(dir_s)) == 0) ? atoi(dir_s) : 0;
    int rc = patch_file_delete(name, dir_idx);
    if (rc == 0) patch_index_invalidate();
    http_send_json(fd, 200,
        rc == 0 ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"delete failed\"}");
}

/* ── Router ─────────────────────────────────────────────────────────────── */

int
cr_api_patches_handle(int fd, const char *method, const char *path,
                       const char *query, const char *body, size_t body_len) {
    (void)method; (void)body; (void)body_len;

    if (strcmp(path, "/api/patches") == 0) {
        handle_patches_list(fd, query);
        return 1;
    }
    if (strcmp(path, "/api/patches/apply") == 0) {
        handle_patches_apply(fd, query);
        return 1;
    }
    if (strcmp(path, "/api/patches/restore") == 0) {
        handle_patches_restore(fd, query);
        return 1;
    }
    if (strcmp(path, "/api/patches/rescan") == 0) {
        patch_index_invalidate();
        http_send_json(fd, 200,
            "{\"ok\":true,\"message\":\"patch index cleared; applied-state reset\"}");
        return 1;
    }
    if (strcmp(path, "/api/patches/files") == 0) {
        handle_patches_files_list(fd);
        return 1;
    }
    if (strcmp(path, "/api/patches/files/toggle") == 0) {
        handle_patches_files_toggle(fd, query);
        return 1;
    }
    if (strcmp(path, "/api/patches/files/delete") == 0) {
        handle_patches_files_delete(fd, query);
        return 1;
    }
    return 0;
}
