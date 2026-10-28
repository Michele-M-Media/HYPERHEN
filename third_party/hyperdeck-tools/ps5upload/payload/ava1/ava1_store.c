#include "ava1_store.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ava1_platform.h"
#include "monocypher.h"

/* Temp file in the same directory, then rename: never crosses a device. */
static int write_file_atomic(const char *path, const uint8_t *data, size_t n, int mode) {
    char tmp[300];
    int fd, ok = 1;
    if (snprintf(tmp, sizeof tmp, "%s.tmp", path) >= (int)sizeof tmp) return -1;
    fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, mode);
    if (fd < 0) return -1;
    while (n > 0 && ok) {
        ssize_t k = write(fd, data, n);
        if (k < 0) {
            if (errno != EINTR) ok = 0;
            continue;
        }
        data += k;
        n -= (size_t)k;
    }
    if (ok && fsync(fd) != 0) ok = 0;
    close(fd);
    if (!ok || rename(tmp, path) != 0) {
        unlink(tmp);
        return -1;
    }
    return 0;
}

int ava1_identity_load_or_create(const char *path, ava1_identity_t *id) {
    uint8_t secret[32];
    FILE *f = fopen(path, "rb");
    if (f) {
        size_t got = fread(secret, 1, sizeof secret, f);
        int extra = fgetc(f);
        fclose(f);
        if (got != sizeof secret || extra != EOF) return -1;
        ava1_identity_from_secret(id, secret);
        crypto_wipe(secret, sizeof secret);
        return 0;
    }
    if (errno != ENOENT) return -1;
    if (ava1_platform_random(secret, sizeof secret) != 0) return -1;
    if (write_file_atomic(path, secret, sizeof secret, 0600) != 0) {
        crypto_wipe(secret, sizeof secret);
        return -1;
    }
    ava1_identity_from_secret(id, secret);
    crypto_wipe(secret, sizeof secret);
    return 0;
}

static int hexval(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int ava1_peers_load(ava1_peers_t *ps, const char *path) {
    char line[256];
    FILE *f;
    memset(ps, 0, sizeof *ps);
    f = fopen(path, "r");
    if (!f) return errno == ENOENT ? 0 : -1;
    while (ps->n < AVA1_MAX_PEERS && fgets(line, sizeof line, f)) {
        ava1_peer_t p;
        char *end;
        size_t i, nl;
        int ok = 1;
        memset(&p, 0, sizeof p);
        for (i = 0; i < 32 && ok; i++) {
            int hi = hexval((unsigned char)line[2 * i]);
            int lo = hi < 0 ? -1 : hexval((unsigned char)line[2 * i + 1]);
            if (hi < 0 || lo < 0) ok = 0;
            else p.key[i] = (uint8_t)(hi * 16 + lo);
        }
        if (!ok || line[64] != ' ') continue;
        p.added_unix = (uint64_t)strtoull(line + 65, &end, 10);
        if (end == line + 65 || *end != ' ') continue;
        end++;
        nl = strcspn(end, "\r\n");
        if (nl > 63) nl = 63;
        memcpy(p.name, end, nl);
        ps->p[ps->n++] = p;
    }
    if (ferror(f)) {
        /* e.g. EISDIR or EIO: what is on disk is unknown, which is not "no peers". */
        fclose(f);
        memset(ps, 0, sizeof *ps);
        return -1;
    }
    fclose(f);
    return 0;
}

int ava1_peers_contains(const ava1_peers_t *ps, const uint8_t key[32]) {
    int i;
    for (i = 0; i < ps->n; i++)
        if (memcmp(ps->p[i].key, key, 32) == 0) return 1;
    return 0;
}

void ava1_peers_put(ava1_peers_t *ps, const uint8_t key[32], const char *name, uint64_t added_unix) {
    int i, j;
    for (i = 0, j = 0; i < ps->n; i++)
        if (memcmp(ps->p[i].key, key, 32) != 0) ps->p[j++] = ps->p[i];
    ps->n = j;
    if (ps->n == AVA1_MAX_PEERS) {
        memmove(&ps->p[0], &ps->p[1], sizeof ps->p[0] * (AVA1_MAX_PEERS - 1));
        ps->n--;
    }
    memset(&ps->p[ps->n], 0, sizeof ps->p[0]);
    memcpy(ps->p[ps->n].key, key, 32);
    ps->p[ps->n].added_unix = added_unix;
    snprintf(ps->p[ps->n].name, sizeof ps->p[0].name, "%s", name);
    ps->n++;
}

int ava1_peers_save(const ava1_peers_t *ps, const char *path) {
    size_t cap = (size_t)AVA1_MAX_PEERS * 160, len = 0;
    char *text = malloc(cap);
    int i, k, rc;
    if (!text) return -1;
    for (i = 0; i < ps->n; i++) {
        for (k = 0; k < 32; k++) len += (size_t)snprintf(text + len, cap - len, "%02x", ps->p[i].key[k]);
        len += (size_t)snprintf(text + len, cap - len, " %llu %s\n",
                                (unsigned long long)ps->p[i].added_unix, ps->p[i].name);
    }
    rc = write_file_atomic(path, (const uint8_t *)text, len, 0600);
    free(text);
    return rc;
}

int ava1_peers_add(ava1_peers_t *ps, const uint8_t key[32], const char *name, uint64_t added_unix,
                   const char *path) {
    ava1_peers_t *next = malloc(sizeof *next);
    int rc;
    if (!next) return -1;
    *next = *ps;
    ava1_peers_put(next, key, name, added_unix);
    rc = ava1_peers_save(next, path);
    if (rc == 0) *ps = *next;
    free(next);
    return rc;
}
