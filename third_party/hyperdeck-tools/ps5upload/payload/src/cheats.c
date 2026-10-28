/*
 * cheats.c — cheat engine for applying byte-stream memory patches to
 * running PS5 games.
 *
 * Architecture:
 *   - Three file formats: JSON (native), SHN (XML trainer), MC4 (base64 over
 *     AES-256-CBC, decrypting to the same XML as SHN). All parsed into a
 *     common in-memory representation.
 *   - Memory reads/writes through the game's physical pages (page-table
 *     walk + kernel direct map), applied all-or-nothing per cheat. See
 *     "Game memory access" below for why not ptrace + kernel_mprotect.
 *   - A background watcher thread polls every 3s for new game processes
 *     and auto-applies "patches" (always-on) + re-applies user-toggled
 *     cheats.
 *   - Toggle state persisted to sidecar JSON files so it survives payload
 *     redeploy.
 *
 * Ported from elf-arsenal/src/cheats.c, adapted to ps5upload's
 * management protocol.
 */

#include "cheats.h"
#include "cheats_list.h"
#include "notif.h"

#include <ps5/kernel.h>

#include <ctype.h>
#include <stddef.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <unistd.h>

#include "notif.h"
#include "proc_list.h"
#include "aes.h" /* tiny-AES, vendored — MC4 cheat decryption */
#include "xml_encoding.h" /* UTF-16 → UTF-8 for SHN/MC4 trainer XML (#317) */
#include "cheats_plan.h"
#include "kernel_rw_lock.h"

#include <ps5/mdbg.h>

/* Defined by the payload's own build of the SDK startup code (third_party/sdk-crt), which every
 * payload links: the kernel direct-map base and the process page-table walk. */
extern unsigned long KERNEL_ADDRESS_DMAP_BASE;
int kernel_proc_getpaddr(int pid, unsigned long vaddr, unsigned long *paddr, unsigned long *plen);

/* ── Constants ───────────────────────────────────────────────────── */

#define CHEATS_ROOT      "/data/ps5upload/cheats"
#define CHEATS_JSON_DIR  CHEATS_ROOT "/json"
#define CHEATS_SHN_DIR   CHEATS_ROOT "/shn"
#define CHEATS_MC4_DIR   CHEATS_ROOT "/mc4"
#define PATCHES_ROOT     "/data/ps5upload/patches"
#define PATCHES_JSON_DIR PATCHES_ROOT "/json"
#define PATCHES_SHN_DIR  PATCHES_ROOT "/shn"
#define PATCHES_MC4_DIR  PATCHES_ROOT "/mc4"
#define CHEATS_STATE_DIR CHEATS_ROOT "/state"

#define MAX_CHEAT_FILEPATH  300
#define MAX_TITLE_ID        16
#define MAX_CHEAT_NAME      256
#define MAX_MODS_PER_FILE   64
#define MAX_MEM_ENTRIES     32
#define MAX_JSON_BUF        (256 * 1024)
#define CHEAT_FILE_MAX      (4 * 1024 * 1024)

#define RG_CACHE_TTL_MS     2000

#define KINFO_STRUCT_MINSIZE 448

/* Canonical layout + title-id helpers. */
#include "app_info.h"

#include "proc_identity.h"
/* ── Engine state ────────────────────────────────────────────────── */

static atomic_int g_engine_enabled = 0;
static atomic_int g_patches_last = 0;
static atomic_int g_patches_total = 0;
static atomic_int g_watcher_running = 0;

/* Running-game cache */
static pthread_mutex_t g_rg_lock = PTHREAD_MUTEX_INITIALIZER;
static struct {
    int      valid;
    pid_t    pid;
    char     title[MAX_TITLE_ID];
    intptr_t base;
    struct timespec ts;
} g_rg_cache;

/* ── JSON string extraction helpers (from runtime.c patterns) ────── */

/* Find the value of a string field in a JSON blob. Writes the unquoted,
 * unescaped value to out. Returns 0 if found, -1 if not. */
static int json_str(const char *json, const char *key, char *out, size_t cap) {
    if (!json || !key || !out || cap == 0) return -1;
    out[0] = '\0';
    size_t klen = strlen(key);
    const char *p = json;
    while ((p = strstr(p, key))) {
        /* Check this is a key: preceded by " and followed by " : */
        const char *q = p + klen;
        if (p > json && p[-1] == '"' && q[0] == '"' &&
            (q[1] == ':' || q[1] == ' ' || q[1] == '\t')) {
            /* Skip to the colon, then to the opening quote */
            p = q + 1;
            while (*p && *p != ':') p++;
            if (*p != ':') return -1;
            p++;
            while (*p == ' ' || *p == '\t') p++;
            if (*p != '"') return -1;
            p++;
            size_t i = 0;
            while (*p && *p != '"' && i + 1 < cap) {
                if (*p == '\\' && p[1]) {
                    switch (p[1]) {
                        case '"': out[i++] = '"'; break;
                        case '\\': out[i++] = '\\'; break;
                        case '/': out[i++] = '/'; break;
                        case 'n': out[i++] = '\n'; break;
                        case 't': out[i++] = '\t'; break;
                        case 'r': out[i++] = '\r'; break;
                        default: out[i++] = p[1]; break;
                    }
                    p += 2;
                } else {
                    out[i++] = *p++;
                }
            }
            out[i] = '\0';
            return 0;
        }
        p += klen;
    }
    return -1;
}

/* Find a numeric field in JSON. Returns 0 if found, -1 if not. */
static int json_num(const char *json, const char *key, int64_t *out) {
    if (!json || !key || !out) return -1;
    size_t klen = strlen(key);
    const char *p = json;
    while ((p = strstr(p, key))) {
        const char *q = p + klen;
        if (p > json && p[-1] == '"' && q[0] == '"' &&
            (q[1] == ':' || q[1] == ' ' || q[1] == '\t')) {
            p = q + 1;
            while (*p && *p != ':') p++;
            if (*p != ':') return -1;
            p++;
            while (*p == ' ' || *p == '\t') p++;
            char *end;
            *out = (int64_t)strtoll(p, &end, 10);
            if (end != p) return 0;
            return -1;
        }
        p += klen;
    }
    return -1;
}

/* Find a boolean field in JSON. Returns 0 if found, -1 if not. */
static int json_bool(const char *json, const char *key, int *out) {
    if (!json || !key || !out) return -1;
    size_t klen = strlen(key);
    const char *p = json;
    while ((p = strstr(p, key))) {
        const char *q = p + klen;
        if (p > json && p[-1] == '"' && q[0] == '"' &&
            (q[1] == ':' || q[1] == ' ' || q[1] == '\t')) {
            p = q + 1;
            while (*p && *p != ':') p++;
            if (*p != ':') return -1;
            p++;
            while (*p == ' ' || *p == '\t') p++;
            if (strncmp(p, "true", 4) == 0) { *out = 1; return 0; }
            if (strncmp(p, "false", 5) == 0) { *out = 0; return 0; }
            return -1;
        }
        p += klen;
    }
    return -1;
}

/* ── JSON string buffer (for building output) ────────────────────── */

typedef struct {
    char  *buf;
    size_t cap;
    size_t off;
} jbuf_t;

static void jb_init(jbuf_t *jb, char *buf, size_t cap) {
    jb->buf = buf;
    jb->cap = cap;
    jb->off = 0;
    if (cap > 0) buf[0] = '\0';
}

static void jb_str(jbuf_t *jb, const char *s) {
    if (!s) return;
    while (*s && jb->off + 2 < jb->cap) {
        char c = *s;
        if (c == '"' || c == '\\') {
            if (jb->off + 3 >= jb->cap) break;
            jb->buf[jb->off++] = '\\';
            jb->buf[jb->off++] = c;
            s++;
        } else if (c == '\n') {
            if (jb->off + 3 >= jb->cap) break;
            jb->buf[jb->off++] = '\\';
            jb->buf[jb->off++] = 'n';
            s++;
        } else if ((unsigned char)c < 0x20) {
            s++;
        } else if ((unsigned char)c < 0x80) {
            jb->buf[jb->off++] = c;
            s++;
        } else {
            /* A non-ASCII byte must start a valid UTF-8 character, or the whole reply fails to
             * parse on the engine (Latin-1 / Shift-JIS names; a name cut mid-character). */
            int n = cheats_utf8_char_len(s);
            if (n == 0) {
                jb->buf[jb->off++] = '?';
                s++;
            } else {
                if (jb->off + (size_t)n + 2 >= jb->cap) break;
                memcpy(jb->buf + jb->off, s, (size_t)n);
                jb->off += (size_t)n;
                s += n;
            }
        }
    }
    jb->buf[jb->off] = '\0';
}

static void jb_raw(jbuf_t *jb, const char *s) {
    size_t len = strlen(s);
    if (jb->off + len >= jb->cap) len = jb->cap - jb->off - 1;
    memcpy(jb->buf + jb->off, s, len);
    jb->off += len;
    jb->buf[jb->off] = '\0';
}

#define JB_PRINTF(jb, ...) do { \
    char _tmp[512]; \
    int _n = snprintf(_tmp, sizeof(_tmp), __VA_ARGS__); \
    if (_n > 0) jb_raw((jb), _tmp); \
} while(0)

/* ── Running-game detection ──────────────────────────────────────── */

/* Find the pid + base address of the running big app (game).
 * Walks sysctl(KERN_PROC_PROC) and uses sceKernelGetAppInfo to find
 * a non-NPXS app. Returns pid (>0) on success, 0 if no game running.
 * Stores title_id and eboot base in the out params. */
