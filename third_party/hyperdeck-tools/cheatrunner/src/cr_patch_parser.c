#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "cr_log.h"
#include "cr_mdbg.h"
#include "cr_memory.h"
#include "cr_notifications.h"
#include "cr_paths.h"
#include "cr_patch_parser.h"
#include "cr_game_monitor.h"
#include "cr_cheats.h"
#include "cr_titles.h"
#include "pt.h"

/* ── Applied-state tracking ─────────────────────────────────────────────── */

#define APPLIED_MAX 256
typedef struct {
    char  title_id[16];
    char  entry_id[PATCH_ENTRY_ID_LEN];
    pid_t pid;
} applied_rec_t;

static applied_rec_t   g_applied[APPLIED_MAX];
static int             g_applied_n = 0;
static pthread_mutex_t g_applied_lock = PTHREAD_MUTEX_INITIALIZER;

void
patch_mark_applied(const char *title_id, const char *entry_id, pid_t pid) {
    pthread_mutex_lock(&g_applied_lock);
    for (int i = 0; i < g_applied_n; i++) {
        if (g_applied[i].pid == pid &&
            strcmp(g_applied[i].title_id, title_id) == 0 &&
            strcmp(g_applied[i].entry_id,  entry_id)  == 0) {
            pthread_mutex_unlock(&g_applied_lock);
            return;
        }
    }
    if (g_applied_n < APPLIED_MAX) {
        snprintf(g_applied[g_applied_n].title_id, 16,               "%s", title_id);
        snprintf(g_applied[g_applied_n].entry_id,  PATCH_ENTRY_ID_LEN, "%s", entry_id);
        g_applied[g_applied_n].pid = pid;
        g_applied_n++;
    }
    pthread_mutex_unlock(&g_applied_lock);
}

int
patch_is_applied(const char *title_id, const char *entry_id, pid_t pid) {
    pthread_mutex_lock(&g_applied_lock);
    for (int i = 0; i < g_applied_n; i++) {
        if (g_applied[i].pid == pid &&
            strcmp(g_applied[i].title_id, title_id) == 0 &&
            strcmp(g_applied[i].entry_id,  entry_id)  == 0) {
            pthread_mutex_unlock(&g_applied_lock);
            return 1;
        }
    }
    pthread_mutex_unlock(&g_applied_lock);
    return 0;
}

void
patch_clear_for_pid(pid_t pid) {
    pthread_mutex_lock(&g_applied_lock);
    for (int i = g_applied_n - 1; i >= 0; i--) {
        if (g_applied[i].pid == pid)
            g_applied[i] = g_applied[--g_applied_n];
    }
    pthread_mutex_unlock(&g_applied_lock);
}

void
patch_mark_unapplied(const char *title_id, const char *entry_id, pid_t pid) {
    pthread_mutex_lock(&g_applied_lock);
    for (int i = g_applied_n - 1; i >= 0; i--) {
        if (g_applied[i].pid == pid &&
            strcmp(g_applied[i].title_id, title_id) == 0 &&
            strcmp(g_applied[i].entry_id,  entry_id)  == 0) {
            g_applied[i] = g_applied[--g_applied_n];
            break;
        }
    }
    pthread_mutex_unlock(&g_applied_lock);
}

void
patch_clear_all_applied(void) {
    pthread_mutex_lock(&g_applied_lock);
    g_applied_n = 0;
    pthread_mutex_unlock(&g_applied_lock);
    cr_log("info", "patches", "applied-state cleared (rescan)");
}

typedef struct {
    uint64_t address;
    uint8_t  old_bytes[PATCH_LINE_MAX_BYTES];
    size_t   len;
    int      valid;
} cr_patch_backup_t;

/* ── Patch backup store ───────────────────────────────────────────────────── */

#define PATCH_BACKUP_STORE_MAX 16  /* simultaneous applied patches with backups */

typedef struct {
    char     title_id[16];
    char     entry_id[PATCH_ENTRY_ID_LEN];
    char     name[128];
    pid_t    pid;
    struct {
        uint64_t address;
        uint8_t  bytes[PATCH_LINE_MAX_BYTES];
        size_t   len;
    } lines[PATCH_MAX_LINES];
    int      line_count;
} patch_backup_rec_t;

static patch_backup_rec_t  g_patch_backups[PATCH_BACKUP_STORE_MAX];
static int                 g_patch_backup_n = 0;
static pthread_mutex_t     g_patch_backup_lock = PTHREAD_MUTEX_INITIALIZER;

static void
patch_store_backup(const char *title_id, const char *entry_id, const char *name,
                   pid_t pid, const cr_patch_backup_t *bkups, int count) {
    if (!title_id || !entry_id || count <= 0) return;
    pthread_mutex_lock(&g_patch_backup_lock);
    int slot = -1;
    for (int i = 0; i < g_patch_backup_n; i++) {
        if (g_patch_backups[i].pid == pid &&
            strcmp(g_patch_backups[i].title_id, title_id) == 0 &&
            strcmp(g_patch_backups[i].entry_id,  entry_id)  == 0) {
            slot = i; break;
        }
    }
    if (slot < 0) {
        if (g_patch_backup_n >= PATCH_BACKUP_STORE_MAX) {
            pthread_mutex_unlock(&g_patch_backup_lock);
            cr_log("warn", "patches", "backup_store_full max=%d entry='%s'",
                   PATCH_BACKUP_STORE_MAX, name ? name : "?");
            return;
        }
        slot = g_patch_backup_n++;
    }
    patch_backup_rec_t *rec = &g_patch_backups[slot];
    memset(rec, 0, sizeof(*rec));
    snprintf(rec->title_id, sizeof(rec->title_id), "%s", title_id);
    snprintf(rec->entry_id, sizeof(rec->entry_id), "%s", entry_id);
    snprintf(rec->name,     sizeof(rec->name),     "%s", name ? name : "");
    rec->pid = pid;
    int n = count < PATCH_MAX_LINES ? count : PATCH_MAX_LINES;
    for (int i = 0; i < n; i++) {
        if (!bkups[i].valid) continue;
        rec->lines[rec->line_count].address = bkups[i].address;
        rec->lines[rec->line_count].len     = bkups[i].len;
        memcpy(rec->lines[rec->line_count].bytes, bkups[i].old_bytes, bkups[i].len);
        rec->line_count++;
    }
    pthread_mutex_unlock(&g_patch_backup_lock);
}

void
patch_clear_backups_for_pid(pid_t pid) {
    pthread_mutex_lock(&g_patch_backup_lock);
    for (int i = g_patch_backup_n - 1; i >= 0; i--) {
        if (g_patch_backups[i].pid == pid)
            g_patch_backups[i] = g_patch_backups[--g_patch_backup_n];
    }
    pthread_mutex_unlock(&g_patch_backup_lock);
}

int
patch_restore_entry(const char *title_id, const char *entry_id, pid_t pid,
                    patch_apply_result_t *result) {
    if (result) memset(result, 0, sizeof(*result));

    pthread_mutex_lock(&g_patch_backup_lock);
    int slot = -1;
    for (int i = 0; i < g_patch_backup_n; i++) {
        if (g_patch_backups[i].pid == pid &&
            strcmp(g_patch_backups[i].title_id, title_id) == 0 &&
            strcmp(g_patch_backups[i].entry_id,  entry_id)  == 0) {
            slot = i; break;
        }
    }
    if (slot < 0) {
        pthread_mutex_unlock(&g_patch_backup_lock);
        if (result) {
            snprintf(result->error,   sizeof(result->error),   "no_backup");
            snprintf(result->message, sizeof(result->message), "no backup found for this patch");
        }
        return -1;
    }
    /* Heap-allocate the backup (~17 KB); as a local it would overflow the thread stack. */
    patch_backup_rec_t *rec = malloc(sizeof(*rec));
    if (!rec) {
        pthread_mutex_unlock(&g_patch_backup_lock);
        if (result) {
            snprintf(result->error,   sizeof(result->error),   "oom");
            snprintf(result->message, sizeof(result->message), "out of memory");
        }
        return -1;
    }
    *rec = g_patch_backups[slot];
    pthread_mutex_unlock(&g_patch_backup_lock);

    /* Block on the apply gate (fine, this is a user action) and flag g_cheat_applying for other subsystems. */
    pthread_mutex_lock(&g_cheat_apply_lock);
    g_cheat_applying = 1;

    /* mdbg writes need no ptrace attach; just confirm the process is still alive. */
    if (kill(pid, 0) != 0 && errno == ESRCH) {
        g_cheat_applying = 0;
        pthread_mutex_unlock(&g_cheat_apply_lock);
        free(rec);
        if (result) {
            snprintf(result->error,   sizeof(result->error),   "attach_failed");
            snprintf(result->message, sizeof(result->message), "game has exited");
        }
        return -1;
    }

    int errors = 0;
    for (int i = 0; i < rec->line_count; i++) {
        int wrc = write_process_memory_forced(pid, (intptr_t)rec->lines[i].address,
                                              rec->lines[i].bytes, rec->lines[i].len);
        if (wrc != 0) {
            cr_log("warn", "patches", "restore_line_failed line=%d addr=0x%llx rc=%d entry='%s'",
                   i, (long long)rec->lines[i].address, wrc, rec->name);
            errors++;
        }
    }
    g_cheat_applying = 0;
    pthread_mutex_unlock(&g_cheat_apply_lock);

    /* Remove backup record regardless of errors */
    pthread_mutex_lock(&g_patch_backup_lock);
    for (int i = 0; i < g_patch_backup_n; i++) {
        if (g_patch_backups[i].pid == pid &&
            strcmp(g_patch_backups[i].title_id, title_id) == 0 &&
            strcmp(g_patch_backups[i].entry_id,  entry_id)  == 0) {
            g_patch_backups[i] = g_patch_backups[--g_patch_backup_n];
            break;
        }
    }
    pthread_mutex_unlock(&g_patch_backup_lock);

    patch_mark_unapplied(title_id, entry_id, pid);

    cr_log(errors ? "warn" : "info", "patches",
           "restored '%s' title=%s pid=%d lines=%d errors=%d",
           rec->name, title_id, (int)pid, rec->line_count, errors);
    if (errors == 0)
        notify("CheatRunner: Patch restored: %s", rec->name);

    if (result) {
        result->ok             = (errors == 0) ? 1 : 0;
        result->rollback_errors = errors;
        if (errors > 0) {
            snprintf(result->error,   sizeof(result->error),   "partial_restore");
            snprintf(result->message, sizeof(result->message),
                     "%d of %d lines failed to restore", errors, rec->line_count);
        }
    }
    free(rec);
    return errors > 0 ? -1 : 0;
}

