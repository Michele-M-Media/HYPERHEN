/*
 * Garlic SaveMgr for PS5 - save decrypt/encrypt/browse with embedded web UI
 * Open http://<ps5-ip>:8082/ in any browser — no local app needed
 * by earthonion
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <dlfcn.h>
#include <pthread.h>

#include <ps5/kernel.h>
#include "fw_offsets_guard.h"
#include "sqlite3.h"

/* ── SDK imports ────────────────────────────────────────────────── */
typedef struct { uint8_t reserved; char *budgetid; } MountOpt;
typedef struct { uint8_t dummy; } UmountOpt;

int sceFsInitMountSaveDataOpt(MountOpt *opt);
int sceFsMountSaveData(MountOpt *opt, const char *path, const char *mount, uint8_t *key);
int sceFsInitUmountSaveDataOpt(UmountOpt *opt);
int sceFsUmountSaveData(UmountOpt *opt, const char *mount, int handle, int ignore);

typedef struct { int blockSize; uint8_t flags[2]; } CreateOpt;
int sceFsInitCreatePfsSaveDataOpt(CreateOpt *opt);
int sceFsCreatePfsSaveDataImage(CreateOpt *opt, const char *path, int x, uint64_t size, uint8_t *key);
/* sceFsCreatePprPfsSaveDataImage - loaded via dlsym at runtime */
typedef int (*PprCreateFn)(CreateOpt *opt, const char *path, int x, uint64_t size, uint8_t *key);
static PprCreateFn g_pprCreate = NULL;
int sceFsUfsAllocateSaveData(int fd, uint64_t size, uint64_t flags, int ext);

int sceUserServiceInitialize(void*);
int sceUserServiceGetForegroundUser(int *user_id);

/* Registry manager — for reading account ID */
int sceRegMgrGetBin(int, void*, size_t);
int sceRegMgrGetStr(int, char*, size_t);

typedef struct { char unused[45]; char message[3075]; } notify_request_t;
int sceKernelSendNotificationRequest(int, notify_request_t*, size_t, int);

/* ── Constants ──────────────────────────────────────────────────── */
#define PORT            8082
#define MOUNT_BASE      "/data/save_mnt"
#define BUF_SIZE        (512 * 1024)
#define MAX_REQ         8192
#define MAX_PATH_LEN    1024

/* Mutex for state-mutating operations (mount/unmount/scan) */
static pthread_mutex_t g_mtx = PTHREAD_MUTEX_INITIALIZER;
/* Separate mutex for log buffer */
static pthread_mutex_t g_log_mtx = PTHREAD_MUTEX_INITIALIZER;

/* Ring log buffer for remote debug */
#define LOG_BUF_SIZE (32 * 1024)
static char g_logbuf[LOG_BUF_SIZE];
static int g_logpos = 0;
static void logprintf(const char *fmt, ...) {
    char tmp[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    printf("%s", tmp);
    if (n > 0) {
        pthread_mutex_lock(&g_log_mtx);
        for (int i = 0; i < n && g_logpos < LOG_BUF_SIZE - 1; i++)
            g_logbuf[g_logpos++] = tmp[i];
        pthread_mutex_unlock(&g_log_mtx);
    }
}

/* ── Save entry ─────────────────────────────────────────────────── */
typedef struct {
    char path[MAX_PATH_LEN];     /* full path to save image file */
    char title_id[32];           /* e.g. PPSA01234 */
    char save_name[256];         /* save file name (varies per game) */
    char dir_name[256];          /* for mount point naming */
    char user_id[16];            /* hex user id e.g. "10000000" */
    int is_ps4;                  /* 1 if PS4 save (byte 0x0 == 0x01) */
    int is_backup;               /* 1 if backup save (sdimg_sce_bu_) */
    int is_usb;                  /* 1 if save is on USB */
} save_entry_t;

static save_entry_t *g_saves = NULL;
static int g_save_count = 0;
static int g_save_cap = 0;

static save_entry_t *save_alloc(void) {
    if (g_save_count >= g_save_cap) {
        int newcap = g_save_cap ? g_save_cap * 2 : 64;
        save_entry_t *p = realloc(g_saves, newcap * sizeof(save_entry_t));
        if (!p) return NULL;
        g_saves = p;
        g_save_cap = newcap;
    }
    return &g_saves[g_save_count++];
}
static char g_mounted_path[MAX_PATH_LEN] = {0};
static char g_mount_point[MAX_PATH_LEN] = {0};
static int g_mounted = 0;
static char g_local_copy[MAX_PATH_LEN] = {0};
static uint8_t g_sealed_key[96] = {0};
static int g_enc_ps4 = 0;

/* ── Server-side editor buffer ─────────────────────────────────── */
static char  g_ed_name[512] = {0};   /* file path relative to mount */
static char *g_ed_buf = NULL;        /* file contents in memory */
static size_t g_ed_size = 0;         /* buffer size */
static int   g_ed_dirty = 0;        /* has unsaved edits */

/* ── Title name lookup from app.db ──────────────────────────────── */
typedef struct {
    char title_id[32];
    char title_name[256];
} title_entry_t;

static title_entry_t *g_titles = NULL;
static int g_title_count = 0;
static int g_title_cap = 0;

static void load_app_db(void) {
    const char *db_path = "/system_data/priv/mms/app.db";
    sqlite3 *db = NULL;
    if (sqlite3_open_v2(db_path, &db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK) {
        printf("[GarlicMgr] Cannot open app.db: %s\n", sqlite3_errmsg(db));
        if (db) sqlite3_close(db);
        return;
    }
    sqlite3_stmt *stmt = NULL;
    const char *sql = "SELECT titleId, titleName FROM tbl_contentinfo WHERE titleName IS NOT NULL";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        printf("[GarlicMgr] SQL error: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return;
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *tid = (const char *)sqlite3_column_text(stmt, 0);
        const char *tname = (const char *)sqlite3_column_text(stmt, 1);
        if (!tid || !tname) continue;
        if (g_title_count >= g_title_cap) {
            int newcap = g_title_cap ? g_title_cap * 2 : 64;
            title_entry_t *p = realloc(g_titles, newcap * sizeof(title_entry_t));
            if (!p) break;
            g_titles = p;
            g_title_cap = newcap;
        }
        title_entry_t *t = &g_titles[g_title_count++];
        strncpy(t->title_id, tid, sizeof(t->title_id) - 1);
        strncpy(t->title_name, tname, sizeof(t->title_name) - 1);
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    printf("[GarlicMgr] Loaded %d title names from app.db\n", g_title_count);
}

static const char *lookup_title(const char *title_id) {
    for (int i = 0; i < g_title_count; i++) {
        if (strcmp(g_titles[i].title_id, title_id) == 0)
            return g_titles[i].title_name;
    }
    return NULL;
}

/* ── Account ID lookup via registry ─────────────────────────────── */
static uint64_t get_account_id_for_user(const char *uid_hex) {
    /* Read username from /user/home/<uid>/username.dat */
    char upath[MAX_PATH_LEN];
    snprintf(upath, sizeof(upath), "/user/home/%s/username.dat", uid_hex);
    char username[17] = {0};
    int fd = open(upath, O_RDONLY);
    if (fd >= 0) {
        read(fd, username, 16);
        username[16] = 0;
        close(fd);
    }
    if (!username[0]) return 0;

    /* Iterate registry accounts 1-16, match by name */
    for (int i = 1; i <= 16; i++) {
        int key_name = (i - 1) * 65536 + 125829632;
        char reg_name[32] = {0};
        if (sceRegMgrGetStr(key_name, reg_name, sizeof(reg_name)) < 0) continue;
        if (!reg_name[0]) continue;
        if (strcmp(username, reg_name) == 0) {
            int key_aid = (i - 1) * 65536 + 125830400;
            uint64_t aid = 0;
            sceRegMgrGetBin(key_aid, &aid, sizeof(aid));
            printf("[GarlicMgr] Registry: user '%s' = account %d, aid=%llx\n",
                   username, i, (unsigned long long)aid);
            return aid;
        }
    }
    printf("[GarlicMgr] Registry: no account found for user '%s'\n", username);
    return 0;
}

/* ── Notification ───────────────────────────────────────────────── */
static void notify(const char *fmt, ...) {
    notify_request_t req;
    memset(&req, 0, sizeof(req));
    va_list a; va_start(a, fmt);
    vsnprintf(req.message, sizeof(req.message), fmt, a);
    va_end(a);
    sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
    printf("[GarlicMgr] %s\n", req.message);
}

/* ── Save discovery ─────────────────────────────────────────────── */
static void scan_title_dir(const char *title_path, const char *title_id, const char *uid) {
    DIR *d = opendir(title_path);
    if (!d) return;

    /* Check if this is a PS4 savedata dir (contains sdimg_ files) */
    int is_ps4 = (strstr(title_path, "/savedata/") != NULL &&
                  strstr(title_path, "/savedata_prospero/") == NULL);

    struct dirent *ent;
    while ((ent = readdir(d))) {
        if (ent->d_name[0] == '.') continue;

        /* Skip .bin sealed key companions */
        int len = strlen(ent->d_name);
        if (len > 4 && strcmp(ent->d_name + len - 4, ".bin") == 0) continue;

        /* For PS4 dirs, only pick up sdimg_ files */
        if (is_ps4 && strncmp(ent->d_name, "sdimg_", 6) != 0) continue;

        char filepath[MAX_PATH_LEN];
        snprintf(filepath, sizeof(filepath), "%s/%s", title_path, ent->d_name);

        struct stat st;
        if (stat(filepath, &st) == 0 && S_ISREG(st.st_mode)) {
            save_entry_t *s = save_alloc();
            if (!s) break;
            snprintf(s->path, sizeof(s->path), "%s", filepath);
            snprintf(s->title_id, sizeof(s->title_id), "%s", title_id);
            snprintf(s->save_name, sizeof(s->save_name), "%s", ent->d_name);
            snprintf(s->dir_name, sizeof(s->dir_name), "%s_%s", title_id, ent->d_name);
            s->is_backup = (strncmp(ent->d_name, "sdimg_sce_bu_", 13) == 0);
            snprintf(s->user_id, sizeof(s->user_id), "%s", uid);
            s->is_usb = 0;
            /* Detect PS4 by byte 0x0 */
            s->is_ps4 = 0;
            int hfd = open(filepath, O_RDONLY);
            if (hfd >= 0) {
                uint8_t hdr;
                if (pread(hfd, &hdr, 1, 0) == 1 && hdr == 0x01) s->is_ps4 = 1;
                close(hfd);
            }
        }
    }
    closedir(d);
}

static void scan_savedata_prospero(const char *prospero_path, const char *uid) {
    DIR *d = opendir(prospero_path);
    if (!d) return;

    struct dirent *ent;
    while ((ent = readdir(d))) {
        if (ent->d_name[0] == '.') continue;

        char title_path[MAX_PATH_LEN];
        snprintf(title_path, sizeof(title_path), "%s/%s", prospero_path, ent->d_name);

        struct stat st;
        if (stat(title_path, &st) == 0 && S_ISDIR(st.st_mode))
            scan_title_dir(title_path, ent->d_name, uid);
    }
    closedir(d);
}

/* ── SFO types (parser defined later) ──────────────────────────── */
typedef struct sfo_info {
    char title_id[16];
    char dir_name[64];
    char main_title[128];
    char sub_title[128];
    char detail[256];
    char game_title_id[16];
    uint32_t user_param;
    uint32_t blocks;
    uint64_t account_id;
    uint32_t user_id;
    uint32_t aid_offset;
    uint32_t uid_offset;
    char format[4];
} sfo_info_t;
static int parse_sfo(const char *path, sfo_info_t *info);

static void scan_usb_saves(const char *garlic_path) {
    DIR *d = opendir(garlic_path);
    if (!d) return;
    struct dirent *ent;
    while ((ent = readdir(d))) {
        if (ent->d_name[0] == '.') continue;
        char title_path[MAX_PATH_LEN];
        snprintf(title_path, sizeof(title_path), "%s/%s", garlic_path, ent->d_name);
        struct stat st;
        if (stat(title_path, &st) < 0 || !S_ISDIR(st.st_mode)) continue;

        DIR *td = opendir(title_path);
        if (!td) continue;
        struct dirent *se;
        while ((se = readdir(td))) {
            if (se->d_name[0] == '.') continue;
            int len = strlen(se->d_name);
            if (len > 4 && strcmp(se->d_name + len - 4, ".bin") == 0) continue;

            char filepath[MAX_PATH_LEN];
            snprintf(filepath, sizeof(filepath), "%s/%s", title_path, se->d_name);
            struct stat fst;
            if (stat(filepath, &fst) < 0) continue;
            if (!S_ISREG(fst.st_mode) && !S_ISDIR(fst.st_mode)) continue;

            save_entry_t *s = save_alloc();
            if (!s) break;
            snprintf(s->path, sizeof(s->path), "%s", filepath);
            snprintf(s->title_id, sizeof(s->title_id), "%s", ent->d_name);
            snprintf(s->save_name, sizeof(s->save_name), "%s", se->d_name);
            snprintf(s->dir_name, sizeof(s->dir_name), "%s_%s", ent->d_name, se->d_name);
            s->is_backup = (strncmp(se->d_name, "sdimg_sce_bu_", 13) == 0);
            s->user_id[0] = 0;
            s->is_usb = 1;
            s->is_ps4 = 0;
            if (S_ISREG(fst.st_mode)) {
                /* Encrypted image: PS4 if byte 0x0 == 0x01 */
                int hfd = open(filepath, O_RDONLY);
                if (hfd >= 0) {
                    uint8_t hdr;
                    if (pread(hfd, &hdr, 1, 0) == 1 && hdr == 0x01) s->is_ps4 = 1;
                    close(hfd);
                }
            } else if (S_ISDIR(fst.st_mode)) {
                /* Decrypted folder: check FORMAT in param.sfo — "obs"=PS4, "ppr"=PS5 */
                char sfo[MAX_PATH_LEN];
                snprintf(sfo, sizeof(sfo), "%s/sce_sys/param.sfo", filepath);
                sfo_info_t sfo_info;
                if (parse_sfo(sfo, &sfo_info) == 0 && strcmp(sfo_info.format, "obs") == 0)
                    s->is_ps4 = 1;
            }
        }
        closedir(td);
    }
    closedir(d);
}

static void scan_saves(void) {
    g_save_count = 0;

    /* /user/home/<userid>/savedata_prospero/<TitleId>/<savefile> (PS5) */
    /* /user/home/<userid>/savedata/<TitleId>/sdimg_* (PS4) */
    DIR *home = opendir("/user/home");
    if (home) {
        struct dirent *ue;
        while ((ue = readdir(home))) {
            if (ue->d_name[0] == '.') continue;
            char prospero[MAX_PATH_LEN];
            snprintf(prospero, sizeof(prospero), "/user/home/%s/savedata_prospero", ue->d_name);
            scan_savedata_prospero(prospero, ue->d_name);
            char ps4[MAX_PATH_LEN];
            snprintf(ps4, sizeof(ps4), "/user/home/%s/savedata", ue->d_name);
            scan_savedata_prospero(ps4, ue->d_name);
        }
        closedir(home);
    }

    /* /mnt/usb0-7/PS5/SAVEDATA/GARLIC/<TitleId>/<savefile> */
    for (int u = 0; u < 8; u++) {
        char usb_path[MAX_PATH_LEN];
        snprintf(usb_path, sizeof(usb_path), "/mnt/usb%d/PS5/SAVEDATA/GARLIC", u);
        scan_usb_saves(usb_path);
    }

}

/* ── Recursive delete helper ────────────────────────────────────── */
static void delete_recursive(const char *path) {
    DIR *d = opendir(path);
    if (!d) return;
    struct dirent *ent;
    while ((ent = readdir(d))) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        char fp[MAX_PATH_LEN];
        snprintf(fp, sizeof(fp), "%s/%s", path, ent->d_name);
        struct stat st;
        if (stat(fp, &st) < 0) continue;
        if (S_ISDIR(st.st_mode)) {
            delete_recursive(fp);
            rmdir(fp);
        } else {
            unlink(fp);
        }
    }
    closedir(d);
}

/* ── Copy file helper ───────────────────────────────────────────── */
static int copy_file(const char *src, const char *dst) {
    int sfd = open(src, O_RDONLY);
    if (sfd < 0) return -1;
    int dfd = open(dst, O_CREAT | O_WRONLY | O_TRUNC, 0755);
    if (dfd < 0) { close(sfd); return -2; }
    char *buf = malloc(BUF_SIZE);
    if (!buf) { close(sfd); close(dfd); return -3; }
    ssize_t n;
    while ((n = read(sfd, buf, BUF_SIZE)) > 0)
        write(dfd, buf, n);
    free(buf);
    close(sfd);
    close(dfd);
    return 0;
}

/* ── Recursive directory copy ───────────────────────────────────── */
static int copy_dir_recursive(const char *src, const char *dst) {
    mkdir(dst, 0777);
    DIR *d = opendir(src);
    if (!d) return -1;
    struct dirent *ent;
    while ((ent = readdir(d))) {
        if (ent->d_name[0] == '.') continue;
        char sp[MAX_PATH_LEN], dp[MAX_PATH_LEN];
        snprintf(sp, sizeof(sp), "%s/%s", src, ent->d_name);
        snprintf(dp, sizeof(dp), "%s/%s", dst, ent->d_name);
        struct stat st;
        if (stat(sp, &st) < 0) continue;
        if (S_ISDIR(st.st_mode)) {
            copy_dir_recursive(sp, dp);
        } else {
            copy_file(sp, dp);
        }
    }
    closedir(d);
    return 0;
}

/* ── Base64 encode/decode ───────────────────────────────────────── */
static const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int b64_encode(const uint8_t *in, size_t len, char *out, size_t out_sz) {
    size_t needed = 4 * ((len + 2) / 3) + 1;
    if (out_sz < needed) return -1;
    char *p = out;
    for (size_t i = 0; i < len; i += 3) {
        uint32_t v = (uint32_t)in[i] << 16;
        if (i + 1 < len) v |= (uint32_t)in[i+1] << 8;
        if (i + 2 < len) v |= (uint32_t)in[i+2];
        *p++ = b64_table[(v >> 18) & 0x3F];
        *p++ = b64_table[(v >> 12) & 0x3F];
        *p++ = (i + 1 < len) ? b64_table[(v >> 6) & 0x3F] : '=';
        *p++ = (i + 2 < len) ? b64_table[v & 0x3F] : '=';
    }
    *p = 0;
    return (int)(p - out);
}

static int b64_decode_char(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static int b64_decode(const char *in, size_t in_len, uint8_t *out, size_t out_sz) {
    size_t j = 0;
    for (size_t i = 0; i + 3 < in_len; i += 4) {
        if (in[i] == '=' || in[i] == 0) break;
        int a = b64_decode_char(in[i]);
        int b = b64_decode_char(in[i+1]);
        int c = (in[i+2] != '=') ? b64_decode_char(in[i+2]) : 0;
        int d = (in[i+3] != '=') ? b64_decode_char(in[i+3]) : 0;
        if (a < 0 || b < 0) break;
        if (j < out_sz) out[j++] = (a << 2) | (b >> 4);
        if (in[i+2] != '=' && j < out_sz) out[j++] = ((b & 0xF) << 4) | (c >> 2);
        if (in[i+3] != '=' && j < out_sz) out[j++] = ((c & 3) << 6) | d;
    }
    return (int)j;
}

/* ── Mount / unmount ────────────────────────────────────────────── */
static int mount_save(int idx) {
    if (g_mounted) return -1;
    if (idx < 0 || idx >= g_save_count) return -2;

    save_entry_t *s = &g_saves[idx];
    const char *bname = strrchr(s->path, '/');
    bname = bname ? bname + 1 : s->path;

    /* Detect PS4 vs PS5 by reading byte 0 of image (0x01=PS4, 0x02=PS5) */
    int is_ps4 = 0;
    {
        int hfd = open(s->path, O_RDONLY);
        if (hfd >= 0) {
            uint8_t hdr;
            if (pread(hfd, &hdr, 1, 0) == 1 && hdr == 0x01) is_ps4 = 1;
            close(hfd);
        }
    }
    logprintf("[GarlicMgr] mount: %s detected as %s\n", bname, is_ps4 ? "PS4" : "PS5");

    /* Elevate credentials */
    kernel_set_ucred_authid(getpid(), 0x4800000000000010ULL);
    uint8_t ucaps[16];
    kernel_get_ucred_caps(getpid(), ucaps);
    ucaps[7] |= 0x40;
    kernel_set_ucred_caps(getpid(), ucaps);
    setuid(0);

    /*
     * If save is NOT on /data/, copy there first.
     * Direct mount from savedata_prospero causes EPIPE.
     */
    const char *mount_src = s->path;
    g_local_copy[0] = 0;
    int is_on_data = (strncmp(s->path, "/data/", 6) == 0);

    if (!is_on_data) {
        snprintf(g_local_copy, sizeof(g_local_copy), "/data/save_files/%s", s->save_name);
        logprintf("[GarlicMgr] Copying %s -> %s\n", s->path, g_local_copy);
        mkdir("/data/save_files", 0777);
        if (copy_file(s->path, g_local_copy) < 0) {
            logprintf("[GarlicMgr] Copy failed (errno %d: %s)\n", errno, strerror(errno));
            return -3;
        }
        chmod(g_local_copy, 0755);
        mount_src = g_local_copy;

        /* For PS4 saves, also copy the .bin sealed key */
        if (is_ps4 && strncmp(bname, "sdimg_", 6) == 0) {
            const char *savename = bname + 6;
            char src_dir[MAX_PATH_LEN], src_bin[MAX_PATH_LEN], dst_bin[MAX_PATH_LEN];
            strncpy(src_dir, s->path, sizeof(src_dir) - 1);
            src_dir[sizeof(src_dir) - 1] = 0;
            char *sl = strrchr(src_dir, '/');
            if (sl) *(sl + 1) = 0;
            snprintf(src_bin, sizeof(src_bin), "%s%s.bin", src_dir, savename);
            snprintf(dst_bin, sizeof(dst_bin), "/data/save_files/%s.bin", savename);
            copy_file(src_bin, dst_bin);
            chmod(dst_bin, 0755);
            logprintf("[GarlicMgr] Copied .bin key %s -> %s\n", src_bin, dst_bin);
        }
    }

    /* Heap-allocate ioctl buffer — ioctl can corrupt stack beyond bounds */
    uint8_t *data = malloc(0x100);
    if (!data) return -10;
    memset(data, 0, 0x100);

    uint8_t decrypted_key[0x20];
    memset(decrypted_key, 0, sizeof(decrypted_key));
    int key_ok = 0;

    if (is_ps4) {
        /* PS4: read sealed key from companion .bin file */
        const char *savename = bname + 6;
        char dir[MAX_PATH_LEN];
        strncpy(dir, mount_src, sizeof(dir) - 1);
        dir[sizeof(dir) - 1] = 0;
        char *slash = strrchr(dir, '/');
        if (slash) *(slash + 1) = 0; else dir[0] = 0;

        char bin_path[MAX_PATH_LEN];
        snprintf(bin_path, sizeof(bin_path), "%s%s.bin", dir, savename);

        int fd = open(bin_path, O_RDONLY);
        if (fd < 0) {
            logprintf("[GarlicMgr] mount: cannot open sealed key %s (errno=%d)\n", bin_path, errno);
            free(data);
            return -4;
        }
        int r = read(fd, data, 0x60);
        close(fd);
        if (r != 0x60) {
            logprintf("[GarlicMgr] mount: short read on .bin (%d)\n", r);
            free(data);
            return -5;
        }
        logprintf("[GarlicMgr] mount: PS4 sealed key from %s (keyset=%d)\n",
               bin_path, (data[9] << 8) | data[8]);

        /* Decrypt PS4 sealed key via pfsmgr */
        int pfsmgr = open("/dev/pfsmgr", O_RDWR);
        if (pfsmgr >= 0) {
            int ret = ioctl(pfsmgr, 0xc0845302, data);
            close(pfsmgr);
            if (ret >= 0) {
                memcpy(decrypted_key, data + 0x60, 0x20);
                key_ok = 1;
                logprintf("[GarlicMgr] mount: PS4 key decrypted OK\n");
            } else {
                logprintf("[GarlicMgr] mount: PS4 key decrypt failed (ret=%d)\n", ret);
            }
        } else {
            logprintf("[GarlicMgr] mount: cannot open /dev/pfsmgr\n");
        }
    } else {
        /* PS5: read key from offset 0x800 in image */
        int fd = open(mount_src, O_RDONLY);
        if (fd < 0) {
            logprintf("[GarlicMgr] mount: cannot open %s (errno=%d)\n", mount_src, errno);
            free(data); return -4;
        }
        int ret = pread(fd, data, 0x60, 0x800);
        close(fd);
        if (ret != 0x60) {
            logprintf("[GarlicMgr] mount: short read key from %s (%d)\n", mount_src, ret);
            free(data); return -5;
        }
        logprintf("[GarlicMgr] mount: PS5 key from %s (keyset=%d)\n",
               mount_src, (data[9] << 8) | data[8]);

        int pfsmgr = open("/dev/pfsmgr", O_RDWR);
        if (pfsmgr < 0) {
            logprintf("[GarlicMgr] mount: cannot open /dev/pfsmgr (errno=%d)\n", errno);
            free(data); return -6;
        }
        ret = ioctl(pfsmgr, 0xc0845302, data);
        close(pfsmgr);
        if (ret >= 0) {
            memcpy(decrypted_key, data + 0x60, 0x20);
            key_ok = 1;
            logprintf("[GarlicMgr] mount: PS5 key decrypted OK\n");
        } else {
            logprintf("[GarlicMgr] mount: PS5 key decrypt failed (ret=%d, errno=%d)\n", ret, errno);
        }
    }

    free(data);

    if (!key_ok) {
        logprintf("[GarlicMgr] mount: key decryption failed for %s\n", mount_src);
    }

    snprintf(g_mount_point, sizeof(g_mount_point), "/data/mount_sd");
    mkdir(g_mount_point, 0777);

    MountOpt mopt;
    memset(&mopt, 0, sizeof(mopt));
    sceFsInitMountSaveDataOpt(&mopt);
    mopt.budgetid = "system";

    signal(SIGPIPE, SIG_DFL);
    int ret = sceFsMountSaveData(&mopt, mount_src, g_mount_point, decrypted_key);
    signal(SIGPIPE, SIG_IGN);

    if (ret >= 0) {
        logprintf("[GarlicMgr] Mounted OK (handle=%d, key_ok=%d)\n", ret, key_ok);
        snprintf(g_mounted_path, sizeof(g_mounted_path), "%s", s->path);
        g_mounted = 1;
        return 0;
    }
    logprintf("[GarlicMgr] Mount failed (0x%x, key_ok=%d, errno=%d: %s)\n", ret, key_ok, errno, strerror(errno));
    return ret;
}

static int unmount_save(void) {
    if (!g_mounted) return -1;

    UmountOpt uopt;
    memset(&uopt, 0, sizeof(uopt));
    sceFsInitUmountSaveDataOpt(&uopt);
    sceFsUmountSaveData(&uopt, g_mount_point, 0, 0);
    sync();

    /* Copy modified save back to original location */
    if (g_local_copy[0] && g_mounted_path[0]) {
        printf("[GarlicMgr] Copying back %s -> %s\n", g_local_copy, g_mounted_path);
        copy_file(g_local_copy, g_mounted_path);
        unlink(g_local_copy);
    }

    g_mounted = 0;
    g_mounted_path[0] = 0;
    g_mount_point[0] = 0;
    g_local_copy[0] = 0;
    return 0;
}

/* ── Mount by path (for uploaded files already on /data/) ──────── */
static int mount_by_path(const char *path) {
    if (g_mounted) unmount_save();

    const char *bname = strrchr(path, '/');
    bname = bname ? bname + 1 : path;

    /* Detect PS4 vs PS5 by reading byte 0 (0x01=PS4, 0x02=PS5) */
    int is_ps4 = 0;
    {
        int hfd = open(path, O_RDONLY);
        if (hfd >= 0) {
            uint8_t hdr;
            if (pread(hfd, &hdr, 1, 0) == 1 && hdr == 0x01) is_ps4 = 1;
            close(hfd);
        }
    }
    g_enc_ps4 = is_ps4;
    logprintf("[GarlicMgr] mount_by_path: %s detected as %s\n", bname, is_ps4 ? "PS4" : "PS5");

    kernel_set_ucred_authid(getpid(), 0x4800000000000010ULL);
    uint8_t caps[16];
    kernel_get_ucred_caps(getpid(), caps);
    caps[7] |= 0x40;
    kernel_set_ucred_caps(getpid(), caps);
    setuid(0);

    /* Heap-allocate ioctl buffer — ioctl can corrupt stack beyond bounds */
    uint8_t *data = malloc(0x100);
    if (!data) return -10;
    memset(data, 0, 0x100);

    uint8_t decrypted_key[0x20];
    memset(decrypted_key, 0, sizeof(decrypted_key));
    int key_ok = 0;

    if (is_ps4) {
        /* PS4: read sealed key from companion .bin file */
        const char *savename = bname + 6;
        char dir[MAX_PATH_LEN];
        strncpy(dir, path, sizeof(dir) - 1);
        dir[sizeof(dir) - 1] = 0;
        char *slash = strrchr(dir, '/');
        if (slash) *(slash + 1) = 0; else dir[0] = 0;

        char bin_path[MAX_PATH_LEN];
        snprintf(bin_path, sizeof(bin_path), "%s%s.bin", dir, savename);

        int fd = open(bin_path, O_RDONLY);
        if (fd < 0) {
            logprintf("[GarlicMgr] mount: cannot open sealed key %s\n", bin_path);
            free(data);
            return -1;
        }
        int r = read(fd, data, 0x60);
        close(fd);
        if (r != 0x60) {
            logprintf("[GarlicMgr] mount: short read on .bin (%d)\n", r);
            free(data);
            return -2;
        }
        logprintf("[GarlicMgr] mount: PS4 sealed key from %s (keyset=%d)\n",
               bin_path, (data[9] << 8) | data[8]);

        /* Decrypt PS4 sealed key via pfsmgr */
        int pfsmgr = open("/dev/pfsmgr", O_RDWR);
        if (pfsmgr >= 0) {
            int ret = ioctl(pfsmgr, 0xc0845302, data);
            close(pfsmgr);
            if (ret >= 0) {
                memcpy(decrypted_key, data + 0x60, 0x20);
                key_ok = 1;
                logprintf("[GarlicMgr] mount: PS4 key decrypted OK\n");
            } else {
                logprintf("[GarlicMgr] mount: PS4 key decrypt failed (ret=%d)\n", ret);
            }
        } else {
            logprintf("[GarlicMgr] mount: cannot open /dev/pfsmgr\n");
        }
    } else {
        /* PS5: read key from offset 0x800 in image */
        int fd = open(path, O_RDONLY);
        if (fd < 0) { free(data); return -1; }
        int ret = pread(fd, data, 0x60, 0x800);
        close(fd);
        if (ret != 0x60) { free(data); return -2; }

        int pfsmgr = open("/dev/pfsmgr", 2);
        if (pfsmgr < 0) { free(data); return -3; }
        ret = ioctl(pfsmgr, 0xc0845302, data);
        close(pfsmgr);
        if (ret >= 0) {
            memcpy(decrypted_key, data + 0x60, 0x20);
            key_ok = 1;
        }
    }

    free(data);

    if (!key_ok) {
        logprintf("[GarlicMgr] mount: key decryption failed for %s\n", path);
        /* Try mounting with zeroed key anyway */
    }

    snprintf(g_mount_point, sizeof(g_mount_point), "/data/mount_sd");
    mkdir(g_mount_point, 0777);

    MountOpt mopt;
    memset(&mopt, 0, sizeof(mopt));
    sceFsInitMountSaveDataOpt(&mopt);
    mopt.budgetid = "system";

    signal(SIGPIPE, SIG_DFL);
    int ret = sceFsMountSaveData(&mopt, path, g_mount_point, decrypted_key);
    signal(SIGPIPE, SIG_IGN);

    if (ret >= 0) {
        logprintf("[GarlicMgr] mount: mounted %s (handle=%d, key_ok=%d)\n", path, ret, key_ok);
        snprintf(g_mounted_path, sizeof(g_mounted_path), "%s", path);
        g_local_copy[0] = 0;
        g_mounted = 1;
        return 0;
    }
    logprintf("[GarlicMgr] mount: failed 0x%x (key_ok=%d)\n", ret, key_ok);
    return ret;
}

/* ── SFO parser ────────────────────────────────────────────────── */
static int parse_sfo(const char *path, sfo_info_t *info) {
    memset(info, 0, sizeof(*info));
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;

    struct stat st;
    if (fstat(fd, &st) < 0 || st.st_size < 20) { close(fd); return -1; }

    uint8_t *buf = malloc(st.st_size);
    if (!buf) { close(fd); return -1; }
    if (read(fd, buf, st.st_size) != st.st_size) { free(buf); close(fd); return -1; }
    close(fd);

    /* Check if all zeros (corrupted) */
    int all_zero = 1;
    size_t check_len = st.st_size < 256 ? st.st_size : 256;
    for (size_t i = 0; i < check_len; i++) {
        if (buf[i] != 0) { all_zero = 0; break; }
    }
    if (all_zero) { free(buf); return -2; }

    /* Check magic PSF\0 */
    if (buf[0] != 0x00 || buf[1] != 0x50 || buf[2] != 0x53 || buf[3] != 0x46) {
        free(buf); return -1;
    }

    uint32_t key_off, data_off, n_entries;
    memcpy(&key_off, buf + 8, 4);
    memcpy(&data_off, buf + 12, 4);
    memcpy(&n_entries, buf + 16, 4);

    for (uint32_t i = 0; i < n_entries; i++) {
        uint8_t *idx = buf + 20 + i * 16;
        uint16_t koff;
        uint32_t param_len, doff;
        memcpy(&koff, idx, 2);
        memcpy(&param_len, idx + 4, 4);
        memcpy(&doff, idx + 12, 4);

        const char *key = (const char *)(buf + key_off + koff);
        uint8_t *val = buf + data_off + doff;

        if (strcmp(key, "TITLE_ID") == 0)
            snprintf(info->title_id, sizeof(info->title_id), "%.*s", (int)param_len, (char*)val);
        else if (strcmp(key, "SAVEDATA_DIRECTORY") == 0)
            snprintf(info->dir_name, sizeof(info->dir_name), "%.*s", (int)param_len, (char*)val);
        else if (strcmp(key, "MAINTITLE") == 0 || strcmp(key, "TITLE") == 0)
            snprintf(info->main_title, sizeof(info->main_title), "%.*s", (int)param_len, (char*)val);
        else if (strcmp(key, "SUBTITLE") == 0)
            snprintf(info->sub_title, sizeof(info->sub_title), "%.*s", (int)param_len, (char*)val);
        else if (strcmp(key, "DETAIL") == 0)
            snprintf(info->detail, sizeof(info->detail), "%.*s", (int)param_len, (char*)val);
        else if (strcmp(key, "SAVEDATA_BLOCKS") == 0 && param_len >= 4)
            memcpy(&info->blocks, val, 4);
        else if (strcmp(key, "PARAMS") == 0 || strcmp(key, "USER_PARAM") == 0) {
            if (param_len >= 4) memcpy(&info->user_param, val, 4);
        }
        else if (strcmp(key, "ACCOUNT_ID") == 0 && param_len >= 8)
            memcpy(&info->account_id, val, 8);
        else if (strcmp(key, "USER_ID") == 0 && param_len >= 4)
            memcpy(&info->user_id, val, 4);
        else if (strcmp(key, "FORMAT") == 0)
            snprintf(info->format, sizeof(info->format), "%.*s", (int)param_len, (char*)val);
        else if (strcmp(key, "SAVEDATA_LIST_PARAM") == 0 || strcmp(key, "CATEGORY") == 0) {
            /* game_title_id from CATEGORY not needed, use title_id */
        }
    }
    /* Default game_title_id to title_id */
    if (!info->game_title_id[0])
        strncpy(info->game_title_id, info->title_id, sizeof(info->game_title_id) - 1);

    free(buf);
    return 0;
}

/* ── Recursive mkdir helper ────────────────────────────────────── */
static void recursive_mkdir(const char *path) {
    char tmp[MAX_PATH_LEN];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0777);
            *p = '/';
        }
    }
    mkdir(tmp, 0777);
}