static pid_t find_running_game(char *title_out, size_t title_cap,
                               intptr_t *base_out) {
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0};
    size_t buf_size = 0;
    if (sysctl(mib, 4, NULL, &buf_size, NULL, 0) != 0) return 0;
    if (buf_size == 0) return 0;

    uint8_t *buf = (uint8_t *)malloc(buf_size);
    if (!buf) return 0;
    if (sysctl(mib, 4, buf, &buf_size, NULL, 0) != 0) {
        free(buf);
        return 0;
    }

    pid_t found = 0;
    for (uint8_t *ptr = buf; ptr < buf + buf_size && !found; ) {
        int ki_structsize = *(int *)ptr;
        if (ki_structsize < KINFO_STRUCT_MINSIZE ||
            (size_t)(ptr - buf) + (size_t)ki_structsize > buf_size) break;
        pid_t pid = *(pid_t *)&ptr[KINFO_PID_OFFSET];
        app_info_t info;
        memset(&info, 0, sizeof(info));
        char tid[10];
        if (sceKernelGetAppInfo(pid, &info) == 0 &&
            info.app_id != 0 &&
            app_info_title_id(&info, tid, sizeof(tid)) &&
            strncmp(tid, "NPXS", 4) != 0) {
            found = pid;
            if (title_out && title_cap > 0) {
                snprintf(title_out, title_cap, "%s", tid);
            }
            if (base_out) {
                *base_out = kernel_dynlib_mapbase_addr(pid, 0);
            }
        }
        ptr += ki_structsize;
    }
    free(buf);
    return found;
}

/* Cached running-game lookup. elf-arsenal caches for 2s because the
 * sysctl + GetAppInfo walk is expensive and called from multiple paths. */
static pid_t get_running_game_cached(char *title_out, size_t title_cap,
                                     intptr_t *base_out) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    pthread_mutex_lock(&g_rg_lock);
    int64_t age_ms = (int64_t)(now.tv_sec - g_rg_cache.ts.tv_sec) * 1000 +
                     (int64_t)(now.tv_nsec - g_rg_cache.ts.tv_nsec) / 1000000;
    if (g_rg_cache.valid && age_ms < RG_CACHE_TTL_MS) {
        pid_t pid = g_rg_cache.pid;
        if (title_out && title_cap > 0)
            snprintf(title_out, title_cap, "%s", g_rg_cache.title);
        if (base_out) *base_out = g_rg_cache.base;
        pthread_mutex_unlock(&g_rg_lock);
        return pid;
    }
    pthread_mutex_unlock(&g_rg_lock);

    /* Cache miss — do the expensive lookup */
    char title[MAX_TITLE_ID] = "";
    intptr_t base = 0;
    pid_t pid = find_running_game(title, sizeof(title), &base);

    pthread_mutex_lock(&g_rg_lock);
    g_rg_cache.valid = 1;
    g_rg_cache.pid = pid;
    snprintf(g_rg_cache.title, sizeof(g_rg_cache.title), "%s", title);
    g_rg_cache.base = base;
    g_rg_cache.ts = now;
    pthread_mutex_unlock(&g_rg_lock);

    if (title_out && title_cap > 0)
        snprintf(title_out, title_cap, "%s", title);
    if (base_out) *base_out = base;
    return pid;
}

/* Invalidate the cache (used after toggles / reloads). */
static void invalidate_rg_cache(void) {
    pthread_mutex_lock(&g_rg_lock);
    g_rg_cache.valid = 0;
    pthread_mutex_unlock(&g_rg_lock);
}

/* ── Game memory access ──────────────────────────────────────────── */

/* Read and write the game's memory through its physical pages: walk the process page tables
 * to the physical address and copy through the kernel's direct map. Nothing about the game
 * changes except the bytes: no ptrace stop, no page-protection change.
 *
 * The engine used to PT_ATTACH the game, flip the protection byte of the vm_map entry holding
 * the address to RWX with kernel_mprotect, write with PT_IO, and "restore" it to R-X. Measured
 * on FW 13.60 (Marvel's Wolverine, 2026-10-18): the game's .text is ONE 95 MiB vm_map entry
 * (0x400000-0x6324000) whose protection is execute-only (0x04). Every toggle therefore made the
 * whole executable writable and then left it R-X, a protection it never had, under a PT_IO
 * write that has to copy-on-write a page of Sony's executable mapping. kernel_mprotect edits
 * the entry byte directly (no pmap update, no entry split), and CheatRunner traced two real
 * kernel panics / console freezes to exactly that call before it moved to this physical-page
 * path, which works on the same consoles. The user report: every Wolverine cheat crashed the
 * console with ps5upload, while CheatRunner worked.
 *
 * Which path, by firmware (the same split CheatRunner uses):
 *   - FW <= 8.20: mdbg (the debugger syscall). It writes copy-on-write, so the change stays
 *     private to the game.
 *   - FW >  8.20: the physical page. mdbg's write fails there (measured on 13.60), so there is
 *     no fallback: a failed physical write is a failed write. A physical write changes the page
 *     for EVERY process that maps it, so it is only allowed inside the game's own code: its
 *     eboot (dynlib handle 0) or a module loaded from its /app0. A Section, module_name or
 *     absolute address that lands in a system library is refused. */

static int use_mdbg(void) {
    static int fw = 0;
    if (!fw) fw = (int)(kernel_get_fw_version() >> 16);
    return fw <= 0x820;
}

static int phys_copy(pid_t pid, intptr_t addr, void *buf, size_t len, int write) {
    if (!KERNEL_ADDRESS_DMAP_BASE) return -1;
    size_t done = 0;
    while (done < len) {
        unsigned long pa = 0, plen = 0;
        pthread_mutex_lock(&kernel_rw_lock);
        int rc = kernel_proc_getpaddr(pid, (unsigned long)(addr + (intptr_t)done), &pa, &plen);
        if (rc == 0 && plen > 0) {
            size_t chunk = len - done;
            if (plen < chunk) chunk = (size_t)plen;
            rc = write ? kernel_copyin((uint8_t *)buf + done, KERNEL_ADDRESS_DMAP_BASE + pa, chunk)
                       : kernel_copyout(KERNEL_ADDRESS_DMAP_BASE + pa, (uint8_t *)buf + done, chunk);
            if (rc == 0) done += chunk;
        } else {
            rc = -1;
        }
        pthread_mutex_unlock(&kernel_rw_lock);
        if (rc != 0) return -1;
    }
    return 0;
}

static int mdbg_copy(pid_t pid, intptr_t addr, void *buf, size_t len, int write) {
    /* mdbg swaps this process's authid, so it runs inside the shared swap-window lock. */
    pthread_mutex_lock(&kernel_rw_lock);
    int rc = write ? mdbg_copyin(pid, buf, addr, len) : mdbg_copyout(pid, addr, buf, len);
    pthread_mutex_unlock(&kernel_rw_lock);
    return rc == 0 ? 0 : -1;
}

static int game_mem_read(pid_t pid, intptr_t addr, uint8_t *out, size_t len) {
    if (len == 0) return 0;
    if (!cheat_addr_ok(addr, (int)len)) return -1;
    if (use_mdbg()) return mdbg_copy(pid, addr, out, len, 0);
    if (phys_copy(pid, addr, out, len, 0) == 0) return 0;
    return mdbg_copy(pid, addr, out, len, 0); /* a read changes nothing */
}

/* The first fields of the kernel's dynlib object (third_party/sdk-crt/kernel.h dynlib_obj_t). */
typedef struct {
    uint64_t next;
    uint64_t path;
    uint64_t unknown0[2];
    uint32_t refcount;
    uint64_t handle;
    uint64_t mapbase;
    uint64_t mapsize;
} dynlib_head_t;
_Static_assert(offsetof(dynlib_head_t, handle) == 0x28, "dynlib_obj_t.handle");
_Static_assert(offsetof(dynlib_head_t, mapbase) == 0x30, "dynlib_obj_t.mapbase");

/* Where a physical write (FW > 8.20) may go.
 *
 * A physical write lands in the page every process mapping it shares, so a range inside a
 * loaded module must be inside one of the GAME's modules: its eboot (handle 0) or a module
 * whose path is under its /app0. A Section, module_name or absolute address that lands in a
 * system library (/system/..., /system_ex/...) is refused.
 *
 * A range in no module at all (heap, flexible/direct memory, the stack: what absolute and
 * data cheats point at) is written like CheatRunner writes it: the only gates are a canonical
 * user address (cheat_addr_ok) and a page that is present and readable through the same
 * page-table walk. CheatRunner does not inspect the vm_map entry for those either, on purpose:
 * walking vm_map entries (kernel_get_vmem_protection) panicked the kernel on PS4-BC games and on
 * PS5 games with special vmem entry types, so we do not either.
 *
 * Returns 1 when the write may go ahead; 0 with `why` set when it may not. */
