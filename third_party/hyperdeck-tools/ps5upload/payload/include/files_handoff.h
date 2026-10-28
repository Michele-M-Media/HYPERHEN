/* "Show on TV" / "Show on PC": one folder handed between the console page and the PC app.
 *
 * Helper memory only (nothing on disk): one slot per direction. A put replaces what the slot held;
 * a take empties it; a handoff older than FILES_HANDOFF_TTL_MS is gone (a take answers nothing and
 * clears it). One mutex covers both slots. The path is cleaned as files_prefs_clean_path does.
 *
 * The four management methods (files.prefs_get / prefs_set / handoff_put / handoff_take, the PC
 * app's way in through the engine) have their JSON bodies here too, so the host self-test drives the
 * same code runtime.c's handlers call. Each writes a JSON answer into out and returns its length (-1
 * only when cap cannot hold any answer); a refusal is {"ok":false,"err":"<token>"}, which the
 * management dispatcher turns into an error status. */
#ifndef PS5UPLOAD_FILES_HANDOFF_H
#define PS5UPLOAD_FILES_HANDOFF_H

#include <stddef.h>

#include "files_prefs.h"

#define FILES_HANDOFF_TTL_MS 120000LL
/* A request body's bound (a path escaped, plus the key). */
#define FILES_HANDOFF_REQ_MAX 8192u
/* An answer's size: {"path":"<path, each byte escaped to at most 6>","age_ms":N} */
#define FILES_HANDOFF_OUT_MAX (6u * FILES_PREFS_PATH_MAX + 64u)

enum { FILES_HANDOFF_TO_CONSOLE = 0, FILES_HANDOFF_TO_PC = 1 };

/* "console" -> FILES_HANDOFF_TO_CONSOLE, "pc" -> FILES_HANDOFF_TO_PC, anything else -1. */
int files_handoff_side(const char *name);

/* Stores path for `side` at now_ms (replacing what was there). 0, FILES_PREFS_E_BAD_PATH, or
 * FILES_PREFS_E_BAD_OP for a bad side. */
int files_handoff_put(int side, const char *path, long long now_ms);

/* Takes `side`'s handoff: 1 with its path (cap FILES_PREFS_PATH_MAX) and age, 0 when there is none
 * or it expired (the slot is empty afterwards either way). */
int files_handoff_take(int side, long long now_ms, char *path, long long *age_ms);

/* Empties both slots (tests). */
void files_handoff_reset(void);

/* CLOCK_MONOTONIC in milliseconds (what runtime.c and the web routes pass as now_ms). */
long long files_handoff_now_ms(void);

/* {"path":"..","age_ms":N} for a taken handoff, {} when none: what the take answers. */
int files_handoff_take_json(int side, long long now_ms, char *out, size_t cap);

/* `,"uploading":["<path>",..]}` for n paths (running uploads' destinations): what /api/jobs adds in
 * place of its closing '}'. Writes nothing and returns -1 when it does not fit cap; else its length. */
int files_uploads_json(const char *const *paths, int n, char *out, size_t cap);

/* ---- the management methods' bodies ---- */

/* files.prefs_get: the prefs file's JSON (as /api/files/prefs answers). out cap >= FILES_PREFS_FILE_MAX + 1. */
int files_mgmt_prefs_get(const char *file, char *out, size_t cap);
/* files.prefs_set {"op":"pin"|"unpin"|"visit","path":".."}: the new prefs. Same out cap. */
int files_mgmt_prefs_set(const char *file, const char *body, size_t n, char *out, size_t cap);
/* files.handoff_put {"to":"console"|"pc","path":".."}: {"ok":true}. */
int files_mgmt_handoff_put(const char *body, size_t n, long long now_ms, char *out, size_t cap);
/* files.handoff_take {"for":"console"|"pc"}: as files_handoff_take_json. */
int files_mgmt_handoff_take(const char *body, size_t n, long long now_ms, char *out, size_t cap);

#endif
