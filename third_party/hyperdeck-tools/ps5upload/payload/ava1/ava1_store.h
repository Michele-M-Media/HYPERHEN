/* The node's identity file and its paired peers (SPEC.md §5). */
#ifndef AVA1_STORE_H
#define AVA1_STORE_H

#include <stdint.h>

#include "ava1_keys.h"

#define AVA1_MAX_PEERS 32

typedef struct {
    uint8_t key[32];
    uint64_t added_unix;
    char name[64];
} ava1_peer_t;

typedef struct {
    ava1_peer_t p[AVA1_MAX_PEERS];
    int n;
} ava1_peers_t;

/* Reads a 32-byte secret or creates one (0600). A file of the wrong size is an
 * error and is left untouched: replacing it would unpair every device. */
int ava1_identity_load_or_create(const char *path, ava1_identity_t *id);
/* Missing file = empty store (0); unparseable lines are skipped. -1 when the file exists
 * but cannot be read: the caller then knows no peers and must never write the file
 * (that would unpair every device it lists) nor open its automatic pairing window. */
int ava1_peers_load(ava1_peers_t *ps, const char *path);
int ava1_peers_contains(const ava1_peers_t *ps, const uint8_t key[32]);
/* Adds or replaces key in memory (oldest dropped past AVA1_MAX_PEERS). */
void ava1_peers_put(ava1_peers_t *ps, const uint8_t key[32], const char *name, uint64_t added_unix);
/* Writes the store atomically (temp file + fsync + rename in the same directory). Slow:
 * never call it holding a lock other threads need. */
int ava1_peers_save(const ava1_peers_t *ps, const char *path);
/* put + save; *ps changes only if the save succeeded. */
int ava1_peers_add(ava1_peers_t *ps, const uint8_t key[32], const char *name, uint64_t added_unix,
                   const char *path);

#endif