static int phys_write_allowed(pid_t pid, intptr_t addr, size_t len, char *why, size_t why_cap) {
    int verdict = 1; /* no module overlaps: allowed (see above) */
    int walked = 0;
    char path[256] = "";
    uint64_t a = (uint64_t)addr, e = a + len;
    pthread_mutex_lock(&kernel_rw_lock);
    unsigned long kproc = (unsigned long)kernel_get_proc(pid);
    unsigned long obj = 0;
    if (kproc && kernel_copyout(kproc + 0x3e8, &obj, sizeof(obj)) == 0) {
        walked = 1;
        /* The list head holds the first object's address; each object starts with `next`. */
        for (int guard = 0; guard < 1024; guard++) {
            if (kernel_copyout(obj, &obj, sizeof(obj)) != 0) { walked = 0; break; }
            if (!obj) break;
            dynlib_head_t h;
            if (kernel_copyout(obj, &h, sizeof(h)) != 0) { walked = 0; break; }
            uint64_t ms = h.mapbase, me = h.mapbase + h.mapsize;
            if (!(a < me && ms < e)) continue; /* no overlap with this module */
            path[0] = '\0';
            if (h.path) (void)kernel_copyout(h.path, path, sizeof(path) - 1);
            path[sizeof(path) - 1] = '\0';
            int game_module = (h.handle == 0) || strstr(path, "/app0/") != NULL;
            if (!game_module) {
                if (why) snprintf(why, why_cap, "inside %s", path[0] ? path : "a system module");
                verdict = 0;
            } else if (!(a >= ms && e <= me)) {
                if (why) snprintf(why, why_cap, "crosses the edge of %s",
                                  path[0] ? path : "the game's module");
                verdict = 0;
            }
            break;
        }
    }
    pthread_mutex_unlock(&kernel_rw_lock);
    if (!walked) {
        if (why) snprintf(why, why_cap, "the game's module list could not be read");
        return 0;
    }
    return verdict;
}

typedef struct {
    pid_t pid;
} game_mem_ctx_t;

/* cheat_mem_ops_t.write: 0 when the bytes were written and read back equal. */
static int game_mem_write(void *opaque, intptr_t addr, const uint8_t *data, size_t len) {
    pid_t pid = ((game_mem_ctx_t *)opaque)->pid;
    if (len == 0) return 0;
    if (!cheat_addr_ok(addr, (int)len) || len > CHEAT_PLAN_MAX_BYTES) return -1;
    uint8_t back[CHEAT_PLAN_MAX_BYTES];
    if (use_mdbg()) {
        if (mdbg_copy(pid, addr, (void *)data, len, 1) != 0) return -1;
        if (mdbg_copy(pid, addr, back, len, 0) != 0) return -2;
    } else {
        if (phys_copy(pid, addr, (void *)data, len, 1) != 0) return -1;
        if (phys_copy(pid, addr, back, len, 0) != 0) return -2;
    }
    return memcmp(back, data, len) == 0 ? 0 : -3;
}

static int game_alive(void *opaque) {
    pid_t pid = ((game_mem_ctx_t *)opaque)->pid;
    return !(kill(pid, 0) != 0 && errno == ESRCH);
}

/* ── Module base resolution ──────────────────────────────────────── */

/* Resolve the base address of a named module in the target process.
 * NULL or empty → eboot base (handle 0). */
static intptr_t resolve_module_base(pid_t pid, const char *module_name) {
    if (!module_name || !module_name[0]) {
        return kernel_dynlib_mapbase_addr(pid, 0);
    }
    uint32_t handle = 0;
    if (kernel_dynlib_handle(pid, module_name, &handle) != 0 || !handle) {
        return 0;
    }
    return kernel_dynlib_mapbase_addr(pid, handle);
}

/* ── In-memory mod representation ────────────────────────────────── */

typedef struct {
    char    offset_str[32];
    char    on_str[256];
    char    off_str[256];
    int     absolute;
    int     section;
} mem_entry_t;

typedef struct {
    char        name[MAX_CHEAT_NAME];
    char        hint[MAX_CHEAT_NAME];
    char        type[16];       /* "checkbox" or "button" */
    char        module_name[128];
    int         enabled;        /* persisted toggle state */
    int         incomplete;     /* the file had more than the parser could hold: never apply */
    mem_entry_t mem[MAX_MEM_ENTRIES];
    int         mem_count;
} cheat_mod_t;

typedef struct {
    char        title_id[MAX_TITLE_ID];
    char        game_name[MAX_CHEAT_NAME];
    char        process[128];
    char        filepath[MAX_CHEAT_FILEPATH];
    int         format;         /* 1=json, 2=shn, 3=mc4 */
    cheat_mod_t mods[MAX_MODS_PER_FILE];
    int         mod_count;
} cheat_file_t;

/* ── JSON parser (parses one cheat file from disk) ───────────────── */

/* Extract the mods array from a JSON cheat file. The JSON format is
 * the canonical internal representation:
 *   {"name":"...","id":"PPSA...","mods":[{"name":"...","type":"checkbox",
 *    "memory":[{"offset":"...","on":"...","off":"..."}], ...}], ...}
 * We parse it with string scanning, same approach as runtime.c. */
static int parse_json_file(const char *json, size_t json_len,
                           cheat_file_t *cf) {
    (void)json_len;
    /* Extract top-level fields */
    json_str(json, "id", cf->title_id, sizeof(cf->title_id));
    json_str(json, "name", cf->game_name, sizeof(cf->game_name));
    json_str(json, "process", cf->process, sizeof(cf->process));

    /* Find "mods":[ */
    const char *p = strstr(json, "\"mods\"");
    if (!p) return 0;
    p = strchr(p, '[');
    if (!p) return 0;
    p++;

    int mod_idx = 0;
    while (*p && mod_idx < MAX_MODS_PER_FILE) {
        /* Skip to next '{' */
        while (*p && *p != '{') p++;
        if (!*p || *p == ']') break;

        /* Find the matching close brace by depth counting */
        int depth = 0;
        const char *mod_start = p;
        while (*p) {
            if (*p == '{') depth++;
            else if (*p == '}') { depth--; if (depth == 0) break; }
            p++;
        }
        if (!*p) break;
        const char *mod_end = p + 1;

        /* Extract this mod's substring */
        size_t mod_len = (size_t)(mod_end - mod_start);
        char mod_buf[8192];
        int mod_cut = 0;
        if (mod_len >= sizeof(mod_buf)) { mod_len = sizeof(mod_buf) - 1; mod_cut = 1; }
        memcpy(mod_buf, mod_start, mod_len);
        mod_buf[mod_len] = '\0';

        cheat_mod_t *m = &cf->mods[mod_idx];
        memset(m, 0, sizeof(*m));
        strcpy(m->type, "checkbox");
        m->incomplete = mod_cut;

        json_str(mod_buf, "name", m->name, sizeof(m->name));
        json_str(mod_buf, "hint", m->hint, sizeof(m->hint));
        json_str(mod_buf, "type", m->type, sizeof(m->type));
        json_str(mod_buf, "module_name", m->module_name,
                 sizeof(m->module_name));

        int bval = 0;
        if (json_bool(mod_buf, "_sonic_enabled", &bval) == 0) {
            m->enabled = bval;
        }

        /* Parse memory array */
        const char *mp = strstr(mod_buf, "\"memory\"");
        if (mp) {
            mp = strchr(mp, '[');
            if (mp) {
                mp++;
                int mi = 0;
                while (*mp && mi < MAX_MEM_ENTRIES) {
                    while (*mp && *mp != '{') mp++;
                    if (!*mp || *mp == ']') break;

                    depth = 0;
                    const char *me_start = mp;
                    while (*mp) {
                        if (*mp == '{') depth++;
                        else if (*mp == '}') {
                            depth--;
                            if (depth == 0) break;
                        }
                        mp++;
                    }
                    if (!*mp) break;
                    const char *me_end = mp + 1;

                    size_t me_len = (size_t)(me_end - me_start);
                    char me_buf[2048];
                    if (me_len >= sizeof(me_buf)) {
                        me_len = sizeof(me_buf) - 1;
                        m->incomplete = 1;
                    }
                    memcpy(me_buf, me_start, me_len);
                    me_buf[me_len] = '\0';

                    mem_entry_t *e = &m->mem[mi];
                    memset(e, 0, sizeof(*e));
                    json_str(me_buf, "offset", e->offset_str,
                             sizeof(e->offset_str));
                    json_str(me_buf, "on", e->on_str,
                             sizeof(e->on_str));
                    json_str(me_buf, "off", e->off_str,
                             sizeof(e->off_str));
                    int64_t sv = 0;
                    if (json_num(me_buf, "section", &sv) == 0)
                        e->section = (int)sv;
                    int bv = 0;
                    if (json_bool(me_buf, "absolute", &bv) == 0)
                        e->absolute = bv;

                    if (e->offset_str[0]) mi++;
                    mp = me_end;
                }
                /* Stopped at the cap with another entry still to come. */
                if (mi >= MAX_MEM_ENTRIES && strchr(mp, '{')) m->incomplete = 1;
                m->mem_count = mi;
            }
        }

        mod_idx++;
        p = mod_end;
    }

    cf->mod_count = mod_idx;
    return mod_idx;
}

/* ── SHN (XML) parser ────────────────────────────────────────────── */

/* Tiny XML attribute extractor. Finds attr="value" in tag. */
static int xml_attr(const char *tag, const char *attr,
                    char *out, size_t cap) {
    if (!tag || !attr || !out || cap == 0) return -1;
    out[0] = '\0';
    size_t alen = strlen(attr);
    const char *p = tag;
    while ((p = strstr(p, attr))) {
        if ((size_t)(p - tag) >= alen || p == tag || p[-1] == ' ' ||
            p[-1] == '\t') {
            p += alen;
            while (*p == ' ' || *p == '\t' || *p == '=') p++;
            if (*p == '"') {
                p++;
                size_t i = 0;
                while (*p && *p != '"' && i + 1 < cap) {
                    out[i++] = *p++;
                }
                out[i] = '\0';
                return 0;
            }
        }
        p += alen;
    }
    return -1;
}