/* ── CRC32 ─────────────────────────────────────────────────────── */
static uint32_t crc_tab[256];
static void init_crc(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int j = 0; j < 8; j++) c = (c >> 1) ^ (c & 1 ? 0xEDB88320 : 0);
        crc_tab[i] = c;
    }
}
static uint32_t calc_crc(const uint8_t *buf, size_t len) {
    uint32_t c = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) c = crc_tab[(c ^ buf[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFF;
}

/* ── Zip streaming (download) ──────────────────────────────────── */
typedef struct { char name[512]; uint32_t crc; uint32_t size; uint32_t offset; } zip_entry_t;
#define MAX_ZIP_ENTRIES 4096

static void collect_files(const char *base, const char *prefix,
                          zip_entry_t *entries, int *count) {
    char dirpath[MAX_PATH_LEN];
    if (prefix[0])
        snprintf(dirpath, sizeof(dirpath), "%s/%s", base, prefix);
    else
        snprintf(dirpath, sizeof(dirpath), "%s", base);

    DIR *d = opendir(dirpath);
    if (!d) return;

    struct dirent *ent;
    while ((ent = readdir(d)) && *count < MAX_ZIP_ENTRIES) {
        if (ent->d_name[0] == '.') continue;
        char relpath[MAX_PATH_LEN];
        if (prefix[0])
            snprintf(relpath, sizeof(relpath), "%s/%s", prefix, ent->d_name);
        else
            snprintf(relpath, sizeof(relpath), "%s", ent->d_name);
        char fullpath[MAX_PATH_LEN];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", base, relpath);
        struct stat st;
        if (stat(fullpath, &st) < 0) continue;
        if (S_ISDIR(st.st_mode)) {
            collect_files(base, relpath, entries, count);
        } else if (S_ISREG(st.st_mode)) {
            zip_entry_t *e = &entries[*count];
            snprintf(e->name, sizeof(e->name), "%s", relpath);
            e->size = (uint32_t)st.st_size;
            e->crc = 0;
            e->offset = 0;
            (*count)++;
        }
    }
    closedir(d);
}

static void zip_send(int sock, const char *base) {
    zip_entry_t *entries = malloc(MAX_ZIP_ENTRIES * sizeof(zip_entry_t));
    if (!entries) return;
    int count = 0;
    collect_files(base, "", entries, &count);

    uint32_t offset = 0;
    for (int i = 0; i < count; i++) {
        zip_entry_t *e = &entries[i];
        e->offset = offset;
        uint16_t nlen = strlen(e->name);

        char fullpath[MAX_PATH_LEN];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", base, e->name);
        int fd = open(fullpath, O_RDONLY);
        uint8_t *fdata = NULL;
        if (fd >= 0 && e->size > 0) {
            fdata = malloc(e->size);
            if (fdata) { ssize_t r = read(fd, fdata, e->size); (void)r; }
            close(fd);
        } else if (fd >= 0) { close(fd); }
        e->crc = fdata ? calc_crc(fdata, e->size) : 0;

        uint8_t lh[30]; memset(lh, 0, 30);
        lh[0]=0x50; lh[1]=0x4b; lh[2]=0x03; lh[3]=0x04;
        lh[4]=20;
        memcpy(lh+14, &e->crc, 4);
        memcpy(lh+18, &e->size, 4);
        memcpy(lh+22, &e->size, 4);
        memcpy(lh+26, &nlen, 2);
        send(sock, lh, 30, 0);
        send(sock, e->name, nlen, 0);
        if (fdata) { send(sock, fdata, e->size, 0); free(fdata); }
        offset += 30 + nlen + e->size;
    }

    uint32_t cd_offset = offset;
    for (int i = 0; i < count; i++) {
        zip_entry_t *e = &entries[i];
        uint16_t nlen = strlen(e->name);
        uint8_t cd[46]; memset(cd, 0, 46);
        cd[0]=0x50; cd[1]=0x4b; cd[2]=0x01; cd[3]=0x02;
        cd[4]=20; cd[6]=20;
        memcpy(cd+16, &e->crc, 4);
        memcpy(cd+20, &e->size, 4);
        memcpy(cd+24, &e->size, 4);
        memcpy(cd+28, &nlen, 2);
        memcpy(cd+42, &e->offset, 4);
        send(sock, cd, 46, 0);
        send(sock, e->name, nlen, 0);
        offset += 46 + nlen;
    }
    uint32_t cd_size = offset - cd_offset;

    uint8_t ecd[22]; memset(ecd, 0, 22);
    ecd[0]=0x50; ecd[1]=0x4b; ecd[2]=0x05; ecd[3]=0x06;
    uint16_t cnt16 = (uint16_t)count;
    memcpy(ecd+8, &cnt16, 2);
    memcpy(ecd+10, &cnt16, 2);
    memcpy(ecd+12, &cd_size, 4);
    memcpy(ecd+16, &cd_offset, 4);
    send(sock, ecd, 22, 0);
    free(entries);
}

/* ── Zip streaming for specific files by path ──────────────────── */
typedef struct { const char *path; const char *name; } zip_file_t;

static void zip_send_files(int sock, const zip_file_t *files, int nfiles, char *iobuf) {
    zip_entry_t *entries = malloc(nfiles * sizeof(zip_entry_t));
    if (!entries) return;

    uint32_t offset = 0;
    for (int i = 0; i < nfiles; i++) {
        zip_entry_t *e = &entries[i];
        snprintf(e->name, sizeof(e->name), "%s", files[i].name);

        struct stat st;
        if (stat(files[i].path, &st) < 0) { e->size = 0; e->crc = 0; e->offset = offset; continue; }
        e->size = (uint32_t)st.st_size;
        e->offset = offset;

        int fd = open(files[i].path, O_RDONLY);
        if (fd < 0) { e->crc = 0; continue; }

        /* Stream: compute CRC while sending */
        uint16_t nlen = strlen(e->name);
        /* Send local header with CRC=0, will use data descriptor */
        uint8_t lh[30]; memset(lh, 0, 30);
        lh[0]=0x50; lh[1]=0x4b; lh[2]=0x03; lh[3]=0x04;
        lh[4]=20;
        lh[6]=0x08; /* flag: data descriptor follows — CRC+sizes must be 0 */
        memcpy(lh+26, &nlen, 2);
        send(sock, lh, 30, 0);
        send(sock, e->name, nlen, 0);

        uint32_t crc = 0xFFFFFFFF;
        char *buf = iobuf;
        ssize_t nr;
        while ((nr = read(fd, buf, BUF_SIZE)) > 0) {
            for (ssize_t j = 0; j < nr; j++) {
                crc ^= (uint8_t)buf[j];
                for (int k = 0; k < 8; k++)
                    crc = (crc >> 1) ^ (0xEDB88320 & (-(crc & 1)));
            }
            send(sock, buf, nr, 0);
        }
        close(fd);
        crc ^= 0xFFFFFFFF;
        e->crc = crc;

        /* Data descriptor */
        uint8_t dd[16];
        dd[0]=0x50; dd[1]=0x4b; dd[2]=0x07; dd[3]=0x08;
        memcpy(dd+4, &e->crc, 4);
        memcpy(dd+8, &e->size, 4);
        memcpy(dd+12, &e->size, 4);
        send(sock, dd, 16, 0);

        offset += 30 + nlen + e->size + 16;
    }

    uint32_t cd_offset = offset;
    for (int i = 0; i < nfiles; i++) {
        zip_entry_t *e = &entries[i];
        uint16_t nlen = strlen(e->name);
        uint8_t cd[46]; memset(cd, 0, 46);
        cd[0]=0x50; cd[1]=0x4b; cd[2]=0x01; cd[3]=0x02;
        cd[4]=20; cd[6]=20;
        memcpy(cd+16, &e->crc, 4);
        memcpy(cd+20, &e->size, 4);
        memcpy(cd+24, &e->size, 4);
        memcpy(cd+28, &nlen, 2);
        memcpy(cd+42, &e->offset, 4);
        send(sock, cd, 46, 0);
        send(sock, e->name, nlen, 0);
        offset += 46 + nlen;
    }
    uint32_t cd_size = offset - cd_offset;

    uint8_t ecd[22]; memset(ecd, 0, 22);
    ecd[0]=0x50; ecd[1]=0x4b; ecd[2]=0x05; ecd[3]=0x06;
    uint16_t cnt16 = (uint16_t)nfiles;
    memcpy(ecd+8, &cnt16, 2);
    memcpy(ecd+10, &cnt16, 2);
    memcpy(ecd+12, &cd_size, 4);
    memcpy(ecd+16, &cd_offset, 4);
    send(sock, ecd, 22, 0);
    free(entries);
}

/* ── Zip extraction (upload) ────────────────────────────────────── */
static int extract_zip(const char *dest, const uint8_t *data, size_t len) {
    size_t off = 0;
    int count = 0;

    while (off + 30 <= len) {
        if (data[off]!=0x50 || data[off+1]!=0x4b ||
            data[off+2]!=0x03 || data[off+3]!=0x04) break;

        uint16_t compression = 0, name_len = 0, extra_len = 0;
        uint32_t comp_size = 0, uncomp_size = 0;
        memcpy(&compression, data + off + 8, 2);
        memcpy(&comp_size, data + off + 18, 4);
        memcpy(&uncomp_size, data + off + 22, 4);
        memcpy(&name_len, data + off + 26, 2);
        memcpy(&extra_len, data + off + 28, 2);

        char name[MAX_PATH_LEN] = {0};
        uint16_t copy_len = name_len < MAX_PATH_LEN - 1 ? name_len : (uint16_t)(MAX_PATH_LEN - 1);
        memcpy(name, data + off + 30, copy_len);
        off += 30 + name_len + extra_len;

        if (compression != 0) {
            printf("[GarlicMgr] Skipping compressed entry: %s\n", name);
            off += comp_size;
            continue;
        }

        if (name[0] && name[strlen(name)-1] == '/') {
            char dirpath[MAX_PATH_LEN];
            snprintf(dirpath, sizeof(dirpath), "%s/%s", dest, name);
            mkdir(dirpath, 0777);
        } else {
            char filepath[MAX_PATH_LEN];
            snprintf(filepath, sizeof(filepath), "%s/%s", dest, name);
            char *slash = filepath;
            while ((slash = strchr(slash + 1, '/')) != NULL) {
                *slash = 0; mkdir(filepath, 0777); *slash = '/';
            }
            int fd = open(filepath, O_CREAT | O_WRONLY | O_TRUNC, 0644);
            if (fd >= 0) {
                size_t to_write = uncomp_size;
                if (off + to_write > len) to_write = len - off;
                write(fd, data + off, to_write);
                close(fd);
                count++;
            }
        }
        off += comp_size;
    }
    return count;
}

/* ── File listing as JSON ───────────────────────────────────────── */
static int json_list_dir(char *out, size_t max, const char *base, const char *prefix) {
    char dirpath[MAX_PATH_LEN];
    if (prefix[0])
        snprintf(dirpath, sizeof(dirpath), "%s/%s", base, prefix);
    else
        snprintf(dirpath, sizeof(dirpath), "%s", base);

    DIR *d = opendir(dirpath);
    if (!d) return 0;

    int pos = 0, first = 1;
    struct dirent *ent;
    while ((ent = readdir(d))) {
        if (ent->d_name[0] == '.') continue;
        char fullpath[MAX_PATH_LEN];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", dirpath, ent->d_name);
        struct stat st;
        if (stat(fullpath, &st) < 0) continue;

        char relpath[MAX_PATH_LEN];
        if (prefix[0])
            snprintf(relpath, sizeof(relpath), "%s/%s", prefix, ent->d_name);
        else
            snprintf(relpath, sizeof(relpath), "%s", ent->d_name);

        if (!first) {
            if (out && pos < (int)max - 1) out[pos] = ',';
            pos++;
        }
        first = 0;
        pos += snprintf(out ? out + pos : NULL, out ? max - pos : 0,
            "{\"name\":\"%s\",\"dir\":%s,\"size\":%lld}",
            relpath, S_ISDIR(st.st_mode) ? "true" : "false", (long long)st.st_size);
        if (S_ISDIR(st.st_mode)) {
            if (out && pos < (int)max - 1) out[pos] = ',';
            pos++;
            pos += json_list_dir(out ? out + pos : NULL, out ? max - pos : 0, base, relpath);
        }
    }
    closedir(d);
    return pos;
}

/* ── Robust recv (retries on EINTR) ─────────────────────────────── */
static ssize_t recv_all(int sock, void *buf, size_t len) {
    while (1) {
        ssize_t r = recv(sock, buf, len, 0);
        if (r < 0 && errno == EINTR) continue;
        return r;
    }
}

/* ── HTTP helpers ───────────────────────────────────────────────── */
static void http_send(int sock, const char *status_line, const char *content_type,
                      const char *body, int body_len) {
    char hdr[1024];
    int hlen = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\n"
        "Connection: close\r\n"
        "\r\n",
        status_line, content_type, body_len);
    send(sock, hdr, hlen, 0);
    if (body && body_len > 0) {
        int off = 0;
        while (off < body_len) {
            int chunk = body_len - off;
            if (chunk > 262144) chunk = 262144;
            ssize_t s = send(sock, body + off, chunk, 0);
            if (s <= 0) break;
            off += s;
        }
    }
}

static void http_json(int sock, const char *json) {
    http_send(sock, "200 OK", "application/json", json, strlen(json));
}

/* ── Embedded Web UI (generated from src/ui.html via xxd -i) ──── */
#include "ui.h"

/* ── Request handler ────────────────────────────────────────────── */
static void handle_request_inner(int sock, char *iobuf, char *req, int n);

/* Thread entry — receives socket fd via malloc'd int */
static void *request_thread(void *arg) {
    int sock = *(int *)arg;
    free(arg);

    int rcvbuf = 1024 * 1024;
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
    setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &rcvbuf, sizeof(rcvbuf));
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &(int){1}, sizeof(int));

    char req[MAX_REQ];
    int n = recv(sock, req, sizeof(req) - 1, 0);
    if (n <= 0) { close(sock); return NULL; }
    req[n] = 0;

    /* Serve static HTML without lock */
    char method[8] = {0}, url[2048] = {0};
    sscanf(req, "%7s %2047s", method, url);

    if (strcmp(method, "OPTIONS") == 0) {
        http_send(sock, "204 No Content", "text/plain", NULL, 0);
        close(sock);
        return NULL;
    }
    if (strcmp(method, "GET") == 0 && (strcmp(url, "/") == 0 || strcmp(url, "/index.html") == 0)) {
        http_send(sock, "200 OK", "text/html", (const char *)src_ui_html, src_ui_html_len);
        close(sock);
        return NULL;
    }
    /* Favicon — return 404 immediately without lock */
    if (strcmp(method, "GET") == 0 && strcmp(url, "/favicon.ico") == 0) {
        http_send(sock, "404 Not Found", "text/plain", "", 0);
        close(sock);
        return NULL;
    }
    /* Only /api/ requests should proceed — reject anything else without lock */
    if (strncmp(url, "/api/", 5) != 0) {
        http_send(sock, "404 Not Found", "text/plain", "Not found", 9);
        close(sock);
        return NULL;
    }

    /* Shutdown — must not wait for mutex */
    if (strcmp(method, "GET") == 0 && strcmp(url, "/api/shutdown") == 0) {
        if (g_mounted) unmount_save();
        UmountOpt uclean;
        memset(&uclean, 0, sizeof(uclean));
        sceFsInitUmountSaveDataOpt(&uclean);
        sceFsUmountSaveData(&uclean, "/data/mount_sd", 0, 0);
        sync();
        http_json(sock, "{\"ok\":true}");
        close(sock);
        notify("Garlic SaveMgr: Shutting down");
        kill(getpid(), SIGKILL);
        return NULL;
    }

    /* Read-only file download — no lock needed */
    if (strcmp(method, "GET") == 0 && strncmp(url, "/api/download_file", 18) == 0) {
        if (!g_mounted) { http_json(sock, "{\"error\":\"No save mounted\"}"); close(sock); return NULL; }
        char fname[512] = {0};
        char *np = strstr(url, "name=");
        if (np) {
            strncpy(fname, np + 5, sizeof(fname) - 1);
            char *r = fname, *w = fname;
            while (*r && *r != '&') {
                if (*r == '%' && r[1] && r[2]) {
                    char hex[3] = {r[1], r[2], 0};
                    *w++ = (char)strtol(hex, NULL, 16);
                    r += 3;
                } else { *w++ = *r++; }
            }
            *w = 0;
        }
        if (!fname[0]) { http_json(sock, "{\"error\":\"Missing name\"}"); close(sock); return NULL; }
        char filepath[MAX_PATH_LEN];
        snprintf(filepath, sizeof(filepath), "%s/%s", g_mount_point, fname);
        struct stat st;
        if (stat(filepath, &st) < 0 || !S_ISREG(st.st_mode)) {
            http_json(sock, "{\"error\":\"File not found\"}"); close(sock); return NULL;
        }
        int fd = open(filepath, O_RDONLY);
        if (fd < 0) { http_json(sock, "{\"error\":\"Cannot read file\"}"); close(sock); return NULL; }
        const char *bname = strrchr(fname, '/');
        bname = bname ? bname + 1 : fname;
        char hdr[1024];
        int hlen = snprintf(hdr, sizeof(hdr),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/octet-stream\r\n"
            "Content-Disposition: attachment; filename=\"%s\"\r\n"
            "Content-Length: %lld\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Connection: close\r\n\r\n", bname, (long long)st.st_size);
        send(sock, hdr, hlen, 0);
        /* Stream read→send */
        char *buf = malloc(BUF_SIZE);
        if (buf) {
            ssize_t nr;
            while ((nr = read(fd, buf, BUF_SIZE)) > 0) {
                ssize_t off = 0;
                while (off < nr) {
                    ssize_t s = send(sock, buf + off, nr - off, 0);
                    if (s <= 0) goto dl_done;
                    off += s;
                }
            }
            dl_done:
            free(buf);
        }
        close(fd);
        close(sock);
        return NULL;
    }

    /* Read-only editor read — no lock needed (reads from server buffer) */
    if (strcmp(method, "GET") == 0 && strncmp(url, "/api/editor/read", 16) == 0) {
        if (!g_ed_buf) { http_json(sock, "{\"error\":\"No file open\"}"); close(sock); return NULL; }
        size_t off = 0, len = 4096;
        char *op = strstr(url, "offset=");
        char *lp = strstr(url, "length=");
        if (op) off = strtoul(op + 7, NULL, 10);
        if (lp) len = strtoul(lp + 7, NULL, 10);
        if (off >= g_ed_size) { http_send(sock, "200 OK", "application/octet-stream", "", 0); close(sock); return NULL; }
        if (off + len > g_ed_size) len = g_ed_size - off;
        http_send(sock, "200 OK", "application/octet-stream", g_ed_buf + off, len);
        close(sock);
        return NULL;
    }

    /* Editor status — no lock needed */
    if (strcmp(method, "GET") == 0 && strncmp(url, "/api/editor/status", 18) == 0) {
        char json[512];
        snprintf(json, sizeof(json), "{\"open\":%s,\"name\":\"%s\",\"size\":%zu,\"dirty\":%s}",
            g_ed_buf ? "true" : "false", g_ed_name, g_ed_size, g_ed_dirty ? "true" : "false");
        http_json(sock, json);
        close(sock);
        return NULL;
    }

    /* All other requests go through the handler with mutex */
    char *iobuf = malloc(BUF_SIZE);
    if (!iobuf) { close(sock); return NULL; }
    pthread_mutex_lock(&g_mtx);
    handle_request_inner(sock, iobuf, req, n);
    pthread_mutex_unlock(&g_mtx);
    free(iobuf);
    close(sock);
    return NULL;
}

