/* PS5 SDK feature guards, ABI types and prototypes. Include before main.c internals that use SCE/sqlite APIs. */
#pragma once

#include <stdint.h>
#include <sys/types.h>

/* ── Feature availability macros ──────────────────────────────────────────── */

#ifndef CHEATRUNNER_HAVE_SQLITE_APPDB
#define CHEATRUNNER_HAVE_SQLITE_APPDB 0
#endif

/* sceLncUtil* lives in libSceSystemService — no separate libSceLncUtil in sdk */
#ifndef CHEATRUNNER_HAVE_SCE_LNCUTIL
#define CHEATRUNNER_HAVE_SCE_LNCUTIL 0
#endif

/* sceUserServiceGetLoginUserIdList in libSceUserService */
#ifndef CHEATRUNNER_HAVE_SCE_USER_LIST
#define CHEATRUNNER_HAVE_SCE_USER_LIST 0
#endif

/* ── PS5 ABI types used by SDK call sites ─────────────────────────────────── */

typedef struct app_info {
    uint32_t app_id;
    uint64_t unknown1;
    char     title_id[14];
    char     unknown2[0x3c];
} app_info_t;

typedef struct app_launch_ctx {
    uint32_t structsize;
    uint32_t user_id;
    uint32_t app_opt;
    uint64_t crash_report;
    uint32_t check_flag;
} app_launch_ctx_t;

typedef struct notify_request {
    char useless1[45];
    char message[3075];
} notify_request_t;

/* ── SceSystemService ─────────────────────────────────────────────────────── */

int sceSystemServiceGetAppIdOfRunningBigApp(void);
int sceSystemServiceGetAppTitleId(int app_id, char *title_id_out);
int sceSystemServiceKillApp(int app_id, int how, int reason, int core_dump);
int sceSystemServiceLaunchApp(const char *title_id, char **argv, app_launch_ctx_t *ctx);

#if CHEATRUNNER_HAVE_SCE_LNCUTIL
int sceLncUtilInitialize(void);
int sceLncUtilLaunchApp(const char *title_id, const char **argv, app_launch_ctx_t *ctx);
#endif

/* ── SceKernel / SceUserService ───────────────────────────────────────────── */

int sceKernelGetAppInfo(pid_t pid, app_info_t *info);
int sceUserServiceInitialize(void *params);
void sceUserServiceTerminate(void);
int sceUserServiceGetForegroundUser(uint32_t *user_id);
int sceUserServiceGetInitialUser(uint32_t *user_id);
int sceUserServiceGetUserName(int32_t user_id, char *name, size_t size);
int sceKernelSendNotificationRequest(int, notify_request_t *, size_t, int);

#if CHEATRUNNER_HAVE_SCE_USER_LIST
int sceUserServiceGetLoginUserIdList(uint32_t *list, size_t count, size_t *actual);
#endif

/* ── SQLite minimal declarations (bundled amalgamation only) ──────────────── */

#if CHEATRUNNER_HAVE_SQLITE_APPDB && !defined(SQLITE_OK)
typedef struct sqlite3      sqlite3;
typedef struct sqlite3_stmt sqlite3_stmt;

int sqlite3_open_v2(const char *filename, sqlite3 **ppDb, int flags, const char *zVfs);
int sqlite3_close(sqlite3 *);
int sqlite3_busy_timeout(sqlite3 *, int ms);
int sqlite3_prepare_v2(sqlite3 *db, const char *zSql, int nByte, sqlite3_stmt **ppStmt, const char **pzTail);
int sqlite3_bind_text(sqlite3_stmt *, int, const char *, int, void (*)(void *));
int sqlite3_step(sqlite3_stmt *);
const unsigned char *sqlite3_column_text(sqlite3_stmt *, int iCol);
int sqlite3_finalize(sqlite3_stmt *pStmt);
const char *sqlite3_errmsg(sqlite3 *);

#define SQLITE_OK            0
#define SQLITE_ROW           100
#define SQLITE_OPEN_READONLY 0x00000001
#define SQLITE_OPEN_NOMUTEX  0x00008000
#endif