/* Extract text between <tag>...</tag>. */
static int xml_text(const char *start, const char *tag_name,
                    char *out, size_t cap) {
    if (!start || !tag_name || !out || cap == 0) return -1;
    out[0] = '\0';
    char open_tag[64];
    char close_tag[64];
    snprintf(open_tag, sizeof(open_tag), "<%s", tag_name);
    snprintf(close_tag, sizeof(close_tag), "</%s>", tag_name);

    const char *p = strstr(start, open_tag);
    if (!p) return -1;
    p += strlen(open_tag);
    while (*p && *p != '>') p++;  /* skip attrs */
    if (*p == '>') p++;

    const char *end = strstr(p, close_tag);
    if (!end) return -1;

    size_t len = (size_t)(end - p);
    if (len >= cap) len = cap - 1;
    memcpy(out, p, len);
    out[len] = '\0';

    /* Trim whitespace */
    while (len > 0 && isspace((unsigned char)out[len-1])) out[--len] = '\0';
    while (isspace((unsigned char)out[0])) memmove(out, out+1, strlen(out));

    return 0;
}

/* Find next occurrence of <TagName in xml text starting from cursor.
 * Returns pointer to the '<', or NULL. Updates *next_end to point past
 * the tag's '>' (self-closing or paired). */
static const char *xml_find_tag(const char *cursor, const char *tag_name,
                                const char **tag_start,
                                const char **tag_end) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "<%s", tag_name);
    const char *p = strstr(cursor, pattern);
    if (!p) return NULL;
    *tag_start = p;
    const char *end = strchr(p, '>');
    if (!end) return NULL;
    *tag_end = end + 1;
    return p;
}

/* Parse SHN (XML trainer) format. The XML schema is:
 *   <Trainer Game="..." Version="...">
 *     <Cheat Text="Infinite Health" Description="...">
 *       <Cheatline>
 *         <Offset>01A4F800</Offset>
 *         <ValueOn>901F0000</ValueOn>
 *         <ValueOff>801F0000</ValueOff>
 *         <Absolute>false</Absolute>
 *         <Section>0</Section>
 *       </Cheatline>
 *     </Cheat>
 *   </Trainer> */
static int parse_shn_file(const char *xml, size_t xml_len,
                          cheat_file_t *cf) {
    (void)xml_len;
    cf->process[0] = '\0';

    /* Extract game name from <Trainer Game="..."> */
    const char *trainer = strstr(xml, "<Trainer");
    if (trainer) {
        xml_attr(trainer, "Game", cf->game_name, sizeof(cf->game_name));
        if (!cf->game_name[0])
            xml_attr(trainer, "GameName", cf->game_name,
                     sizeof(cf->game_name));
    }

    /* Iterate over <Cheat> tags */
    int mod_idx = 0;
    const char *cursor = xml;
    while (mod_idx < MAX_MODS_PER_FILE) {
        const char *cheat_start, *cheat_end;
        if (!xml_find_tag(cursor, "Cheat", &cheat_start, &cheat_end)) break;

        /* Extract the full Cheat tag content (to its matching </Cheat>) */
        const char *close_cheat = strstr(cheat_end, "</Cheat>");
        if (!close_cheat) break;

        size_t cheat_len = (size_t)(close_cheat - cheat_end);
        char cheat_buf[8192];
        int cheat_cut = 0;
        if (cheat_len >= sizeof(cheat_buf)) { cheat_len = sizeof(cheat_buf) - 1; cheat_cut = 1; }
        memcpy(cheat_buf, cheat_end, cheat_len);
        cheat_buf[cheat_len] = '\0';

        cheat_mod_t *m = &cf->mods[mod_idx];
        memset(m, 0, sizeof(*m));
        strcpy(m->type, "checkbox");
        m->incomplete = cheat_cut;

        xml_attr(cheat_start, "Text", m->name, sizeof(m->name));
        if (!m->name[0])
            xml_attr(cheat_start, "CheatName", m->name, sizeof(m->name));
        if (!m->name[0])
            xml_attr(cheat_start, "Name", m->name, sizeof(m->name));

        char desc[256];
        if (xml_attr(cheat_start, "Description", desc, sizeof(desc)) == 0) {
            snprintf(m->hint, sizeof(m->hint), "%s", desc);
        }

        /* Iterate over <Cheatline> tags within this Cheat */
        int mi = 0;
        const char *cl_cursor = cheat_buf;
        while (mi < MAX_MEM_ENTRIES) {
            const char *cl_start, *cl_end;
            if (!xml_find_tag(cl_cursor, "Cheatline", &cl_start, &cl_end))
                break;

            const char *cl_close = strstr(cl_end, "</Cheatline>");
            const char *cl_real_end;
            if (cl_close) {
                cl_real_end = cl_close + strlen("</Cheatline>");
            } else {
                /* Self-closing: <Cheatline ... /> */
                cl_real_end = cl_end;
                cl_close = cl_start + strlen("<Cheatline");
            }

            size_t cl_len;
            if (cl_close && cl_close > cl_end) {
                cl_len = (size_t)(cl_close - cl_end);
            } else {
                cl_len = 0;
            }

            char cl_buf[2048];
            if (cl_len >= sizeof(cl_buf)) { cl_len = sizeof(cl_buf) - 1; m->incomplete = 1; }
            memcpy(cl_buf, cl_end, cl_len);
            cl_buf[cl_len] = '\0';

            mem_entry_t *e = &m->mem[mi];
            memset(e, 0, sizeof(*e));

            /* For self-closing tags, extract attributes directly */
            char cl_full[2048];
            size_t full_len = (size_t)(cl_real_end - cl_start);
            if (full_len >= sizeof(cl_full)) { full_len = sizeof(cl_full) - 1; m->incomplete = 1; }
            memcpy(cl_full, cl_start, full_len);
            cl_full[full_len] = '\0';

            /* Try attributes first (self-closing form) */
            xml_attr(cl_full, "Offset", e->offset_str, sizeof(e->offset_str));
            xml_attr(cl_full, "ValueOn", e->on_str, sizeof(e->on_str));
            xml_attr(cl_full, "ValueOff", e->off_str, sizeof(e->off_str));

            char abs_str[8] = "";
            xml_attr(cl_full, "Absolute", abs_str, sizeof(abs_str));
            if (abs_str[0] == 't' || abs_str[0] == 'T' || abs_str[0] == '1')
                e->absolute = 1;

            char sec_str[8] = "";
            xml_attr(cl_full, "Section", sec_str, sizeof(sec_str));
            if (sec_str[0]) e->section = atoi(sec_str);

            /* Try child tags (paired form) */
            if (!e->offset_str[0])
                xml_text(cl_full, "Offset", e->offset_str, sizeof(e->offset_str));
            if (!e->on_str[0])
                xml_text(cl_full, "ValueOn", e->on_str, sizeof(e->on_str));
            if (!e->off_str[0])
                xml_text(cl_full, "ValueOff", e->off_str, sizeof(e->off_str));

            if (!e->absolute) {
                char tmp[8] = "";
                xml_text(cl_full, "Absolute", tmp, sizeof(tmp));
                if (tmp[0] == 't' || tmp[0] == 'T' || tmp[0] == '1')
                    e->absolute = 1;
            }
            if (!e->section) {
                char tmp[8] = "";
                xml_text(cl_full, "Section", tmp, sizeof(tmp));
                if (tmp[0]) e->section = atoi(tmp);
            }

            if (e->offset_str[0]) mi++;
            cl_cursor = cl_real_end;
        }
        {
            const char *dummy_s, *dummy_e;
            if (mi >= MAX_MEM_ENTRIES && xml_find_tag(cl_cursor, "Cheatline", &dummy_s, &dummy_e))
                m->incomplete = 1;
        }
        m->mem_count = mi;

        if (m->name[0] || mi > 0) mod_idx++;
        cursor = close_cheat + strlen("</Cheat>");
    }

    cf->mod_count = mod_idx;
    return mod_idx;
}

/* ── File discovery ──────────────────────────────────────────────── */

/* Check if filename starts with <title_id> followed by . or _ */
static int filename_matches_title(const char *filename, const char *title_id) {
    size_t tlen = strlen(title_id);
    if (strncasecmp(filename, title_id, tlen) != 0) return 0;
    char next = filename[tlen];
    return (next == '.' || next == '_');
}

typedef struct {
    char path[MAX_CHEAT_FILEPATH];
    int  format;  /* 1=json, 2=shn, 3=mc4 */
} found_file_t;

/* Scan all cheat directories for files matching the title_id.
 * Returns count found (0 to N). */
static int find_cheat_files(const char *title_id,
                            found_file_t *out, int max_out,
                            int is_patches) {
    const char *json_dir = is_patches ? PATCHES_JSON_DIR : CHEATS_JSON_DIR;
    const char *shn_dir = is_patches ? PATCHES_SHN_DIR : CHEATS_SHN_DIR;
    const char *mc4_dir = is_patches ? PATCHES_MC4_DIR : CHEATS_MC4_DIR;
    const char *dirs[] = {json_dir, shn_dir, mc4_dir};
    int formats[] = {1, 2, 3};
    int count = 0;

    for (int d = 0; d < 3 && count < max_out; d++) {
        DIR *dir = opendir(dirs[d]);
        if (!dir) continue;
        struct dirent *de;
        while ((de = readdir(dir)) && count < max_out) {
            if (de->d_name[0] == '.') continue;
            const char *ext = strrchr(de->d_name, '.');
            if (!ext) continue;

            int fmt = 0;
            if (formats[d] == 1 && strcasecmp(ext, ".json") == 0) fmt = 1;
            else if (formats[d] == 2 && strcasecmp(ext, ".shn") == 0) fmt = 2;
            else if (formats[d] == 3 && strcasecmp(ext, ".mc4") == 0) fmt = 3;
            if (!fmt) continue;

            if (!filename_matches_title(de->d_name, title_id)) continue;

            snprintf(out[count].path, sizeof(out[count].path),
                     "%s/%s", dirs[d], de->d_name);
            out[count].format = fmt;
            count++;
        }
        closedir(dir);
    }
    return count;
}