void
patch_restore_all_for_pid(pid_t pid, const char *title_id) {
    pthread_mutex_lock(&g_patch_backup_lock);
    int count = 0;
    for (int i = 0; i < g_patch_backup_n; i++)
        if (g_patch_backups[i].pid == pid) count++;
    pthread_mutex_unlock(&g_patch_backup_lock);
    if (count == 0) return;

    /* Quick liveness check — by the time game-stop fires, the process is usually dead. */
    if (kill(pid, 0) != 0 && errno == ESRCH) {
        cr_log("info", "patches",
               "game_stop: pid=%d already dead — %d patch(es) not restored (bytes gone with process)",
               (int)pid, count);
        patch_clear_backups_for_pid(pid);
        return;
    }

    cr_log("info", "patches",
           "game_stop: restoring %d applied patch(es) title=%s pid=%d",
           count, title_id ? title_id : "?", (int)pid);

    /* trylock, not blocking: the monitor poll must not stall; if busy, defer to a later tick. */
    if (pthread_mutex_trylock(&g_cheat_apply_lock) != 0) {
        cr_log("info", "patches",
               "game_stop: apply busy, deferring restore of %d patch(es) pid=%d",
               count, (int)pid);
        return;
    }

    /* Snapshot names + entry_ids to iterate while lock is released per write */
    char entry_ids[PATCH_BACKUP_STORE_MAX][PATCH_ENTRY_ID_LEN];
    int snap_n = 0;
    pthread_mutex_lock(&g_patch_backup_lock);
    for (int i = 0; i < g_patch_backup_n && snap_n < PATCH_BACKUP_STORE_MAX; i++)
        if (g_patch_backups[i].pid == pid)
            snprintf(entry_ids[snap_n++], PATCH_ENTRY_ID_LEN, "%s", g_patch_backups[i].entry_id);
    pthread_mutex_unlock(&g_patch_backup_lock);

    for (int ei = 0; ei < snap_n; ei++) {
        pthread_mutex_lock(&g_patch_backup_lock);
        int slot = -1;
        for (int i = 0; i < g_patch_backup_n; i++) {
            if (g_patch_backups[i].pid == pid &&
                strcmp(g_patch_backups[i].entry_id, entry_ids[ei]) == 0) {
                slot = i; break;
            }
        }
        if (slot < 0) { pthread_mutex_unlock(&g_patch_backup_lock); continue; }
        patch_backup_rec_t *rec = malloc(sizeof(*rec));
        if (!rec) { pthread_mutex_unlock(&g_patch_backup_lock); continue; }
        *rec = g_patch_backups[slot];
        pthread_mutex_unlock(&g_patch_backup_lock);

        int errors = 0;
        for (int k = 0; k < rec->line_count; k++) {
            int wrc = write_process_memory_forced(pid, (intptr_t)rec->lines[k].address,
                                                  rec->lines[k].bytes, rec->lines[k].len);
            if (wrc != 0) errors++;
        }
        cr_log(errors ? "warn" : "info", "patches",
               "game_stop restore '%s' pid=%d lines=%d errors=%d",
               rec->name, (int)pid, rec->line_count, errors);
        free(rec);
    }

    patch_clear_backups_for_pid(pid);
    pthread_mutex_unlock(&g_cheat_apply_lock);
}

/* ── Helpers ─────────────────────────────────────────────────────────────── */

int
cr_patch_line_has_trailing_pattern_bytes(const patch_line_t *ln) {
    if (!ln) return 0;
    if (ln->type != PATCH_LINE_MASK) return 0;
    if (ln->match_offset < 0) return 0;
    if ((size_t)ln->match_offset >= ln->pattern_len) return 0;
    return ((size_t)ln->match_offset + ln->value_len < ln->pattern_len);
}

static uint32_t
fnv1a32(const char *data, size_t len) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; i++) {
        h ^= (uint8_t)data[i];
        h *= 16777619u;
    }
    return h;
}

void
cr_patch_compute_entry_id(patch_entry_t *entry) {
    char buf[1024];
    const char *ks = (entry->source_kind == CR_PATCH_SOURCE_CHEATRUNNER_PS5) ? "ps5" :
                     (entry->source_kind == CR_PATCH_SOURCE_CHEATRUNNER_XML)  ? "xml" : "elf";
    snprintf(buf, sizeof(buf), "%s|%s|%d|%s|%s|%d",
             entry->source_path, ks, entry->metadata_index,
             entry->name, entry->app_ver, entry->line_count);
    uint32_t h = fnv1a32(buf, strlen(buf));
    snprintf(entry->entry_id, PATCH_ENTRY_ID_LEN, "%08x", (unsigned)h);
}

/* ── Title-ID → XML path index ───────────────────────────────────────────── */

typedef struct {
    char                   title_id[16];
    char                   path[384];
    cr_patch_source_kind_t kind;
    int                    prio;   /* from CheatRunner_Patches, wins over other sources */
} idx_entry_t;

static idx_entry_t     g_idx[PATCH_NAME_DIR_MAX];
static int             g_idx_n     = 0;
static int             g_idx_built = 0;
static time_t          g_idx_sig   = 0;
static pthread_mutex_t g_idx_lock  = PTHREAD_MUTEX_INITIALIZER;

/* Changes whenever a file is added to or removed from any patch folder. */
static time_t
patch_dirs_signature(void) {
    static const char *const dirs[] = {CHEATRUNNER_PATCHES_PS5_DIR, CHEATRUNNER_PATCHES_XML_DIR, ELF_ARSENAL_PATCHES_XML_DIR, CHEATRUNNER_PATCHES_DIR};
    time_t sig = 0;
    for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
        struct stat st;
        if (stat(dirs[i], &st) == 0) sig = sig * 31 + st.st_mtime;
    }
    return sig;
}

/* Rebuilds the index on the next lookup without forgetting which patches are applied. */
void
patch_index_refresh(void) {
    pthread_mutex_lock(&g_idx_lock);
    g_idx_built = 0;
    g_idx_n     = 0;
    pthread_mutex_unlock(&g_idx_lock);
}

void
patch_index_invalidate(void) {
    pthread_mutex_lock(&g_idx_lock);
    g_idx_built = 0;
    g_idx_n     = 0;
    pthread_mutex_unlock(&g_idx_lock);
    patch_clear_all_applied();
}

/* ── XML patch file management ──────────────────────────────────────────────── */

static const struct {
    const char *dir;
    const char *source;
} k_pf_dirs[] = {
    { CHEATRUNNER_PATCHES_PS5_DIR, "cheatrunner_ps5" },
    { CHEATRUNNER_PATCHES_XML_DIR, "cheatrunner_xml"  },
    { ELF_ARSENAL_PATCHES_XML_DIR, "elf_arsenal_xml"  },
    { CHEATRUNNER_PATCHES_DIR,     "cheatrunner_xml"  },
};
#define K_PF_NDIRS ((int)(sizeof(k_pf_dirs)/sizeof(k_pf_dirs[0])))