static void handle_request_inner(int sock, char *iobuf, char *req, int n) {

    char method[8] = {0}, url[2048] = {0};
    sscanf(req, "%7s %2047s", method, url);

    /* Handle Expect: 100-continue for large uploads */
    if (strcasestr(req, "Expect: 100-continue"))
        send(sock, "HTTP/1.1 100 Continue\r\n\r\n", 25, 0);

    /* CORS preflight */
    if (strcmp(method, "OPTIONS") == 0) {
        http_send(sock, "204 No Content", "text/plain", NULL, 0);
        return;
    }

    /* GET / → embedded web UI */
    if (strcmp(method, "GET") == 0 && (strcmp(url, "/") == 0 || strcmp(url, "/index.html") == 0)) {
        http_send(sock, "200 OK", "text/html", (const char *)src_ui_html, src_ui_html_len);
        return;
    }

    /* GET /api/file_exists?name=... */
    if (strcmp(method, "GET") == 0 && strncmp(url, "/api/file_exists", 16) == 0) {
        if (!g_mounted) { http_json(sock, "{\"exists\":false}"); return; }
        char fname[512] = {0};
        char *np = strstr(url, "name=");
        if (np) {
            strncpy(fname, np + 5, sizeof(fname) - 1);
            char *r = fname, *w = fname;
            while (*r) {
                if (*r == '%' && r[1] && r[2]) {
                    char hex[3] = {r[1], r[2], 0};
                    *w++ = (char)strtol(hex, NULL, 16);
                    r += 3;
                } else { *w++ = *r++; }
            }
            *w = 0;
        }
        char filepath[MAX_PATH_LEN];
        snprintf(filepath, sizeof(filepath), "%s/%s", g_mount_point, fname);
        struct stat st;
        int exists = (stat(filepath, &st) == 0 && S_ISREG(st.st_mode));
        char json[128];
        snprintf(json, sizeof(json), "{\"exists\":%s}", exists ? "true" : "false");
        http_json(sock, json);
        return;
    }

    /* GET /api/download_file?name=<path> -> download single file from mounted save */
    if (strcmp(method, "GET") == 0 && strncmp(url, "/api/download_file", 18) == 0) {
        if (!g_mounted) { http_json(sock, "{\"error\":\"No save mounted\"}"); return; }
        char fname[512] = {0};
        char *np = strstr(url, "name=");
        if (np) {
            strncpy(fname, np + 5, sizeof(fname) - 1);
            char *r = fname, *w = fname;
            while (*r && *r != '&') {
                if (*r == '%' && r[1] && r[2]) {
                    char hex[3] = {r[1], r[2], 0};
                    *w++ = (char)strtol(hex, NULL, 16);
                    r += 3;
                } else { *w++ = *r++; }
            }
            *w = 0;
        }
        if (!fname[0]) { http_json(sock, "{\"error\":\"Missing name\"}"); return; }
        char filepath[MAX_PATH_LEN];
        snprintf(filepath, sizeof(filepath), "%s/%s", g_mount_point, fname);
        struct stat st;
        if (stat(filepath, &st) < 0 || !S_ISREG(st.st_mode)) {
            http_json(sock, "{\"error\":\"File not found\"}"); return;
        }
        int fd = open(filepath, O_RDONLY);
        if (fd < 0) { http_json(sock, "{\"error\":\"Cannot read file\"}"); return; }
        /* Use just the filename for download */
        const char *basename = strrchr(fname, '/');
        basename = basename ? basename + 1 : fname;
        char hdr[1024];
        int hlen = snprintf(hdr, sizeof(hdr),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/octet-stream\r\n"
            "Content-Disposition: attachment; filename=\"%s\"\r\n"
            "Content-Length: %lld\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Connection: close\r\n\r\n", basename, (long long)st.st_size);
        send(sock, hdr, hlen, 0);
        char *buf = iobuf;
        ssize_t nr;
        while ((nr = read(fd, buf, BUF_SIZE)) > 0) {
            ssize_t off = 0;
            while (off < nr) {
                ssize_t s = send(sock, buf + off, nr - off, 0);
                if (s <= 0) break;
                off += s;
            }
            if (off < nr) break;
        }
        close(fd);
        return;
    }

    /* GET /api/delete_file?name=<path> -> delete file from mounted save */
    if (strcmp(method, "GET") == 0 && strncmp(url, "/api/delete_file", 16) == 0) {
        if (!g_mounted) { http_json(sock, "{\"error\":\"No save mounted\"}"); return; }
        char fname[512] = {0};
        char *np = strstr(url, "name=");
        if (np) {
            strncpy(fname, np + 5, sizeof(fname) - 1);
            char *r = fname, *w = fname;
            while (*r && *r != '&') {
                if (*r == '%' && r[1] && r[2]) {
                    char hex[3] = {r[1], r[2], 0};
                    *w++ = (char)strtol(hex, NULL, 16);
                    r += 3;
                } else { *w++ = *r++; }
            }
            *w = 0;
        }
        if (!fname[0]) { http_json(sock, "{\"error\":\"Missing name\"}"); return; }
        char filepath[MAX_PATH_LEN];
        snprintf(filepath, sizeof(filepath), "%s/%s", g_mount_point, fname);
        if (unlink(filepath) < 0) {
            http_json(sock, "{\"error\":\"Delete failed\"}"); return;
        }
        sync();
        printf("[GarlicMgr] Deleted %s\n", fname);
        http_json(sock, "{\"ok\":true}");
        return;
    }

    /* GET /api/read_file?name=<path>&offset=N&length=N -> read chunk as base64 JSON */
    if (strcmp(method, "GET") == 0 && strncmp(url, "/api/read_file", 14) == 0) {
        if (!g_mounted) { http_json(sock, "{\"error\":\"No save mounted\"}"); return; }
        char fname[512] = {0};
        char *np = strstr(url, "name=");
        if (np) {
            strncpy(fname, np + 5, sizeof(fname) - 1);
            char *r = fname, *w = fname;
            while (*r && *r != '&') {
                if (*r == '%' && r[1] && r[2]) {
                    char hex[3] = {r[1], r[2], 0};
                    *w++ = (char)strtol(hex, NULL, 16);
                    r += 3;
                } else { *w++ = *r++; }
            }
            *w = 0;
        }
        if (!fname[0]) { http_json(sock, "{\"error\":\"Missing name\"}"); return; }

        /* Parse optional offset and length */
        off_t req_off = 0;
        size_t req_len = 0; /* 0 = whole file */
        char *op = strstr(url, "offset=");
        if (op) req_off = (off_t)atoll(op + 7);
        char *lp = strstr(url, "length=");
        if (lp) req_len = (size_t)atoll(lp + 7);

        char filepath[MAX_PATH_LEN];
        snprintf(filepath, sizeof(filepath), "%s/%s", g_mount_point, fname);
        struct stat st;
        if (stat(filepath, &st) < 0 || !S_ISREG(st.st_mode)) {
            http_json(sock, "{\"error\":\"File not found\"}"); return;
        }

        /* Clamp offset/length */
        if (req_off >= st.st_size) req_off = st.st_size;
        if (req_len == 0 || req_len > (size_t)(st.st_size - req_off))
            req_len = (size_t)(st.st_size - req_off);
        /* Cap single read at 64KB */
        if (req_len > 262144) req_len = 262144; /* 256KB max per read */

        int fd = open(filepath, O_RDONLY);
        if (fd < 0) { http_json(sock, "{\"error\":\"Cannot read file\"}"); return; }
        uint8_t *fbuf = malloc(req_len);
        if (!fbuf) { close(fd); http_json(sock, "{\"error\":\"Out of memory\"}"); return; }
        ssize_t nr = pread(fd, fbuf, req_len, req_off);
        close(fd);
        if (nr < 0) { free(fbuf); http_json(sock, "{\"error\":\"Read error\"}"); return; }

        size_t b64_len = 4 * ((nr + 2) / 3) + 1;
        size_t json_len = b64_len + 128;
        char *json = malloc(json_len);
        if (!json) { free(fbuf); http_json(sock, "{\"error\":\"Out of memory\"}"); return; }
        int pos = snprintf(json, json_len, "{\"total\":%lld,\"offset\":%lld,\"length\":%d,\"data\":\"",
                           (long long)st.st_size, (long long)req_off, (int)nr);
        b64_encode(fbuf, nr, json + pos, json_len - pos);
        pos += strlen(json + pos);
        snprintf(json + pos, json_len - pos, "\"}");
        free(fbuf);
        http_json(sock, json);
        free(json);
        return;
    }

    /* POST /api/write_file?name=<path> -> write base64 JSON body to file */
    if (strcmp(method, "POST") == 0 && strncmp(url, "/api/write_file", 15) == 0) {
        if (!g_mounted) { http_json(sock, "{\"error\":\"No save mounted\"}"); return; }
        char fname[512] = {0};
        char *np = strstr(url, "name=");
        if (np) {
            strncpy(fname, np + 5, sizeof(fname) - 1);
            char *r = fname, *w = fname;
            while (*r && *r != '&') {
                if (*r == '%' && r[1] && r[2]) {
                    char hex[3] = {r[1], r[2], 0};
                    *w++ = (char)strtol(hex, NULL, 16);
                    r += 3;
                } else { *w++ = *r++; }
            }
            *w = 0;
        }
        if (!fname[0]) { http_json(sock, "{\"error\":\"Missing name\"}"); return; }

        /* Read POST body */
        char *cl_hdr = strcasestr(req, "Content-Length:");
        size_t body_len = 0;
        if (cl_hdr) body_len = (size_t)atol(cl_hdr + 15);
        if (body_len == 0 || body_len > 4 * 1024 * 1024) {
            http_json(sock, "{\"error\":\"Invalid body\"}"); return;
        }
        char *body = malloc(body_len + 1);
        if (!body) { http_json(sock, "{\"error\":\"Out of memory\"}"); return; }
        char *hdr_end = strstr(req, "\r\n\r\n");
        size_t hdr_len = hdr_end ? (hdr_end + 4 - req) : n;
        size_t already = (n > (int)hdr_len) ? n - hdr_len : 0;
        if (already > body_len) already = body_len;
        if (already) memcpy(body, req + hdr_len, already);
        size_t got = already;
        while (got < body_len) {
            ssize_t r2 = recv(sock, body + got, body_len - got, 0);
            if (r2 <= 0) break;
            got += r2;
        }
        body[got] = 0;

        /* Body is base64 string */
        size_t dec_sz = (got * 3) / 4 + 4;
        uint8_t *dec = malloc(dec_sz);
        if (!dec) { free(body); http_json(sock, "{\"error\":\"Out of memory\"}"); return; }
        int dec_len = b64_decode(body, got, dec, dec_sz);
        free(body);

        char filepath[MAX_PATH_LEN];
        snprintf(filepath, sizeof(filepath), "%s/%s", g_mount_point, fname);
        int fd = open(filepath, O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (fd < 0) { free(dec); http_json(sock, "{\"error\":\"Cannot write file\"}"); return; }
        write(fd, dec, dec_len);
        close(fd);
        sync();
        free(dec);
        printf("[GarlicMgr] Wrote %d bytes to %s\n", dec_len, fname);
        char json[128];
        snprintf(json, sizeof(json), "{\"ok\":true,\"size\":%d}", dec_len);
        http_json(sock, json);
        return;
    }

    /* POST /api/patch_file?name=<path>&offset=N&length=N -> patch bytes in file */
    if (strcmp(method, "POST") == 0 && strncmp(url, "/api/patch_file", 15) == 0) {
        if (!g_mounted) { http_json(sock, "{\"error\":\"No save mounted\"}"); return; }
        char fname[512] = {0};
        char *np = strstr(url, "name=");
        if (np) {
            strncpy(fname, np + 5, sizeof(fname) - 1);
            char *r = fname, *w = fname;
            while (*r && *r != '&') {
                if (*r == '%' && r[1] && r[2]) {
                    char hex[3] = {r[1], r[2], 0};
                    *w++ = (char)strtol(hex, NULL, 16);
                    r += 3;
                } else { *w++ = *r++; }
            }
            *w = 0;
        }
        if (!fname[0]) { http_json(sock, "{\"error\":\"Missing name\"}"); return; }

        off_t patch_off = 0;
        char *op = strstr(url, "offset=");
        if (op) patch_off = (off_t)atoll(op + 7);

        /* Read POST body (base64) */
        char *cl_hdr = strcasestr(req, "Content-Length:");
        size_t body_len = 0;
        if (cl_hdr) body_len = (size_t)atol(cl_hdr + 15);
        if (body_len == 0 || body_len > 1024 * 1024) {
            http_json(sock, "{\"error\":\"Invalid body\"}"); return;
        }
        char *body = malloc(body_len + 1);
        if (!body) { http_json(sock, "{\"error\":\"Out of memory\"}"); return; }
        char *hdr_end = strstr(req, "\r\n\r\n");
        size_t hdr_len = hdr_end ? (hdr_end + 4 - req) : n;
        size_t already = (n > (int)hdr_len) ? n - hdr_len : 0;
        if (already > body_len) already = body_len;
        if (already) memcpy(body, req + hdr_len, already);
        size_t got = already;
        while (got < body_len) {
            ssize_t r2 = recv(sock, body + got, body_len - got, 0);
            if (r2 <= 0) break;
            got += r2;
        }
        body[got] = 0;

        /* Decode base64 */
        size_t dec_sz = (got * 3) / 4 + 4;
        uint8_t *dec = malloc(dec_sz);
        if (!dec) { free(body); http_json(sock, "{\"error\":\"Out of memory\"}"); return; }
        int dec_len = b64_decode(body, got, dec, dec_sz);
        free(body);

        char filepath[MAX_PATH_LEN];
        snprintf(filepath, sizeof(filepath), "%s/%s", g_mount_point, fname);
        int fd = open(filepath, O_WRONLY);
        if (fd < 0) { free(dec); http_json(sock, "{\"error\":\"Cannot open file\"}"); return; }
        ssize_t written = pwrite(fd, dec, dec_len, patch_off);
        close(fd);
        sync();
        free(dec);
        printf("[GarlicMgr] Patched %d bytes at offset %lld in %s\n", (int)written, (long long)patch_off, fname);
        char json[128];
        snprintf(json, sizeof(json), "{\"ok\":true,\"written\":%d}", (int)written);
        http_json(sock, json);
        return;
    }

    /* GET /api/files → file listing of mounted save */
    if (strcmp(method, "GET") == 0 && strcmp(url, "/api/files") == 0) {
        if (!g_mounted) { http_json(sock, "{\"files\":[]}"); return; }
        int need = snprintf(NULL, 0, "{\"files\":[");
        need += json_list_dir(NULL, 0, g_mount_point, "");
        need += snprintf(NULL, 0, "]}");
        char *json = malloc(need + 1);
        if (!json) { http_json(sock, "{\"error\":\"Out of memory\"}"); return; }
        int pos = snprintf(json, need + 1, "{\"files\":[");
        pos += json_list_dir(json + pos, need + 1 - pos, g_mount_point, "");
        snprintf(json + pos, need + 1 - pos, "]}");
        http_json(sock, json);
        free(json);
        return;
    }

    /* GET /api/icon -> serve icon0.png from mounted save */
    if (strcmp(method, "GET") == 0 && strncmp(url, "/api/icon", 9) == 0) {
        if (!g_mounted) { http_send(sock, "404 Not Found", "text/plain", "No mount", 8); return; }
        char icon_path[MAX_PATH_LEN];
        snprintf(icon_path, sizeof(icon_path), "%s/sce_sys/icon0.png", g_mount_point);
        struct stat st;
        if (stat(icon_path, &st) < 0) {
            http_send(sock, "404 Not Found", "text/plain", "No icon", 7); return;
        }
        int fd = open(icon_path, O_RDONLY);
        if (fd < 0) { http_send(sock, "500 Error", "text/plain", "Open fail", 9); return; }
        char hdr[512];
        int hlen = snprintf(hdr, sizeof(hdr),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: image/png\r\n"
            "Content-Length: %lld\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Cache-Control: no-cache\r\n"
            "Connection: close\r\n\r\n", (long long)st.st_size);
        send(sock, hdr, hlen, 0);
        char *buf = iobuf;
        ssize_t nr;
        while ((nr = read(fd, buf, BUF_SIZE)) > 0)
            send(sock, buf, nr, 0);
        close(fd);
        return;
    }

    /* GET /api/usb_list -> detect connected USB drives */
    if (strcmp(method, "GET") == 0 && strcmp(url, "/api/usb_list") == 0) {
        char json[256] = "{\"usb\":[";
        int pos = strlen(json), first = 1;
        for (int u = 0; u < 8; u++) {
            char mp[32];
            snprintf(mp, sizeof(mp), "/mnt/usb%d", u);
            struct stat st;
            if (stat(mp, &st) == 0 && S_ISDIR(st.st_mode)) {
                /* Check if it's actually mounted (has contents) */
                DIR *d = opendir(mp);
                if (d) {
                    struct dirent *ent;
                    int has_files = 0;
                    while ((ent = readdir(d))) {
                        if (ent->d_name[0] != '.') { has_files = 1; break; }
                    }
                    closedir(d);
                    if (has_files) {
                        if (!first) json[pos++] = ',';
                        pos += snprintf(json + pos, sizeof(json) - pos, "%d", u);
                        first = 0;
                    }
                }
            }
        }
        snprintf(json + pos, sizeof(json) - pos, "]}");
        http_json(sock, json);
        return;
    }

    /* GET /api/users -> list users with usernames */
    if (strcmp(method, "GET") == 0 && strcmp(url, "/api/users") == 0) {
        DIR *home = opendir("/user/home");
        char json[2048] = "{\"users\":[";
        int pos = strlen(json), first = 1;
        if (home) {
            struct dirent *ue;
            while ((ue = readdir(home))) {
                if (ue->d_name[0] == '.') continue;
                char upath[MAX_PATH_LEN];
                snprintf(upath, sizeof(upath), "/user/home/%s/username.dat", ue->d_name);
                char uname[17] = {0};
                int fd = open(upath, O_RDONLY);
                if (fd >= 0) {
                    read(fd, uname, 16);
                    uname[16] = 0;
                    close(fd);
                }
                if (!uname[0]) snprintf(uname, sizeof(uname), "User %s", ue->d_name);
                if (!first) json[pos++] = ',';
                pos += snprintf(json + pos, sizeof(json) - pos,
                    "{\"id\":\"%s\",\"name\":\"%s\"}", ue->d_name, uname);
                first = 0;
            }
            closedir(home);
        }
        snprintf(json + pos, sizeof(json) - pos, "]}");
        http_json(sock, json);
        return;
    }

    /* GET /api/saves */
    if (strcmp(method, "GET") == 0 && strcmp(url, "/api/saves") == 0) {
        scan_saves();
        int need = snprintf(NULL, 0, "{\"saves\":[");
        for (int i = 0; i < g_save_count; i++) {
            if (i > 0) need++;
            const char *tname = lookup_title(g_saves[i].title_id);
            const char *stype = g_saves[i].is_ps4 ? "ps4" : "ps5";
            /* Count escaped title_name length */
            int tname_esc_len = 0;
            if (tname) {
                for (const char *c = tname; *c; c++)
                    tname_esc_len += (*c == '"' || *c == '\\' || *c < 0x20) ? 2 : 1;
            }
            need += snprintf(NULL, 0,
                "{\"title_id\":\"%s\",\"save_name\":\"%s\",\"path\":\"%s\",\"dir\":\"%s\",\"title_name\":\"\",\"type\":\"%s\",\"backup\":%s,\"usb\":%s,\"uid\":\"%s\"}",
                g_saves[i].title_id, g_saves[i].save_name, g_saves[i].path, g_saves[i].dir_name,
                stype, g_saves[i].is_backup ? "true" : "false", g_saves[i].is_usb ? "true" : "false",
                g_saves[i].user_id);
            need += tname_esc_len;
        }
        need += snprintf(NULL, 0, "]}");
        char *json = malloc(need + 1);
        if (!json) { http_json(sock, "{\"error\":\"Out of memory\"}"); return; }
        int pos = snprintf(json, need + 1, "{\"saves\":[");
        for (int i = 0; i < g_save_count; i++) {
            if (i > 0) json[pos++] = ',';
            const char *tname = lookup_title(g_saves[i].title_id);
            const char *stype = g_saves[i].is_ps4 ? "ps4" : "ps5";
            /* Escape title_name for JSON */
            char esc_tname[512] = {0};
            if (tname) {
                int tp = 0;
                for (const char *c = tname; *c && tp < (int)sizeof(esc_tname) - 2; c++) {
                    if (*c == '"' || *c == '\\') { esc_tname[tp++] = '\\'; esc_tname[tp++] = *c; }
                    else if (*c >= 0x20) { esc_tname[tp++] = *c; }
                }
                esc_tname[tp] = 0;
            }
            pos += snprintf(json + pos, need + 1 - pos,
                "{\"title_id\":\"%s\",\"save_name\":\"%s\",\"path\":\"%s\",\"dir\":\"%s\",\"title_name\":\"%s\",\"type\":\"%s\",\"backup\":%s,\"usb\":%s,\"uid\":\"%s\"}",
                g_saves[i].title_id, g_saves[i].save_name, g_saves[i].path, g_saves[i].dir_name,
                esc_tname, stype, g_saves[i].is_backup ? "true" : "false", g_saves[i].is_usb ? "true" : "false",
                g_saves[i].user_id);
        }
        snprintf(json + pos, need + 1 - pos, "]}");
        http_json(sock, json);
        free(json);
        return;
    }

    /* GET /api/mount?idx=N */
    if (strcmp(method, "GET") == 0 && strncmp(url, "/api/mount", 10) == 0) {
        if (g_mounted) unmount_save();

        int idx = -1;
        char *p = strstr(url, "idx=");
        if (p) idx = atoi(p + 4);
        if (idx < 0 || idx >= g_save_count) {
            http_json(sock, "{\"error\":\"Invalid index\"}"); return;
        }

        /* Check if this is a decrypted folder (USB) — just list contents, no mount */
        struct stat path_st;
        int is_dir = (stat(g_saves[idx].path, &path_st) == 0 && S_ISDIR(path_st.st_mode));

        const char *browse_root;
        if (is_dir) {
            browse_root = g_saves[idx].path;
            /* Set mount point so read_file/write_file work on USB folders */
            snprintf(g_mount_point, sizeof(g_mount_point), "%s", browse_root);
            g_mounted = 1;
            g_mounted_path[0] = 0;
            g_local_copy[0] = 0;
        } else {
            int ret = mount_save(idx);
            if (ret < 0) {
                char json[256];
                snprintf(json, sizeof(json), "{\"error\":\"Mount failed: %d (0x%x)\"}", ret, ret);
                http_json(sock, json);
                return;
            }
            browse_root = g_mount_point;
        }

        /* Parse param.sfo properly using key-value parser */
        char acct_id[20] = {0};
        char save_title[64] = {0};
        char sfo_title_id[16] = {0};
        char sfo_path[MAX_PATH_LEN];
        snprintf(sfo_path, sizeof(sfo_path), "%s/sce_sys/param.sfo", browse_root);
        sfo_info_t sfo_info;
        if (parse_sfo(sfo_path, &sfo_info) == 0) {
            if (sfo_info.account_id) {
                uint8_t *a = (uint8_t *)&sfo_info.account_id;
                snprintf(acct_id, sizeof(acct_id), "0x%02X%02X%02X%02X%02X%02X%02X%02X",
                    a[0],a[1],a[2],a[3],a[4],a[5],a[6],a[7]);
            }
            if (sfo_info.main_title[0]) {
                int tp = 0;
                for (int ti = 0; sfo_info.main_title[ti] && tp < (int)sizeof(save_title) - 2; ti++) {
                    char ch = sfo_info.main_title[ti];
                    if (ch == '"' || ch == '\\') { save_title[tp++] = '\\'; }
                    if (ch >= 0x20) save_title[tp++] = ch;
                }
                save_title[tp] = 0;
            }
            if (sfo_info.title_id[0])
                strncpy(sfo_title_id, sfo_info.title_id, sizeof(sfo_title_id) - 1);
        }

        const char *tid = sfo_title_id[0] ? sfo_title_id : g_saves[idx].title_id;
        int need = snprintf(NULL, 0,
            "{\"title_id\":\"%s\",\"save_name\":\"%s\",\"mount\":\"%s\","
            "\"account_id\":\"%s\",\"save_title\":\"%s\",\"files\":[",
            tid, g_saves[idx].save_name, browse_root, acct_id, save_title);
        need += json_list_dir(NULL, 0, browse_root, "");
        need += snprintf(NULL, 0, "]}");
        char *json = malloc(need + 1);
        if (!json) { http_json(sock, "{\"error\":\"Out of memory\"}"); return; }
        int pos = snprintf(json, need + 1,
            "{\"title_id\":\"%s\",\"save_name\":\"%s\",\"mount\":\"%s\","
            "\"account_id\":\"%s\",\"save_title\":\"%s\",\"files\":[",
            tid, g_saves[idx].save_name, browse_root, acct_id, save_title);
        pos += json_list_dir(json + pos, need + 1 - pos, browse_root, "");
        snprintf(json + pos, need + 1 - pos, "]}");
        http_json(sock, json);
        free(json);
        return;
    }

    /* GET /api/download_raw?idx=N -> stream raw save image (no mount needed) */
    if (strcmp(method, "GET") == 0 && strncmp(url, "/api/download_raw", 17) == 0) {
        scan_saves();
        int idx = -1;
        char *p = strstr(url, "idx=");
        if (p) idx = atoi(p + 4);
        if (idx < 0 || idx >= g_save_count) {
            http_json(sock, "{\"error\":\"Invalid index\"}");
            return;
        }
        save_entry_t *s = &g_saves[idx];
        /* Escalate to read savedata_prospero */
        pid_t rpid = getpid();
        kernel_set_ucred_uid(rpid, 0);
        kernel_set_ucred_authid(rpid, 0x4800000000000010ULL);

        struct stat st;
        if (stat(s->path, &st) < 0) {
            http_json(sock, "{\"error\":\"File not found\"}");
            return;
        }

        if (s->is_ps4 && strncmp(s->save_name, "sdimg_", 6) == 0) {
            /* PS4: zip the save image + .bin sealed key together */
            const char *savename = s->save_name + 6;
            char dir[MAX_PATH_LEN];
            strncpy(dir, s->path, sizeof(dir) - 1);
            dir[sizeof(dir) - 1] = 0;
            char *sl = strrchr(dir, '/');
            if (sl) *(sl + 1) = 0;
            char bin_path[MAX_PATH_LEN];
            snprintf(bin_path, sizeof(bin_path), "%s%s.bin", dir, savename);

            char zip_name[512];
            snprintf(zip_name, sizeof(zip_name), "%s_%s.zip", s->title_id, savename);
            char key_name[256];
            snprintf(key_name, sizeof(key_name), "%s.bin", savename);

            zip_file_t zfiles[2] = {
                { s->path, s->save_name },
                { bin_path, key_name }
            };
            /* Check if .bin exists; if not, just send 1 file */
            int nfiles = (stat(bin_path, &st) == 0) ? 2 : 1;

            char hdr[512];
            int hlen = snprintf(hdr, sizeof(hdr),
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: application/zip\r\n"
                "Content-Disposition: attachment; filename=\"%s\"\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "Connection: close\r\n"
                "\r\n", zip_name);
            send(sock, hdr, hlen, 0);
            zip_send_files(sock, zfiles, nfiles, iobuf);
        } else {
            /* PS5: stream raw image */
            int fd = open(s->path, O_RDONLY);
            if (fd < 0) {
                http_json(sock, "{\"error\":\"Cannot open file\"}");
                return;
            }
            char hdr[512];
            int hlen = snprintf(hdr, sizeof(hdr),
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: application/octet-stream\r\n"
                "Content-Disposition: attachment; filename=\"%s\"\r\n"
                "Content-Length: %lld\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "Connection: close\r\n"
                "\r\n", s->save_name, (long long)st.st_size);
            send(sock, hdr, hlen, 0);
            char *buf = iobuf;
            ssize_t nr;
            while ((nr = read(fd, buf, BUF_SIZE)) > 0)
                send(sock, buf, nr, 0);
            close(fd);
        }
        return;
    }

    /* GET /api/download -> tar stream */
    if (strcmp(method, "GET") == 0 && strcmp(url, "/api/download") == 0) {
        if (!g_mounted) { http_json(sock, "{\"error\":\"No save mounted\"}"); return; }
        char hdr[512];
        int hlen = snprintf(hdr, sizeof(hdr),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/zip\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Connection: close\r\n"
            "\r\n");
        send(sock, hdr, hlen, 0);
        zip_send(sock, g_mount_point);
        return;
    }

    /* GET /api/dump_usb?idx=N&usb=U -> mount, copy decrypted files to USB, unmount */
    if (strcmp(method, "GET") == 0 && strncmp(url, "/api/dump_usb", 13) == 0) {
        scan_saves();
        int idx = -1, usb_num = 0;
        char *p = strstr(url, "idx=");
        if (p) idx = atoi(p + 4);
        p = strstr(url, "usb=");
        if (p) usb_num = atoi(p + 4);
        if (usb_num < 0 || usb_num > 7) usb_num = 0;
        if (idx < 0 || idx >= g_save_count) {
            http_json(sock, "{\"error\":\"Invalid index\"}"); return;
        }
        save_entry_t *s = &g_saves[idx];
        pid_t rpid = getpid();
        kernel_set_ucred_uid(rpid, 0);
        kernel_set_ucred_authid(rpid, 0x4800000000000010ULL);

        /* Mount the save */
        if (g_mounted) unmount_save();
        int ret = mount_save(idx);
        if (ret < 0) {
            char json[128];
            snprintf(json, sizeof(json), "{\"error\":\"Mount failed: %d\"}", ret);
            http_json(sock, json);
            return;
        }

        /* Create USB destination dirs */
        char usb_base[64];
        snprintf(usb_base, sizeof(usb_base), "/mnt/usb%d", usb_num);
        char tmp[MAX_PATH_LEN];
        snprintf(tmp, sizeof(tmp), "%s/PS5", usb_base); mkdir(tmp, 0777);
        snprintf(tmp, sizeof(tmp), "%s/PS5/SAVEDATA", usb_base); mkdir(tmp, 0777);
        snprintf(tmp, sizeof(tmp), "%s/PS5/SAVEDATA/GARLIC", usb_base); mkdir(tmp, 0777);
        char title_dir[MAX_PATH_LEN];
        snprintf(title_dir, sizeof(title_dir), "%s/PS5/SAVEDATA/GARLIC/%s", usb_base, s->title_id);
        mkdir(title_dir, 0777);
        char save_dir[MAX_PATH_LEN];
        snprintf(save_dir, sizeof(save_dir), "%s/%s", title_dir, s->save_name);

        /* Copy decrypted files from mount point to USB */
        int cpret = copy_dir_recursive(g_mount_point, save_dir);

        /* Unmount (no changes made, just reading) */
        unmount_save();

        if (cpret < 0) {
            http_json(sock, "{\"error\":\"Failed to copy files to USB\"}");
            return;
        }

        char json[512];
        snprintf(json, sizeof(json),
            "{\"ok\":true,\"path\":\"%s\"}", save_dir);
        http_json(sock, json);
        return;
    }

    /* GET /api/unmount */
    if (strcmp(method, "GET") == 0 && strcmp(url, "/api/unmount") == 0) {
        int ret = unmount_save();
        char json[128];
        snprintf(json, sizeof(json), "{\"ok\":%s}", ret == 0 ? "true" : "false");
        http_json(sock, json);
        return;
    }

    /* GET /api/log */
    if (strcmp(method, "GET") == 0 && strcmp(url, "/api/log") == 0) {
        g_logbuf[g_logpos] = 0;
        http_send(sock, "200 OK", "text/plain", g_logbuf, g_logpos);
        return;
    }

    /* ── Server-side editor APIs ─────────────────────────────────── */

    /* GET /api/editor/open?name=<path> — load file into server memory */
    if (strcmp(method, "GET") == 0 && strncmp(url, "/api/editor/open", 16) == 0) {
        if (!g_mounted) { http_json(sock, "{\"error\":\"No save mounted\"}"); return; }
        char fname[512] = {0};
        char *np = strstr(url, "name=");
        if (np) {
            strncpy(fname, np + 5, sizeof(fname) - 1);
            char *r = fname, *w = fname;
            while (*r && *r != '&') {
                if (*r == '%' && r[1] && r[2]) {
                    char hex[3] = {r[1], r[2], 0};
                    *w++ = (char)strtol(hex, NULL, 16);
                    r += 3;
                } else { *w++ = *r++; }
            }
            *w = 0;
        }
        if (!fname[0]) { http_json(sock, "{\"error\":\"Missing name\"}"); return; }

        /* Close previous editor buffer */
        if (g_ed_buf) { free(g_ed_buf); g_ed_buf = NULL; }
        g_ed_size = 0; g_ed_dirty = 0; g_ed_name[0] = 0;

        char filepath[MAX_PATH_LEN];
        snprintf(filepath, sizeof(filepath), "%s/%s", g_mount_point, fname);
        struct stat st;
        if (stat(filepath, &st) < 0 || !S_ISREG(st.st_mode)) {
            http_json(sock, "{\"error\":\"File not found\"}"); return;
        }
        g_ed_buf = malloc(st.st_size);
        if (!g_ed_buf) { http_json(sock, "{\"error\":\"Out of memory\"}"); return; }
        int fd = open(filepath, O_RDONLY);
        if (fd < 0) { free(g_ed_buf); g_ed_buf = NULL; http_json(sock, "{\"error\":\"Cannot read\"}"); return; }
        ssize_t total = 0;
        while (total < st.st_size) {
            ssize_t nr = read(fd, g_ed_buf + total, st.st_size - total);
            if (nr <= 0) break;
            total += nr;
        }
        close(fd);
        g_ed_size = total;
        strncpy(g_ed_name, fname, sizeof(g_ed_name) - 1);
        logprintf("[GarlicMgr] Editor opened: %s (%zu bytes)\n", fname, g_ed_size);
        char json[256];
        snprintf(json, sizeof(json), "{\"ok\":true,\"size\":%zu}", g_ed_size);
        http_json(sock, json);
        return;
    }

    /* GET /api/editor/read?offset=N&length=N — return raw bytes from buffer */
    if (strcmp(method, "GET") == 0 && strncmp(url, "/api/editor/read", 16) == 0) {
        if (!g_ed_buf) { http_json(sock, "{\"error\":\"No file open\"}"); return; }
        size_t off = 0, len = 4096;
        char *op = strstr(url, "offset=");
        char *lp = strstr(url, "length=");
        if (op) off = strtoul(op + 7, NULL, 10);
        if (lp) len = strtoul(lp + 7, NULL, 10);
        if (off >= g_ed_size) { http_send(sock, "200 OK", "application/octet-stream", "", 0); return; }
        if (off + len > g_ed_size) len = g_ed_size - off;
        http_send(sock, "200 OK", "application/octet-stream", g_ed_buf + off, len);
        return;
    }

    /* POST /api/editor/patch — body: raw bytes, query: offset=N */
    if (strcmp(method, "POST") == 0 && strncmp(url, "/api/editor/patch", 17) == 0) {
        if (!g_ed_buf) { http_json(sock, "{\"error\":\"No file open\"}"); return; }
        size_t off = 0;
        char *op = strstr(url, "offset=");
        if (op) off = strtoul(op + 7, NULL, 10);

        size_t clen = 0;
        char *cl = strcasestr(req, "Content-Length:");
        if (cl) clen = strtoul(cl + 15, NULL, 10);

        char *body_start = strstr(req, "\r\n\r\n");
        if (!body_start) { http_json(sock, "{\"error\":\"Bad request\"}"); return; }
        body_start += 4;
        size_t body_in_buf = n - (body_start - req);

        /* Read remaining body if needed */
        char *patch = malloc(clen);
        if (!patch) { http_json(sock, "{\"error\":\"Out of memory\"}"); return; }
        size_t got = body_in_buf < clen ? body_in_buf : clen;
        memcpy(patch, body_start, got);
        while (got < clen) {
            ssize_t r = recv(sock, patch + got, clen - got, 0);
            if (r <= 0) break;
            got += r;
        }

        if (off + got > g_ed_size) {
            /* Grow buffer if patching past end */
            char *nb = realloc(g_ed_buf, off + got);
            if (!nb) { free(patch); http_json(sock, "{\"error\":\"Out of memory\"}"); return; }
            g_ed_buf = nb;
            if (off > g_ed_size) memset(g_ed_buf + g_ed_size, 0, off - g_ed_size);
            g_ed_size = off + got;
        }
        memcpy(g_ed_buf + off, patch, got);
        free(patch);
        g_ed_dirty = 1;
        char json[128];
        snprintf(json, sizeof(json), "{\"ok\":true,\"size\":%zu}", g_ed_size);
        http_json(sock, json);
        return;
    }

    /* GET /api/editor/save — write buffer back to file */
    if (strcmp(method, "GET") == 0 && strncmp(url, "/api/editor/save", 16) == 0) {
        if (!g_ed_buf || !g_ed_name[0]) { http_json(sock, "{\"error\":\"No file open\"}"); return; }
        if (!g_mounted) { http_json(sock, "{\"error\":\"No save mounted\"}"); return; }
        char filepath[MAX_PATH_LEN];
        snprintf(filepath, sizeof(filepath), "%s/%s", g_mount_point, g_ed_name);
        int fd = open(filepath, O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (fd < 0) { http_json(sock, "{\"error\":\"Cannot write file\"}"); return; }
        ssize_t written = 0;
        while ((size_t)written < g_ed_size) {
            ssize_t w = write(fd, g_ed_buf + written, g_ed_size - written);
            if (w <= 0) break;
            written += w;
        }
        close(fd);
        sync();
        g_ed_dirty = 0;
        logprintf("[GarlicMgr] Editor saved: %s (%zu bytes)\n", g_ed_name, g_ed_size);
        char json[128];
        snprintf(json, sizeof(json), "{\"ok\":true,\"written\":%zd}", written);
        http_json(sock, json);
        return;
    }

    /* GET /api/editor/close — free buffer */
    if (strcmp(method, "GET") == 0 && strncmp(url, "/api/editor/close", 17) == 0) {
        if (g_ed_buf) { free(g_ed_buf); g_ed_buf = NULL; }
        g_ed_size = 0; g_ed_dirty = 0; g_ed_name[0] = 0;
        http_json(sock, "{\"ok\":true}");
        return;
    }

    /* GET /api/editor/status — check editor state */
    if (strcmp(method, "GET") == 0 && strncmp(url, "/api/editor/status", 18) == 0) {
        char json[512];
        snprintf(json, sizeof(json), "{\"open\":%s,\"name\":\"%s\",\"size\":%zu,\"dirty\":%s}",
            g_ed_buf ? "true" : "false", g_ed_name, g_ed_size, g_ed_dirty ? "true" : "false");
        http_json(sock, json);
        return;
    }

    /* GET /api/shutdown */
    if (strcmp(method, "GET") == 0 && strcmp(url, "/api/shutdown") == 0) {
        if (g_mounted) unmount_save();
        /* Clean up all known mount points */
        UmountOpt uclean;
        memset(&uclean, 0, sizeof(uclean));
        sceFsInitUmountSaveDataOpt(&uclean);
        sceFsUmountSaveData(&uclean, "/data/mount_sd", 0, 0);
        sync();
        http_json(sock, "{\"ok\":true}");
        close(sock);
        notify("Garlic SaveMgr: Shutting down");
        kill(getpid(), SIGKILL);
        return;
    }

    /* GET /api/status */
    if (strcmp(method, "GET") == 0 && strcmp(url, "/api/status") == 0) {
        char json[512];
        snprintf(json, sizeof(json),
            "{\"mounted\":%s,\"mount_point\":\"%s\",\"save_path\":\"%s\"}",
            g_mounted ? "true" : "false", g_mount_point, g_mounted_path);
        http_json(sock, json);
        return;
    }

    /* POST /api/upload_key -> upload PS4 .bin sealed key for decrypt */
    if (strcmp(method, "POST") == 0 && strcmp(url, "/api/upload_key") == 0) {
        size_t clen = 0;
        char *cl = strcasestr(req, "Content-Length:");
        if (cl) clen = strtoul(cl + 15, NULL, 10);
        if (clen == 0 || clen > 4096) {
            http_json(sock, "{\"error\":\"Invalid key size\"}"); return;
        }
        char *body_start = strstr(req, "\r\n\r\n");
        if (!body_start) { http_json(sock, "{\"error\":\"Bad request\"}"); return; }
        body_start += 4;
        size_t body_in_buf = n - (body_start - req);
        if (body_in_buf > clen) body_in_buf = clen;

        mkdir("/data/save_files", 0777);
        int fd = open("/data/save_files/_tmp_key.bin", O_CREAT | O_WRONLY | O_TRUNC, 0755);
        if (fd < 0) { http_json(sock, "{\"error\":\"Cannot create key file\"}"); return; }
        write(fd, body_start, body_in_buf);
        size_t received = body_in_buf;
        while (received < clen) {
            char *buf = iobuf;
            size_t want = BUF_SIZE < (clen - received) ? BUF_SIZE : (clen - received);
            ssize_t r = recv_all(sock, buf, want);
            if (r <= 0) break;
            write(fd, buf, r);
            received += r;
        }
        close(fd);
        printf("[GarlicMgr] upload_key: received %zu bytes\n", received);
        http_json(sock, "{\"ok\":true}");
        return;
    }

    /* POST /api/decrypt_upload -> upload encrypted save, get decrypted .zip */
    if (strcmp(method, "POST") == 0 && strncmp(url, "/api/decrypt_upload", 19) == 0) {
        size_t clen = 0;
        char *cl = strcasestr(req, "Content-Length:");
        if (cl) clen = strtoul(cl + 15, NULL, 10);
        if (clen == 0 || clen > (size_t)2 * 1024 * 1024 * 1024) {
            http_json(sock, "{\"error\":\"Invalid size\"}"); return;
        }
        char *body_start = strstr(req, "\r\n\r\n");
        if (!body_start) { http_json(sock, "{\"error\":\"Bad request\"}"); return; }
        body_start += 4;
        size_t body_in_buf = n - (body_start - req);

        mkdir("/data/save_files", 0777);
        int is_ps4_dec = (strstr(url, "ps4=1") != NULL);
        const char *tmp = is_ps4_dec ? "/data/save_files/sdimg__tmp_dec" : "/data/save_files/_tmp_dec";
        int fd = open(tmp, O_CREAT | O_WRONLY | O_TRUNC, 0755);
        if (fd < 0) { http_json(sock, "{\"error\":\"Cannot create temp file\"}"); return; }
        if (body_in_buf > clen) body_in_buf = clen;
        write(fd, body_start, body_in_buf);
        size_t received = body_in_buf;
        while (received < clen) {
            char *buf = iobuf;
            size_t want = BUF_SIZE < (clen - received) ? BUF_SIZE : (clen - received);
            ssize_t r = recv_all(sock, buf, want);
            if (r <= 0) break;
            write(fd, buf, r);
            received += r;
        }
        close(fd);
        printf("[GarlicMgr] decrypt_upload: received %zu bytes (ps4=%d)\n", received, is_ps4_dec);

        /* For PS4, rename uploaded key to match the expected .bin path */
        if (is_ps4_dec)
            rename("/data/save_files/_tmp_key.bin", "/data/save_files/_tmp_dec.bin");

        int ret = mount_by_path(tmp);
        if (ret < 0) {
            unlink(tmp);
            char json[128];
            snprintf(json, sizeof(json), "{\"error\":\"Mount failed: %d\"}", ret);
            http_json(sock, json); return;
        }

        /* Mount succeeded — return JSON with download URL instead of streaming zip
         * on the same connection (avoids browser XHR issues with large blob responses) */
        http_json(sock, "{\"ok\":true,\"download\":\"/api/download\"}");
        return;
    }

    /* POST /api/import_encrypted?uid=<hex>&ps4=1 -> upload encrypted save, mount, check SFO + AID */
    if (strcmp(method, "POST") == 0 && strncmp(url, "/api/import_encrypted", 21) == 0) {
        size_t clen = 0;
        char *cl = strcasestr(req, "Content-Length:");
        if (cl) clen = strtoul(cl + 15, NULL, 10);
        if (clen == 0 || clen > (size_t)2 * 1024 * 1024 * 1024) {
            http_json(sock, "{\"error\":\"Invalid size\"}"); return;
        }
        char *body_start = strstr(req, "\r\n\r\n");
        if (!body_start) { http_json(sock, "{\"error\":\"Bad request\"}"); return; }
        body_start += 4;
        size_t body_in_buf = n - (body_start - req);

        int is_ps4_imp = (strstr(url, "ps4=1") != NULL);
        mkdir("/data/save_files", 0777);
        const char *tmp = is_ps4_imp ? "/data/save_files/sdimg__tmp_import" : "/data/save_files/_tmp_import";
        int fd = open(tmp, O_CREAT | O_WRONLY | O_TRUNC, 0755);
        if (fd < 0) { http_json(sock, "{\"error\":\"Cannot create temp file\"}"); return; }
        if (body_in_buf > clen) body_in_buf = clen;
        write(fd, body_start, body_in_buf);
        size_t received = body_in_buf;
        while (received < clen) {
            char *buf = iobuf;
            size_t want = BUF_SIZE < (clen - received) ? BUF_SIZE : (clen - received);
            ssize_t r = recv_all(sock, buf, want);
            if (r <= 0) break;
            write(fd, buf, r);
            received += r;
        }
        close(fd);
        printf("[GarlicMgr] import_encrypted: received %zu bytes (ps4=%d)\n", received, is_ps4_imp);

        /* For PS4, rename uploaded key to match */
        if (is_ps4_imp)
            rename("/data/save_files/_tmp_key.bin", "/data/save_files/_tmp_import.bin");

        /* Mount the encrypted image */
        int ret = mount_by_path(tmp);
        if (ret < 0) {
            unlink(tmp);
            char json[128];
            snprintf(json, sizeof(json), "{\"error\":\"Mount failed: %d\"}", ret);
            http_json(sock, json); return;
        }

        /* Parse param.sfo from mounted save */
        char sfo_path[MAX_PATH_LEN];
        snprintf(sfo_path, sizeof(sfo_path), "%s/sce_sys/param.sfo", g_mount_point);
        sfo_info_t sfo;
        int sfo_ret = parse_sfo(sfo_path, &sfo);
        if (sfo_ret == -2) {
            unmount_save(); unlink(tmp);
            http_json(sock, "{\"error\":\"param.sfo is corrupted!\"}"); return;
        }
        if (sfo_ret < 0) {
            unmount_save(); unlink(tmp);
            http_json(sock, "{\"error\":\"Cannot parse param.sfo\"}"); return;
        }
        if (!sfo.title_id[0] || !sfo.dir_name[0]) {
            unmount_save(); unlink(tmp);
            http_json(sock, "{\"error\":\"param.sfo missing TITLE_ID or SAVEDATA_DIRECTORY\"}"); return;
        }

        /* Detect PS4 vs PS5 from SFO FORMAT field: "obs"=PS4, "ppr"=PS5 */
        g_enc_ps4 = (strcmp(sfo.format, "obs") == 0) ? 1 : 0;
        printf("[GarlicMgr] import_encrypted: FORMAT=%s -> %s\n",
               sfo.format, g_enc_ps4 ? "PS4" : "PS5");

        /* Get user's account_id for comparison */
        char uid_hex[16] = {0};
        char *uidp = strstr(url, "uid=");
        if (uidp) {
            strncpy(uid_hex, uidp + 4, sizeof(uid_hex) - 1);
            char *amp = strchr(uid_hex, '&');
            if (amp) *amp = 0;
        }
        if (!uid_hex[0]) {
            int local_uid = 0;
            sceUserServiceGetForegroundUser(&local_uid);
            snprintf(uid_hex, sizeof(uid_hex), "%x", (uint32_t)local_uid);
        }

        uint64_t user_aid = get_account_id_for_user(uid_hex);

        int match = (user_aid != 0 && sfo.account_id == user_aid) ? 1 : 0;

        /* Check if save already exists on system */
        char exist_path[MAX_PATH_LEN];
        snprintf(exist_path, sizeof(exist_path), "/user/home/%s/%s/%s/sdimg_%s",
                 uid_hex, g_enc_ps4 ? "savedata" : "savedata_prospero",
                 sfo.title_id, sfo.dir_name);
        struct stat ex_st;
        int exists = (stat(exist_path, &ex_st) == 0) ? 1 : 0;

        logprintf("[GarlicMgr] import_encrypted: title=%s dir=%s save_aid=%llx user_aid=%llx match=%d exists=%d\n",
               sfo.title_id, sfo.dir_name,
               (unsigned long long)sfo.account_id, (unsigned long long)user_aid, match, exists);

        char json[512];
        snprintf(json, sizeof(json),
                 "{\"ok\":true,\"title_id\":\"%s\",\"dir_name\":\"%s\",\"main_title\":\"%s\","
                 "\"save_aid\":\"%llx\",\"user_aid\":\"%llx\",\"match\":%s,\"exists\":%s}",
                 sfo.title_id, sfo.dir_name, sfo.main_title,
                 (unsigned long long)sfo.account_id, (unsigned long long)user_aid,
                 match ? "true" : "false", exists ? "true" : "false");
        http_json(sock, json);
        return;
    }

    /* POST /api/encrypt_upload?aid=<optional hex> -> upload .zip, get encrypted save */
    if (strcmp(method, "POST") == 0 && strncmp(url, "/api/encrypt_upload", 19) == 0) {
        size_t clen = 0;
        char *cl = strcasestr(req, "Content-Length:");
        if (cl) clen = strtoul(cl + 15, NULL, 10);
        if (clen == 0 || clen > (size_t)2 * 1024 * 1024 * 1024) {
            http_json(sock, "{\"error\":\"Invalid size\"}"); return;
        }
        char *body_start = strstr(req, "\r\n\r\n");
        if (!body_start) { http_json(sock, "{\"error\":\"Bad request\"}"); return; }
        body_start += 4;
        size_t body_in_buf = n - (body_start - req);

        uint8_t *tar_data = malloc(clen);
        if (!tar_data) { http_json(sock, "{\"error\":\"Out of memory\"}"); return; }
        if (body_in_buf > clen) body_in_buf = clen;
        memcpy(tar_data, body_start, body_in_buf);
        size_t received = body_in_buf;
        while (received < clen) {
            ssize_t r = recv_all(sock, tar_data + received, clen - received);
            if (r <= 0) break;
            received += r;
        }
        printf("[GarlicMgr] encrypt_upload: received %zu bytes zip\n", received);

        mkdir("/data/save_files", 0777);
        const char *tmp = "/data/save_files/_tmp_enc";
        unlink(tmp);
        kernel_set_ucred_authid(getpid(), 0x4800000000000010ULL);

        /* Size: data + 25% overhead + 4MB, min 32MB, aligned to 32K */
        uint64_t img_size = clen + (clen / 4) + (4 * 1024 * 1024);
        if (img_size < 32 * 1024 * 1024)
            img_size = 32 * 1024 * 1024;
        img_size = ((img_size + 32767) / 32768) * 32768;

        /* Step 1: Create file and allocate space (same as mkpfs) */
        int imgfd = open(tmp, O_CREAT | O_TRUNC | O_RDWR, 0777);
        if (imgfd < 0) {
            free(tar_data);
            http_json(sock, "{\"error\":\"Cannot create temp file\"}"); return;
        }
        int ret = sceFsUfsAllocateSaveData(imgfd, img_size, 0, 0);
        if (ret < 0) {
            printf("[GarlicMgr] UfsAllocate failed (0x%x), using ftruncate\n", ret);
            if (ftruncate(imgfd, img_size) < 0) {
                close(imgfd); free(tar_data); unlink(tmp);
                http_json(sock, "{\"error\":\"Cannot allocate image\"}"); return;
            }
        }
        close(imgfd);
        printf("[GarlicMgr] Created image file %llu bytes\n", (unsigned long long)img_size);

        /* Step 2: Format as PFS with compression */
        CreateOpt copt;
        memset(&copt, 0, sizeof(copt));
        sceFsInitCreatePfsSaveDataOpt(&copt);
        copt.flags[1] = 0x02;
        uint8_t ckey[0x20] = {0};
        if (!g_pprCreate) {
            free(tar_data); unlink(tmp);
            http_json(sock, "{\"error\":\"PprCreate not available\"}"); return;
        }
        ret = g_pprCreate(&copt, tmp, 0, img_size, ckey);
        printf("[GarlicMgr] PprCreate returned 0x%x\n", ret);
        if (ret < 0) {
            free(tar_data); unlink(tmp);
            char json[128];
            snprintf(json, sizeof(json), "{\"error\":\"Format PFS failed: 0x%x\"}", ret);
            http_json(sock, json); return;
        }
        printf("[GarlicMgr] Formatted PFS image OK\n");

        /* Step 3: Mount with zeroed key directly (no ioctl needed) */
        if (g_mounted) unmount_save();
        snprintf(g_mount_point, sizeof(g_mount_point), "/data/mount_sd");
        mkdir(g_mount_point, 0777);
        MountOpt emopt;
        memset(&emopt, 0, sizeof(emopt));
        sceFsInitMountSaveDataOpt(&emopt);
        emopt.budgetid = "system";
        signal(SIGPIPE, SIG_DFL);
        ret = sceFsMountSaveData(&emopt, tmp, g_mount_point, ckey);
        signal(SIGPIPE, SIG_IGN);
        if (ret < 0) {
            free(tar_data); unlink(tmp);
            char json[128];
            snprintf(json, sizeof(json), "{\"error\":\"Mount failed: 0x%x\"}", ret);
            http_json(sock, json); return;
        }
        snprintf(g_mounted_path, sizeof(g_mounted_path), "%s", tmp);
        g_local_copy[0] = 0;
        g_mounted = 1;
        printf("[GarlicMgr] Mounted new PFS (handle=%d)\n", ret);

        /* Step 4: Extract zip into mounted PFS */
        int files = extract_zip(g_mount_point, tar_data, received);
        free(tar_data);
        sync();
        printf("[GarlicMgr] Extracted %d files for encrypt\n", files);

        /* Optional: patch account ID in param.sfo */
        char *ap = strstr(url, "aid=");
        if (ap) {
            char aid_hex[64] = {0};
            strncpy(aid_hex, ap + 4, sizeof(aid_hex) - 1);
            char *rp2 = aid_hex, *wp2 = aid_hex;
            while (*rp2 && *rp2 != '&') {
                if (*rp2 == '%' && rp2[1] && rp2[2]) {
                    char hx[3] = {rp2[1], rp2[2], 0};
                    *wp2++ = (char)strtol(hx, NULL, 16);
                    rp2 += 3;
                } else { *wp2++ = *rp2++; }
            }
            *wp2 = 0;
            char *hs = aid_hex;
            if (hs[0] == '0' && (hs[1] == 'x' || hs[1] == 'X')) hs += 2;
            if (strlen(hs) == 16) {
                uint8_t new_aid[8];
                for (int i = 0; i < 8; i++) {
                    char bh[3] = {hs[i*2], hs[i*2+1], 0};
                    new_aid[i] = (uint8_t)strtol(bh, NULL, 16);
                }
                char sfo_path[MAX_PATH_LEN];
                snprintf(sfo_path, sizeof(sfo_path), "%s/sce_sys/param.sfo", g_mount_point);
                int sfo_fd = open(sfo_path, O_RDWR);
                if (sfo_fd >= 0) {
                    pwrite(sfo_fd, new_aid, 8, 0x1B8);
                    close(sfo_fd);
                    sync();
                    printf("[GarlicMgr] encrypt: patched account ID\n");
                }
            }
        }

        unmount_save();

        struct stat est;
        if (stat(tmp, &est) < 0) {
            unlink(tmp);
            http_json(sock, "{\"error\":\"Encrypted image not found\"}"); return;
        }
        char json[128];
        snprintf(json, sizeof(json), "{\"ok\":true,\"size\":%lld}", (long long)est.st_size);
        http_json(sock, json);
        return;
    }

    /* GET /api/encrypt_download?name=<optional> -> serve the encrypted file */
    if (strcmp(method, "GET") == 0 && strncmp(url, "/api/encrypt_download", 21) == 0) {
        const char *tmp = "/data/save_files/_tmp_enc";
        struct stat est;
        if (stat(tmp, &est) < 0) {
            http_json(sock, "{\"error\":\"No encrypted file\"}"); return;
        }
        int fd = open(tmp, O_RDONLY);
        if (fd < 0) {
            http_json(sock, "{\"error\":\"Cannot read file\"}"); return;
        }
        char dlname[512] = "encrypted_save";
        char *np = strstr(url, "name=");
        if (np) {
            strncpy(dlname, np + 5, sizeof(dlname) - 1);
            dlname[sizeof(dlname) - 1] = 0;
            /* URL-decode */
            char *r = dlname, *w = dlname;
            while (*r && *r != '&') {
                if (*r == '%' && r[1] && r[2]) {
                    char hex[3] = {r[1], r[2], 0};
                    *w++ = (char)strtol(hex, NULL, 16);
                    r += 3;
                } else { *w++ = *r++; }
            }
            *w = 0;
        }
        char hdr[1024];
        int hlen = snprintf(hdr, sizeof(hdr),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/octet-stream\r\n"
            "Content-Disposition: attachment; filename=\"%s\"\r\n"
            "Content-Length: %lld\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Connection: close\r\n\r\n", dlname, (long long)est.st_size);
        send(sock, hdr, hlen, 0);
        char *buf = iobuf;
        ssize_t nr;
        while ((nr = read(fd, buf, BUF_SIZE)) > 0)
            send(sock, buf, nr, 0);
        close(fd);
        unlink(tmp);
        return;
    }

    /* GET /api/resign_download?name=<optional>&ps4=1 -> serve the resigned file */
    if (strcmp(method, "GET") == 0 && strncmp(url, "/api/resign_download", 20) == 0) {
        int is_ps4 = (strstr(url, "ps4=1") != NULL);
        const char *tmp = is_ps4 ? "/data/save_files/sdimg__tmp_resign" : "/data/save_files/_tmp_resign";
        struct stat rst;
        if (stat(tmp, &rst) < 0) {
            http_json(sock, "{\"error\":\"No resigned file\"}"); return;
        }
        char dlname[512] = "resigned_save";
        char *np = strstr(url, "name=");
        if (np) {
            strncpy(dlname, np + 5, sizeof(dlname) - 1);
            dlname[sizeof(dlname) - 1] = 0;
            char *r = dlname, *w = dlname;
            while (*r && *r != '&') {
                if (*r == '%' && r[1] && r[2]) {
                    char hex[3] = {r[1], r[2], 0};
                    *w++ = (char)strtol(hex, NULL, 16);
                    r += 3;
                } else { *w++ = *r++; }
            }
            *w = 0;
        }

        if (is_ps4) {
            /* PS4: zip both sdimg_ image and .bin key */
            const char *bin_tmp = "/data/save_files/_tmp_resign.bin";
            char img_name[512], key_name[512], zip_name[512];
            /* Strip sdimg_ prefix from dlname if present for key naming */
            const char *base = dlname;
            if (strncmp(base, "sdimg_", 6) == 0) base += 6;
            snprintf(img_name, sizeof(img_name), "sdimg_%s", base);
            snprintf(key_name, sizeof(key_name), "%s.bin", base);
            snprintf(zip_name, sizeof(zip_name), "%s.zip", base);

            zip_file_t zfiles[2] = {
                { tmp,     img_name },
                { bin_tmp, key_name }
            };
            char hdr[1024];
            int hlen = snprintf(hdr, sizeof(hdr),
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: application/zip\r\n"
                "Content-Disposition: attachment; filename=\"%s\"\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "Connection: close\r\n\r\n", zip_name);
            send(sock, hdr, hlen, 0);
            zip_send_files(sock, zfiles, 2, iobuf);
            unlink(tmp);
            unlink(bin_tmp);
        } else {
            int fd = open(tmp, O_RDONLY);
            if (fd < 0) {
                http_json(sock, "{\"error\":\"Cannot read file\"}"); return;
            }
            char hdr[1024];
            int hlen = snprintf(hdr, sizeof(hdr),
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: application/octet-stream\r\n"
                "Content-Disposition: attachment; filename=\"%s\"\r\n"
                "Content-Length: %lld\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "Connection: close\r\n\r\n", dlname, (long long)rst.st_size);
            send(sock, hdr, hlen, 0);
            char *buf = iobuf;
            ssize_t nr;
            while ((nr = read(fd, buf, BUF_SIZE)) > 0)
                send(sock, buf, nr, 0);
            close(fd);
            unlink(tmp);
        }
        return;
    }

    /* POST /api/upload_file?name=<path> -> write raw file into mounted save */
    if (strcmp(method, "POST") == 0 && strncmp(url, "/api/upload_file", 16) == 0) {
        if (!g_mounted) { http_json(sock, "{\"error\":\"No save mounted\"}"); return; }

        char fname[512] = {0};
        char *np = strstr(url, "name=");
        if (np) {
            strncpy(fname, np + 5, sizeof(fname) - 1);
            /* URL-decode %xx sequences */
            char *r = fname, *w = fname;
            while (*r) {
                if (*r == '%' && r[1] && r[2]) {
                    char hex[3] = {r[1], r[2], 0};
                    *w++ = (char)strtol(hex, NULL, 16);
                    r += 3;
                } else { *w++ = *r++; }
            }
            *w = 0;
        }
        if (!fname[0]) { http_json(sock, "{\"error\":\"Missing name param\"}"); return; }

        size_t clen = 0;
        char *cl = strcasestr(req, "Content-Length:");
        if (cl) clen = strtoul(cl + 15, NULL, 10);
        if (clen > (size_t)2 * 1024 * 1024 * 1024) {
            http_json(sock, "{\"error\":\"File too large\"}"); return;
        }

        char *body_start = strstr(req, "\r\n\r\n");
        if (!body_start) { http_json(sock, "{\"error\":\"Bad request\"}"); return; }
        body_start += 4;
        size_t body_in_buf = n - (body_start - req);

        char filepath[MAX_PATH_LEN];
        snprintf(filepath, sizeof(filepath), "%s/%s", g_mount_point, fname);

        /* Create parent dirs */
        char *slash = filepath + strlen(g_mount_point) + 1;
        while ((slash = strchr(slash, '/')) != NULL) {
            *slash = 0; mkdir(filepath, 0777); *slash = '/'; slash++;
        }

        int fd = open(filepath, O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (fd < 0) {
            char json[256];
            snprintf(json, sizeof(json), "{\"error\":\"Cannot create %s\"}", fname);
            http_json(sock, json); return;
        }

        if (body_in_buf > clen) body_in_buf = clen;
        write(fd, body_start, body_in_buf);
        size_t received = body_in_buf;
        while (received < clen) {
            char *buf = iobuf;
            size_t want = BUF_SIZE < (clen - received) ? BUF_SIZE : (clen - received);
            ssize_t r = recv_all(sock, buf, want);
            if (r <= 0) break;
            write(fd, buf, r);
            received += r;
        }
        close(fd);
        sync();

        char json[256];
        snprintf(json, sizeof(json), "{\"ok\":true,\"name\":\"%s\",\"size\":%zu}", fname, received);
        http_json(sock, json);
        return;
    }

    /* POST /api/upload -> zip extract into mounted save */
    if (strcmp(method, "POST") == 0 && strcmp(url, "/api/upload") == 0) {
        if (!g_mounted) { http_json(sock, "{\"error\":\"No save mounted\"}"); return; }

        size_t clen = 0;
        char *cl = strcasestr(req, "Content-Length:");
        if (cl) clen = strtoul(cl + 15, NULL, 10);
        if (clen == 0 || clen > (size_t)2 * 1024 * 1024 * 1024) {
            http_json(sock, "{\"error\":\"Invalid content length\"}");
            return;
        }

        char *body_start = strstr(req, "\r\n\r\n");
        if (!body_start) { http_json(sock, "{\"error\":\"Bad request\"}"); return; }
        body_start += 4;
        size_t body_in_buf = n - (body_start - req);

        uint8_t *body = malloc(clen);
        if (!body) { http_json(sock, "{\"error\":\"Out of memory\"}"); return; }

        if (body_in_buf > clen) body_in_buf = clen;
        memcpy(body, body_start, body_in_buf);
        size_t received = body_in_buf;

        while (received < clen) {
            ssize_t r = recv_all(sock, body + received, clen - received);
            if (r <= 0) break;
            received += r;
        }

        int files = extract_zip(g_mount_point, body, received);
        free(body);
        sync();

        char json[128];
        snprintf(json, sizeof(json), "{\"files\":%d}", files);
        http_json(sock, json);
        return;
    }

    /* GET /api/create_pfs?size=<bytes>&ps4=1 -> create fresh PFS image, mount it */
    if (strcmp(method, "GET") == 0 && strncmp(url, "/api/create_pfs", 15) == 0) {
        if (g_mounted) unmount_save();
        kernel_set_ucred_authid(getpid(), 0x4800000000000010ULL);

        int is_ps4 = (strstr(url, "ps4=1") != NULL);
        g_enc_ps4 = is_ps4;

        /* Parse optional size (default 256MB) */
        uint64_t img_size = 256 * 1024 * 1024;
        char *sp = strstr(url, "size=");
        if (sp) {
            uint64_t req_size = strtoull(sp + 5, NULL, 10);
            if (req_size > 0) {
                /* Add 25% overhead + 4MB, min 32MB */
                img_size = req_size + (req_size / 4) + (4 * 1024 * 1024);
                if (img_size < 32 * 1024 * 1024)
                    img_size = 32 * 1024 * 1024;
            }
        }
        img_size = ((img_size + 32767) / 32768) * 32768;

        mkdir("/data/save_files", 0777);
        const char *tmp = "/data/save_files/_tmp_enc";
        unlink(tmp);

        uint8_t ckey[0x20] = {0};

        if (is_ps4) {
            /* PS4: generate sealed key, decrypt it, use non-Ppr create */
            uint8_t *kbuf = malloc(0x100);
            if (!kbuf) { http_json(sock, "{\"error\":\"Out of memory\"}"); return; }
            memset(kbuf, 0, 0x100);

            /* Try sbl_srv first, then pfsmgr for sealed key generation */
            int gen_ok = 0;
            int kfd = open("/dev/sbl_srv", O_RDWR);
            if (kfd >= 0) {
                if (ioctl(kfd, 0x40845303, kbuf) >= 0) gen_ok = 1;
                close(kfd);
            }
            if (!gen_ok) {
                memset(kbuf, 0, 0x100);
                kfd = open("/dev/pfsmgr", O_RDWR);
                if (kfd >= 0) {
                    if (ioctl(kfd, 0x40845303, kbuf) >= 0) gen_ok = 1;
                    close(kfd);
                }
            }
            if (!gen_ok) {
                free(kbuf);
                http_json(sock, "{\"error\":\"Cannot generate sealed key\"}"); return;
            }
            memcpy(g_sealed_key, kbuf, 96);
            printf("[GarlicMgr] create_pfs: PS4 sealed key generated (keyset=%d)\n",
                   (kbuf[9] << 8) | kbuf[8]);

            /* Decrypt sealed key via pfsmgr */
            memset(kbuf, 0, 0x100);
            memcpy(kbuf, g_sealed_key, 96);
            int pfsmgr = open("/dev/pfsmgr", O_RDWR);
            if (pfsmgr < 0) {
                free(kbuf);
                http_json(sock, "{\"error\":\"Cannot open pfsmgr\"}"); return;
            }
            int ret = ioctl(pfsmgr, 0xc0845302, kbuf);
            close(pfsmgr);
            if (ret < 0) {
                free(kbuf);
                http_json(sock, "{\"error\":\"Cannot decrypt sealed key\"}"); return;
            }
            memcpy(ckey, kbuf + 0x60, 0x20);
            free(kbuf);
            printf("[GarlicMgr] create_pfs: PS4 sealed key decrypted OK\n");
        }

        int imgfd = open(tmp, O_CREAT | O_TRUNC | O_RDWR, 0777);
        if (imgfd < 0) {
            http_json(sock, "{\"error\":\"Cannot create temp file\"}"); return;
        }
        int ret = sceFsUfsAllocateSaveData(imgfd, img_size, 0, 0);
        if (ret < 0) {
            printf("[GarlicMgr] UfsAllocate failed (0x%x), using ftruncate\n", ret);
            if (ftruncate(imgfd, img_size) < 0) {
                close(imgfd); unlink(tmp);
                http_json(sock, "{\"error\":\"Cannot allocate image\"}"); return;
            }
        }
        close(imgfd);
        printf("[GarlicMgr] create_pfs: image %llu bytes\n", (unsigned long long)img_size);

        CreateOpt copt;
        memset(&copt, 0, sizeof(copt));
        sceFsInitCreatePfsSaveDataOpt(&copt);

        if (is_ps4) {
            /* PS4: use non-Ppr create with decrypted key */
            ret = sceFsCreatePfsSaveDataImage(&copt, tmp, 0, img_size, ckey);
        } else {
            /* PS5: use PprCreate with zeroed key + compression flag */
            copt.flags[1] = 0x02;
            if (!g_pprCreate) {
                unlink(tmp);
                http_json(sock, "{\"error\":\"PprCreate not available\"}"); return;
            }
            ret = g_pprCreate(&copt, tmp, 0, img_size, ckey);
        }
        if (ret < 0) {
            unlink(tmp);
            char json[128];
            snprintf(json, sizeof(json), "{\"error\":\"Format PFS failed: 0x%x\"}", ret);
            http_json(sock, json); return;
        }
        printf("[GarlicMgr] create_pfs: formatted OK (%s)\n", is_ps4 ? "PS4" : "PS5");

        snprintf(g_mount_point, sizeof(g_mount_point), "/data/mount_sd");
        mkdir(g_mount_point, 0777);
        MountOpt emopt;
        memset(&emopt, 0, sizeof(emopt));
        sceFsInitMountSaveDataOpt(&emopt);
        emopt.budgetid = "system";
        signal(SIGPIPE, SIG_DFL);
        ret = sceFsMountSaveData(&emopt, tmp, g_mount_point, ckey);
        signal(SIGPIPE, SIG_IGN);
        if (ret < 0) {
            unlink(tmp);
            char json[128];
            snprintf(json, sizeof(json), "{\"error\":\"Mount failed: 0x%x\"}", ret);
            http_json(sock, json); return;
        }
        snprintf(g_mounted_path, sizeof(g_mounted_path), "%s", tmp);
        g_local_copy[0] = 0;
        g_mounted = 1;
        printf("[GarlicMgr] create_pfs: mounted at %s\n", g_mount_point);

        http_json(sock, "{\"ok\":true}");
        return;
    }

    /* GET /api/mount_copy?idx=N -> copy save, mount, clear contents */
    if (strcmp(method, "GET") == 0 && strncmp(url, "/api/mount_copy", 15) == 0) {
        if (g_mounted) unmount_save();

        int idx = -1;
        char *p = strstr(url, "idx=");
        if (p) idx = atoi(p + 4);
        if (idx < 0 || idx >= g_save_count) {
            http_json(sock, "{\"error\":\"Invalid index\"}"); return;
        }

        save_entry_t *s = &g_saves[idx];
        kernel_set_ucred_authid(getpid(), 0x4800000000000010ULL);

        /* Copy save to temp location */
        const char *tmp = "/data/save_files/_tmp_newenc";
        mkdir("/data/save_files", 0777);
        if (copy_file(s->path, tmp) < 0) {
            http_json(sock, "{\"error\":\"Copy failed\"}"); return;
        }
        chmod(tmp, 0755);

        /* Mount the copy */
        int ret = mount_by_path(tmp);
        if (ret < 0) {
            unlink(tmp);
            char json[128];
            snprintf(json, sizeof(json), "{\"error\":\"Mount failed: %d\"}", ret);
            http_json(sock, json); return;
        }

        /* Delete all contents */
        delete_recursive(g_mount_point);
        sync();
        printf("[GarlicMgr] mount_copy: cleared contents\n");

        /* Return empty file list */
        char json[1024];
        snprintf(json, sizeof(json), "{\"files\":[]}");
        http_json(sock, json);
        return;
    }

    /* GET /api/download_new?aid=<optional hex> -> patch aid, unmount, stream encrypted */
    if (strcmp(method, "GET") == 0 && strncmp(url, "/api/download_new", 17) == 0) {
        if (!g_mounted) {
            http_json(sock, "{\"error\":\"No save mounted\"}"); return;
        }

        /* Re-detect PS4 vs PS5 from SFO FORMAT field */
        {
            char sfo_chk[MAX_PATH_LEN];
            snprintf(sfo_chk, sizeof(sfo_chk), "%s/sce_sys/param.sfo", g_mount_point);
            sfo_info_t sfo_tmp;
            if (parse_sfo(sfo_chk, &sfo_tmp) == 0)
                g_enc_ps4 = (strcmp(sfo_tmp.format, "obs") == 0) ? 1 : 0;
        }

        /* Optional account ID patching */
        char *ap = strstr(url, "aid=");
        if (ap) {
            char aid_hex[64] = {0};
            strncpy(aid_hex, ap + 4, sizeof(aid_hex) - 1);
            char *rp = aid_hex, *wp = aid_hex;
            while (*rp && *rp != '&') {
                if (*rp == '%' && rp[1] && rp[2]) {
                    char hex[3] = {rp[1], rp[2], 0};
                    *wp++ = (char)strtol(hex, NULL, 16);
                    rp += 3;
                } else { *wp++ = *rp++; }
            }
            *wp = 0;
            char *hs = aid_hex;
            if (hs[0] == '0' && (hs[1] == 'x' || hs[1] == 'X')) hs += 2;
            if (strlen(hs) == 16) {
                uint8_t new_aid[8];
                for (int i = 0; i < 8; i++) {
                    char bh[3] = {hs[i*2], hs[i*2+1], 0};
                    new_aid[i] = (uint8_t)strtol(bh, NULL, 16);
                }
                char sfo_path[MAX_PATH_LEN];
                snprintf(sfo_path, sizeof(sfo_path), "%s/sce_sys/param.sfo", g_mount_point);
                int sfo_fd = open(sfo_path, O_RDWR);
                if (sfo_fd >= 0) {
                    /* PS4: AID at 0x15C, PS5: AID at 0x1B8 */
                    pwrite(sfo_fd, new_aid, 8, g_enc_ps4 ? 0x15C : 0x1B8);
                    int local_uid = 0;
                    if (!g_enc_ps4 && sceUserServiceGetForegroundUser(&local_uid) == 0) {
                        uint32_t uid = (uint32_t)local_uid;
                        pwrite(sfo_fd, &uid, 4, 0x660);
                        printf("[GarlicMgr] download_new: patched user_id to 0x%x\n", uid);
                    }
                    close(sfo_fd);
                    sync();
                    printf("[GarlicMgr] download_new: patched account ID\n");
                }
            }
        }

        /* Save path before unmount clears it */
        char tmp_path[MAX_PATH_LEN];
        snprintf(tmp_path, sizeof(tmp_path), "%s", g_mounted_path);
        unmount_save();

        /* For PS4: write sealed key .bin companion file */
        if (g_enc_ps4) {
            const char *bin_tmp = "/data/save_files/_tmp_enc.bin";
            int bfd = open(bin_tmp, O_CREAT | O_WRONLY | O_TRUNC, 0644);
            if (bfd >= 0) {
                write(bfd, g_sealed_key, 96);
                close(bfd);
                printf("[GarlicMgr] download_new: wrote .bin sealed key\n");
            }
        }

        struct stat st;
        if (stat(tmp_path, &st) < 0) {
            unlink(tmp_path);
            http_json(sock, "{\"error\":\"File not found\"}"); return;
        }
        char dlname[512] = "new_save";
        char *np = strstr(url, "name=");
        if (np) {
            strncpy(dlname, np + 5, sizeof(dlname) - 1);
            dlname[sizeof(dlname) - 1] = 0;
            char *r = dlname, *w = dlname;
            while (*r && *r != '&') {
                if (*r == '%' && r[1] && r[2]) {
                    char hex[3] = {r[1], r[2], 0};
                    *w++ = (char)strtol(hex, NULL, 16);
                    r += 3;
                } else { *w++ = *r++; }
            }
            *w = 0;
        }

        if (g_enc_ps4) {
            /* PS4: zip both sdimg_ image and .bin key together */
            const char *bin_tmp = "/data/save_files/_tmp_enc.bin";
            char img_name[512], key_name[512], zip_name[512];
            snprintf(img_name, sizeof(img_name), "sdimg_%s", dlname);
            snprintf(key_name, sizeof(key_name), "%s.bin", dlname);
            snprintf(zip_name, sizeof(zip_name), "%s_ps4.zip", dlname);

            zip_file_t zfiles[2] = {
                { tmp_path, img_name },
                { bin_tmp,  key_name }
            };
            char hdr[1024];
            int hlen = snprintf(hdr, sizeof(hdr),
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: application/zip\r\n"
                "Content-Disposition: attachment; filename=\"%s\"\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "Connection: close\r\n\r\n", zip_name);
            send(sock, hdr, hlen, 0);
            zip_send_files(sock, zfiles, 2, iobuf);
            unlink(tmp_path);
            unlink(bin_tmp);
        } else {
            /* PS5: stream raw image */
            int fd = open(tmp_path, O_RDONLY);
            if (fd < 0) {
                unlink(tmp_path);
                http_json(sock, "{\"error\":\"Cannot read file\"}"); return;
            }
            char hdr[1024];
            int hlen = snprintf(hdr, sizeof(hdr),
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: application/octet-stream\r\n"
                "Content-Disposition: attachment; filename=\"%s\"\r\n"
                "Content-Length: %lld\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "Connection: close\r\n\r\n", dlname, (long long)st.st_size);
            send(sock, hdr, hlen, 0);
            char *buf = iobuf;
            ssize_t nr;
            while ((nr = read(fd, buf, BUF_SIZE)) > 0)
                send(sock, buf, nr, 0);
            close(fd);
            unlink(tmp_path);
        }
        return;
    }

    /* POST /api/resign?aid=<hex>&ps4=1 -> upload encrypted, change account id, download */
    if (strcmp(method, "POST") == 0 && strncmp(url, "/api/resign", 11) == 0) {
        int is_ps4 = (strstr(url, "ps4=1") != NULL);

        /* Parse account ID from query string */
        char aid_hex[64] = {0};
        char *ap = strstr(url, "aid=");
        if (ap) {
            strncpy(aid_hex, ap + 4, sizeof(aid_hex) - 1);
            char *rp = aid_hex, *wp = aid_hex;
            while (*rp && *rp != '&') {
                if (*rp == '%' && rp[1] && rp[2]) {
                    char hex[3] = {rp[1], rp[2], 0};
                    *wp++ = (char)strtol(hex, NULL, 16);
                    rp += 3;
                } else { *wp++ = *rp++; }
            }
            *wp = 0;
        }
        /* Strip 0x prefix if present */
        char *hex_str = aid_hex;
        if (hex_str[0] == '0' && (hex_str[1] == 'x' || hex_str[1] == 'X'))
            hex_str += 2;
        if (strlen(hex_str) != 16) {
            http_json(sock, "{\"error\":\"Account ID must be 16 hex chars (e.g. 0x0000000000000000)\"}");
            return;
        }
        uint8_t new_aid[8];
        for (int i = 0; i < 8; i++) {
            char byte_hex[3] = {hex_str[i*2], hex_str[i*2+1], 0};
            new_aid[i] = (uint8_t)strtol(byte_hex, NULL, 16);
        }

        /* Receive body */
        size_t clen = 0;
        char *cl = strcasestr(req, "Content-Length:");
        if (cl) clen = strtoul(cl + 15, NULL, 10);
        if (clen == 0 || clen > (size_t)2 * 1024 * 1024 * 1024) {
            http_json(sock, "{\"error\":\"Invalid size\"}"); return;
        }
        char *body_start = strstr(req, "\r\n\r\n");
        if (!body_start) { http_json(sock, "{\"error\":\"Bad request\"}"); return; }
        body_start += 4;
        size_t body_in_buf = n - (body_start - req);

        mkdir("/data/save_files", 0777);
        const char *tmp = "/data/save_files/_tmp_resign";
        /* PS4: name as sdimg_ so mount_by_path detects it */
        if (is_ps4) tmp = "/data/save_files/sdimg__tmp_resign";
        int fd = open(tmp, O_CREAT | O_WRONLY | O_TRUNC, 0755);
        if (fd < 0) { http_json(sock, "{\"error\":\"Cannot create temp file\"}"); return; }
        if (body_in_buf > clen) body_in_buf = clen;
        write(fd, body_start, body_in_buf);
        size_t received = body_in_buf;
        while (received < clen) {
            char *buf = iobuf;
            size_t want = BUF_SIZE < (clen - received) ? BUF_SIZE : (clen - received);
            ssize_t r = recv_all(sock, buf, want);
            if (r <= 0) break;
            write(fd, buf, r);
            received += r;
        }
        close(fd);
        printf("[GarlicMgr] resign: received %zu bytes (ps4=%d)\n", received, is_ps4);

        /* For PS4: rename uploaded .bin key to match image name */
        if (is_ps4) {
            rename("/data/save_files/_tmp_key.bin", "/data/save_files/_tmp_resign.bin");
        }

        /* Mount the save */
        int ret = mount_by_path(tmp);
        if (ret < 0) {
            unlink(tmp);
            if (is_ps4) unlink("/data/save_files/_tmp_resign.bin");
            char json[128];
            snprintf(json, sizeof(json), "{\"error\":\"Mount failed: %d\"}", ret);
            http_json(sock, json); return;
        }

        /* Patch account ID in param.sfo */
        char sfo_path[MAX_PATH_LEN];
        snprintf(sfo_path, sizeof(sfo_path), "%s/sce_sys/param.sfo", g_mount_point);
        int sfo_fd = open(sfo_path, O_RDWR);
        if (sfo_fd < 0) {
            unmount_save(); unlink(tmp);
            if (is_ps4) unlink("/data/save_files/_tmp_resign.bin");
            http_json(sock, "{\"error\":\"Cannot open param.sfo\"}"); return;
        }
        /* PS4: AID at 0x15C, PS5: AID at 0x1B8 */
        off_t aid_offset = is_ps4 ? 0x15C : 0x1B8;
        if (pwrite(sfo_fd, new_aid, 8, aid_offset) != 8) {
            close(sfo_fd); unmount_save(); unlink(tmp);
            if (is_ps4) unlink("/data/save_files/_tmp_resign.bin");
            http_json(sock, "{\"error\":\"Failed to write account ID\"}"); return;
        }

        if (!is_ps4) {
            /* PS5 only: patch user_id (uint32_t) at offset 0x660 */
            char *up = strstr(url, "uid=");
            uint32_t uid = 0;
            int have_uid = 0;
            if (up) {
                char uid_hex[16] = {0};
                strncpy(uid_hex, up + 4, sizeof(uid_hex) - 1);
                char *rp2 = uid_hex, *wp2 = uid_hex;
                while (*rp2 && *rp2 != '&') {
                    if (*rp2 == '%' && rp2[1] && rp2[2]) {
                        char hx[3] = {rp2[1], rp2[2], 0};
                        *wp2++ = (char)strtol(hx, NULL, 16);
                        rp2 += 3;
                    } else { *wp2++ = *rp2++; }
                }
                *wp2 = 0;
                char *us = uid_hex;
                if (us[0] == '0' && (us[1] == 'x' || us[1] == 'X')) us += 2;
                uid = (uint32_t)strtoul(us, NULL, 16);
                have_uid = 1;
            }
            if (!have_uid) {
                int local_uid = 0;
                if (sceUserServiceGetForegroundUser(&local_uid) == 0) {
                    uid = (uint32_t)local_uid;
                    have_uid = 1;
                }
            }
            if (have_uid) {
                pwrite(sfo_fd, &uid, 4, 0x660);
                printf("[GarlicMgr] resign: patched user_id to 0x%x\n", uid);
            }
        }
        close(sfo_fd);
        sync();
        printf("[GarlicMgr] resign: patched account ID to %s (%s)\n", aid_hex, is_ps4 ? "PS4" : "PS5");

        /* Unmount (re-encrypts) */
        unmount_save();

        /* Return JSON, file available via GET /api/resign_download */
        struct stat st;
        if (stat(tmp, &st) < 0) {
            unlink(tmp);
            if (is_ps4) unlink("/data/save_files/_tmp_resign.bin");
            http_json(sock, "{\"error\":\"Resigned file not found\"}"); return;
        }
        char json[128];
        snprintf(json, sizeof(json), "{\"ok\":true,\"size\":%lld,\"ps4\":%s}",
                 (long long)st.st_size, is_ps4 ? "true" : "false");
        http_json(sock, json);
        return;
    }

    /* GET /api/import_check?uid=<hex> -> check if mounted save already exists on system */
    if (strcmp(method, "GET") == 0 && strncmp(url, "/api/import_check", 17) == 0) {
        if (!g_mounted) { http_json(sock, "{\"error\":\"No save mounted\"}"); return; }
        char sfo_path[MAX_PATH_LEN];
        snprintf(sfo_path, sizeof(sfo_path), "%s/sce_sys/param.sfo", g_mount_point);
        sfo_info_t sfo;
        if (parse_sfo(sfo_path, &sfo) < 0 || !sfo.title_id[0] || !sfo.dir_name[0]) {
            http_json(sock, "{\"exists\":false}"); return;
        }
        char uid_hex[16] = {0};
        char *uidp = strstr(url, "uid=");
        if (uidp) {
            strncpy(uid_hex, uidp + 4, sizeof(uid_hex) - 1);
            char *amp = strchr(uid_hex, '&'); if (amp) *amp = 0;
        }
        if (!uid_hex[0]) {
            int local_uid = 0; sceUserServiceGetForegroundUser(&local_uid);
            snprintf(uid_hex, sizeof(uid_hex), "%x", (uint32_t)local_uid);
        }
        char dst[MAX_PATH_LEN];
        snprintf(dst, sizeof(dst), "/user/home/%s/%s/%s/sdimg_%s",
                 uid_hex, g_enc_ps4 ? "savedata" : "savedata_prospero",
                 sfo.title_id, sfo.dir_name);
        struct stat dst_st;
        int exists = (stat(dst, &dst_st) == 0) ? 1 : 0;
        char json[256];
        snprintf(json, sizeof(json),
                 "{\"exists\":%s,\"title_id\":\"%s\",\"dir_name\":\"%s\",\"main_title\":\"%s\"}",
                 exists ? "true" : "false", sfo.title_id, sfo.dir_name, sfo.main_title);
        http_json(sock, json);
        return;
    }

    /* GET /api/import_usb_save?idx=N&uid=<hex> -> import decrypted USB save to system */
    if (strcmp(method, "GET") == 0 && strncmp(url, "/api/import_usb_save", 20) == 0) {
        scan_saves();
        int idx = -1;
        char *p = strstr(url, "idx=");
        if (p) idx = atoi(p + 4);
        if (idx < 0 || idx >= g_save_count) {
            http_json(sock, "{\"error\":\"Invalid index\"}"); return;
        }
        save_entry_t *s = &g_saves[idx];

        /* Check if encrypted file or decrypted folder */
        struct stat path_st;
        if (stat(s->path, &path_st) < 0) {
            http_json(sock, "{\"error\":\"Save not found\"}"); return;
        }
        int is_encrypted = S_ISREG(path_st.st_mode);

        if (is_encrypted) {
            /* Encrypted USB save: mount, then use import_finish logic */
            if (g_mounted) unmount_save();
            int ret = mount_save(idx);
            if (ret < 0) {
                char json[128];
                snprintf(json, sizeof(json), "{\"error\":\"Mount failed: %d\"}", ret);
                http_json(sock, json); return;
            }

            /* Parse param.sfo from mounted save */
            char sfo_path[MAX_PATH_LEN];
            snprintf(sfo_path, sizeof(sfo_path), "%s/sce_sys/param.sfo", g_mount_point);
            sfo_info_t sfo;
            if (parse_sfo(sfo_path, &sfo) < 0 || !sfo.title_id[0] || !sfo.dir_name[0]) {
                unmount_save();
                http_json(sock, "{\"error\":\"Cannot parse param.sfo\"}"); return;
            }

            /* Detect PS4/PS5 from FORMAT */
            g_enc_ps4 = (strcmp(sfo.format, "obs") == 0) ? 1 : 0;
            printf("[GarlicMgr] import_usb: encrypted %s/%s FORMAT=%s %s\n",
                   sfo.title_id, sfo.dir_name, sfo.format, g_enc_ps4 ? "PS4" : "PS5");

            /* Get user info */
            char uid_hex[16] = {0};
            char *uidp = strstr(url, "uid=");
            if (uidp) {
                strncpy(uid_hex, uidp + 4, sizeof(uid_hex) - 1);
                char *amp = strchr(uid_hex, '&'); if (amp) *amp = 0;
            }
            uint32_t uid;
            if (uid_hex[0]) {
                uid = (uint32_t)strtoul(uid_hex, NULL, 16);
            } else {
                int local_uid = 0;
                sceUserServiceGetForegroundUser(&local_uid);
                uid = (uint32_t)local_uid;
                snprintf(uid_hex, sizeof(uid_hex), "%x", uid);
            }

            /* Resign */
            uint64_t local_aid = get_account_id_for_user(uid_hex);
            if (local_aid) {
                int sfo_fd = open(sfo_path, O_RDWR);
                if (sfo_fd >= 0) {
                    if (g_enc_ps4) {
                        pwrite(sfo_fd, &local_aid, 8, 0x15C);
                    } else {
                        pwrite(sfo_fd, &local_aid, 8, 0x1B8);
                        pwrite(sfo_fd, &uid, 4, 0x660);
                    }
                    close(sfo_fd); sync();
                }
            }

            /* Copy icon before unmount */
            char icon_tmp[MAX_PATH_LEN] = {0};
            {
                char icon_src[MAX_PATH_LEN];
                snprintf(icon_src, sizeof(icon_src), "%s/sce_sys/icon0.png", g_mount_point);
                struct stat ist;
                if (stat(icon_src, &ist) == 0) {
                    snprintf(icon_tmp, sizeof(icon_tmp), "/data/save_files/_tmp_icon.png");
                    copy_file(icon_src, icon_tmp);
                }
            }

            /* Save path before unmount */
            char tmp_path[MAX_PATH_LEN];
            snprintf(tmp_path, sizeof(tmp_path), "%s", g_mounted_path);
            unmount_save();

            /* Copy to system */
            char sd_dir[MAX_PATH_LEN], sd_dst[MAX_PATH_LEN];
            snprintf(sd_dir, sizeof(sd_dir), "/user/home/%s/%s/%s", uid_hex,
                     g_enc_ps4 ? "savedata" : "savedata_prospero", sfo.title_id);
            snprintf(sd_dst, sizeof(sd_dst), "%s/sdimg_%s", sd_dir, sfo.dir_name);
            recursive_mkdir(sd_dir);

            int from_usb = (strncmp(tmp_path, "/mnt/usb", 8) == 0);
            if (copy_file(tmp_path, sd_dst) < 0) {
                if (!from_usb) unlink(tmp_path);
                if (icon_tmp[0]) unlink(icon_tmp);
                http_json(sock, "{\"error\":\"Failed to copy save\"}"); return;
            }
            if (!from_usb) unlink(tmp_path);
            chmod(sd_dst, 0644);

            /* PS4: copy .bin key */
            if (g_enc_ps4 && strncmp(s->save_name, "sdimg_", 6) == 0) {
                const char *savename = s->save_name + 6;
                char src_dir[MAX_PATH_LEN];
                strncpy(src_dir, s->path, sizeof(src_dir) - 1);
                src_dir[sizeof(src_dir) - 1] = 0;
                char *sl = strrchr(src_dir, '/');
                if (sl) *(sl + 1) = 0;
                char bin_src[MAX_PATH_LEN], bin_dst[MAX_PATH_LEN];
                snprintf(bin_src, sizeof(bin_src), "%s%s.bin", src_dir, savename);
                snprintf(bin_dst, sizeof(bin_dst), "%s/%s.bin", sd_dir, sfo.dir_name);
                struct stat bst;
                if (stat(bin_src, &bst) == 0) copy_file(bin_src, bin_dst);
            }

            /* Copy icon */
            if (icon_tmp[0]) {
                char icon_dir[MAX_PATH_LEN], icon_dst[MAX_PATH_LEN];
                snprintf(icon_dir, sizeof(icon_dir),
                         "/user/home/%s/savedata_prospero_meta/user/%s", uid_hex, sfo.title_id);
                snprintf(icon_dst, sizeof(icon_dst), "%s/%s_icon0.png", icon_dir, sfo.dir_name);
                recursive_mkdir(icon_dir);
                copy_file(icon_tmp, icon_dst);
                unlink(icon_tmp);
            }

            /* Update DB */
            char db_path[MAX_PATH_LEN];
            snprintf(db_path, sizeof(db_path), "/system_data/%s/%s/db/user/savedata.db",
                     g_enc_ps4 ? "savedata" : "savedata_prospero", uid_hex);
            { char db_dir[MAX_PATH_LEN];
              snprintf(db_dir, sizeof(db_dir), "/system_data/%s/%s/db/user",
                       g_enc_ps4 ? "savedata" : "savedata_prospero", uid_hex);
              recursive_mkdir(db_dir); }
            sqlite3 *db = NULL;
            if (sqlite3_open(db_path, &db) == SQLITE_OK) {
                char *err_msg = NULL;
                sqlite3_exec(db,
                    "CREATE TABLE IF NOT EXISTS savedata ("
                    "id INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,"
                    "title_id NOT NULL, dir_name NOT NULL, main_title NOT NULL,"
                    "sub_title, detail, tmp_dir_name, is_broken, user_param,"
                    "blocks, free_blocks, size_kib, mtime NOT NULL,"
                    "fake_broken, account_id, user_id, faked_owner,"
                    "cloud_icon_url, cloud_revision, game_title_id NOT NULL, system_blocks)",
                    NULL, 0, &err_msg);
                if (err_msg) { sqlite3_free(err_msg); err_msg = NULL; }

                char esc_title[256] = {0};
                { char *src2 = sfo.main_title, *dst2 = esc_title;
                  while (*src2 && dst2 < esc_title + sizeof(esc_title) - 2) {
                      if (*src2 == '\'') *dst2++ = '\'';
                      *dst2++ = *src2++;
                  }
                  *dst2 = 0; }

                char mtime[64];
                { time_t now = time(NULL); struct tm *tm = gmtime(&now);
                  strftime(mtime, sizeof(mtime), "%Y-%m-%dT%H:%M:%S.00Z", tm); }

                uint64_t aid_val = local_aid ? local_aid : sfo.account_id;

                char check_sql[512];
                snprintf(check_sql, sizeof(check_sql),
                         "SELECT COUNT(*) FROM savedata WHERE title_id='%s' AND dir_name='%s'",
                         sfo.title_id, sfo.dir_name);
                sqlite3_stmt *stmt = NULL;
                int db_exists = 0;
                if (sqlite3_prepare_v2(db, check_sql, -1, &stmt, NULL) == SQLITE_OK) {
                    if (sqlite3_step(stmt) == SQLITE_ROW) db_exists = sqlite3_column_int(stmt, 0);
                    sqlite3_finalize(stmt);
                }

                char sql[2048];
                if (db_exists) {
                    snprintf(sql, sizeof(sql),
                        "UPDATE savedata SET main_title='%s', mtime='%s', account_id=%llu, user_id=%u "
                        "WHERE title_id='%s' AND dir_name='%s'",
                        esc_title, mtime, (unsigned long long)aid_val, uid, sfo.title_id, sfo.dir_name);
                } else {
                    snprintf(sql, sizeof(sql),
                        "INSERT INTO savedata (title_id, dir_name, main_title, sub_title, detail,"
                        "is_broken, user_param, blocks, free_blocks, size_kib, mtime,"
                        "fake_broken, account_id, user_id, faked_owner, cloud_revision,"
                        "game_title_id, system_blocks) VALUES ("
                        "'%s','%s','%s','','',0,%u,%u,%u,%u,'%s',0,%llu,%u,0,0,'%s',0)",
                        sfo.title_id, sfo.dir_name, esc_title,
                        sfo.user_param, sfo.blocks, sfo.blocks, sfo.blocks * 0x40, mtime,
                        (unsigned long long)aid_val, uid, sfo.title_id);
                }
                ret = sqlite3_exec(db, sql, NULL, 0, &err_msg);
                if (ret != SQLITE_OK) { printf("[GarlicMgr] import_usb: DB error: %s\n", err_msg); sqlite3_free(err_msg); }
                else printf("[GarlicMgr] import_usb: DB %s OK\n", db_exists ? "updated" : "inserted");
                sqlite3_close(db);
            }

            char json[512];
            snprintf(json, sizeof(json),
                     "{\"ok\":true,\"title_id\":\"%s\",\"dir_name\":\"%s\",\"main_title\":\"%s\"}",
                     sfo.title_id, sfo.dir_name, sfo.main_title);
            http_json(sock, json);
            return;
        }

        /* Decrypted folder: parse param.sfo directly */
        char sfo_path[MAX_PATH_LEN];
        snprintf(sfo_path, sizeof(sfo_path), "%s/sce_sys/param.sfo", s->path);
        sfo_info_t sfo;
        if (parse_sfo(sfo_path, &sfo) < 0 || !sfo.title_id[0] || !sfo.dir_name[0]) {
            http_json(sock, "{\"error\":\"Cannot parse param.sfo\"}"); return;
        }

        /* Detect PS4/PS5 from FORMAT */
        int is_ps4 = (strcmp(sfo.format, "obs") == 0);
        g_enc_ps4 = is_ps4;
        printf("[GarlicMgr] import_usb: %s/%s FORMAT=%s %s\n",
               sfo.title_id, sfo.dir_name, sfo.format, is_ps4 ? "PS4" : "PS5");

        /* Get user info */
        char uid_hex[16] = {0};
        char *uidp = strstr(url, "uid=");
        if (uidp) {
            strncpy(uid_hex, uidp + 4, sizeof(uid_hex) - 1);
            char *amp = strchr(uid_hex, '&'); if (amp) *amp = 0;
        }
        uint32_t uid;
        if (uid_hex[0]) {
            uid = (uint32_t)strtoul(uid_hex, NULL, 16);
        } else {
            int local_uid = 0;
            sceUserServiceGetForegroundUser(&local_uid);
            uid = (uint32_t)local_uid;
            snprintf(uid_hex, sizeof(uid_hex), "%x", uid);
        }

        pid_t rpid = getpid();
        kernel_set_ucred_uid(rpid, 0);
        kernel_set_ucred_authid(rpid, 0x4800000000000010ULL);

        /* Calculate total size of USB save folder */
        uint64_t total_size = 0;
        {
            DIR *sd = opendir(s->path);
            if (sd) {
                struct dirent *se;
                while ((se = readdir(sd))) {
                    if (se->d_name[0] == '.') continue;
                    char fp[MAX_PATH_LEN];
                    snprintf(fp, sizeof(fp), "%s/%s", s->path, se->d_name);
                    struct stat fst;
                    if (stat(fp, &fst) == 0) total_size += fst.st_size;
                }
                closedir(sd);
            }
        }
        if (total_size == 0) total_size = 1024 * 1024;

        /* Create PFS image: size + 25% overhead + 4MB, min 32MB */
        uint64_t img_size = total_size + (total_size / 4) + (4 * 1024 * 1024);
        if (img_size < 32 * 1024 * 1024) img_size = 32 * 1024 * 1024;
        img_size = ((img_size + 32767) / 32768) * 32768;

        mkdir("/data/save_files", 0777);
        const char *tmp = "/data/save_files/_tmp_usb_import";
        unlink(tmp);

        int imgfd = open(tmp, O_CREAT | O_TRUNC | O_RDWR, 0777);
        if (imgfd < 0) {
            http_json(sock, "{\"error\":\"Cannot create temp file\"}"); return;
        }
        int ret = sceFsUfsAllocateSaveData(imgfd, img_size, 0, 0);
        if (ret < 0) {
            if (ftruncate(imgfd, img_size) < 0) {
                close(imgfd); unlink(tmp);
                http_json(sock, "{\"error\":\"Cannot allocate image\"}"); return;
            }
        }
        close(imgfd);

        /* Create PFS image — PS4 and PS5 use different key/format methods */
        uint8_t ckey[0x20] = {0};

        if (is_ps4) {
            /* PS4: generate sealed key, decrypt it */
            uint8_t *kbuf = malloc(0x100);
            if (!kbuf) { unlink(tmp); http_json(sock, "{\"error\":\"Out of memory\"}"); return; }
            memset(kbuf, 0, 0x100);

            int gen_ok = 0;
            int kfd = open("/dev/sbl_srv", O_RDWR);
            if (kfd >= 0) {
                if (ioctl(kfd, 0x40845303, kbuf) >= 0) gen_ok = 1;
                close(kfd);
            }
            if (!gen_ok) {
                memset(kbuf, 0, 0x100);
                kfd = open("/dev/pfsmgr", O_RDWR);
                if (kfd >= 0) {
                    if (ioctl(kfd, 0x40845303, kbuf) >= 0) gen_ok = 1;
                    close(kfd);
                }
            }
            if (!gen_ok) {
                free(kbuf); unlink(tmp);
                http_json(sock, "{\"error\":\"Cannot generate sealed key\"}"); return;
            }
            memcpy(g_sealed_key, kbuf, 96);
            printf("[GarlicMgr] import_usb: PS4 sealed key generated\n");

            /* Decrypt sealed key */
            memset(kbuf, 0, 0x100);
            memcpy(kbuf, g_sealed_key, 96);
            int pfsmgr = open("/dev/pfsmgr", O_RDWR);
            if (pfsmgr < 0) {
                free(kbuf); unlink(tmp);
                http_json(sock, "{\"error\":\"Cannot open pfsmgr\"}"); return;
            }
            ret = ioctl(pfsmgr, 0xc0845302, kbuf);
            close(pfsmgr);
            if (ret < 0) {
                free(kbuf); unlink(tmp);
                http_json(sock, "{\"error\":\"Cannot decrypt sealed key\"}"); return;
            }
            memcpy(ckey, kbuf + 0x60, 0x20);
            free(kbuf);

            /* Format with non-Ppr create */
            CreateOpt copt;
            memset(&copt, 0, sizeof(copt));
            sceFsInitCreatePfsSaveDataOpt(&copt);
            ret = sceFsCreatePfsSaveDataImage(&copt, tmp, 0, img_size, ckey);
        } else {
            /* PS5: PprCreate with zeroed key + compression */
            CreateOpt copt;
            memset(&copt, 0, sizeof(copt));
            sceFsInitCreatePfsSaveDataOpt(&copt);
            copt.flags[1] = 0x02;
            if (!g_pprCreate) {
                unlink(tmp);
                http_json(sock, "{\"error\":\"PprCreate not available\"}"); return;
            }
            ret = g_pprCreate(&copt, tmp, 0, img_size, ckey);
        }
        if (ret < 0) {
            unlink(tmp);
            char json[128];
            snprintf(json, sizeof(json), "{\"error\":\"Format PFS failed: 0x%x\"}", ret);
            http_json(sock, json); return;
        }
        printf("[GarlicMgr] import_usb: PFS formatted (%s)\n", is_ps4 ? "PS4" : "PS5");

        /* Mount PFS */
        if (g_mounted) unmount_save();
        snprintf(g_mount_point, sizeof(g_mount_point), "/data/mount_sd");
        mkdir(g_mount_point, 0777);
        MountOpt mopt;
        memset(&mopt, 0, sizeof(mopt));
        sceFsInitMountSaveDataOpt(&mopt);
        mopt.budgetid = "system";
        signal(SIGPIPE, SIG_DFL);
        ret = sceFsMountSaveData(&mopt, tmp, g_mount_point, ckey);
        signal(SIGPIPE, SIG_IGN);
        if (ret < 0) {
            unlink(tmp);
            char json[128];
            snprintf(json, sizeof(json), "{\"error\":\"Mount failed: 0x%x\"}", ret);
            http_json(sock, json); return;
        }
        snprintf(g_mounted_path, sizeof(g_mounted_path), "%s", tmp);
        g_local_copy[0] = 0;
        g_mounted = 1;

        /* Copy all files from USB save into PFS */
        copy_dir_recursive(s->path, g_mount_point);
        sync();
        printf("[GarlicMgr] import_usb: copied files into PFS\n");

        /* Resign param.sfo */
        uint64_t local_aid = get_account_id_for_user(uid_hex);
        if (local_aid) {
            char pfs_sfo[MAX_PATH_LEN];
            snprintf(pfs_sfo, sizeof(pfs_sfo), "%s/sce_sys/param.sfo", g_mount_point);
            int sfo_fd = open(pfs_sfo, O_RDWR);
            if (sfo_fd >= 0) {
                if (is_ps4) {
                    pwrite(sfo_fd, &local_aid, 8, 0x15C);
                } else {
                    pwrite(sfo_fd, &local_aid, 8, 0x1B8);
                    pwrite(sfo_fd, &uid, 4, 0x660);
                }
                close(sfo_fd);
                sync();
                printf("[GarlicMgr] import_usb: resigned %s\n", is_ps4 ? "PS4" : "PS5");
            }
        }

        /* Copy icon before unmount */
        char icon_tmp[MAX_PATH_LEN] = {0};
        {
            char icon_src[MAX_PATH_LEN];
            snprintf(icon_src, sizeof(icon_src), "%s/sce_sys/icon0.png", g_mount_point);
            struct stat ist;
            if (stat(icon_src, &ist) == 0) {
                snprintf(icon_tmp, sizeof(icon_tmp), "/data/save_files/_tmp_icon.png");
                copy_file(icon_src, icon_tmp);
            }
        }

        /* Unmount (re-encrypts) */
        unmount_save();

        /* Copy encrypted image to system save location */
        char sd_dir[MAX_PATH_LEN], sd_dst[MAX_PATH_LEN];
        snprintf(sd_dir, sizeof(sd_dir), "/user/home/%s/%s/%s", uid_hex,
                 is_ps4 ? "savedata" : "savedata_prospero", sfo.title_id);
        snprintf(sd_dst, sizeof(sd_dst), "%s/sdimg_%s", sd_dir, sfo.dir_name);
        recursive_mkdir(sd_dir);

        if (copy_file(tmp, sd_dst) < 0) {
            unlink(tmp);
            if (icon_tmp[0]) unlink(icon_tmp);
            http_json(sock, "{\"error\":\"Failed to copy save\"}"); return;
        }
        unlink(tmp);
        chmod(sd_dst, 0644);

        /* PS4: write .bin sealed key companion */
        if (is_ps4) {
            char bin_dst[MAX_PATH_LEN];
            snprintf(bin_dst, sizeof(bin_dst), "%s/%s.bin", sd_dir, sfo.dir_name);
            int bfd = open(bin_dst, O_CREAT | O_WRONLY | O_TRUNC, 0644);
            if (bfd >= 0) {
                write(bfd, g_sealed_key, 96);
                close(bfd);
                printf("[GarlicMgr] import_usb: wrote PS4 .bin key -> %s\n", bin_dst);
            }
        }

        /* Copy icon */
        if (icon_tmp[0]) {
            char icon_dir[MAX_PATH_LEN], icon_dst[MAX_PATH_LEN];
            snprintf(icon_dir, sizeof(icon_dir),
                     "/user/home/%s/savedata_prospero_meta/user/%s", uid_hex, sfo.title_id);
            snprintf(icon_dst, sizeof(icon_dst), "%s/%s_icon0.png", icon_dir, sfo.dir_name);
            recursive_mkdir(icon_dir);
            copy_file(icon_tmp, icon_dst);
            unlink(icon_tmp);
        }

        /* Update savedata.db */
        char db_path[MAX_PATH_LEN];
        snprintf(db_path, sizeof(db_path), "/system_data/%s/%s/db/user/savedata.db",
                 is_ps4 ? "savedata" : "savedata_prospero", uid_hex);
        {
            char db_dir[MAX_PATH_LEN];
            snprintf(db_dir, sizeof(db_dir), "/system_data/%s/%s/db/user",
                     is_ps4 ? "savedata" : "savedata_prospero", uid_hex);
            recursive_mkdir(db_dir);
        }
        sqlite3 *db = NULL;
        ret = sqlite3_open(db_path, &db);
        if (ret == SQLITE_OK) {
            char *err_msg = NULL;
            sqlite3_exec(db,
                "CREATE TABLE IF NOT EXISTS savedata ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,"
                "title_id NOT NULL, dir_name NOT NULL, main_title NOT NULL,"
                "sub_title, detail, tmp_dir_name, is_broken, user_param,"
                "blocks, free_blocks, size_kib, mtime NOT NULL,"
                "fake_broken, account_id, user_id, faked_owner,"
                "cloud_icon_url, cloud_revision, game_title_id NOT NULL, system_blocks)",
                NULL, 0, &err_msg);
            if (err_msg) { sqlite3_free(err_msg); err_msg = NULL; }

            /* Escape strings */
            char esc_title[256] = {0};
            {
                char *src = sfo.main_title, *dst = esc_title;
                while (*src && dst < esc_title + sizeof(esc_title) - 2) {
                    if (*src == '\'') *dst++ = '\'';
                    *dst++ = *src++;
                }
                *dst = 0;
            }

            char mtime[64];
            { time_t now = time(NULL); struct tm *tm = gmtime(&now);
              strftime(mtime, sizeof(mtime), "%Y-%m-%dT%H:%M:%S.00Z", tm); }

            uint64_t aid_val = local_aid ? local_aid : sfo.account_id;
            uint32_t size_kib = sfo.blocks * 0x40;

            /* Check if exists */
            char check_sql[512];
            snprintf(check_sql, sizeof(check_sql),
                     "SELECT COUNT(*) FROM savedata WHERE title_id='%s' AND dir_name='%s'",
                     sfo.title_id, sfo.dir_name);
            sqlite3_stmt *stmt = NULL;
            int db_exists = 0;
            if (sqlite3_prepare_v2(db, check_sql, -1, &stmt, NULL) == SQLITE_OK) {
                if (sqlite3_step(stmt) == SQLITE_ROW)
                    db_exists = sqlite3_column_int(stmt, 0);
                sqlite3_finalize(stmt);
            }

            char sql[2048];
            if (db_exists) {
                snprintf(sql, sizeof(sql),
                    "UPDATE savedata SET main_title='%s', mtime='%s', account_id=%llu, user_id=%u "
                    "WHERE title_id='%s' AND dir_name='%s'",
                    esc_title, mtime, (unsigned long long)aid_val, uid,
                    sfo.title_id, sfo.dir_name);
            } else {
                snprintf(sql, sizeof(sql),
                    "INSERT INTO savedata (title_id, dir_name, main_title, sub_title, detail,"
                    "is_broken, user_param, blocks, free_blocks, size_kib, mtime,"
                    "fake_broken, account_id, user_id, faked_owner, cloud_revision,"
                    "game_title_id, system_blocks) VALUES ("
                    "'%s','%s','%s','','',0,%u,%u,%u,%u,'%s',0,%llu,%u,0,0,'%s',0)",
                    sfo.title_id, sfo.dir_name, esc_title,
                    sfo.user_param, sfo.blocks, sfo.blocks, size_kib, mtime,
                    (unsigned long long)aid_val, uid, sfo.title_id);
            }
            ret = sqlite3_exec(db, sql, NULL, 0, &err_msg);
            if (ret != SQLITE_OK) {
                printf("[GarlicMgr] import_usb: DB error: %s\n", err_msg);
                sqlite3_free(err_msg);
            } else {
                printf("[GarlicMgr] import_usb: DB %s OK\n", db_exists ? "updated" : "inserted");
            }
            sqlite3_close(db);
        }

        char json[512];
        snprintf(json, sizeof(json),
                 "{\"ok\":true,\"title_id\":\"%s\",\"dir_name\":\"%s\",\"main_title\":\"%s\"}",
                 sfo.title_id, sfo.dir_name, sfo.main_title);
        http_json(sock, json);
        return;
    }

    /* GET /api/import_finish -> parse SFO, resign, unmount, copy to final location, update DB */
    if (strcmp(method, "GET") == 0 && strncmp(url, "/api/import_finish", 18) == 0) {
        if (!g_mounted) {
            http_json(sock, "{\"error\":\"No save mounted\"}"); return;
        }

        /* Parse param.sfo from mounted save */
        char sfo_path[MAX_PATH_LEN];
        snprintf(sfo_path, sizeof(sfo_path), "%s/sce_sys/param.sfo", g_mount_point);
        sfo_info_t sfo;
        int sfo_ret = parse_sfo(sfo_path, &sfo);
        if (sfo_ret == -2) {
            http_json(sock, "{\"error\":\"param.sfo is corrupted!\"}");
            return;
        }
        if (sfo_ret < 0) {
            http_json(sock, "{\"error\":\"Cannot parse param.sfo — is sce_sys/param.sfo present?\"}");
            return;
        }
        if (!sfo.title_id[0] || !sfo.dir_name[0]) {
            http_json(sock, "{\"error\":\"param.sfo missing TITLE_ID or SAVEDATA_DIRECTORY\"}");
            return;
        }
        /* Detect PS4 vs PS5 from SFO FORMAT field: "obs"=PS4, "ppr"=PS5 */
        g_enc_ps4 = (strcmp(sfo.format, "obs") == 0) ? 1 : 0;
        printf("[GarlicMgr] import: title_id=%s dir=%s title=%s blocks=%u FORMAT=%s %s\n",
               sfo.title_id, sfo.dir_name, sfo.main_title, sfo.blocks,
               sfo.format, g_enc_ps4 ? "(PS4)" : "(PS5)");

        /* Get user info — prefer uid query param, fallback to foreground user */
        char uid_hex[16] = {0};
        char *uidp = strstr(url, "uid=");
        if (uidp) {
            strncpy(uid_hex, uidp + 4, sizeof(uid_hex) - 1);
            char *amp = strchr(uid_hex, '&');
            if (amp) *amp = 0;
        }
        uint32_t uid;
        if (uid_hex[0]) {
            uid = (uint32_t)strtoul(uid_hex, NULL, 16);
        } else {
            int local_uid = 0;
            sceUserServiceGetForegroundUser(&local_uid);
            uid = (uint32_t)local_uid;
            snprintf(uid_hex, sizeof(uid_hex), "%x", uid);
        }

        /* Read local account_id from registry */
        uint64_t local_aid = get_account_id_for_user(uid_hex);

        /* Resign: PS4 = AID at 0x15C only, PS5 = AID at 0x1B8 + UID at 0x660 */
        if (local_aid) {
            int sfo_fd = open(sfo_path, O_RDWR);
            if (sfo_fd >= 0) {
                if (g_enc_ps4) {
                    pwrite(sfo_fd, &local_aid, 8, 0x15C);
                    printf("[GarlicMgr] import: PS4 resigned aid=%llu at 0x15C\n",
                           (unsigned long long)local_aid);
                } else {
                    pwrite(sfo_fd, &local_aid, 8, 0x1B8);
                    pwrite(sfo_fd, &uid, 4, 0x660);
                    printf("[GarlicMgr] import: PS5 resigned aid=%llu uid=0x%x\n",
                           (unsigned long long)local_aid, uid);
                }
                close(sfo_fd);
                sync();
            }
        }

        /* Copy icon before unmount */
        char icon_src[MAX_PATH_LEN];
        char icon_tmp[MAX_PATH_LEN] = {0};
        snprintf(icon_src, sizeof(icon_src), "%s/sce_sys/icon0.png", g_mount_point);
        struct stat icon_st;
        if (stat(icon_src, &icon_st) == 0) {
            snprintf(icon_tmp, sizeof(icon_tmp), "/data/save_files/_tmp_icon.png");
            copy_file(icon_src, icon_tmp);
        }

        /* Save mounted path before unmount clears it */
        char tmp_path[MAX_PATH_LEN];
        snprintf(tmp_path, sizeof(tmp_path), "%s", g_mounted_path);

        /* Unmount (re-encrypts the PFS image) */
        unmount_save();

        /* Build destination paths — PS4: savedata/, PS5: savedata_prospero/ */
        char sd_dir[MAX_PATH_LEN], sd_dst[MAX_PATH_LEN];
        snprintf(sd_dir, sizeof(sd_dir),
                 "/user/home/%s/%s/%s", uid_hex,
                 g_enc_ps4 ? "savedata" : "savedata_prospero", sfo.title_id);
        snprintf(sd_dst, sizeof(sd_dst),
                 "%s/sdimg_%s", sd_dir, sfo.dir_name);

        /* Create directories */
        recursive_mkdir(sd_dir);

        /* Copy encrypted image to final location */
        int from_usb = (strncmp(tmp_path, "/mnt/usb", 8) == 0);
        printf("[GarlicMgr] import: copying %s -> %s\n", tmp_path, sd_dst);
        if (copy_file(tmp_path, sd_dst) < 0) {
            if (!from_usb) unlink(tmp_path);
            if (icon_tmp[0]) unlink(icon_tmp);
            http_json(sock, "{\"error\":\"Failed to copy save to destination\"}");
            return;
        }
        if (!from_usb) unlink(tmp_path);
        chmod(sd_dst, 0644);

        /* PS4: also copy the .bin sealed key companion */
        if (g_enc_ps4) {
            char bin_src[MAX_PATH_LEN], bin_dst[MAX_PATH_LEN];
            /* tmp_path was like /data/save_files/sdimg__tmp_import
             * .bin is at /data/save_files/_tmp_import.bin */
            snprintf(bin_src, sizeof(bin_src), "/data/save_files/_tmp_import.bin");
            snprintf(bin_dst, sizeof(bin_dst), "%s/%s.bin", sd_dir, sfo.dir_name);
            if (copy_file(bin_src, bin_dst) == 0) {
                chmod(bin_dst, 0644);
                printf("[GarlicMgr] import: PS4 .bin key -> %s\n", bin_dst);
            } else {
                printf("[GarlicMgr] import: WARNING - failed to copy .bin key\n");
            }
            if (!from_usb) unlink(bin_src);
        }

        /* Copy icon to meta directory */
        if (icon_tmp[0]) {
            char icon_dir[MAX_PATH_LEN], icon_dst[MAX_PATH_LEN];
            snprintf(icon_dir, sizeof(icon_dir),
                     "/user/home/%s/savedata_prospero_meta/user/%s", uid_hex, sfo.title_id);
            snprintf(icon_dst, sizeof(icon_dst),
                     "%s/%s_icon0.png", icon_dir, sfo.dir_name);
            recursive_mkdir(icon_dir);
            copy_file(icon_tmp, icon_dst);
            unlink(icon_tmp);
            printf("[GarlicMgr] import: icon -> %s\n", icon_dst);
        }

        /* Update savedata.db — PS4: savedata/, PS5: savedata_prospero/ */
        char db_path[MAX_PATH_LEN];
        snprintf(db_path, sizeof(db_path),
                 "/system_data/%s/%s/db/user/savedata.db",
                 g_enc_ps4 ? "savedata" : "savedata_prospero", uid_hex);

        /* Create DB dir if needed */
        {
            char db_dir[MAX_PATH_LEN];
            snprintf(db_dir, sizeof(db_dir),
                     "/system_data/%s/%s/db/user",
                     g_enc_ps4 ? "savedata" : "savedata_prospero", uid_hex);
            recursive_mkdir(db_dir);
        }

        sqlite3 *db = NULL;
        int ret = sqlite3_open(db_path, &db);
        if (ret != SQLITE_OK) {
            printf("[GarlicMgr] import: cannot open savedata.db: %s\n", sqlite3_errmsg(db));
            if (db) sqlite3_close(db);
            /* Save is already copied, just DB update failed */
            char json[256];
            snprintf(json, sizeof(json),
                     "{\"ok\":true,\"warning\":\"Save imported but DB update failed\","
                     "\"title_id\":\"%s\",\"dir_name\":\"%s\"}", sfo.title_id, sfo.dir_name);
            http_json(sock, json);
            return;
        }

        /* Create table if it doesn't exist */
        char *err_msg = NULL;
        sqlite3_exec(db,
            "CREATE TABLE IF NOT EXISTS savedata ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,"
            "title_id NOT NULL, dir_name NOT NULL, main_title NOT NULL,"
            "sub_title, detail, tmp_dir_name, is_broken, user_param,"
            "blocks, free_blocks, size_kib, mtime NOT NULL,"
            "fake_broken, account_id, user_id, faked_owner,"
            "cloud_icon_url, cloud_revision, game_title_id NOT NULL, system_blocks)",
            NULL, 0, &err_msg);
        if (err_msg) { sqlite3_free(err_msg); err_msg = NULL; }

        sqlite3_exec(db,
            "CREATE INDEX IF NOT EXISTS savedata_index ON savedata"
            "(title_id, dir_name, user_param, size_kib, mtime, game_title_id)",
            NULL, 0, &err_msg);
        if (err_msg) { sqlite3_free(err_msg); err_msg = NULL; }

        /* Escape single quotes in strings */
        char esc_title[256], esc_sub[256], esc_detail[512];
        {
            char *src, *dst;
            src = sfo.main_title; dst = esc_title;
            while (*src && dst < esc_title + sizeof(esc_title) - 2) {
                if (*src == '\'') *dst++ = '\'';
                *dst++ = *src++;
            }
            *dst = 0;
            src = sfo.sub_title; dst = esc_sub;
            while (*src && dst < esc_sub + sizeof(esc_sub) - 2) {
                if (*src == '\'') *dst++ = '\'';
                *dst++ = *src++;
            }
            *dst = 0;
            src = sfo.detail; dst = esc_detail;
            while (*src && dst < esc_detail + sizeof(esc_detail) - 2) {
                if (*src == '\'') *dst++ = '\'';
                *dst++ = *src++;
            }
            *dst = 0;
        }

        /* Get current timestamp */
        char mtime[64];
        {
            time_t now = time(NULL);
            struct tm *tm = gmtime(&now);
            strftime(mtime, sizeof(mtime), "%Y-%m-%dT%H:%M:%S.00Z", tm);
        }

        /* Check if entry already exists */
        char check_sql[512];
        snprintf(check_sql, sizeof(check_sql),
                 "SELECT COUNT(*) FROM savedata WHERE title_id='%s' AND dir_name='%s'",
                 sfo.title_id, sfo.dir_name);
        sqlite3_stmt *stmt = NULL;
        int exists = 0;
        if (sqlite3_prepare_v2(db, check_sql, -1, &stmt, NULL) == SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW)
                exists = sqlite3_column_int(stmt, 0);
            sqlite3_finalize(stmt);
        }

        uint64_t aid_val = local_aid ? local_aid : sfo.account_id;
        uint32_t size_kib = sfo.blocks * 0x40;

        char sql[2048];
        if (exists) {
            snprintf(sql, sizeof(sql),
                "UPDATE savedata SET main_title='%s', sub_title='%s', detail='%s',"
                "user_param=%u, blocks=%u, free_blocks=%u, size_kib=%u,"
                "mtime='%s', account_id=%llu, user_id=%u "
                "WHERE title_id='%s' AND dir_name='%s'",
                esc_title, esc_sub, esc_detail,
                sfo.user_param, sfo.blocks, sfo.blocks, size_kib,
                mtime, (unsigned long long)aid_val, uid,
                sfo.title_id, sfo.dir_name);
        } else {
            snprintf(sql, sizeof(sql),
                "INSERT INTO savedata (title_id, dir_name, main_title, sub_title, detail,"
                "is_broken, user_param, blocks, free_blocks, size_kib, mtime,"
                "fake_broken, account_id, user_id, faked_owner, cloud_revision,"
                "game_title_id, system_blocks) VALUES ("
                "'%s','%s','%s','%s','%s',"
                "0,%u,%u,%u,%u,'%s',"
                "0,%llu,%u,0,0,"
                "'%s',0)",
                sfo.title_id, sfo.dir_name, esc_title, esc_sub, esc_detail,
                sfo.user_param, sfo.blocks, sfo.blocks, size_kib, mtime,
                (unsigned long long)aid_val, uid,
                sfo.game_title_id);
        }

        ret = sqlite3_exec(db, sql, NULL, 0, &err_msg);
        if (ret != SQLITE_OK) {
            printf("[GarlicMgr] import: DB error: %s\n", err_msg);
            sqlite3_free(err_msg);
        } else {
            printf("[GarlicMgr] import: DB %s OK\n", exists ? "updated" : "inserted");
        }
        sqlite3_close(db);

        char json[512];
        snprintf(json, sizeof(json),
                 "{\"ok\":true,\"title_id\":\"%s\",\"dir_name\":\"%s\","
                 "\"main_title\":\"%s\",\"exists\":%s}",
                 sfo.title_id, sfo.dir_name, sfo.main_title,
                 exists ? "true" : "false");
        http_json(sock, json);
        return;
    }

    /* GET /api/export_title?id=<title_id> -> zip all saves for a title */
    if (strcmp(method, "GET") == 0 && strncmp(url, "/api/export_title", 17) == 0 && url[17] != '_') {
        char tid[64] = {0};
        char *tp = strstr(url, "id=");
        if (tp) {
            strncpy(tid, tp + 3, sizeof(tid) - 1);
            char *amp = strchr(tid, '&');
            if (amp) *amp = 0;
            /* URL-decode */
            char *r = tid, *w = tid;
            while (*r) {
                if (*r == '%' && r[1] && r[2]) {
                    char hex[3] = {r[1], r[2], 0};
                    *w++ = (char)strtol(hex, NULL, 16);
                    r += 3;
                } else { *w++ = *r++; }
            }
            *w = 0;
        }
        if (!tid[0]) { http_json(sock, "{\"error\":\"Missing id\"}"); return; }

        scan_saves();
        kernel_set_ucred_authid(getpid(), 0x4800000000000010ULL);

        /* Collect all saves matching this title_id (x2 for PS4 .bin companions) */
        zip_file_t *zfiles = malloc(g_save_count * 2 * sizeof(zip_file_t));
        char **names = malloc(g_save_count * 2 * sizeof(char*));
        char **allocs = malloc(g_save_count * sizeof(char*));
        int nfiles = 0, nallocs = 0;
        for (int i = 0; i < g_save_count; i++) {
            if (strcmp(g_saves[i].title_id, tid) != 0) continue;
            struct stat fst;
            if (stat(g_saves[i].path, &fst) < 0) continue;
            zfiles[nfiles].path = g_saves[i].path;
            names[nfiles] = malloc(512);
            snprintf(names[nfiles], 512, "%s/%s", tid, g_saves[i].save_name);
            zfiles[nfiles].name = names[nfiles];

            /* For PS4 saves, also include .bin key if it exists */
            if (g_saves[i].is_ps4 && strncmp(g_saves[i].save_name, "sdimg_", 6) == 0) {
                const char *savename = g_saves[i].save_name + 6;
                char dir[MAX_PATH_LEN];
                strncpy(dir, g_saves[i].path, sizeof(dir) - 1);
                dir[sizeof(dir) - 1] = 0;
                char *sl = strrchr(dir, '/');
                if (sl) *(sl + 1) = 0;
                char *bin_path = malloc(MAX_PATH_LEN);
                snprintf(bin_path, MAX_PATH_LEN, "%s%s.bin", dir, savename);
                if (stat(bin_path, &fst) == 0) {
                    nfiles++;
                    zfiles[nfiles].path = bin_path;
                    allocs[nallocs++] = bin_path;
                    names[nfiles] = malloc(512);
                    snprintf(names[nfiles], 512, "%s/%s.bin", tid, savename);
                    zfiles[nfiles].name = names[nfiles];
                } else {
                    free(bin_path);
                }
            }
            nfiles++;
        }

        if (nfiles == 0) {
            free(zfiles); free(names); free(allocs);
            http_json(sock, "{\"error\":\"No saves found for this title\"}");
            return;
        }

        char zip_name[128];
        snprintf(zip_name, sizeof(zip_name), "%s_saves.zip", tid);
        char hdr[512];
        int hlen = snprintf(hdr, sizeof(hdr),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/zip\r\n"
            "Content-Disposition: attachment; filename=\"%s\"\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Connection: close\r\n\r\n", zip_name);
        send(sock, hdr, hlen, 0);
        zip_send_files(sock, zfiles, nfiles, iobuf);

        for (int i = 0; i < nfiles; i++) free(names[i]);
        for (int i = 0; i < nallocs; i++) free(allocs[i]);
        free(names); free(zfiles); free(allocs);
        return;
    }

    /* GET /api/export_title_dec?id=<title_id> -> mount each save, zip decrypted contents */
    if (strcmp(method, "GET") == 0 && strncmp(url, "/api/export_title_dec", 21) == 0) {
        char tid[64] = {0};
        char *tp = strstr(url, "id=");
        if (tp) {
            strncpy(tid, tp + 3, sizeof(tid) - 1);
            char *amp = strchr(tid, '&');
            if (amp) *amp = 0;
            char *r = tid, *w = tid;
            while (*r) {
                if (*r == '%' && r[1] && r[2]) {
                    char hex[3] = {r[1], r[2], 0};
                    *w++ = (char)strtol(hex, NULL, 16);
                    r += 3;
                } else { *w++ = *r++; }
            }
            *w = 0;
        }
        if (!tid[0]) { http_json(sock, "{\"error\":\"Missing id\"}"); return; }

        scan_saves();
        kernel_set_ucred_authid(getpid(), 0x4800000000000010ULL);

        /* Collect indices of saves matching this title */
        int *indices = malloc(g_save_count * sizeof(int));
        int nidx = 0;
        for (int i = 0; i < g_save_count; i++)
            if (strcmp(g_saves[i].title_id, tid) == 0)
                indices[nidx++] = i;

        if (nidx == 0) {
            free(indices);
            http_json(sock, "{\"error\":\"No saves found for this title\"}");
            return;
        }

        /* For each save: mount, collect files into memory, unmount.
         * Store all entries with save_name/ prefix for the final zip. */
        typedef struct {
            char name[512];
            uint8_t *data;
            uint32_t size;
            uint32_t crc;
            uint32_t offset;
        } dec_entry_t;

        dec_entry_t *all_entries = malloc(MAX_ZIP_ENTRIES * sizeof(dec_entry_t));
        int total = 0;

        for (int n = 0; n < nidx && total < MAX_ZIP_ENTRIES; n++) {
            int idx = indices[n];
            if (g_mounted) unmount_save();

            int ret = mount_save(idx);
            if (ret < 0) continue;

            /* Collect file list from mounted save */
            zip_entry_t *tmp_entries = malloc(MAX_ZIP_ENTRIES * sizeof(zip_entry_t));
            int tmp_count = 0;
            collect_files(g_mount_point, "", tmp_entries, &tmp_count);

            const char *sname = g_saves[idx].save_name;
            for (int f = 0; f < tmp_count && total < MAX_ZIP_ENTRIES; f++) {
                dec_entry_t *de = &all_entries[total];
                snprintf(de->name, sizeof(de->name), "%s/%s/%s", tid, sname, tmp_entries[f].name);
                de->size = tmp_entries[f].size;
                de->data = NULL;

                char fullpath[MAX_PATH_LEN];
                snprintf(fullpath, sizeof(fullpath), "%s/%s", g_mount_point, tmp_entries[f].name);
                if (de->size > 0) {
                    int fd = open(fullpath, O_RDONLY);
                    if (fd >= 0) {
                        de->data = malloc(de->size);
                        if (de->data) {
                            ssize_t rd = read(fd, de->data, de->size);
                            if (rd < (ssize_t)de->size) de->size = rd > 0 ? (uint32_t)rd : 0;
                        }
                        close(fd);
                    }
                }
                de->crc = de->data ? calc_crc(de->data, de->size) : 0;
                total++;
            }
            free(tmp_entries);
            unmount_save();
        }
        free(indices);

        /* Stream the combined zip */
        char zip_name[128];
        snprintf(zip_name, sizeof(zip_name), "%s_saves_dec.zip", tid);
        char hdr[512];
        int hlen = snprintf(hdr, sizeof(hdr),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/zip\r\n"
            "Content-Disposition: attachment; filename=\"%s\"\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Connection: close\r\n\r\n", zip_name);
        send(sock, hdr, hlen, 0);

        uint32_t offset = 0;
        for (int i = 0; i < total; i++) {
            dec_entry_t *e = &all_entries[i];
            e->offset = offset;
            uint16_t nlen = strlen(e->name);

            uint8_t lh[30]; memset(lh, 0, 30);
            lh[0]=0x50; lh[1]=0x4b; lh[2]=0x03; lh[3]=0x04;
            lh[4]=20;
            memcpy(lh+14, &e->crc, 4);
            memcpy(lh+18, &e->size, 4);
            memcpy(lh+22, &e->size, 4);
            memcpy(lh+26, &nlen, 2);
            send(sock, lh, 30, 0);
            send(sock, e->name, nlen, 0);
            if (e->data) { send(sock, e->data, e->size, 0); free(e->data); }
            offset += 30 + nlen + e->size;
        }

        uint32_t cd_offset = offset;
        for (int i = 0; i < total; i++) {
            dec_entry_t *e = &all_entries[i];
            uint16_t nlen = strlen(e->name);
            uint8_t cd[46]; memset(cd, 0, 46);
            cd[0]=0x50; cd[1]=0x4b; cd[2]=0x01; cd[3]=0x02;
            cd[4]=20; cd[6]=20;
            memcpy(cd+16, &e->crc, 4);
            memcpy(cd+20, &e->size, 4);
            memcpy(cd+24, &e->size, 4);
            memcpy(cd+28, &nlen, 2);
            memcpy(cd+42, &e->offset, 4);
            send(sock, cd, 46, 0);
            send(sock, e->name, nlen, 0);
            offset += 46 + nlen;
        }
        uint32_t cd_size = offset - cd_offset;

        uint8_t ecd[22]; memset(ecd, 0, 22);
        ecd[0]=0x50; ecd[1]=0x4b; ecd[2]=0x05; ecd[3]=0x06;
        uint16_t cnt16 = (uint16_t)total;
        memcpy(ecd+8, &cnt16, 2);
        memcpy(ecd+10, &cnt16, 2);
        memcpy(ecd+12, &cd_size, 4);
        memcpy(ecd+16, &cd_offset, 4);
        send(sock, ecd, 22, 0);

        free(all_entries);
        return;
    }

    /* GET /api/delete_save?idx=N -> delete a single save from disk and DB */
    if (strcmp(method, "GET") == 0 && strncmp(url, "/api/delete_save", 16) == 0) {
        scan_saves();
        int idx = -1;
        char *p = strstr(url, "idx=");
        if (p) idx = atoi(p + 4);
        if (idx < 0 || idx >= g_save_count) {
            http_json(sock, "{\"error\":\"Invalid index\"}"); return;
        }

        pid_t rpid = getpid();
        kernel_set_ucred_uid(rpid, 0);
        kernel_set_ucred_authid(rpid, 0x4800000000000010ULL);

        save_entry_t *s = &g_saves[idx];

        /* Delete the save file */
        unlink(s->path);
        printf("[GarlicMgr] delete_save: %s\n", s->path);

        /* Delete .bin companion for PS4 */
        if (s->is_ps4 && strncmp(s->save_name, "sdimg_", 6) == 0) {
            const char *sname = s->save_name + 6;
            char dir[MAX_PATH_LEN];
            strncpy(dir, s->path, sizeof(dir) - 1);
            dir[sizeof(dir) - 1] = 0;
            char *sl = strrchr(dir, '/');
            if (sl) *(sl + 1) = 0;
            char bin_path[MAX_PATH_LEN];
            snprintf(bin_path, sizeof(bin_path), "%s%s.bin", dir, sname);
            unlink(bin_path);
        }

        /* Delete from DB if not a USB save */
        if (!s->is_usb && strncmp(s->save_name, "sdimg_", 6) == 0) {
            const char *dir_name = s->save_name + 6;
            const char *sd = s->is_ps4 ? "savedata" : "savedata_prospero";
            char db_path[MAX_PATH_LEN];
            snprintf(db_path, sizeof(db_path),
                     "/system_data/%s/%s/db/user/savedata.db", sd, s->user_id);
            sqlite3 *db = NULL;
            if (sqlite3_open(db_path, &db) == SQLITE_OK) {
                char sql[512];
                snprintf(sql, sizeof(sql),
                         "DELETE FROM savedata WHERE title_id='%s' AND dir_name='%s'",
                         s->title_id, dir_name);
                char *err_msg = NULL;
                int ret = sqlite3_exec(db, sql, NULL, 0, &err_msg);
                if (ret != SQLITE_OK) {
                    printf("[GarlicMgr] delete_save DB error: %s\n", err_msg);
                    sqlite3_free(err_msg);
                } else {
                    printf("[GarlicMgr] delete_save: removed %s/%s from %s DB\n",
                           s->title_id, dir_name, sd);
                }
                sqlite3_close(db);
            }
        }

        /* Try to remove title dir if now empty */
        {
            char title_dir[MAX_PATH_LEN];
            strncpy(title_dir, s->path, sizeof(title_dir) - 1);
            title_dir[sizeof(title_dir) - 1] = 0;
            char *sl = strrchr(title_dir, '/');
            if (sl) *sl = 0;
            rmdir(title_dir); /* only succeeds if empty */
        }

        http_json(sock, "{\"ok\":true}");
        return;
    }

    /* GET /api/delete_title?id=<title_id> -> delete all saves for a title from disk and DB */
    if (strcmp(method, "GET") == 0 && strncmp(url, "/api/delete_title", 17) == 0) {
        char tid[64] = {0};
        char *tp = strstr(url, "id=");
        if (tp) {
            strncpy(tid, tp + 3, sizeof(tid) - 1);
            char *amp = strchr(tid, '&');
            if (amp) *amp = 0;
            char *r = tid, *w = tid;
            while (*r) {
                if (*r == '%' && r[1] && r[2]) {
                    char hex[3] = {r[1], r[2], 0};
                    *w++ = (char)strtol(hex, NULL, 16);
                    r += 3;
                } else { *w++ = *r++; }
            }
            *w = 0;
        }
        if (!tid[0]) { http_json(sock, "{\"error\":\"Missing id\"}"); return; }

        scan_saves();
        pid_t rpid = getpid();
        kernel_set_ucred_uid(rpid, 0);
        kernel_set_ucred_authid(rpid, 0x4800000000000010ULL);

        int deleted = 0;
        /* Track unique (uid, is_ps4) pairs for DB cleanup */
        typedef struct { char uid[16]; int is_ps4; } uid_entry_t;
        uid_entry_t uids[32];
        int nuid = 0;

        for (int i = 0; i < g_save_count; i++) {
            if (strcmp(g_saves[i].title_id, tid) != 0) continue;
            if (g_saves[i].is_usb) continue; /* don't delete USB saves */

            /* Delete the save file */
            unlink(g_saves[i].path);
            printf("[GarlicMgr] delete: %s\n", g_saves[i].path);

            /* Delete .bin companion for PS4 */
            if (g_saves[i].is_ps4 && strncmp(g_saves[i].save_name, "sdimg_", 6) == 0) {
                const char *sname = g_saves[i].save_name + 6;
                char dir[MAX_PATH_LEN];
                strncpy(dir, g_saves[i].path, sizeof(dir) - 1);
                dir[sizeof(dir) - 1] = 0;
                char *sl = strrchr(dir, '/');
                if (sl) *(sl + 1) = 0;
                char bin_path[MAX_PATH_LEN];
                snprintf(bin_path, sizeof(bin_path), "%s%s.bin", dir, sname);
                unlink(bin_path);
            }

            /* Track uid for DB cleanup */
            int found = 0;
            for (int u = 0; u < nuid; u++) {
                if (strcmp(uids[u].uid, g_saves[i].user_id) == 0 && uids[u].is_ps4 == g_saves[i].is_ps4) {
                    found = 1; break;
                }
            }
            if (!found && nuid < 32) {
                strncpy(uids[nuid].uid, g_saves[i].user_id, sizeof(uids[nuid].uid) - 1);
                uids[nuid].is_ps4 = g_saves[i].is_ps4;
                nuid++;
            }
            deleted++;
        }

        /* Remove title directories if empty */
        for (int u = 0; u < nuid; u++) {
            const char *sd = uids[u].is_ps4 ? "savedata" : "savedata_prospero";
            char title_dir[MAX_PATH_LEN];
            snprintf(title_dir, sizeof(title_dir), "/user/home/%s/%s/%s", uids[u].uid, sd, tid);
            rmdir(title_dir); /* only succeeds if empty */

            /* Delete from savedata.db */
            char db_path[MAX_PATH_LEN];
            snprintf(db_path, sizeof(db_path),
                     "/system_data/%s/%s/db/user/savedata.db", sd, uids[u].uid);
            sqlite3 *db = NULL;
            if (sqlite3_open(db_path, &db) == SQLITE_OK) {
                char sql[256];
                snprintf(sql, sizeof(sql),
                         "DELETE FROM savedata WHERE title_id='%s'", tid);
                char *err_msg = NULL;
                int ret = sqlite3_exec(db, sql, NULL, 0, &err_msg);
                if (ret != SQLITE_OK) {
                    printf("[GarlicMgr] delete DB error: %s\n", err_msg);
                    sqlite3_free(err_msg);
                } else {
                    printf("[GarlicMgr] delete: removed %s from %s DB\n", tid, sd);
                }
                sqlite3_close(db);
            }
        }

        char json[128];
        snprintf(json, sizeof(json), "{\"ok\":true,\"deleted\":%d}", deleted);
        http_json(sock, json);
        return;
    }

    /* GET /api/dump_title_usb?id=<title_id>&usb=N -> copy all encrypted saves for a title to USB */
    if (strcmp(method, "GET") == 0 && strncmp(url, "/api/dump_title_usb", 19) == 0 && url[19] != '_') {
        char tid[64] = {0};
        int usb_num = 0;
        char *tp = strstr(url, "id=");
        if (tp) {
            strncpy(tid, tp + 3, sizeof(tid) - 1);
            char *amp = strchr(tid, '&');
            if (amp) *amp = 0;
            char *r = tid, *w = tid;
            while (*r) {
                if (*r == '%' && r[1] && r[2]) {
                    char hex[3] = {r[1], r[2], 0};
                    *w++ = (char)strtol(hex, NULL, 16);
                    r += 3;
                } else { *w++ = *r++; }
            }
            *w = 0;
        }
        tp = strstr(url, "usb=");
        if (tp) usb_num = atoi(tp + 4);
        if (usb_num < 0 || usb_num > 7) usb_num = 0;
        if (!tid[0]) { http_json(sock, "{\"error\":\"Missing id\"}"); return; }

        scan_saves();
        kernel_set_ucred_authid(getpid(), 0x4800000000000010ULL);

        char usb_base[64];
        snprintf(usb_base, sizeof(usb_base), "/mnt/usb%d", usb_num);
        char tmp[MAX_PATH_LEN];
        snprintf(tmp, sizeof(tmp), "%s/PS5", usb_base); mkdir(tmp, 0777);
        snprintf(tmp, sizeof(tmp), "%s/PS5/SAVEDATA", usb_base); mkdir(tmp, 0777);
        snprintf(tmp, sizeof(tmp), "%s/PS5/SAVEDATA/GARLIC", usb_base); mkdir(tmp, 0777);
        char title_dir[MAX_PATH_LEN];
        snprintf(title_dir, sizeof(title_dir), "%s/PS5/SAVEDATA/GARLIC/%s", usb_base, tid);
        mkdir(title_dir, 0777);

        int copied = 0, failed = 0;
        for (int i = 0; i < g_save_count; i++) {
            if (strcmp(g_saves[i].title_id, tid) != 0) continue;
            char dst[MAX_PATH_LEN];
            snprintf(dst, sizeof(dst), "%s/%s", title_dir, g_saves[i].save_name);
            if (copy_file(g_saves[i].path, dst) == 0) {
                copied++;
                /* For PS4 saves, also copy .bin key */
                if (g_saves[i].is_ps4 && strncmp(g_saves[i].save_name, "sdimg_", 6) == 0) {
                    const char *savename = g_saves[i].save_name + 6;
                    char dir[MAX_PATH_LEN];
                    strncpy(dir, g_saves[i].path, sizeof(dir) - 1);
                    dir[sizeof(dir) - 1] = 0;
                    char *sl = strrchr(dir, '/');
                    if (sl) *(sl + 1) = 0;
                    char bin_src[MAX_PATH_LEN], bin_dst[MAX_PATH_LEN];
                    snprintf(bin_src, sizeof(bin_src), "%s%s.bin", dir, savename);
                    snprintf(bin_dst, sizeof(bin_dst), "%s/%s.bin", title_dir, savename);
                    struct stat bst;
                    if (stat(bin_src, &bst) == 0) copy_file(bin_src, bin_dst);
                }
            } else {
                failed++;
            }
        }

        char json[256];
        snprintf(json, sizeof(json), "{\"ok\":true,\"copied\":%d,\"failed\":%d,\"path\":\"%s\"}", copied, failed, title_dir);
        http_json(sock, json);
        return;
    }

    /* GET /api/dump_title_dec_usb?id=<title_id>&usb=N -> mount each save, copy decrypted to USB */
    if (strcmp(method, "GET") == 0 && strncmp(url, "/api/dump_title_dec_usb", 23) == 0) {
        char tid[64] = {0};
        int usb_num = 0;
        char *tp = strstr(url, "id=");
        if (tp) {
            strncpy(tid, tp + 3, sizeof(tid) - 1);
            char *amp = strchr(tid, '&');
            if (amp) *amp = 0;
            char *r = tid, *w = tid;
            while (*r) {
                if (*r == '%' && r[1] && r[2]) {
                    char hex[3] = {r[1], r[2], 0};
                    *w++ = (char)strtol(hex, NULL, 16);
                    r += 3;
                } else { *w++ = *r++; }
            }
            *w = 0;
        }
        tp = strstr(url, "usb=");
        if (tp) usb_num = atoi(tp + 4);
        if (usb_num < 0 || usb_num > 7) usb_num = 0;
        if (!tid[0]) { http_json(sock, "{\"error\":\"Missing id\"}"); return; }

        scan_saves();
        kernel_set_ucred_authid(getpid(), 0x4800000000000010ULL);

        char usb_base[64];
        snprintf(usb_base, sizeof(usb_base), "/mnt/usb%d", usb_num);
        char tmp[MAX_PATH_LEN];
        snprintf(tmp, sizeof(tmp), "%s/PS5", usb_base); mkdir(tmp, 0777);
        snprintf(tmp, sizeof(tmp), "%s/PS5/SAVEDATA", usb_base); mkdir(tmp, 0777);
        snprintf(tmp, sizeof(tmp), "%s/PS5/SAVEDATA/GARLIC", usb_base); mkdir(tmp, 0777);
        char title_dir[MAX_PATH_LEN];
        snprintf(title_dir, sizeof(title_dir), "%s/PS5/SAVEDATA/GARLIC/%s", usb_base, tid);
        mkdir(title_dir, 0777);

        int copied = 0, failed = 0;
        for (int i = 0; i < g_save_count; i++) {
            if (strcmp(g_saves[i].title_id, tid) != 0) continue;

            if (g_mounted) unmount_save();
            int ret = mount_save(i);
            if (ret < 0) { failed++; continue; }

            char save_dir[MAX_PATH_LEN];
            snprintf(save_dir, sizeof(save_dir), "%s/%s", title_dir, g_saves[i].save_name);
            int cpret = copy_dir_recursive(g_mount_point, save_dir);
            unmount_save();

            if (cpret == 0) copied++;
            else failed++;
        }

        char json[256];
        snprintf(json, sizeof(json), "{\"ok\":true,\"copied\":%d,\"failed\":%d,\"path\":\"%s\"}", copied, failed, title_dir);
        http_json(sock, json);
        return;
    }

    /* 404 */
    http_send(sock, "404 Not Found", "application/json", "{\"error\":\"Not found\"}", 20);
}

/* ── Main ───────────────────────────────────────────────────────── */
int main(void) {
    if (!garlic_fw_offsets_preflight()) return 1;
    sceUserServiceInitialize(0);
    kernel_set_ucred_authid(getpid(), 0x4800000000000010ULL);
    signal(SIGPIPE, SIG_IGN);
    init_crc();
    load_app_db();

    /* Try to load sceFsCreatePprPfsSaveDataImage via dlsym */
    void *vsh = dlopen("libSceFsInternalForVsh.sprx", RTLD_LAZY);
    if (vsh) {
        g_pprCreate = (PprCreateFn)dlsym(vsh, "sceFsCreatePprPfsSaveDataImage");
        printf("[GarlicMgr] PprCreate: %s\n", g_pprCreate ? "available" : "not found");
    }

    /* Force unmount any stale mounts from previous runs */
    struct stat st0;
    if (stat("/data/mount_sd", &st0) == 0) {
        UmountOpt u0; memset(&u0, 0, sizeof(u0));
        sceFsInitUmountSaveDataOpt(&u0);
        sceFsUmountSaveData(&u0, "/data/mount_sd", 0, 0);
    }

    printf("=== Garlic SaveMgr for PS5 by earthonion ===\n");
    notify("Garlic SaveMgr: port %d", PORT);

    int srvfd = socket(AF_INET, SOCK_STREAM, 0);
    if (srvfd < 0) { perror("socket"); return 1; }
    setsockopt(srvfd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(PORT);

    if (bind(srvfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind"); close(srvfd); return 1;
    }
    if (listen(srvfd, 16) < 0) {
        perror("listen"); close(srvfd); return 1;
    }

    printf("Listening on port %d\n", PORT);

    while (1) {
        struct sockaddr_in client;
        socklen_t clen = sizeof(client);
        int conn = accept(srvfd, (struct sockaddr*)&client, &clen);
        if (conn < 0) continue;

        int *fd_ptr = malloc(sizeof(int));
        if (!fd_ptr) { close(conn); continue; }
        *fd_ptr = conn;

        pthread_t tid;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, 2 * 1024 * 1024);
        if (pthread_create(&tid, &attr, request_thread, fd_ptr) != 0) {
            /* Fallback: handle inline if thread creation fails */
            free(fd_ptr);
            int rcvbuf = 1024 * 1024;
            setsockopt(conn, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
            setsockopt(conn, SOL_SOCKET, SO_SNDBUF, &rcvbuf, sizeof(rcvbuf));
            char req[MAX_REQ];
            int n = recv(conn, req, sizeof(req) - 1, 0);
            if (n > 0) {
                req[n] = 0;
                char *iobuf = malloc(BUF_SIZE);
                if (iobuf) {
                    handle_request_inner(conn, iobuf, req, n);
                    free(iobuf);
                }
            }
            close(conn);
        } else {
            pthread_detach(tid);
        }
        pthread_attr_destroy(&attr);
    }

    return 0;
}