/* ── MC4 (encrypted XML) ─────────────────────────────────────────────
 *
 * An MC4 file is base64 text of an AES-256-CBC ciphertext; decrypted it is
 * the same <Trainer>/<Cheat>/<Cheatline> XML as SHN, so it is handed to the
 * SHN parser once decrypted. The key and IV are fixed constants baked into
 * the format (public, not console-derived), so no device secrets are needed.
 * Recovered from OnionHEN's mc4_cheat_parser and verified against its fixture.
 */
/* The 32-byte key and 16-byte IV are the ASCII bytes of these literals; the
 * arrays are auto-sized (a trailing NUL rides along, which AES never reads). */
static const uint8_t MC4_AES_KEY[] = "304c6528f659c766110239a51cl5dd9c";
static const uint8_t MC4_AES_IV[] = "u@}kzW2u[u(8DWar";

/* Standard base64 decode. Ignores whitespace; returns bytes written, or -1
 * on a malformed character. `out` must hold at least `3*(in_len/4)` bytes. */
static int mc4_base64_decode(const char *in, size_t in_len, uint8_t *out, size_t out_cap) {
    static const int8_t T[256] = {
        ['A']=0,['B']=1,['C']=2,['D']=3,['E']=4,['F']=5,['G']=6,['H']=7,['I']=8,
        ['J']=9,['K']=10,['L']=11,['M']=12,['N']=13,['O']=14,['P']=15,['Q']=16,
        ['R']=17,['S']=18,['T']=19,['U']=20,['V']=21,['W']=22,['X']=23,['Y']=24,
        ['Z']=25,['a']=26,['b']=27,['c']=28,['d']=29,['e']=30,['f']=31,['g']=32,
        ['h']=33,['i']=34,['j']=35,['k']=36,['l']=37,['m']=38,['n']=39,['o']=40,
        ['p']=41,['q']=42,['r']=43,['s']=44,['t']=45,['u']=46,['v']=47,['w']=48,
        ['x']=49,['y']=50,['z']=51,['0']=52,['1']=53,['2']=54,['3']=55,['4']=56,
        ['5']=57,['6']=58,['7']=59,['8']=60,['9']=61,['+']=62,['/']=63,
    };
    size_t o = 0;
    uint32_t acc = 0;
    int bits = 0;
    for (size_t i = 0; i < in_len; i++) {
        unsigned char c = (unsigned char)in[i];
        if (c == '=' ) break;
        if (c == '\r' || c == '\n' || c == ' ' || c == '\t') continue;
        /* T[] is zero for every unlisted byte, so membership is checked
         * against the alphabet; otherwise garbage decoded as 'A'. */
        /* strchr matches the terminator, so a NUL byte must be refused explicitly. */
        if (c == 0 || !strchr("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/", c))
            return -1;
        int8_t v = T[c];
        acc = (acc << 6) | (uint32_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (o >= out_cap) return -1;
            out[o++] = (uint8_t)((acc >> bits) & 0xFF);
        }
    }
    return (int)o;
}

/* Decrypt an MC4 file in place-ish and parse it as SHN XML. */
static int parse_mc4_file(const char *encoded, size_t enc_len, cheat_file_t *cf) {
    /* base64 → binary */
    size_t cap = (enc_len / 4 + 1) * 3 + 16;
    uint8_t *bin = (uint8_t *)malloc(cap);
    if (!bin) return -1;
    int bin_len = mc4_base64_decode(encoded, enc_len, bin, cap);
    /* AES-256-CBC works on whole 16-byte blocks. */
    if (bin_len <= 0 || (bin_len % 16) != 0) {
        free(bin);
        return -1;
    }

    struct AES_ctx ctx;
    AES_init_ctx_iv(&ctx, MC4_AES_KEY, MC4_AES_IV);
    AES_CBC_decrypt_buffer(&ctx, bin, (size_t)bin_len);

    /* The plaintext is NUL-terminated XML; PKCS pad bytes at the tail are
     * harmless — the SHN parser stops at the closing tags. */
    char *xml = (char *)realloc(bin, (size_t)bin_len + 1);
    if (!xml) { free(bin); return -1; }
    xml[bin_len] = '\0';

    /* The decrypted XML can itself be UTF-16 (same trainer tools) — normalise
     * it before handing off to the ASCII-oriented SHN parser. */
    size_t clen = 0;
    char *conv = xml_to_utf8(xml, (size_t)bin_len, &clen);
    /* Real MC4 plaintext is entity/backslash-escaped XML ("&lt;Trainer
     * Game=\&quot;..."). Without unescaping it the parser sees no tags and
     * every MC4 file lists zero cheats (R16, #373). */
    if (conv) clen = xml_unescape_entities(conv, clen);
    else      bin_len = (int)xml_unescape_entities(xml, (size_t)bin_len);
    int rc = conv ? parse_shn_file(conv, clen, cf)
                  : parse_shn_file(xml, (size_t)bin_len, cf);
    free(conv);
    free(xml);
    return rc;
}

/* Load and parse a cheat file from disk. Returns 0 on success. */
static int load_cheat_file(const char *path, int format, cheat_file_t *cf) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size > CHEAT_FILE_MAX) {
        close(fd);
        return -1;
    }
    size_t fsize = (size_t)st.st_size;
    char *buf = (char *)malloc(fsize + 1);
    if (!buf) { close(fd); return -1; }
    ssize_t rd = read(fd, buf, fsize);
    close(fd);
    if (rd < 0) { free(buf); return -1; }
    buf[rd] = '\0';

    memset(cf, 0, sizeof(*cf));
    snprintf(cf->filepath, sizeof(cf->filepath), "%s", path);
    cf->format = format;

    int rc = -1;
    if (format == 1) {
        rc = parse_json_file(buf, (size_t)rd, cf);
    } else if (format == 2) {
        /* SHN trainer XML is sometimes saved UTF-16; the ASCII tag scan in
         * parse_shn_file can't see tags there, so down-convert first. */
        size_t clen = 0;
        char *conv = xml_to_utf8(buf, (size_t)rd, &clen);
        if (conv) {
            rc = parse_shn_file(conv, clen, cf);
            free(conv);
        } else {
            rc = parse_shn_file(buf, (size_t)rd, cf);
        }
    } else if (format == 3) {
        /* MC4 = base64 + AES-256-CBC over SHN-shaped XML. */
        rc = parse_mc4_file(buf, (size_t)rd, cf);
    }

    free(buf);
    return (rc >= 0) ? 0 : -1;
}

/* ── State persistence (sidecar JSON) ────────────────────────────── */

static void state_path(const char *title_id, char *out, size_t cap) {
    snprintf(out, cap, "%s/%s.json", CHEATS_STATE_DIR, title_id);
}

/* The sidecar is {"<flat index>":true,...}: one flat index across every cheat file of the
 * title, in find_cheat_files() order — the same numbering the mod list and toggles use.
 * Every key fits: 1024 mods at most (64 per file x 16 files), about 11 KiB. */
#define STATE_FILE_MAX (64 * 1024)

/* The whole sidecar as a NUL-terminated heap string ("" when there is none), or NULL when it
 * exists but cannot be read in full. Caller frees. */
static char *state_read(const char *title_id) {
    char path[MAX_CHEAT_FILEPATH];
    state_path(title_id, path, sizeof(path));
    int fd = open(path, O_RDONLY);
    if (fd < 0) return errno == ENOENT ? strdup("") : NULL;
    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size < 0 || st.st_size > STATE_FILE_MAX) {
        close(fd);
        return NULL;
    }
    size_t size = (size_t)st.st_size;
    char *buf = (char *)malloc(size + 1);
    if (!buf) { close(fd); return NULL; }
    size_t got = 0;
    while (got < size) {
        ssize_t rd = read(fd, buf + got, size - got);
        if (rd <= 0) break;
        got += (size_t)rd;
    }
    close(fd);
    if (got < size) { /* a short read: never hand back a state with keys missing */
        free(buf);
        return NULL;
    }
    buf[got] = '\0';
    return buf;
}

static int state_has(const char *buf, int flat) {
    char key[24];
    snprintf(key, sizeof(key), "\"%d\"", flat);
    const char *p = strstr(buf, key);
    if (!p) return 0;
    p += strlen(key);
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == ':') p++;
    return strncmp(p, "true", 4) == 0;
}

/* Load enabled state from sidecar into one file's mods, whose first mod has flat index
 * `flat_base`. */
static void cheats_load_state(const char *title_id, cheat_file_t *cf, int flat_base) {
    char *buf = state_read(title_id);
    if (!buf) return;
    for (int i = 0; i < cf->mod_count; i++)
        if (state_has(buf, flat_base + i)) cf->mods[i].enabled = 1;
    free(buf);
}