static int
pf_valid_name(const char *s) {
    if (!s || !*s) return 0;
    for (const char *p = s; *p; p++) {
        char c = *p;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')
            continue;
        return 0;
    }
    size_t n = strlen(s);
    return (n > 4 && strcmp(s + n - 4, ".xml") == 0) ||
           (n > 8 && strcmp(s + n - 8, ".xml.off") == 0);
}

char *
patch_files_list_json(void) {
    size_t cap = 512;
    char *out  = malloc(cap);
    if (!out) return NULL;
    size_t pos = 0;
    out[pos++] = '[';
    int first = 1;

    for (int di = 0; di < K_PF_NDIRS; di++) {
        DIR *d = opendir(k_pf_dirs[di].dir);
        if (!d) continue;
        struct dirent *de;
        while ((de = readdir(d))) {
            const char *n = de->d_name;
            size_t nl = strlen(n);
            int is_xml = (nl > 4  && strcasecmp(n + nl - 4, ".xml")     == 0);
            int is_off = (nl > 8  && strcasecmp(n + nl - 8, ".xml.off") == 0);
            if (!is_xml && !is_off) continue;

            /* display name strips the extension */
            char display[384];
            size_t dl = is_off ? nl - 8 : nl - 4;
            if (dl >= sizeof(display)) dl = sizeof(display) - 1;
            memcpy(display, n, dl); display[dl] = '\0';

            /* JSON-escape display and file names */
            char esc_disp[768], esc_file[768];
            size_t ei = 0;
            for (size_t j = 0; j < dl && ei < sizeof(esc_disp) - 2; j++) {
                if (display[j] == '"' || display[j] == '\\') esc_disp[ei++] = '\\';
                esc_disp[ei++] = display[j];
            }
            esc_disp[ei] = '\0';
            ei = 0;
            for (size_t j = 0; j < nl && ei < sizeof(esc_file) - 2; j++) {
                if (n[j] == '"' || n[j] == '\\') esc_file[ei++] = '\\';
                esc_file[ei++] = n[j];
            }
            esc_file[ei] = '\0';

            char entry[1024];
            int en = snprintf(entry, sizeof(entry),
                "%s{\"name\":\"%s\",\"file\":\"%s\",\"enabled\":%s,\"dir\":%d,\"source\":\"%s\"}",
                first ? "" : ",",
                esc_disp, esc_file,
                is_xml ? "true" : "false",
                di, k_pf_dirs[di].source);
            if (en <= 0 || (size_t)en >= sizeof(entry)) continue;
            first = 0;

            while (pos + (size_t)en + 4 > cap) {
                cap *= 2;
                char *nb = realloc(out, cap);
                if (!nb) { closedir(d); free(out); return NULL; }
                out = nb;
            }
            memcpy(out + pos, entry, (size_t)en);
            pos += (size_t)en;
        }
        closedir(d);
    }

    if (pos + 3 > cap) {
        char *nb = realloc(out, cap + 4);
        if (!nb) { free(out); return NULL; }
        out = nb;
    }
    out[pos++] = ']';
    out[pos]   = '\0';
    return out;
}

int
patch_file_toggle(const char *name, int on, int dir_idx) {
    if (!pf_valid_name(name)) return -1;
    if (dir_idx < 0 || dir_idx >= K_PF_NDIRS) return -1;
    const char *dir = k_pf_dirs[dir_idx].dir;
    char from[512], to[512];
    size_t nl = strlen(name);
    if (on) {
        if (nl < 8 || strcmp(name + nl - 8, ".xml.off") != 0) return -1;
        /* build base: strip ".off" (4 chars) */
        char base[512];
        size_t bl = nl - 4;
        if (bl >= sizeof(base)) return -1;
        memcpy(base, name, bl); base[bl] = '\0';
        snprintf(from, sizeof(from), "%s/%s", dir, name);
        snprintf(to,   sizeof(to),   "%s/%s", dir, base);
    } else {
        if (nl < 4 || strcmp(name + nl - 4, ".xml") != 0) return -1;
        snprintf(from, sizeof(from), "%s/%s",     dir, name);
        snprintf(to,   sizeof(to),   "%s/%s.off", dir, name);
    }
    return rename(from, to) == 0 ? 0 : -1;
}

int
patch_file_delete(const char *name, int dir_idx) {
    if (!pf_valid_name(name)) return -1;
    if (dir_idx < 0 || dir_idx >= K_PF_NDIRS) return -1;
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", k_pf_dirs[dir_idx].dir, name);
    return unlink(path) == 0 ? 0 : -1;
}

/* Adds every <ID> in the <TitleID> block to the index as (id, path, kind); duplicate (title_id, path)
 * pairs are skipped. */
static void
index_xml_file(const char *xml_path, cr_patch_source_kind_t kind,
               const char *xml_text) {
    const char *ts = strstr(xml_text, "<TitleID>");
    const char *te = ts ? strstr(ts, "</TitleID>") : NULL;
    if (!ts || !te) return;

    const char *p = ts;
    while (p < te) {
        const char *id_open  = strstr(p, "<ID>");
        const char *id_close = id_open ? strstr(id_open + 4, "</ID>") : NULL;
        if (!id_open || !id_close || id_open >= te) break;
        id_open += 4;
        size_t id_len = (size_t)(id_close - id_open);
        if (id_len > 0 && id_len < 16) {
            char tid[16];
            memcpy(tid, id_open, id_len);
            tid[id_len] = '\0';
            /* Dedup by (title_id, path) */
            int found = 0;
            for (int i = 0; i < g_idx_n; i++) {
                if (strcmp(g_idx[i].title_id, tid) == 0 &&
                    strcmp(g_idx[i].path,     xml_path) == 0) {
                    found = 1; break;
                }
            }
            if (!found && g_idx_n < PATCH_NAME_DIR_MAX) {
                snprintf(g_idx[g_idx_n].title_id, 16,             "%s", tid);
                snprintf(g_idx[g_idx_n].path,     sizeof(g_idx[0].path), "%s", xml_path);
                g_idx[g_idx_n].kind = kind;
                g_idx[g_idx_n].prio = strstr(xml_path, "/xml/cheatrunner/") || strstr(xml_path, "/xml_prospero/cheatrunner/");
                g_idx_n++;
            }
        }
        p = id_close + 5;
    }
}

/* Compare two C-string rows of width `stride` bytes for qsort. */
static int
cmp_strbuf(const void *a, const void *b) {
    return strcmp((const char *)a, (const char *)b);
}

#define DIRSCAN_MAX  1024
#define DIRSCAN_NAME 256
#define DIRSCAN_SUBS 64
#define DIRSCAN_DEPTH 2

