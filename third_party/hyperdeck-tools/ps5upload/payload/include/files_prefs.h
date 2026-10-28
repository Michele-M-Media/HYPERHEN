/* The console file manager's shared preferences: pinned folders (favourites), recently visited
 * folders and the last folder shown, kept ON THE CONSOLE so the console page and the PC app see
 * the same lists.
 *
 * File: FILES_PREFS_PATH, one JSON object
 *   {"v":1,"favorites":["/mnt/usb0/games",...],"recent":["/data/homebrew",...],"last":"/data"|null}
 * Bounds: <= FILES_PREFS_FAV_MAX favourites, <= FILES_PREFS_RECENT_MAX recent (newest first), every
 * path absolute and < FILES_PREFS_PATH_MAX bytes, the whole file <= FILES_PREFS_FILE_MAX bytes.
 * Anything else on disk (garbage, a cut-short write, a file too large) reads as empty prefs and is
 * replaced by the next save.
 *
 * Callers: the web route (/api/files/prefs, webui_api.c) and, later, a management method for the PC
 * engine. Both go through files_prefs_get / files_prefs_update, which serialize every
 * load-change-save under one lock in this module: concurrent writers never interleave, the last
 * writer wins, and the file on disk is always a whole one (same-directory temp file, fsync, rename).
 * Callers check access policy themselves (the route refuses the AVA1 trust store, path_in_protected)
 * before pinning or visiting a path; this module only checks the path's shape. */
#ifndef PS5UPLOAD_FILES_PREFS_H
#define PS5UPLOAD_FILES_PREFS_H

#include <stddef.h>

#define FILES_PREFS_PATH "/data/ps5upload/files-prefs.json"
#define FILES_PREFS_FAV_MAX 50
#define FILES_PREFS_RECENT_MAX 12
#define FILES_PREFS_PATH_MAX 1024u        /* a path's bytes, NUL included */
#define FILES_PREFS_FILE_MAX (64u * 1024u) /* the file, and the JSON answer */
#define FILES_PREFS_REQ_MAX 4096u          /* a change request's body (the route's bound) */

typedef struct {
    int nfav, nrecent, has_last;
    char fav[FILES_PREFS_FAV_MAX][FILES_PREFS_PATH_MAX];
    char recent[FILES_PREFS_RECENT_MAX][FILES_PREFS_PATH_MAX];
    char last[FILES_PREFS_PATH_MAX];
} files_prefs_t; /* ~64 KiB: allocate it on the heap, never on a worker's stack */

enum {
    FILES_PREFS_OK = 0,
    FILES_PREFS_E_BAD_PATH = -1, /* not an absolute, clean path that fits */
    FILES_PREFS_E_FULL = -2,     /* FILES_PREFS_FAV_MAX pinned already, or the file would pass its cap */
    FILES_PREFS_E_IO = -3,       /* the save failed: the file is as it was */
    FILES_PREFS_E_NOMEM = -4,
    FILES_PREFS_E_BAD_OP = -5,
};

typedef enum { FILES_PREFS_PIN = 1, FILES_PREFS_UNPIN = 2, FILES_PREFS_VISIT = 3, FILES_PREFS_FORGET = 4 } files_prefs_op_t;

/* "pin" / "unpin" / "visit" / "forget" -> the op; 0 for anything else. */
int files_prefs_op_from_name(const char *name);

/* Copies `in` into `out` (cap FILES_PREFS_PATH_MAX) in its stored form: '//' collapsed, a trailing
 * '/' dropped ("/" stays "/"). 0, or FILES_PREFS_E_BAD_PATH: not starting with '/', a '.' or '..'
 * component, a control byte, or too long. */
int files_prefs_clean_path(const char *in, char *out);

void files_prefs_clear(files_prefs_t *p);

/* Parses `json` (n bytes, not NUL-terminated). Returns 0 when it is a prefs object; 1 when it is
 * not (*p is then empty, whole). Entries that are not clean paths are dropped, duplicates keep their
 * first place, and lists longer than their bound are cut. */
int files_prefs_parse(const char *json, size_t n, files_prefs_t *p);

/* Reads the file into *p: 0 read, 1 missing / garbled / too large (*p empty). Never fails. */
int files_prefs_load(const char *file, files_prefs_t *p);

/* The file's JSON into out (NUL-terminated). Returns its length, or -1 when it does not fit cap. */
int files_prefs_to_json(const files_prefs_t *p, char *out, size_t cap);

/* Writes the file atomically: `<file>.tmp` in the same directory, fsync, rename() over `file`
 * within that directory (never across mounts). A failed save leaves the old file and no temp file.
 * FILES_PREFS_OK, FILES_PREFS_E_FULL (the JSON would pass FILES_PREFS_FILE_MAX), _E_IO, _E_NOMEM. */
int files_prefs_save(const char *file, const files_prefs_t *p);

/* In-memory changes (path already cleaned or not: each cleans it).
 * pin:   appended at the end (pinned order is the user's); already pinned = no change.
 *        FILES_PREFS_E_FULL with FILES_PREFS_FAV_MAX pinned.
 * unpin: removed when present (absent = no change).
 * visit: `last` = path, and path moves to the front of recent (once). When the folder before it
 *        was its parent or another ancestor (walking down a tree), that ancestor's entry is
 *        replaced, so one walk leaves one entry. Cut to FILES_PREFS_RECENT_MAX.
 * forget: removed from recent (a folder that no longer exists); `last` cleared when it is that
 *        path. Absent = no change. */
int files_prefs_pin(files_prefs_t *p, const char *path);
int files_prefs_unpin(files_prefs_t *p, const char *path);
int files_prefs_visit(files_prefs_t *p, const char *path);
int files_prefs_forget(files_prefs_t *p, const char *path);

/* Locked: the file's prefs as JSON into out (cap >= FILES_PREFS_FILE_MAX + 1). Length, or an error. */
int files_prefs_get(const char *file, char *out, size_t cap);

/* Locked: load, apply op to path, save, and answer the new prefs as JSON into out (as above). A
 * visit that would pass the file cap drops the oldest recent entries until it fits; a pin that
 * would is refused (FILES_PREFS_E_FULL). A visit of the folder that is already last and newest recent
 * writes nothing. On any error the file is unchanged. Length, or an error. */
int files_prefs_update(const char *file, int op, const char *path, char *out, size_t cap);

#endif