/* Record one mod's state, keeping every other mod's. (This used to rewrite the file from the
 * one cheat file being toggled with per-file indices, so a toggle in a title's second file
 * marked the first file's mods instead, and the watcher re-applied those on the next launch.)
 * Call with g_apply_lock held. Returns 0 on success; a sidecar it cannot read is left alone
 * rather than rewritten without its other keys. */
static int state_set(const char *title_id, int flat, int on) {
    char *buf = state_read(title_id);
    if (!buf) return -1;

    char path[MAX_CHEAT_FILEPATH];
    state_path(title_id, path, sizeof(path));
    char tmp[MAX_CHEAT_FILEPATH + 8];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if (!f) { free(buf); return -1; }
    fprintf(f, "{");
    int first = 1;
    for (int i = 0; i < MAX_MODS_PER_FILE * 16; i++) {
        int en = (i == flat) ? on : state_has(buf, i);
        if (!en) continue;
        fprintf(f, "%s\"%d\":true", first ? "" : ",", i);
        first = 0;
    }
    fprintf(f, "}");
    free(buf);
    if (fclose(f) != 0) {
        unlink(tmp);
        return -1;
    }
    return rename(tmp, path) == 0 ? 0 : -1;
}

/* ── Core: apply a single mod ────────────────────────────────────── */

/* Parse a mod's locations and work out their addresses (no memory access). Returns the number
 * of locations, or -1 with err set. */
static int resolve_mod_locs(pid_t pid, intptr_t base, const cheat_mod_t *mod, cheat_loc_t *locs,
                            char *err, size_t err_cap) {
    if (mod->incomplete) {
        if (err) snprintf(err, err_cap,
                          "cheat has more locations than the engine can hold; not applied");
        return -1;
    }
    if (mod->mem_count == 0) {
        if (err) snprintf(err, err_cap, "mod has no memory entries");
        return -1;
    }
    /* No base yet means the module is not mapped: a relative offset would be used as an
     * absolute address. */
    if (base <= 0) {
        if (err) snprintf(err, err_cap, "the game's code is not loaded yet; try again");
        return -1;
    }
    int n = mod->mem_count;
    for (int i = 0; i < n; i++) {
        const mem_entry_t *e = &mod->mem[i];
        cheat_loc_t *l = &locs[i];
        uint64_t off = 0;
        if (cheat_parse_offset(e->offset_str, &off) != 0) {
            if (err) snprintf(err, err_cap, "location %d: bad offset \"%s\"", i + 1, e->offset_str);
            return -1;
        }
        if (strlen(e->on_str) + 1 >= sizeof(e->on_str) ||
            strlen(e->off_str) + 1 >= sizeof(e->off_str)) {
            if (err) snprintf(err, err_cap, "location %d: value too long for the engine", i + 1);
            return -1;
        }
        int on_len = cheat_parse_hex(e->on_str, l->on, CHEAT_PLAN_MAX_BYTES);
        int off_len = cheat_parse_hex(e->off_str, l->off, CHEAT_PLAN_MAX_BYTES);
        if (on_len <= 0 || off_len <= 0) {
            if (err) snprintf(err, err_cap,
                              "location %d: needs both its cheat and original bytes", i + 1);
            return -1;
        }
        if (on_len != off_len) {
            if (err) snprintf(err, err_cap,
                              "location %d: cheat and original bytes differ in length (%d vs %d)",
                              i + 1, on_len, off_len);
            return -1;
        }
        l->len = on_len;
        if (e->absolute) {
            l->addr = (intptr_t)off;
        } else if (e->section > 0) {
            intptr_t sec_base = kernel_dynlib_mapbase_addr(pid, (uint32_t)e->section);
            if (sec_base <= 0) {
                if (err) snprintf(err, err_cap, "location %d: module %d is not loaded",
                                  i + 1, e->section);
                return -1;
            }
            l->addr = (intptr_t)((uint64_t)sec_base + off);
        } else {
            l->addr = (intptr_t)((uint64_t)base + off);
        }
        if (!cheat_addr_ok(l->addr, l->len)) {
            if (err) snprintf(err, err_cap, "location %d: address 0x%lx is not a game address",
                              i + 1, (unsigned long)l->addr);
            return -1;
        }
    }
    return n;
}

/* Apply or remove one cheat, all of its locations or none of them (cheat_plan_apply does the
 * checking, ordering and rollback). `keep` lists the locations of the title's other enabled
 * cheats, so turning this one off leaves a code cave they share alone.
 *
 * The game keeps running while the writes land, as with CheatRunner: stopping it (ptrace, or
 * SIGSTOP, which also tells SceSysCore through SIGCHLD) froze games. What keeps a thread from
 * running half a cheat is the write order: caves before the jumps into them when turning on,
 * the jumps restored before their caves when turning off.
 *
 * Returns 0 when the cheat is in the asked-for state (including "it already was"). */
static int apply_mod_locked(pid_t pid, intptr_t base, cheat_mod_t *mod, int turn_on,
                            const cheat_range_t *keep, int n_keep, char *err, size_t err_cap) {
    /* "button" type always writes "on" regardless of turn_on */
    int write_on = (strcmp(mod->type, "button") == 0) ? 1 : turn_on;

    cheat_loc_t *locs = (cheat_loc_t *)calloc(MAX_MEM_ENTRIES, sizeof(cheat_loc_t));
    if (!locs) {
        if (err) snprintf(err, err_cap, "out of memory");
        return -1;
    }
    int n = resolve_mod_locs(pid, base, mod, locs, err, err_cap);
    int rc = n > 0 ? 0 : -1;

    for (int i = 0; i < n && rc == 0; i++) {
        cheat_loc_t *l = &locs[i];
        if (!use_mdbg()) {
            char why[160] = "";
            if (!phys_write_allowed(pid, l->addr, (size_t)l->len, why, sizeof(why))) {
                if (err) snprintf(err, err_cap,
                                  "location %d (0x%lx) is not the game's own memory (%s); "
                                  "nothing was written",
                                  i + 1, (unsigned long)l->addr, why);
                rc = -1;
                break;
            }
        }
        if (game_mem_read(pid, l->addr, l->cur, (size_t)l->len) != 0) {
            if (err) snprintf(err, err_cap, "location %d (0x%lx) cannot be read",
                              i + 1, (unsigned long)l->addr);
            rc = -1;
        }
    }

    if (rc == 0) {
        game_mem_ctx_t ctx = { .pid = pid };
        cheat_mem_ops_t ops = { .ctx = &ctx, .write = game_mem_write, .alive = game_alive };
        rc = cheat_plan_apply(locs, n, write_on, keep, n_keep, &ops, err, err_cap);
    }

    free(locs);
    return rc;
}

/* One apply at a time: the PC app, the console page and the watcher can all ask at once, and
 * two applies of overlapping cheats must not interleave their reads and writes. The toggle
 * state file is written under the same lock. */
static pthread_mutex_t g_apply_lock = PTHREAD_MUTEX_INITIALIZER;

static int apply_mod(pid_t pid, intptr_t base, cheat_mod_t *mod,
                     int turn_on, char *err, size_t err_cap) {
    pthread_mutex_lock(&g_apply_lock);
    int rc = apply_mod_locked(pid, base, mod, turn_on, NULL, 0, err, err_cap);
    pthread_mutex_unlock(&g_apply_lock);
    return rc;
}

/* ── Background watcher thread ───────────────────────────────────── */

/* Is this cheat file's "name" actually a game's name?
 *
 * Measured against the published repos: plenty of cheat files carry a title id
 * ("PPSA21564") or a content id ("PPSA23226_00-GAME000000000000") in the name
 * field. Announcing "1 cheat applied to PPSA23226_00-GAME000000000000" on the
 * player's TV is worse than announcing the title id, because it reads as
 * corruption rather than as a missing name. The client applies the same test
 * to what it displays. */
static int is_usable_game_name(const char *name, const char *title_id) {
    if (!name || !name[0]) return 0;
    /* A bare title id tells the player nothing they do not have already. */
    if (title_id && title_id[0] && strcasecmp(name, title_id) == 0) return 0;
    /* Content ids embed the title id followed by '_' or '-'. */
    if (title_id && title_id[0]) {
        size_t n = strlen(title_id);
        if (strncasecmp(name, title_id, n) == 0 &&
            (name[n] == '_' || name[n] == '-')) return 0;
    }
    return 1;
}

/* Returns how many memory writes actually landed, and fills `name_out` with
 * the game's own name when a cheat file carries one — the console
 * notification reads far better as the game's title than as a title id. */
static int apply_patches_for_game(pid_t pid, intptr_t base,
                                  const char *title_id,
                                  char *name_out, size_t name_cap) {
    found_file_t files[16];
    int n = find_cheat_files(title_id, files, 16, 1 /* patches */);

    int total_writes = 0;
    for (int i = 0; i < n; i++) {
        cheat_file_t *cf = (cheat_file_t *)malloc(sizeof(cheat_file_t));
        if (!cf) continue;
        if (load_cheat_file(files[i].path, files[i].format, cf) != 0) {
            free(cf);
            continue;
        }

        if (name_out && name_cap && !name_out[0] &&
            is_usable_game_name(cf->game_name, title_id)) {
            snprintf(name_out, name_cap, "%s", cf->game_name);
        }
        for (int m = 0; m < cf->mod_count; m++) {
            char err[160];
            /* Patches are always-on: turn_on = 1 */
            intptr_t mod_base = base;
            if (cf->mods[m].module_name[0])
                mod_base = resolve_module_base(pid, cf->mods[m].module_name);
            if (mod_base && apply_mod(pid, mod_base, &cf->mods[m], 1, err, sizeof(err)) == 0) {
                total_writes++;
            }
        }
        free(cf);
    }
    atomic_store(&g_patches_last, n);
    atomic_fetch_add(&g_patches_total, total_writes);
    return total_writes;
}