/* Indexes every .xml in dir and in its subfolders (a downloaded repo is often dropped in as a folder). */
static void
scan_dir_into_index(const char *dir, cr_patch_source_kind_t kind, int depth) {
    DIR *dp = opendir(dir);
    if (!dp) {
        cr_log("debug", "patches", "scan_dir: %s not found or not accessible", dir);
        return;
    }

    char (*names)[DIRSCAN_NAME] = calloc(DIRSCAN_MAX, DIRSCAN_NAME);
    char (*subs)[DIRSCAN_NAME] = calloc(DIRSCAN_SUBS, DIRSCAN_NAME);
    if (!names || !subs) {
        free(names);
        free(subs);
        closedir(dp);
        return;
    }
    int ncount = 0, scount = 0, dropped = 0;

    struct dirent *ent;
    while ((ent = readdir(dp)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        if (ends_with_case(ent->d_name, ".xml")) {
            if (ncount < DIRSCAN_MAX) snprintf(names[ncount++], DIRSCAN_NAME, "%s", ent->d_name);
            else dropped++;
        } else if (depth > 0 && scount < DIRSCAN_SUBS && ent->d_type == DT_DIR) {
            snprintf(subs[scount++], DIRSCAN_NAME, "%s", ent->d_name);
        }
    }
    closedir(dp);
    if (dropped) cr_log("warn", "patches", "scan_dir: %s has %d more .xml file(s) than the %d that are read", dir, dropped, DIRSCAN_MAX);

    qsort(names, (size_t)ncount, DIRSCAN_NAME, cmp_strbuf);

    /* Read up to 128 KB per file to cover the <TitleID> block. */
    static char buf[128 * 1024];
    for (int fi = 0; fi < ncount; fi++) {
        char full[512];
        snprintf(full, sizeof(full), "%s/%s", dir, names[fi]);
        FILE *f = fopen(full, "r");
        if (!f) continue;
        size_t n = fread(buf, 1, sizeof(buf) - 1, f);
        fclose(f);
        buf[n] = '\0';
        /* Skip files with no TitleID block */
        if (!strstr(buf, "<TitleID>")) {
            cr_log("info", "patches", "index_skip no_title_id: %s", full);
            continue;
        }
        index_xml_file(full, kind, buf);
    }
    free(names);

    for (int si = 0; si < scount; si++) {
        char sub[512];
        snprintf(sub, sizeof(sub), "%s/%s", dir, subs[si]);
        scan_dir_into_index(sub, kind, depth - 1);
    }
    free(subs);
}

static void
build_index(void) {
    g_idx_n = 0;
    scan_dir_into_index(CHEATRUNNER_PATCHES_PS5_DIR, CR_PATCH_SOURCE_CHEATRUNNER_PS5, DIRSCAN_DEPTH);
    scan_dir_into_index(CHEATRUNNER_PATCHES_XML_DIR,  CR_PATCH_SOURCE_CHEATRUNNER_XML, DIRSCAN_DEPTH);
    scan_dir_into_index(ELF_ARSENAL_PATCHES_XML_DIR,  CR_PATCH_SOURCE_ELF_ARSENAL_XML, DIRSCAN_DEPTH);
    /* Loose files only; xml/ and xml_prospero/ are already scanned above. */
    scan_dir_into_index(CHEATRUNNER_PATCHES_DIR,      CR_PATCH_SOURCE_CHEATRUNNER_XML, 0);
    g_idx_built = 1;
    g_idx_sig = patch_dirs_signature();
    cr_log("debug", "patches", "index built: %d entry/file pair(s)", g_idx_n);
}

int
patch_find_xmls_for_title(const char *title_id,
                          char out_paths[][384], cr_patch_source_kind_t out_kinds[],
                          int max) {
    if (!title_id || !out_paths || !out_kinds || max <= 0) return 0;
    pthread_mutex_lock(&g_idx_lock);
    if (!g_idx_built || g_idx_sig != patch_dirs_signature()) build_index();
    int found = 0;
    int has_prio = 0;
    for (int i = 0; i < g_idx_n; i++)
        if (g_idx[i].prio && strcmp(g_idx[i].title_id, title_id) == 0) has_prio = 1;
    for (int i = 0; i < g_idx_n && found < max; i++) {
        if (strcmp(g_idx[i].title_id, title_id) == 0 && (!has_prio || g_idx[i].prio)) {
            snprintf(out_paths[found], 384, "%s", g_idx[i].path);
            out_kinds[found] = g_idx[i].kind;
            found++;
        }
    }
    pthread_mutex_unlock(&g_idx_lock);
    return found;
}

int
patch_find_xml_for_title(const char *title_id, char *out_path, size_t out_sz) {
    char paths[1][384];
    cr_patch_source_kind_t kinds[1];
    if (patch_find_xmls_for_title(title_id, paths, kinds, 1) == 0) return -1;
    snprintf(out_path, out_sz, "%s", paths[0]);
    return 0;
}

/* ── XML helpers ─────────────────────────────────────────────────────────── */

static int
tag_attr(const char *tag, const char *tag_end,
         const char *name, char *out, size_t out_sz) {
    char pat[128];
    const char *p = NULL;

    snprintf(pat, sizeof(pat), " %s=\"", name);
    p = strstr(tag, pat);
    if (!p || p >= tag_end) {
        snprintf(pat, sizeof(pat), "%s=\"", name);
        p = strstr(tag, pat);
    }
    if (p && p < tag_end) {
        p += strlen(pat);
    } else {
        snprintf(pat, sizeof(pat), " %s=", name);
        p = strstr(tag, pat);
        if (!p || p >= tag_end) {
            snprintf(pat, sizeof(pat), "%s=", name);
            p = strstr(tag, pat);
            if (!p || p >= tag_end) return 0;
        }
        p += strlen(pat);
        while (p < tag_end && (*p == ' ' || *p == '\t')) p++;
        if (p >= tag_end || *p != '"') return 0;
        p++;
    }

    if (p >= tag_end) return 0;
    const char *end = p;
    while (end < tag_end && *end != '"') end++;
    size_t n = (size_t)(end - p);
    if (n >= out_sz) n = out_sz - 1;
    memcpy(out, p, n);
    out[n] = '\0';
    return 1;
}

static void
xml_decode(char *s) {
    static const struct { const char *ent; char ch; } entities[] = {
        { "&amp;",  '&' },
        { "&lt;",   '<' },
        { "&gt;",   '>' },
        { "&quot;", '"' },
        { "&apos;", '\'' },
        { NULL, 0 }
    };
    char *r = s, *w = s;
    while (*r) {
        int matched = 0;
        for (int i = 0; entities[i].ent; i++) {
            size_t el = strlen(entities[i].ent);
            if (strncmp(r, entities[i].ent, el) == 0) {
                *w++ = entities[i].ch;
                r   += el;
                matched = 1;
                break;
            }
        }
        if (!matched) {
            if (r[0] == '\\' && r[1] == 'n') { *w++ = ' '; r += 2; }
            else *w++ = *r++;
        }
    }
    *w = '\0';
}

/* ── Value type parsers ───────────────────────────────────────────────────── */

static int
parse_hex_bytes(const char *s, uint8_t *out, size_t *out_len, size_t max) {
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        uint64_t v = strtoull(s, NULL, 16);
        size_t hex_digits = strlen(s + 2);
        size_t nbytes = (hex_digits + 1) / 2;
        if (nbytes == 0) nbytes = 1;
        if (nbytes > 8) nbytes = 8; /* v only holds 64 bits; shifting by >=64 is UB */
        if (nbytes > max) return -1;
        for (size_t i = 0; i < nbytes; i++)
            out[i] = (uint8_t)(v >> (8 * i));
        *out_len = nbytes;
        return 0;
    }
    size_t w = 0;
    const char *p = s;
    int high = -1;
    while (*p) {
        char c = *p++;
        if (c == ' ' || c == '\t' || c == '-' || c == ':') {
            if (high >= 0) return -1;
            continue;
        }
        int n;
        if (c >= '0' && c <= '9') n = c - '0';
        else if (c >= 'a' && c <= 'f') n = 10 + c - 'a';
        else if (c >= 'A' && c <= 'F') n = 10 + c - 'A';
        else return -1;

        if (high < 0) { high = n; }
        else {
            if (w >= max) return -1;
            out[w++] = (uint8_t)((high << 4) | n);
            high = -1;
        }
    }
    if (high >= 0) return -1;
    if (w == 0) return -1;
    *out_len = w;
    return 0;
}

/* A bytes value can be longer than one line holds; it is split into several lines when parsed. */
#define PATCH_VALUE_MAX 4096

static int
parse_typed_value(const char *type_str, const char *val_str,
                  uint8_t *out, size_t *out_len) {
    if (strcmp(type_str, "bytes") == 0 || strcmp(type_str, "byte") == 0)
        return parse_hex_bytes(val_str, out, out_len, PATCH_VALUE_MAX);

    if (strcmp(type_str, "bytes16") == 0) {
        uint64_t v = strtoull(val_str, NULL, 0);
        out[0] = (uint8_t)v; out[1] = (uint8_t)(v >> 8);
        *out_len = 2; return 0;
    }
    if (strcmp(type_str, "bytes32") == 0) {
        uint64_t v = strtoull(val_str, NULL, 0);
        out[0] = (uint8_t)v;       out[1] = (uint8_t)(v >> 8);
        out[2] = (uint8_t)(v >> 16); out[3] = (uint8_t)(v >> 24);
        *out_len = 4; return 0;
    }
    if (strcmp(type_str, "bytes64") == 0) {
        uint64_t v = strtoull(val_str, NULL, 0);
        for (int i = 0; i < 8; i++) out[i] = (uint8_t)(v >> (8 * i));
        *out_len = 8; return 0;
    }
    if (strcmp(type_str, "float32") == 0) {
        float f = strtof(val_str, NULL);
        memcpy(out, &f, 4);
        *out_len = 4; return 0;
    }
    if (strcmp(type_str, "utf8") == 0) {
        size_t n = strlen(val_str);
        if (n >= PATCH_LINE_MAX_BYTES) n = PATCH_LINE_MAX_BYTES - 1;
        memcpy(out, val_str, n);
        out[n] = '\0';
        *out_len = n + 1;
        return 0;
    }
    if (strcmp(type_str, "utf16") == 0) {
        size_t n = strlen(val_str);
        if (n * 2 + 2 > PATCH_LINE_MAX_BYTES) n = (PATCH_LINE_MAX_BYTES - 2) / 2;
        size_t w = 0;
        for (size_t i = 0; i < n; i++) { out[w++] = (uint8_t)val_str[i]; out[w++] = 0x00; }
        out[w++] = 0x00; out[w++] = 0x00;
        *out_len = w;
        return 0;
    }
    return -1;
}

