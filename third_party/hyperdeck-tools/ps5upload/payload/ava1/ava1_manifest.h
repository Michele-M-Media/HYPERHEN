/* The manifest store: Rust's Manifest, in C, with the same hash (SPEC.md §11). */
#ifndef AVA1_MANIFEST_H
#define AVA1_MANIFEST_H

#include <stddef.h>
#include <stdint.h>

#include "ava1_gen.h" /* ava1_manifest_entry_t, ava1_manifest_page_t */

#define AVA1_MAX_PATH 1024
#define AVA1_E_BADPATH (-20)
#define AVA1_PAGE_BYTES (60u * 1024u)
#define AVA1_MAX_ENTRIES 4000000u /* refuse `add` past this: bounds a hostile manifest's memory */

typedef struct {
    uint64_t size, mtime;
    uint32_t mode, path_off;
    uint32_t root_idx; /* 0 = no root, else 1 + index into the store's roots[] */
    uint16_t path_len;
    uint8_t kind;
} ava1_ment_t; /* 32 bytes: the 32-byte roots live in a side array, allocated only when set */

typedef struct {
    ava1_ment_t *e;
    uint32_t n, cap, files;
    uint64_t bytes;
    uint8_t (*roots)[32]; /* only entries that carry a root (verify policy) */
    uint32_t nroots, roots_cap;
    char *arena; /* NUL-terminated paths */
    size_t arena_len, arena_cap;
} ava1_mstore_t;

int ava1_path_ok(const uint8_t *p, size_t n);
/* Room for n entries up front, when the count is known. 0, AVA1_E_PROTO past the cap, AVA1_E_IO. */
int ava1_mstore_reserve(ava1_mstore_t *m, uint32_t n);
int ava1_mstore_add(ava1_mstore_t *m, const ava1_manifest_entry_t *e);
int ava1_mstore_add_page(ava1_mstore_t *m, const ava1_manifest_page_t *p);
void ava1_mstore_hash(const ava1_mstore_t *m, uint8_t out[32]);
/* NULL when id >= n. */
const char *ava1_mstore_path(const ava1_mstore_t *m, uint32_t id);
/* The entry's root (verify policy), or NULL when it carries none or id >= n. */
const uint8_t *ava1_mstore_root(const ava1_mstore_t *m, uint32_t id);
int ava1_mstore_blob(const ava1_mstore_t *m, uint8_t **blob, size_t *len); /* malloc'd */
int ava1_mstore_from_blob(ava1_mstore_t *m, const uint8_t *blob, size_t len);
/* One ManifestPage; `*next` is the first entry not written. */
int ava1_mstore_page(const ava1_mstore_t *m, const uint8_t job[16], uint32_t *next, uint8_t *out,
                     size_t cap, size_t *len);
/* ava1_mstore_walk_ex flags: follow directory symlinks (the sender's mode, matching
 * Rust's walk); the default mode skips them, so a cycle cannot form. */
#define AVA1_WALK_FOLLOW 1u
int ava1_mstore_walk(ava1_mstore_t *m, const char *root); /* = ava1_mstore_walk_ex(m, root, 0) */
int ava1_mstore_walk_ex(ava1_mstore_t *m, const char *root, unsigned flags); /* depth first, sorted */
/* The walk with a deny hook (review S2): a SYMLINK entry for which `deny(abs)` is 1 (it leads into the trust
 * store or to an ancestor of it) is skipped, never followed or listed. NULL = ava1_mstore_walk_ex. */
int ava1_mstore_walk_deny(ava1_mstore_t *m, const char *root, unsigned flags, int (*deny)(const char *abs));
/* Opens a file for reading without following a final symlink. A symlink (a file link inside a followed
 * walk) is resolved once, refused when `deny(canonical)` is 1, and then opened by its canonical path, again
 * with O_NOFOLLOW, so a link swapped in after the walk cannot lead into the store. Returns an fd or -errno. */
int ava1_open_read_safe(const char *path, int (*deny)(const char *abs));
int ava1_mstore_single(ava1_mstore_t *m, const char *file); /* JF_SINGLE_FILE */
void ava1_mstore_free(ava1_mstore_t *m);

#endif