/* Returns how many enabled cheats were re-applied. See the note above on
 * `name_out`. */
static int reapply_enabled_for_game(pid_t pid, intptr_t base,
                                    const char *title_id,
                                    char *name_out, size_t name_cap) {
    found_file_t files[16];
    int n = find_cheat_files(title_id, files, 16, 0 /* cheats */);

    int applied = 0;
    int flat_base = 0;
    for (int i = 0; i < n; i++) {
        cheat_file_t *cf = (cheat_file_t *)malloc(sizeof(cheat_file_t));
        if (!cf) continue;
        /* A file that fails to load lists no mods, so it adds nothing to the flat numbering
         * (cheats_list_mods skips it the same way). */
        if (load_cheat_file(files[i].path, files[i].format, cf) != 0) {
            free(cf);
            continue;
        }
        cheats_load_state(title_id, cf, flat_base);
        flat_base += cf->mod_count;

        for (int m = 0; m < cf->mod_count; m++) {
            if (!cf->mods[m].enabled) continue;
            if (name_out && name_cap && !name_out[0] &&
                is_usable_game_name(cf->game_name, title_id)) {
                snprintf(name_out, name_cap, "%s", cf->game_name);
            }
            char err[160];
            intptr_t mod_base = base;
            if (cf->mods[m].module_name[0])
                mod_base = resolve_module_base(pid, cf->mods[m].module_name);
            if (mod_base && apply_mod(pid, mod_base, &cf->mods[m], 1, err, sizeof(err)) == 0)
                applied++;
        }
        free(cf);
    }
    return applied;
}

static void *watcher_thread(void *arg) {
    /* Name this thread: the process listing shows a representative
     * thread that is not reliably main, so an unnamed worker makes the
     * whole process read as "payload.elf". See proc_identity.h. */
    proc_name_set_self(PS5UPLOAD2_PROC_NAME);
    (void)arg;
    pid_t last_pid = 0;

    while (1) {
        sleep(3);

        if (!atomic_load(&g_engine_enabled)) {
            last_pid = 0;
            continue;
        }

        char title[MAX_TITLE_ID] = "";
        intptr_t base = 0;
        pid_t pid = get_running_game_cached(title, sizeof(title), &base);

        if (pid <= 0) {
            last_pid = 0;
            continue;
        }

        if (pid == last_pid) continue;
        last_pid = pid;

        invalidate_rg_cache();

        /* Auto-apply patches + re-apply user-toggled cheats */
        char game_name[MAX_CHEAT_NAME] = "";
        int applied = apply_patches_for_game(pid, base, title,
                                             game_name, sizeof(game_name));
        applied += reapply_enabled_for_game(pid, base, title,
                                            game_name, sizeof(game_name));

        /* Tell the player on the console. Without this the engine works
         * silently and there is no way to know a cheat actually took — the
         * request most asked for in issue #315.
         *
         * Fired here rather than per mod: this block runs once per game
         * session (the `pid == last_pid` guard above), so the player gets one
         * notification when the game starts rather than a burst of them. Only
         * when something actually landed — announcing "0 cheats" on every
         * launch of every game would be noise. */
        if (applied > 0) {
            char msg[256];
            snprintf(msg, sizeof(msg), "%d cheat%s applied to %s",
                     applied, applied == 1 ? "" : "s",
                     game_name[0] ? game_name : title);
            notif_send_serialised(msg, NOTIF_LEVEL_INFO);
        }
    }
    return NULL;
}

/* ── Initialization ──────────────────────────────────────────────── */

static void ensure_dir(const char *path) {
    mkdir(path, 0777);
}

void cheats_init(void) {
    ensure_dir(CHEATS_ROOT);
    ensure_dir(CHEATS_JSON_DIR);
    ensure_dir(CHEATS_SHN_DIR);
    ensure_dir(CHEATS_MC4_DIR);
    ensure_dir(PATCHES_ROOT);
    ensure_dir(PATCHES_JSON_DIR);
    ensure_dir(PATCHES_SHN_DIR);
    ensure_dir(PATCHES_MC4_DIR);
    ensure_dir(CHEATS_STATE_DIR);

    /* Start watcher thread if not already running */
    int expected = 0;
    if (atomic_compare_exchange_strong(&g_watcher_running, &expected, 1)) {
        pthread_t thr;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        if (pthread_create(&thr, &attr, watcher_thread, NULL) == 0) {
            /* Thread started */
        } else {
            atomic_store(&g_watcher_running, 0);
        }
        pthread_attr_destroy(&attr);
    }
}

/* ── Engine flag ─────────────────────────────────────────────────── */

void cheats_engine_set_enabled(int on) {
    atomic_store(&g_engine_enabled, on ? 1 : 0);
}

/* ── Public API implementations ──────────────────────────────────── */

/* List all titles that have cheat files. */
/* The list's per-file reader: the game's own name and the number of mods. The caches in
 * cheats_list.c call this only when a file's mtime or size changed. */
static int list_load_file(const char *path, int format, char *name, size_t name_cap,
                          int *mod_count) {
    cheat_file_t *cf = (cheat_file_t *)malloc(sizeof(cheat_file_t));
    if (!cf) return -1;
    int rc = load_cheat_file(path, format, cf);
    if (rc == 0) {
        snprintf(name, name_cap, "%s", cf->game_name);
        *mod_count = cf->mod_count;
    }
    free(cf);
    return rc;
}

int cheats_list_titles(char *buf, size_t cap, size_t *written) {
    if (!buf || cap == 0) return -1;
    /* Running game info (resolve once so we can mark the active title) */
    char rg_title[MAX_TITLE_ID] = "";
    intptr_t rg_base = 0;
    pid_t rg_pid = get_running_game_cached(rg_title, sizeof(rg_title), &rg_base);

    cheats_list_cfg_t cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.dirs[0] = CHEATS_JSON_DIR;
    cfg.dirs[1] = CHEATS_SHN_DIR;
    cfg.dirs[2] = CHEATS_MC4_DIR;
    cfg.state_dir = CHEATS_STATE_DIR;
    cfg.load = list_load_file;
    cfg.usable_name = is_usable_game_name;
    cfg.running_title = rg_title;
    cfg.running = rg_pid > 0;
    return cheats_list_build(&cfg, buf, cap, written);
}

/* List all mods for a title. */
int cheats_list_mods(const char *title_id, char *buf, size_t cap,
                     size_t *written) {
    if (!title_id || !buf || cap == 0) return -1;

    found_file_t files[16];
    int n = find_cheat_files(title_id, files, 16, 0);
    if (n == 0) {
        const char *resp = "{\"mods\":[],\"error\":\"no cheat files found\"}";
        size_t len = strlen(resp);
        if (len >= cap) len = cap - 1;
        memcpy(buf, resp, len);
        buf[len] = '\0';
        if (written) *written = len;
        return 0;
    }

    jbuf_t jb;
    jb_init(&jb, buf, cap);
    jb_raw(&jb, "{\"title_id\":\"");
    jb_str(&jb, title_id);
    jb_raw(&jb, "\",\"mods\":[");

    int flat = 0;
    int first = 1;

    for (int fi = 0; fi < n; fi++) {
        cheat_file_t *cf = (cheat_file_t *)malloc(sizeof(cheat_file_t));
        if (!cf) continue;
        if (load_cheat_file(files[fi].path, files[fi].format, cf) != 0) {
            free(cf);
            continue;
        }
        cheats_load_state(title_id, cf, flat);

        for (int mi = 0; mi < cf->mod_count; mi++) {
            cheat_mod_t *m = &cf->mods[mi];
            if (!first) jb_raw(&jb, ",");
            first = 0;

            JB_PRINTF(&jb, "{\"index\":%d,\"name\":\"", flat);
            jb_str(&jb, m->name);
            JB_PRINTF(&jb, "\",\"desc\":\"");
            jb_str(&jb, m->hint);
            JB_PRINTF(&jb, "\",\"type\":\"%s\",\"on\":%s}",
                      m->type,
                      m->enabled ? "true" : "false");
            flat++;
        }
        free(cf);
    }

    jb_raw(&jb, "]}");

    if (written) *written = jb.off;
    return 0;
}

#define KEEP_RANGES_MAX 1024

/* Every location of the title's enabled cheats other than `skip_flat`, as byte ranges.
 * Returns the count, or -1 when the list could not be made complete (out of memory, a cheat
 * file or the state that cannot be read, an enabled cheat whose locations cannot be resolved,
 * more ranges than fit): the caller then refuses, because a missing range could be a shared
 * cave that would be wiped under another cheat's jump. Call with g_apply_lock held. */