static patch_line_type_t
type_from_str(const char *s) {
    if (strcmp(s, "bytes")       == 0) return PATCH_LINE_BYTES;
    if (strcmp(s, "byte")        == 0) return PATCH_LINE_BYTES;
    if (strcmp(s, "bytes16")     == 0) return PATCH_LINE_BYTES16;
    if (strcmp(s, "bytes32")     == 0) return PATCH_LINE_BYTES32;
    if (strcmp(s, "bytes64")     == 0) return PATCH_LINE_BYTES64;
    if (strcmp(s, "float32")     == 0) return PATCH_LINE_FLOAT32;
    if (strcmp(s, "utf8")        == 0) return PATCH_LINE_UTF8;
    if (strcmp(s, "utf16")       == 0) return PATCH_LINE_UTF16;
    if (strcmp(s, "mask")        == 0) return PATCH_LINE_MASK;
    if (strcmp(s, "mask_jump32") == 0) return PATCH_LINE_MASK_JUMP32;
    return PATCH_LINE_UNKNOWN;
}

static int
parse_mask_pattern(const char *s, uint8_t *pat, uint8_t *wc, size_t max) {
    size_t n = 0;
    const char *p = s;
    while (*p && n < max) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        if (p[0] == '?' && p[1] == '?') {
            pat[n] = 0x00; wc[n] = 1; n++; p += 2; continue;
        }
        int hi, lo;
        if (p[0] >= '0' && p[0] <= '9') hi = p[0] - '0';
        else if ((p[0]|32) >= 'a' && (p[0]|32) <= 'f') hi = (p[0]|32) - 'a' + 10;
        else return -1;
        if (p[1] >= '0' && p[1] <= '9') lo = p[1] - '0';
        else if ((p[1]|32) >= 'a' && (p[1]|32) <= 'f') lo = (p[1]|32) - 'a' + 10;
        else return -1;
        pat[n] = (uint8_t)((hi << 4) | lo);
        wc[n]  = 0; n++; p += 2;
    }
    return (n > 0) ? (int)n : -1;
}

/* ── Main parser ─────────────────────────────────────────────────────────── */

int
patch_parse_xml_file(const char *xml_path, const char *title_id,
                     cr_patch_source_kind_t source_kind, patch_doc_t *doc) {
    if (!xml_path || !title_id || !doc) return -1;
    memset(doc, 0, sizeof(*doc));

    char *txt = NULL;
    if (read_file_text(xml_path, &txt) != 0 || !txt) return -1;

    const char *ts = strstr(txt, "<TitleID>");
    const char *te = ts ? strstr(ts, "</TitleID>") : NULL;
    if (!ts || !te) { free(txt); return -1; }
    char needle[32];
    snprintf(needle, sizeof(needle), "<ID>%s</ID>", title_id);
    if (!strstr(ts, needle)) { free(txt); return 0; }

    const char *p = txt;
    while (doc->count < PATCH_MAX_ENTRIES) {
        const char *meta_open = strstr(p, "<Metadata");
        if (!meta_open) break;

        const char *tag_end = strchr(meta_open, '>');
        if (!tag_end) break;

        const char *meta_close = strstr(tag_end, "</Metadata>");
        if (!meta_close) break;

        patch_entry_t *e = &doc->entries[doc->count];
        memset(e, 0, sizeof(*e));

        char imagebase_str[32] = {0};
        tag_attr(meta_open, tag_end, "Name",      e->name,    sizeof(e->name));
        tag_attr(meta_open, tag_end, "Author",    e->author,  sizeof(e->author));
        tag_attr(meta_open, tag_end, "Note",      e->note,    sizeof(e->note));
        tag_attr(meta_open, tag_end, "AppVer",    e->app_ver, sizeof(e->app_ver));
        tag_attr(meta_open, tag_end, "ImageBase", imagebase_str, sizeof(imagebase_str));

        xml_decode(e->name);
        xml_decode(e->author);
        xml_decode(e->note);

        if (!doc->game_title[0])
            tag_attr(meta_open, tag_end, "Title", doc->game_title, sizeof(doc->game_title));

        intptr_t imagebase = imagebase_str[0] ?
            (intptr_t)strtoull(imagebase_str, NULL, 16) : 0;

        e->is_mask_ver      = (strcmp(e->app_ver, "mask") == 0);
        e->is_absolute_addr = (imagebase == 0);

        /* Stamp source metadata now so entryId can be computed after line parsing. */
        snprintf(e->source_path, sizeof(e->source_path), "%s", xml_path);
        e->source_kind     = source_kind;
        e->metadata_index  = doc->count;

        const char *plist     = strstr(tag_end, "<PatchList>");
        const char *plist_end = plist ? strstr(plist, "</PatchList>") : NULL;
        if (plist && plist_end && plist < meta_close) {
            const char *lp = plist;
            while (e->line_count < PATCH_MAX_LINES) {
                const char *line_open = strstr(lp, "<Line");
                if (!line_open || line_open >= plist_end) break;
                const char *line_end = strstr(line_open, "/>");
                if (!line_end || line_end >= plist_end) break;

                char type_str[32]   = {0};
                char addr_str[1024] = {0};
                char val_str[PATCH_VALUE_MAX * 2 + 4] = {0};
                char off_str[16]    = {0};
                char target_str[512] = {0};
                char size_str[16]   = {0};
                tag_attr(line_open, line_end, "Type",    type_str, sizeof(type_str));
                tag_attr(line_open, line_end, "Address", addr_str, sizeof(addr_str));
                tag_attr(line_open, line_end, "Value",   val_str,  sizeof(val_str));
                tag_attr(line_open, line_end, "Offset",  off_str,  sizeof(off_str));
                tag_attr(line_open, line_end, "Target",  target_str, sizeof(target_str));
                tag_attr(line_open, line_end, "Size",    size_str, sizeof(size_str));

                patch_line_type_t lt = type_from_str(type_str);

                if (lt == PATCH_LINE_UNKNOWN) {
                    e->has_unsupported = 1;
                    e->unsupported_count++;
                    /* Collect unique unsupported type names */
                    int dup = 0;
                    for (int k = 0; k < e->unsupported_type_count; k++) {
                        if (strcmp(e->unsupported_types[k], type_str) == 0) { dup = 1; break; }
                    }
                    if (!dup && e->unsupported_type_count < PATCH_UNSUPPORTED_MAX)
                        snprintf(e->unsupported_types[e->unsupported_type_count++],
                                 32, "%s", type_str[0] ? type_str : "unknown");
                    lp = line_end + 2;
                    continue;
                }

                if (!addr_str[0] || !val_str[0]) { lp = line_end + 2; continue; }

                patch_line_t *ln = &e->lines[e->line_count];
                memset(ln, 0, sizeof(*ln));
                ln->type = lt;

                if (lt == PATCH_LINE_MASK) {
                    int plen = parse_mask_pattern(addr_str, ln->pattern, ln->wildcard,
                                                  PATCH_MASK_MAX_BYTES);
                    if (plen < 0) { lp = line_end + 2; continue; }
                    ln->pattern_len  = (size_t)plen;
                    ln->match_offset = off_str[0] ? atoi(off_str) : 0;
                    uint8_t vbuf[PATCH_LINE_MAX_BYTES];
                    size_t  vlen = 0;
                    if (parse_hex_bytes(val_str, vbuf, &vlen, PATCH_LINE_MAX_BYTES) != 0 || vlen == 0) {
                        lp = line_end + 2; continue;
                    }
                    ln->value_len = vlen;
                    memcpy(ln->value, vbuf, vlen);
                } else if (lt == PATCH_LINE_MASK_JUMP32) {
                    /* Detour trampoline: Address/Offset locate the hook site (pattern scan like mask), Target locates the
                     * cave with a second scan, and Value is written into the cave, not at the hook site. */
                    if (!target_str[0]) { lp = line_end + 2; continue; }
                    int plen = parse_mask_pattern(addr_str, ln->pattern, ln->wildcard,
                                                  PATCH_MASK_MAX_BYTES);
                    if (plen < 0) { lp = line_end + 2; continue; }
                    ln->pattern_len  = (size_t)plen;
                    ln->match_offset = off_str[0] ? atoi(off_str) : 0;

                    int tlen = parse_mask_pattern(target_str, ln->target_pattern,
                                                  ln->target_wildcard, PATCH_MASK_MAX_BYTES);
                    if (tlen < 0) { lp = line_end + 2; continue; }
                    ln->target_pattern_len = (size_t)tlen;

                    ln->jump_size = size_str[0] ? atoi(size_str) : 5;
                    /* rel32 JMP needs >=5 bytes; cap at the NOP-pad buffer size used when applying. */
                    if (ln->jump_size < 5 || ln->jump_size > PATCH_LINE_MAX_BYTES) { lp = line_end + 2; continue; }

                    uint8_t vbuf[PATCH_LINE_MAX_BYTES];
                    size_t  vlen = 0;
                    if (parse_hex_bytes(val_str, vbuf, &vlen, PATCH_LINE_MAX_BYTES) != 0 || vlen == 0) {
                        lp = line_end + 2; continue;
                    }
                    ln->value_len = vlen;
                    memcpy(ln->value, vbuf, vlen);
                } else {
                    uint64_t raw_addr = strtoull(addr_str, NULL, 16);
                    ln->offset = imagebase ? (intptr_t)(raw_addr - (uint64_t)imagebase)
                                           : (intptr_t)raw_addr;
                    uint8_t vbuf[PATCH_VALUE_MAX];
                    size_t  vlen = 0;
                    if (parse_typed_value(type_str, val_str, vbuf, &vlen) != 0 || vlen == 0) {
                        lp = line_end + 2; continue;
                    }
                    size_t first = vlen < PATCH_LINE_MAX_BYTES ? vlen : PATCH_LINE_MAX_BYTES;
                    ln->value_len = first;
                    memcpy(ln->value, vbuf, first);
                    for (size_t done = first; done < vlen && e->line_count + 1 < PATCH_MAX_LINES; done += PATCH_LINE_MAX_BYTES) {
                        size_t n = vlen - done < PATCH_LINE_MAX_BYTES ? vlen - done : PATCH_LINE_MAX_BYTES;
                        e->line_count++;
                        patch_line_t *more = &e->lines[e->line_count];
                        memset(more, 0, sizeof(*more));
                        more->type = lt;
                        more->offset = ln->offset + (intptr_t)done;
                        more->value_len = n;
                        memcpy(more->value, vbuf + done, n);
                    }
                }

                e->line_count++;
                lp = line_end + 2;
            }
            /* Warn if the PatchList had more lines than the cap */
            {
                const char *extra = lp;
                int dropped = 0;
                while (1) {
                    const char *nl = strstr(extra, "<Line");
                    if (!nl || nl >= plist_end) break;
                    dropped++;
                    extra = nl + 5;
                }
                if (dropped > 0)
                    cr_log("warn", "patches",
                           "line_limit_hit entry='%s' file=%s cap=%d dropped=%d"
                           " — increase PATCH_MAX_LINES",
                           e->name, xml_path, PATCH_MAX_LINES, dropped);
            }
        }

        cr_patch_compute_entry_id(e);
        doc->count++;
        p = meta_close + strlen("</Metadata>");
    }

    /* Warn if more <Metadata> blocks follow — they were silently dropped. */
    if (doc->count >= PATCH_MAX_ENTRIES && strstr(p, "<Metadata"))
        cr_log("warn", "patches", "entry_limit_hit file=%s cap=%d — remaining entries skipped",
               xml_path, PATCH_MAX_ENTRIES);

    free(txt);
    return doc->count;
}

/* ── Memory scanner for mask-type patches ────────────────────────────────── */

#define MASK_SCAN_CHUNK  (128 * 1024)
#define MASK_SCAN_LIMIT  (384 * 1024 * 1024)

static intptr_t
scan_for_pattern(pid_t pid, intptr_t scan_start, size_t scan_size,
                 const uint8_t *pat, const uint8_t *wc, size_t pat_len) {
    if (pat_len == 0 || scan_size == 0) return 0;

    size_t buf_sz = MASK_SCAN_CHUNK + pat_len;
    uint8_t *buf = malloc(buf_sz);
    if (!buf) return 0;

    intptr_t found = 0;
    size_t scanned = 0;

    while (!found && scanned < scan_size) {
        size_t want = MASK_SCAN_CHUNK;
        if (scanned + want > scan_size) want = scan_size - scanned;
        size_t read_sz = want + pat_len - 1;
        if (scanned + read_sz > scan_size) read_sz = scan_size - scanned;

        intptr_t chunk_addr = scan_start + (intptr_t)scanned;
        /* Stop at the first unreadable chunk: that is the edge of the mapped module, and scanning on toward
         * MASK_SCAN_LIMIT can hit unmapped/MMIO memory and panic the kernel. */
        if (!ADDR_IN_USER_RANGE(chunk_addr)) break;
        int rrc = mdbg_io_copyout(pid, chunk_addr, buf, read_sz);
        if (rrc < 0) break;

        size_t search_len = (read_sz >= pat_len) ? (read_sz - pat_len + 1) : 0;
        for (size_t i = 0; i < search_len; i++) {
            int match = 1;
            for (size_t j = 0; j < pat_len; j++) {
                if (!wc[j] && buf[i + j] != pat[j]) { match = 0; break; }
            }
            if (match) { found = chunk_addr + (intptr_t)i; break; }
        }
        scanned += want;
    }

    free(buf);
    return found;
}

/* ── Apply — backup/rollback ─────────────────────────────────────────────── */

static void
do_rollback(pid_t pid, const cr_patch_backup_t *backups, int n, int *rollback_errors) {
    *rollback_errors = 0;
    for (int i = n - 1; i >= 0; i--) {
        if (!backups[i].valid) continue;
        int wrc = write_process_memory_forced(pid, (intptr_t)backups[i].address,
                                              backups[i].old_bytes, backups[i].len);
        if (wrc != 0) {
            cr_log("warn", "patches", "rollback_failed addr=0x%llx rc=%d",
                   (long long)backups[i].address, wrc);
            (*rollback_errors)++;
        }
    }
}

static int
patch_line_is_plain(const patch_line_t *ln) {
    return ln->type != PATCH_LINE_MASK && ln->type != PATCH_LINE_MASK_JUMP32;
}

/* Block whose range holds the rel32 target of a jmp/call at the start of block b, or -1. */
static int
patch_jump_target_block(const patch_line_t **bl, int nb, int b) {
    const patch_line_t *j = bl[b];
    if (j->value_len < 5 || (j->value[0] != 0xE9 && j->value[0] != 0xE8)) return -1;
    int32_t rel;
    memcpy(&rel, j->value + 1, 4);
    intptr_t target = j->offset + 5 + rel;
    for (int k = 0; k < nb; k++) {
        if (k != b && target >= bl[k]->offset && target < bl[k]->offset + (intptr_t)bl[k]->value_len) return k;
    }
    return -1;
}

static void
patch_plan_emit(const patch_line_t **bl, int nb, int b, char *done,
                const patch_line_t **plan, int *pn) {
    if (done[b]) return;
    done[b] = 1;
    int t = patch_jump_target_block(bl, nb, b);
    if (t >= 0) patch_plan_emit(bl, nb, t, done, plan, pn);
    plan[(*pn)++] = bl[b];
}

/* Write order for an entry: contiguous lines merge into one write, and a block that jumps into another
 * one in the same run goes after its target. Merged blocks live in `store`; returns the write count. */
static int
patch_build_plan(const patch_entry_t *entry, patch_line_t *store, const patch_line_t **plan) {
    int pn = 0, used = 0, n = entry->line_count;
    for (int i = 0; i < n;) {
        if (!patch_line_is_plain(&entry->lines[i])) { plan[pn++] = &entry->lines[i++]; continue; }

        const patch_line_t *bl[PATCH_MAX_LINES];
        char done[PATCH_MAX_LINES] = {0};
        int nb = 0;
        patch_line_t *lastmod = NULL;
        while (i < n && patch_line_is_plain(&entry->lines[i])) {
            const patch_line_t *ln = &entry->lines[i++];
            const patch_line_t *last = nb ? bl[nb - 1] : NULL;
            if (last && ln->offset == last->offset + (intptr_t)last->value_len &&
                last->value_len + ln->value_len <= PATCH_LINE_MAX_BYTES) {
                if (!lastmod) {
                    lastmod = &store[used++];
                    *lastmod = *last;
                    bl[nb - 1] = lastmod;
                }
                memcpy(lastmod->value + lastmod->value_len, ln->value, ln->value_len);
                lastmod->value_len += ln->value_len;
            } else {
                bl[nb++] = ln;
                lastmod = NULL;
            }
        }
        for (int b = 0; b < nb; b++) patch_plan_emit(bl, nb, b, done, plan, &pn);
    }
    return pn;
}