static int collect_enabled_ranges(pid_t pid, intptr_t base, const char *title_id, int skip_flat,
                                  cheat_range_t *out, int max) {
    found_file_t files[16];
    int n = find_cheat_files(title_id, files, 16, 0);
    char *state = state_read(title_id);
    cheat_file_t *cf = (cheat_file_t *)malloc(sizeof(cheat_file_t));
    cheat_loc_t *locs = (cheat_loc_t *)calloc(MAX_MEM_ENTRIES, sizeof(cheat_loc_t));
    int count = -1;
    if (state && cf && locs) {
        count = 0;
        int flat = 0;
        for (int fi = 0; fi < n && count >= 0; fi++) {
            if (load_cheat_file(files[fi].path, files[fi].format, cf) != 0) { count = -1; break; }
            for (int mi = 0; mi < cf->mod_count && count >= 0; mi++, flat++) {
                if (flat == skip_flat || !state_has(state, flat)) continue;
                intptr_t mb = base;
                if (cf->mods[mi].module_name[0])
                    mb = resolve_module_base(pid, cf->mods[mi].module_name);
                int nl = resolve_mod_locs(pid, mb, &cf->mods[mi], locs, NULL, 0);
                if (nl <= 0 || count + nl > max) { count = -1; break; }
                for (int k = 0; k < nl; k++) {
                    out[count].addr = locs[k].addr;
                    out[count].len = locs[k].len;
                    count++;
                }
            }
        }
    }
    free(locs);
    free(cf);
    free(state);
    return count;
}

/* Toggle a mod by flat index. */
int cheats_toggle(const char *title_id, int mod_index, int turn_on,
                  char *err, size_t err_cap) {
    if (!title_id) {
        if (err) snprintf(err, err_cap, "missing title_id");
        return -1;
    }

    /* Auto-enable engine if toggling while game is running */
    if (turn_on && !atomic_load(&g_engine_enabled)) {
        atomic_store(&g_engine_enabled, 1);
    }

    found_file_t files[16];
    int n = find_cheat_files(title_id, files, 16, 0);
    if (n == 0) {
        if (err) snprintf(err, err_cap,
                          "no cheat file for %s. Drop a .json/.shn/.mc4 into "
                          CHEATS_ROOT,
                          title_id);
        return -1;
    }

    /* Find the running game */
    char rg_title[MAX_TITLE_ID] = "";
    intptr_t base = 0;
    pid_t pid = get_running_game_cached(rg_title, sizeof(rg_title), &base);
    if (pid <= 0) {
        if (err) snprintf(err, err_cap, "no game is currently running");
        return -1;
    }
    if (strcasecmp(rg_title, title_id) != 0) {
        if (err) snprintf(err, err_cap,
                          "running game (%s) does not match cheat target (%s)",
                          rg_title, title_id);
        return -1;
    }

    /* Resolve flat index to file + mod */
    int flat = 0;
    int target_fi = -1, target_mi = -1;
    cheat_file_t *target_cf = (cheat_file_t *)malloc(sizeof(cheat_file_t));
    if (!target_cf) {
        if (err) snprintf(err, err_cap, "out of memory");
        return -1;
    }
    memset(target_cf, 0, sizeof(*target_cf));

    for (int fi = 0; fi < n && target_fi < 0; fi++) {
        cheat_file_t *cf = (cheat_file_t *)malloc(sizeof(cheat_file_t));
        if (!cf) continue;
        if (load_cheat_file(files[fi].path, files[fi].format, cf) != 0) {
            free(cf);
            continue;
        }
        for (int mi = 0; mi < cf->mod_count; mi++) {
            if (flat == mod_index) {
                target_fi = fi;
                target_mi = mi;
                memcpy(target_cf, cf, sizeof(*target_cf));
                break;
            }
            flat++;
        }
        free(cf);
    }

    if (target_fi < 0) {
        free(target_cf);
        if (err) snprintf(err, err_cap, "cheat index out of range");
        return -1;
    }

    cheat_mod_t *mod = &target_cf->mods[target_mi];

    /* Resolve module base */
    intptr_t mod_base = base;
    if (mod->module_name[0]) {
        mod_base = resolve_module_base(pid, mod->module_name);
        if (!mod_base) {
            free(target_cf);
            if (err) snprintf(err, err_cap,
                              "module \"%s\" is not loaded in the target process",
                              mod->module_name);
            return -1;
        }
    }

    /* The game may have exited a moment ago (the 2 s running-game cache still had its pid): say
     * so with the same text as no game at all (webui_codec.c webui_cheats_toggle_error matches
     * it), and drop the cache so the next status read sees it gone. */
    if (kill(pid, 0) != 0 && errno == ESRCH) {
        free(target_cf);
        invalidate_rg_cache();
        if (err) snprintf(err, err_cap, "no game is currently running");
        return -1;
    }

    /* Turning off: the title's other enabled cheats may share a code cave with this one, and
     * restoring the cave's padding would leave their jumps running into int3. */
    cheat_range_t *keep = NULL;
    int n_keep = 0;
    int rc = 0;
    pthread_mutex_lock(&g_apply_lock);
    if (!turn_on) {
        keep = (cheat_range_t *)calloc(KEEP_RANGES_MAX, sizeof(cheat_range_t));
        n_keep = keep ? collect_enabled_ranges(pid, base, title_id, mod_index, keep,
                                               KEEP_RANGES_MAX)
                      : -1;
        if (n_keep < 0) {
            if (err) snprintf(err, err_cap,
                              "could not check this title's other enabled cheats for shared "
                              "code; nothing was written");
            rc = -1;
        }
    }
    if (rc == 0) rc = apply_mod_locked(pid, mod_base, mod, turn_on, keep, n_keep, err, err_cap);
    if (rc == 0 && strcmp(mod->type, "button") != 0) {
        /* Update persisted state (only for checkbox type) */
        mod->enabled = turn_on;
        if (state_set(title_id, mod_index, turn_on) != 0)
            fprintf(stderr, "[cheats] could not record %s #%d as %s\n", title_id, mod_index,
                    turn_on ? "on" : "off");
    }
    pthread_mutex_unlock(&g_apply_lock);
    free(keep);

    if (rc == 0) {

        /* Confirm on the console.
         *
         * This is the moment issue #315 actually describes — "you load a
         * cheat, it works in-game, but there is no notification". Toggling
         * requires a running game and applies the patch live, so it is the
         * one place the player is looking at the TV rather than the app.
         * Naming the cheat matters more than naming the game here: the
         * player already knows what they are playing, and what they need
         * confirmed is WHICH cheat took. */
        char msg[256];
        const char *label = mod->name[0] ? mod->name : "Cheat";
        if (strcmp(mod->type, "button") == 0) {
            snprintf(msg, sizeof(msg), "%s applied", label);
        } else {
            snprintf(msg, sizeof(msg), "%s %s", label, turn_on ? "on" : "off");
        }
        notif_send_serialised(msg, NOTIF_LEVEL_INFO);
    }

    free(target_cf);
    invalidate_rg_cache();
    return rc;
}

/* Delete all cheat files for a title. */
int cheats_delete(const char *title_id, char *err, size_t err_cap) {
    if (!title_id) {
        if (err) snprintf(err, err_cap, "missing title_id");
        return -1;
    }

    found_file_t files[16];
    int n = find_cheat_files(title_id, files, 16, 0);
    if (n == 0) {
        if (err) snprintf(err, err_cap, "no cheat files found for %s", title_id);
        return -1;
    }

    for (int i = 0; i < n; i++) {
        unlink(files[i].path);
    }

    /* Also delete sidecar state */
    char path[MAX_CHEAT_FILEPATH];
    state_path(title_id, path, sizeof(path));
    unlink(path);

    return 0;
}

/* Force re-apply. */
int cheats_reload(char *err, size_t err_cap) {
    (void)err;
    (void)err_cap;

    invalidate_rg_cache();

    char title[MAX_TITLE_ID] = "";
    intptr_t base = 0;
    pid_t pid = get_running_game_cached(title, sizeof(title), &base);
    if (pid <= 0) {
        if (err) snprintf(err, err_cap, "no game is currently running");
        return -1;
    }

    char game_name[MAX_CHEAT_NAME] = "";
    int applied = apply_patches_for_game(pid, base, title,
                                         game_name, sizeof(game_name));
    applied += reapply_enabled_for_game(pid, base, title,
                                        game_name, sizeof(game_name));

    /* A reload is something the player asked for, so confirm it on the
     * console the same way an automatic apply does. Saying so when nothing
     * landed matters here — unlike the watcher, a silent reload leaves the
     * player unsure whether they had toggled anything at all. */
    char msg[256];
    if (applied > 0) {
        snprintf(msg, sizeof(msg), "%d cheat%s applied to %s",
                 applied, applied == 1 ? "" : "s",
                 game_name[0] ? game_name : title);
    } else {
        snprintf(msg, sizeof(msg), "No cheats enabled for %s",
                 game_name[0] ? game_name : title);
    }
    notif_send_serialised(msg, NOTIF_LEVEL_INFO);

    return 0;
}

/* Engine status JSON. */
int cheats_status_json(char *buf, size_t cap, size_t *written) {
    if (!buf || cap == 0) return -1;

    char rg_title[MAX_TITLE_ID] = "";
    intptr_t rg_base = 0;
    pid_t rg_pid = get_running_game_cached(rg_title, sizeof(rg_title), &rg_base);

    int n = snprintf(buf, cap,
        "{\"enabled\":%s,\"patches_last\":%d,\"patches_total\":%d,"
        "\"game_running\":%s,\"game_title_id\":\"%s\",\"game_pid\":%d}",
        atomic_load(&g_engine_enabled) ? "true" : "false",
        atomic_load(&g_patches_last),
        atomic_load(&g_patches_total),
        rg_pid > 0 ? "true" : "false",
        rg_title,
        (int)rg_pid);

    if (written) *written = (size_t)(n > 0 ? n : 0);
    return n > 0 ? 0 : -1;
}