int
patch_apply_entry_ex(const char *title_id, const patch_entry_t *entry,
                     patch_apply_result_t *result) {
    if (result) memset(result, 0, sizeof(*result));

    if (!title_id || !entry) {
        if (result) { snprintf(result->error, 64, "invalid_args");
                      snprintf(result->message, 256, "null title_id or entry"); }
        return -1;
    }
    if (entry->line_count == 0) {
        if (result) { snprintf(result->error, 64, "no_supported_lines");
                      snprintf(result->message, 256, "entry has no supported lines"); }
        return -1;
    }
    if (entry->has_unsupported) {
        if (result) { snprintf(result->error, 64, "unsupported_line_type");
                      snprintf(result->message, 256,
                               "entry contains unsupported line types; not applied"); }
        return -1;
    }

    pid_t pid = -1;
    intptr_t base = 0;
    int app_id = 0;
    char running_title[16] = {0};
    if (get_running_game_ex(&pid, running_title, sizeof(running_title), &base, &app_id) != 0) {
        if (result) { snprintf(result->error, 64, "missing_process");
                      snprintf(result->message, 256, "no game is running"); }
        return -1;
    }
    if (result) result->pid = pid;

    char run_norm[10] = {0}, cheat_norm[10] = {0};
    if (!title_id_normalize(running_title, run_norm) ||
        !title_id_normalize(title_id, cheat_norm) ||
        strcmp(run_norm, cheat_norm) != 0) {
        if (result) { snprintf(result->error, 64, "missing_game");
                      snprintf(result->message, 256,
                               "running game (%s) does not match patch target (%s)",
                               running_title, title_id); }
        return -1;
    }

    /* Same global gate as the cheat path, plus g_cheat_applying so the dashboard poll backs off. */
    pthread_mutex_lock(&g_cheat_apply_lock);
    g_cheat_applying = 1;

    if (kill(pid, 0) != 0 && errno == ESRCH) {
        g_cheat_applying = 0;
        pthread_mutex_unlock(&g_cheat_apply_lock);
        if (result) { snprintf(result->error, 64, "attach_failed");
                      snprintf(result->message, 256, "game has exited"); }
        return -1;
    }

    /* Heap-allocate — 64 × 280 bytes = 17.5 KB would overflow the thread stack. */
    cr_patch_backup_t *backups = calloc(PATCH_MAX_LINES, sizeof(*backups));
    if (!backups) {
        g_cheat_applying = 0;
        pthread_mutex_unlock(&g_cheat_apply_lock);
        if (result) { snprintf(result->error, 64, "oom");
                      snprintf(result->message, 256, "out of memory"); }
        return -1;
    }
    int backup_count = 0;

    patch_line_t *store = calloc(entry->line_count, sizeof(*store));
    const patch_line_t **plan = calloc(entry->line_count, sizeof(*plan));
    if (!store || !plan) {
        free(store);
        free(plan);
        free(backups);
        g_cheat_applying = 0;
        pthread_mutex_unlock(&g_cheat_apply_lock);
        if (result) { snprintf(result->error, 64, "oom");
                      snprintf(result->message, 256, "out of memory"); }
        return -1;
    }
    int plan_count = patch_build_plan(entry, store, plan);
    cr_log("info", "patches", "write plan: %d line(s) -> %d write(s) title=%s",
           entry->line_count, plan_count, title_id);

    int rc            = 0;
    int verify_fails  = 0;
    int skipped_lines = 0;
    char err_msg[256] = {0};
    char err_code[64] = {0};

    for (int i = 0; i < plan_count; i++) {
        const patch_line_t *ln = plan[i];
        intptr_t write_addr = 0;

        if (ln->type == PATCH_LINE_MASK_JUMP32) {
            /* Detour trampoline: a JMP redirects the hook site (Address+Offset) into the cave (Target), which holds
             * Value and a JMP back. Three writes, each with its own backup so rollback restores everything. */
            intptr_t patch_match = scan_for_pattern(pid, base, MASK_SCAN_LIMIT,
                                                    ln->pattern, ln->wildcard, ln->pattern_len);
            if (!patch_match) {
                cr_log("warn", "patches", "mask_jump32 addr_not_found line=%d title=%s - line skipped", i, title_id);
                skipped_lines++;
                continue;
            }
            intptr_t patch_addr = patch_match + (intptr_t)ln->match_offset;

            intptr_t cave_addr = scan_for_pattern(pid, base, MASK_SCAN_LIMIT,
                                                  ln->target_pattern, ln->target_wildcard,
                                                  ln->target_pattern_len);
            if (!cave_addr) {
                cr_log("warn", "patches", "mask_jump32 target_not_found line=%d title=%s - line skipped", i, title_id);
                skipped_lines++;
                continue;
            }
            intptr_t cave_end = cave_addr + (intptr_t)ln->value_len;

            cr_log("info", "patches",
                   "mask_jump32 patch_addr=0x%lx cave_addr=0x%lx cave_end=0x%lx jump_size=%d value_len=%zu title=%s",
                   (long)patch_addr, (long)cave_addr, (long)cave_end, ln->jump_size, ln->value_len, title_id);

            struct { intptr_t addr; const uint8_t *data; size_t len; } writes[3];
            uint8_t jmp_back[5];
            uint8_t jmp_in[PATCH_LINE_MAX_BYTES];
            {
                int32_t rel = (int32_t)((intptr_t)(patch_addr + ln->jump_size) - (intptr_t)(cave_end + 5));
                jmp_back[0] = 0xE9;
                memcpy(jmp_back + 1, &rel, 4);
            }
            {
                memset(jmp_in, 0x90, (size_t)ln->jump_size);
                int32_t rel = (int32_t)((intptr_t)cave_addr - (intptr_t)(patch_addr + 5));
                jmp_in[0] = 0xE9;
                memcpy(jmp_in + 1, &rel, 4);
            }
            writes[0].addr = cave_addr;  writes[0].data = ln->value; writes[0].len = ln->value_len;
            writes[1].addr = cave_end;   writes[1].data = jmp_back;  writes[1].len = sizeof(jmp_back);
            writes[2].addr = patch_addr; writes[2].data = jmp_in;    writes[2].len = (size_t)ln->jump_size;

            int wfail = 0;
            for (int w = 0; w < 3 && !wfail; w++) {
                if (backup_count >= PATCH_MAX_LINES) {
                    snprintf(err_code, sizeof(err_code), "backup_capacity");
                    snprintf(err_msg,  sizeof(err_msg), "line[%d] ran out of backup slots", i);
                    rc = -1; wfail = 1; break;
                }
                int rrc = mdbg_io_copyout(pid, writes[w].addr, backups[backup_count].old_bytes, writes[w].len);
                if (rrc < 0) {
                    snprintf(err_code, sizeof(err_code), "backup_read_failed");
                    snprintf(err_msg,  sizeof(err_msg),
                             "line[%d] failed to read original bytes at 0x%lx", i, (long)writes[w].addr);
                    rc = -1; wfail = 1; break;
                }
                backups[backup_count].address = (uint64_t)writes[w].addr;
                backups[backup_count].len     = writes[w].len;
                backups[backup_count].valid   = 1;
                backup_count++;

                int wrc = write_process_memory_forced(pid, writes[w].addr, writes[w].data, writes[w].len);
                if (wrc != 0) {
                    snprintf(err_code, sizeof(err_code), "write_failed");
                    snprintf(err_msg,  sizeof(err_msg),
                             "line[%d] mask_jump32 write %d/3 failed at 0x%lx (rc=%d)",
                             i, w + 1, (long)writes[w].addr, wrc);
                    rc = -1; wfail = 1; break;
                }
                uint8_t vbuf[PATCH_LINE_MAX_BYTES];
                if (mdbg_io_copyout(pid, writes[w].addr, vbuf, writes[w].len) >= 0 &&
                    memcmp(vbuf, writes[w].data, writes[w].len) != 0) {
                    cr_log("warn", "patches", "mask_jump32_verify_mismatch write=%d/3 addr=0x%lx len=%zu",
                           w + 1, (long)writes[w].addr, writes[w].len);
                    snprintf(err_code, sizeof(err_code), "verify_failed");
                    snprintf(err_msg,  sizeof(err_msg),
                             "line[%d] mask_jump32 write %d/3 verify mismatch at 0x%lx",
                             i, w + 1, (long)writes[w].addr);
                    verify_fails++;
                    rc = -1; wfail = 1; break;
                }
            }
            if (wfail) break;
            continue;
        }

        if (ln->type == PATCH_LINE_MASK) {
            cr_log("info", "patches", "mask_scan pattern_len=%zu offset=%d title=%s",
                   ln->pattern_len, ln->match_offset, title_id);
            intptr_t match = scan_for_pattern(pid, base, MASK_SCAN_LIMIT,
                                              ln->pattern, ln->wildcard, ln->pattern_len);
            if (!match) {
                cr_log("warn", "patches", "mask_scan not_found line=%d title=%s - line skipped", i, title_id);
                skipped_lines++;
                continue;
            }
            write_addr = match + (intptr_t)ln->match_offset;

            /* Pre-write diagnostic logging */
            {
                char pre_hex[PATCH_MASK_MAX_BYTES * 3 + 4] = {0};
                char val_hex[PATCH_LINE_MAX_BYTES * 3 + 4] = {0};
                uint8_t pre[PATCH_MASK_MAX_BYTES];
                size_t rd = (ln->pattern_len <= PATCH_MASK_MAX_BYTES) ? ln->pattern_len : PATCH_MASK_MAX_BYTES;
                if (mdbg_io_copyout(pid, write_addr, pre, rd) >= 0) {
                    size_t pos = 0;
                    for (size_t b = 0; b < rd && pos + 4 < sizeof(pre_hex); b++)
                        pos += (size_t)snprintf(pre_hex + pos, sizeof(pre_hex) - pos, "%02x ", pre[b]);
                }
                { size_t pos = 0;
                  size_t vl = ln->value_len < PATCH_LINE_MAX_BYTES ? ln->value_len : PATCH_LINE_MAX_BYTES;
                  for (size_t b = 0; b < vl && pos + 4 < sizeof(val_hex); b++)
                      pos += (size_t)snprintf(val_hex + pos, sizeof(val_hex) - pos, "%02x ", ln->value[b]); }
                cr_log("info", "patches",
                       "mask_scan match=0x%lx write_addr=0x%lx pre=[%s] val=[%s] value_len=%zu pattern_len=%zu",
                       (long)match, (long)write_addr, pre_hex, val_hex,
                       ln->value_len, ln->pattern_len);

                if (cr_patch_line_has_trailing_pattern_bytes(ln)) {
                    cr_log("warn", "patches",
                           "mask_partial_overwrite line=%d offset=%d value_len=%zu "
                           "leaves %zu original pattern byte(s) after write site",
                           i, ln->match_offset, ln->value_len,
                           ln->pattern_len - (size_t)ln->match_offset - ln->value_len);
                }
            }
        } else {
            write_addr = entry->is_absolute_addr ? ln->offset : (base + ln->offset);
            cr_log("info", "patches", "line[%d] write addr=0x%lx len=%zu abs=%d",
                   i, (long)write_addr, ln->value_len, entry->is_absolute_addr);
        }

        /* Save backup before writing */
        if (ln->value_len <= PATCH_LINE_MAX_BYTES) {
            int rrc = mdbg_io_copyout(pid, write_addr, backups[backup_count].old_bytes, ln->value_len);
            if (rrc < 0) {
                snprintf(err_code, sizeof(err_code), "backup_read_failed");
                snprintf(err_msg,  sizeof(err_msg),
                         "line[%d] failed to read original bytes at 0x%lx", i, (long)write_addr);
                cr_log("warn", "patches", "backup_read_failed line=%d addr=0x%lx rc=%d",
                       i, (long)write_addr, rrc);
                rc = -1; break;
            }
            backups[backup_count].address = (uint64_t)write_addr;
            backups[backup_count].len     = ln->value_len;
            backups[backup_count].valid   = 1;
            if (ln->type != PATCH_LINE_MASK) {
                char old_h[52] = {0}, new_h[52] = {0};
                size_t hn = ln->value_len < 16 ? ln->value_len : 16;
                for (size_t b = 0; b < hn; b++) {
                    snprintf(old_h + b * 3, sizeof(old_h) - b * 3, "%02x ", backups[backup_count].old_bytes[b]);
                    snprintf(new_h + b * 3, sizeof(new_h) - b * 3, "%02x ", ln->value[b]);
                }
                cr_log("info", "patches", "line[%d] original=[%s] new=[%s]", i, old_h, new_h);
            }
        }
        backup_count++;

        /* Write */
        int wrc;
        if (ln->type == PATCH_LINE_MASK) {
            /* Skips kernel_get_vmem_protection (forced write), which hangs on PS4 BC vmem entries. */
            wrc = write_process_memory_forced(pid, write_addr, ln->value, ln->value_len);
            if (wrc != 0) {
                snprintf(err_code, sizeof(err_code), "write_failed");
                snprintf(err_msg,  sizeof(err_msg),
                         "line[%d] mask write failed at 0x%lx (rc=%d)", i, (long)write_addr, wrc);
                rc = -1; break;
            }
            if (ln->value_len <= PATCH_LINE_MAX_BYTES) {
                uint8_t vbuf[PATCH_LINE_MAX_BYTES];
                if (mdbg_io_copyout(pid, write_addr, vbuf, ln->value_len) >= 0 &&
                    memcmp(vbuf, ln->value, ln->value_len) != 0) {
                    cr_log("warn", "patches", "mask_verify_mismatch addr=0x%lx len=%zu",
                           (long)write_addr, ln->value_len);
                    snprintf(err_code, sizeof(err_code), "verify_failed");
                    snprintf(err_msg,  sizeof(err_msg),
                             "line[%d] mask write-verify mismatch at 0x%lx", i, (long)write_addr);
                    verify_fails++;
                    rc = -1; break;
                }
            }
        } else {
            /* Same here: kernel_get_vmem_protection panics the kernel on PS4 BC vmem entries. */
            wrc = write_process_memory_forced(pid, write_addr, ln->value, ln->value_len);
            if (wrc != 0) {
                snprintf(err_code, sizeof(err_code), "write_failed");
                snprintf(err_msg,  sizeof(err_msg),
                         "line[%d] write failed at 0x%lx (rc=%d)", i, (long)write_addr, wrc);
                cr_log("warn", "patches", "write_failed addr=0x%lx len=%zu rc=%d",
                       (long)write_addr, ln->value_len, wrc);
                rc = -1; break;
            }
            /* Readback verify (write_process_memory_forced has no internal verify) */
            if (ln->value_len <= PATCH_LINE_MAX_BYTES) {
                uint8_t vbuf[PATCH_LINE_MAX_BYTES];
                if (mdbg_io_copyout(pid, write_addr, vbuf, ln->value_len) >= 0 &&
                    memcmp(vbuf, ln->value, ln->value_len) != 0) {
                    char exp_h[52] = {0}, got_h[52] = {0};
                    size_t hn = ln->value_len < 16 ? ln->value_len : 16;
                    for (size_t b = 0; b < hn; b++) {
                        snprintf(exp_h + b * 3, sizeof(exp_h) - b * 3, "%02x ", ln->value[b]);
                        snprintf(got_h + b * 3, sizeof(got_h) - b * 3, "%02x ", vbuf[b]);
                    }
                    cr_log("warn", "patches",
                           "write_verify_mismatch line=%d addr=0x%lx len=%zu exp=[%s] got=[%s]",
                           i, (long)write_addr, ln->value_len, exp_h, got_h);
                    snprintf(err_code, sizeof(err_code), "verify_failed");
                    snprintf(err_msg,  sizeof(err_msg),
                             "line[%d] readback mismatch at 0x%lx — bytes did not land",
                             i, (long)write_addr);
                    verify_fails++;
                    rc = -1; break;
                }
            }
        }
    }

    if (rc == 0 && skipped_lines > 0 && skipped_lines == plan_count) {
        snprintf(err_code, sizeof(err_code), "mask_not_found");
        snprintf(err_msg,  sizeof(err_msg), "no line of this patch matched the game's memory");
        rc = -1;
    }

    int rollback_errors = 0;
    int rolled_back     = 0;
    if (rc != 0) {
        do_rollback(pid, backups, backup_count, &rollback_errors);
        rolled_back = 1;
        cr_log(rollback_errors ? "warn" : "info", "patches",
               "rollback title=%s entry='%s' lines_backed=%d rollback_errors=%d",
               title_id, entry->name, backup_count, rollback_errors);
    }

    g_cheat_applying = 0;
    pthread_mutex_unlock(&g_cheat_apply_lock);

    if (result) {
        result->ok               = (rc == 0);
        result->rolled_back      = rolled_back;
        result->rollback_errors  = rollback_errors;
        result->verify_fail_count = verify_fails;
        result->skipped_lines    = skipped_lines;
        snprintf(result->error,   sizeof(result->error),   "%s", err_code);
        snprintf(result->message, sizeof(result->message), "%s", err_msg);
    }

    if (rc == 0) {
        patch_store_backup(title_id, entry->entry_id, entry->name, pid, backups, backup_count);
        cr_log("info", "patches", "applied '%s' title=%s pid=%d lines=%d skipped=%d",
               entry->name, title_id, (int)pid, entry->line_count, skipped_lines);
        if (skipped_lines > 0) {
            notify("CheatRunner: Patch applied: %s (%d line(s) not found)", entry->name, skipped_lines);
        } else {
            notify("CheatRunner: Patch applied: %s", entry->name);
        }
    } else {
        cr_log("warn", "patches", "apply_failed '%s' title=%s error=%s",
               entry->name, title_id, err_code);
        notify("CheatRunner: Patch failed: %s", entry->name);
    }
    free(backups);
    free(store);
    free(plan);
    return rc;
}
