/* Console-local copy and move (SPEC.md §13.5). A copy is a receiver job that talks to
 * Task 18's reader instead of a socket: ava1_recv_open prepares it exactly like an upload,
 * the reader feeds it Chunk/Bundle through ava1_apply_*, and the map it gets back is the
 * resume state. A move deletes the source after a verified Done. */
#include "ava1_copy.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ava1_apply.h"
#include "ava1_data.h"
#include "ava1_platform.h"
#include "ava1_recv.h"
#include "ava1_send.h"   /* Task 18: ava1_reader_t, ava1_read_files */
#include "ava1_thread.h"

typedef struct {
    ava1_bits_t skip;
    ava1_rset_t **durable;
    int have_map, reader_started, move;
    volatile int stop;
    pthread_t reader;
} cp_t;

static cp_t *C_(ava1_job_t *j) { return (cp_t *)j->role; }

uint32_t ava1_copy_test_walk_delay_ms;
int ava1_copy_test_walk_active;
uint32_t ava1_copy_test_delete_delay_ms;
int ava1_copy_test_delete_active;
int ava1_copy_test_crash_before_delete;

int ava1_copy_walk(const ava1_job_copy_t *c, ava1_mstore_t *out) {
    const ava1_data_cfg_t *cfg = ava1_data_cfg();
    char src[AVA1_MAX_PATH + 1];
    struct stat st;
    if (c->flags & ~(AVA1_JF_MOVE | AVA1_JF_OVERWRITE | AVA1_JF_SINGLE_FILE)) return AVA1_ERR_PROTOCOL;
    if (c->src_len > AVA1_MAX_PATH || !c->src_len || memchr(c->src, 0, c->src_len)) return AVA1_ERR_PATH;
    memcpy(src, c->src, c->src_len);
    src[c->src_len] = 0;
    if (src[0] != '/' || !ava1_path_ok((const uint8_t *)src + 1, c->src_len - 1)) return AVA1_ERR_PATH;
    if (!cfg->may_read || !cfg->may_read(src, 0) || stat(src, &st) != 0) return AVA1_ERR_PATH;
    if ((c->flags & AVA1_JF_SINGLE_FILE) && S_ISDIR(st.st_mode)) return AVA1_ERR_PROTOCOL;
    __atomic_store_n(&ava1_copy_test_walk_active, 1, __ATOMIC_RELEASE);
    uint32_t delay = __atomic_load_n(&ava1_copy_test_walk_delay_ms, __ATOMIC_ACQUIRE);
    if (delay) ava1_platform_sleep_ms(delay);
    int rc = S_ISDIR(st.st_mode) ? ava1_mstore_walk_deny(out, src, 0, cfg->refuse_link) : ava1_mstore_single(out, src);
    __atomic_store_n(&ava1_copy_test_walk_active, 0, __ATOMIC_RELEASE);
    return rc == 0 ? AVA1_STATUS_OK : AVA1_ERR_IO;
}

/* The in-process "network": apply the reader's message, waiting for credit. */
int ava1_copy_put(void *ctx, uint8_t type, uint8_t *msg, size_t len) {
    ava1_job_t *j = ctx;
    while (ava1_apply_reserve(j, len) != 0) {
        int ending;
        pthread_mutex_lock(&j->mu);
        ending = C_(j)->stop || j->stopping || j->finished;
        pthread_mutex_unlock(&j->mu);
        if (ending) {
            free(msg);
            return -ECANCELED;
        }
        ava1_platform_sleep_ms(1);
    }
    if (type == AVA1_TYPE_CHUNK) {
        ava1_chunk_t c;
        if (ava1_chunk_decode(msg, len, &c) != 0) {
            ava1_apply_unreserve(j, len);
            free(msg);
            return -EIO;
        }
        return ava1_apply_chunk(j, msg, len, c.file_id, c.offset, c.data, c.data_len) == 0 ? 0 : -EIO;
    }
    {
        ava1_bundle_t b;
        if (ava1_bundle_decode(msg, len, &b) != 0) {
            ava1_apply_unreserve(j, len);
            free(msg);
            return -EIO;
        }
        return ava1_apply_bundle(j, msg, len, &b) == 0 ? 0 : -EIO;
    }
}

static void cp_root(void *ctx, uint32_t id, const uint8_t root[32]) { (void)ava1_apply_root(ctx, id, root); }

static void *cp_reader(void *arg) {
    ava1_job_t *j = arg;
    cp_t *c = C_(j);
    const ava1_data_cfg_t *cfg = ava1_data_cfg();
    ava1_reader_t r;
    struct stat st;
    int rc;
    memset(&r, 0, sizeof r);
    r.j = j;
    r.src_root = j->src;
    r.single = stat(j->src, &st) == 0 && !S_ISDIR(st.st_mode);
    r.cutoff = cfg->cutoff ? cfg->cutoff : 256u << 10; /* must equal the receiver's */
    r.chunk = 8u << 20;
    r.bundle = 1u << 20;
    r.skip = &c->skip;
    r.durable = c->durable;
    r.put = ava1_copy_put;
    r.root = cp_root;
    r.ctx = j;
    r.stop = &c->stop;
    rc = ava1_read_files(&r, NULL, 0);
    if (rc != 0 && rc != -ECANCELED) ava1_apply_fail(j, AVA1_ERR_IO, "reading the source failed", -rc, 0);
    return NULL;
}

/* Delete only the files the manifest saw. A changed file stays at the source; an
 * unlink failure is reported to job.status rather than silently called success. */
static int delete_source(ava1_job_t *j) {
    char p[AVA1_MAX_PATH * 2 + 8];
    struct stat st;
    uint32_t i;
    int left = 0;
    __atomic_store_n(&ava1_copy_test_delete_active, 1, __ATOMIC_RELEASE);
    uint32_t delay = __atomic_load_n(&ava1_copy_test_delete_delay_ms, __ATOMIC_ACQUIRE);
    if (delay) ava1_platform_sleep_ms(delay);
    if (!(j->flags & AVA1_JF_SINGLE_FILE)) {
        /* Directory mode; the per-file checks below do the deletion. */
    } else {
        if (lstat(j->src, &st) == 0) {
            if (!S_ISREG(st.st_mode) || j->m.n != 1 || st.st_size < 0 ||
                (uint64_t)st.st_size != j->m.e[0].size || (uint64_t)st.st_mtime != j->m.e[0].mtime ||
                unlink(j->src) != 0)
                left++;
        } else if (errno != ENOENT) left++;
        __atomic_store_n(&ava1_copy_test_delete_active, 0, __ATOMIC_RELEASE);
        return left;
    }
    for (i = 0; i < j->m.n; i++)
        if (j->m.e[i].kind == AVA1_ENTRY_FILE) {
            if (snprintf(p, sizeof p, "%s/%s", j->src, ava1_mstore_path(&j->m, i)) >= (int)sizeof p) {
                left++;
                continue;
            }
            if (lstat(p, &st) == 0) {
                if (!S_ISREG(st.st_mode) || st.st_size < 0 || (uint64_t)st.st_size != j->m.e[i].size ||
                    (uint64_t)st.st_mtime != j->m.e[i].mtime || unlink(p) != 0)
                    left++;
            } else if (errno != ENOENT) left++;
        }
    for (i = j->m.n; i-- > 0;)
        if (j->m.e[i].kind == AVA1_ENTRY_DIR) {
            if (snprintf(p, sizeof p, "%s/%s", j->src, ava1_mstore_path(&j->m, i)) >= (int)sizeof p ||
                (rmdir(p) != 0 && errno != ENOENT)) left++;
        }
    if (rmdir(j->src) != 0 && errno != ENOENT) left++;
    __atomic_store_n(&ava1_copy_test_delete_active, 0, __ATOMIC_RELEASE);
    return left;
}

void ava1_copy_emit(ava1_job_t *j, uint8_t type, uint8_t flags, const uint8_t *body, size_t len) {
    cp_t *c = C_(j);
    (void)flags;
    if (type == AVA1_TYPE_JOB_MAP) {
        ava1_job_map_t m;
        ava1_r_t it;
        ava1_file_run_t r;
        ava1_file_range_t g;
        if (ava1_job_map_decode(body, len, &m) != 0 || m.status != AVA1_STATUS_OK) return; /* apply_fail reports it */
        ava1_r_init(&it, m.done, m.done_len);
        while (ava1_file_run_next(&it, &r) == 1) {
            uint32_t f;
            for (f = r.first; f < r.first + r.count && f < j->m.n; f++) ava1_bits_set(&c->skip, f);
        }
        ava1_r_init(&it, m.partial, m.partial_len);
        while (ava1_file_range_next(&it, &g) == 1)
            if (g.file_id < j->m.n) {
                if (!c->durable[g.file_id]) c->durable[g.file_id] = calloc(1, sizeof(ava1_rset_t));
                if (c->durable[g.file_id]) (void)ava1_rset_add(c->durable[g.file_id], g.offset, g.offset + g.len);
            }
        if (m.last) c->have_map = 1;
    } else if (type == AVA1_TYPE_JOB_DONE) {
        ava1_job_done_t d;
        if (ava1_job_done_decode(body, len, &d) == 0) {
            /* The reaper reads parked_at_ms under the table lock, which the job thread does
             * not hold when JobDone is emitted: a release store makes the stamp visible
             * (ruling 6). Every finished copy gets the full park age from here; the delete
             * below may run on this call, so the stamp goes first. */
            int left = 0;
            __atomic_store_n(&j->parked_at_ms, ava1_mono_ms(), __ATOMIC_RELEASE);
            if (__atomic_load_n(&ava1_copy_test_crash_before_delete, __ATOMIC_ACQUIRE)) {
                /* Tests: the payload died after the journaled Done and before the delete. */
                __atomic_store_n(&ava1_copy_test_delete_active, 1, __ATOMIC_RELEASE);
                return;
            }
            if (d.status == AVA1_STATUS_OK && c->move && j->durable_ok) left = delete_source(j);
            if (left) {
                ava1_jnl_done_t terminal = { .status = AVA1_ERR_IO };
                uint8_t encoded[16];
                ava1_w_t w;
                ava1_w_init(&w, encoded, sizeof encoded);
                if (ava1_jnl_done_encode(&terminal, &w) == 0)
                    (void)ava1_apply_jnl_append(j, AVA1_JNL_DONE, encoded, w.len);
            }
            pthread_mutex_lock(&j->mu);
            if (left) {
                j->final_status = AVA1_ERR_IO;
                snprintf(j->message, sizeof j->message, "source deletion left %d paths", left);
            }
            __atomic_store_n(&j->parked_at_ms, ava1_mono_ms(), __ATOMIC_RELEASE);
            __atomic_store_n(&j->copy_delete_done, 1, __ATOMIC_RELEASE);
            pthread_mutex_unlock(&j->mu);
        }
    } else if (type == AVA1_TYPE_FILE_RETRY) {
        ava1_file_retry_t r;
        const char *reason = "a copied file did not verify";
        if (ava1_file_retry_decode(body, len, &r) == 0 && r.reason == AVA1_RETRY_CHANGED)
            reason = "source changed while copying";
        /* cp_emit can run on a worker. Record the failure for the job thread, which is
         * the one place that may journal and emit JobDone. */
        pthread_mutex_lock(&j->mu);
        if (!j->finished && !j->final_status) {
            j->final_status = AVA1_ERR_VERIFY;
            snprintf(j->message, sizeof j->message, "%s", reason);
        }
        pthread_mutex_unlock(&j->mu);
    }
}

static void cp_tick(ava1_job_t *j) {
    cp_t *c = C_(j);
    if (c->have_map && !c->reader_started) c->reader_started = ava1_thread_start(cp_reader, j, &c->reader) == 0;
}

static void cp_free(ava1_job_t *j) {
    cp_t *c = C_(j);
    uint32_t i;
    if (!c) return;
    c->stop = 1;
    if (c->reader_started) pthread_join(c->reader, NULL);
    for (i = 0; c->durable && i < j->m.n; i++)
        if (c->durable[i]) {
            ava1_rset_clear(c->durable[i]);
            free(c->durable[i]);
        }
    free(c->durable);
    ava1_bits_free(&c->skip);
    free(c);
    j->role = NULL;
}

/* Follow each existing ancestor. Comparing device/inode catches symlink aliases and
 * hard links; comparing path text misses both, as well as trailing slashes. */
static int path_has_inode_ancestor(const char *path, const struct stat *target) {
    char p[AVA1_MAX_PATH + 1];
    char *slash;
    struct stat st;
    if (strlen(path) > AVA1_MAX_PATH) return -1;
    strcpy(p, path);
    for (;;) {
        if (stat(p, &st) == 0) {
            if (st.st_dev == target->st_dev && st.st_ino == target->st_ino) return 1;
        } else if (errno != ENOENT && errno != ENOTDIR) {
            return -1;
        }
        if (!strcmp(p, "/")) return 0;
        slash = strrchr(p, '/');
        if (!slash) return -1;
        if (slash == p) p[1] = 0;
        else *slash = 0;
    }
}

/* Two job roots overlap when one contains the other by path text, or by disk identity
 * (a symlink alias or hard-linked ancestor). Paths that do not exist yet cannot alias. */
static int text_contains(const char *parent, const char *path) {
    size_t n = strlen(parent);
    if (strlen(path) < n) return 0;
    return strncmp(parent, path, n) == 0 && (path[n] == 0 || path[n] == '/');
}

int ava1_copy_paths_overlap(const char *a, const char *b) {
    struct stat sa, sb;
    if (text_contains(a, b) || text_contains(b, a)) return 1;
    if (stat(a, &sa) == 0 && path_has_inode_ancestor(b, &sa) != 0) return 1;
    if (stat(b, &sb) == 0 && path_has_inode_ancestor(a, &sb) != 0) return 1;
    return 0;
}

/* Without JF_OVERWRITE the destination root must not exist. An absent root cannot
 * contain a colliding child; lstat also counts a dangling symlink as existing. */
static int dest_collides(const char *dest) {
    struct stat st;
    return lstat(dest, &st) == 0 || errno != ENOENT;
}

ava1_job_t *ava1_copy_open(const ava1_job_copy_t *in, const uint8_t owner[32],
                          ava1_mstore_t *prepared, uint16_t *status, char *msg, size_t cap) {
    const ava1_data_cfg_t *cfg = ava1_data_cfg();
    char src[AVA1_MAX_PATH + 1], dest[AVA1_MAX_PATH + 1];
    ava1_recv_spec_t s;
    ava1_job_open_ack_t ack;
    ava1_job_t *j;
    cp_t *c;
    struct stat st, dst_st;
    int rc;
    *status = AVA1_ERR_PATH;
    if (in->src_len > AVA1_MAX_PATH || in->dest_len > AVA1_MAX_PATH) return NULL;
    memcpy(src, in->src, in->src_len);
    src[in->src_len] = 0;
    memcpy(dest, in->dest, in->dest_len);
    dest[in->dest_len] = 0;
    if (src[0] != '/' || dest[0] != '/' ||
        !ava1_path_ok((const uint8_t *)src + 1, strlen(src) - 1) ||
        !ava1_path_ok((const uint8_t *)dest + 1, strlen(dest) - 1)) {
        snprintf(msg, cap, "the source or destination is not a valid path");
        return NULL;
    }
    if (in->flags & ~(AVA1_JF_MOVE | AVA1_JF_OVERWRITE | AVA1_JF_SINGLE_FILE)) {
        *status = AVA1_ERR_PROTOCOL;
        snprintf(msg, cap, "unknown copy flags");
        return NULL;
    }
    j = ava1_job_find(in->job_id);
    if (j) { /* A completed move can answer after its source has gone. */
        int failed;
        if (memcmp(j->owner, owner, 32) != 0) {
            *status = AVA1_ERR_UNKNOWN_JOB;
            ava1_job_put(j);
            return NULL;
        }
        if (j->kind != AVA1_JOB_COPY || strcmp(j->src, src) != 0 || strcmp(j->root, dest) != 0 ||
            j->copy_flags != in->flags) {
            *status = AVA1_ERR_PROTOCOL;
            snprintf(msg, cap, "a copy with this id has different parameters");
            ava1_job_put(j);
            return NULL;
        }
        pthread_mutex_lock(&j->mu);
        failed = j->finished && j->final_status != AVA1_STATUS_OK;
        pthread_mutex_unlock(&j->mu);
        if (!failed) {
            *status = AVA1_STATUS_OK;
            return j;
        }
        if (ava1_job_retire(j) != 0) {
            *status = AVA1_ERR_BUSY;
            snprintf(msg, cap, "the failed copy is still closing");
            return NULL;
        }
    }
    if (!cfg->may_read || !cfg->may_read(src, 0) || stat(src, &st) != 0) {
        snprintf(msg, cap, "cannot read %s", src);
        return NULL;
    }
    if ((in->flags & AVA1_JF_SINGLE_FILE) && S_ISDIR(st.st_mode)) {
        *status = AVA1_ERR_PROTOCOL;
        snprintf(msg, cap, "single-file flag on a folder");
        return NULL;
    }
    /* CORRECTED (ruling 3): the destination must be writable, and a move must be able to
     * delete the source. */
    if (!cfg->may_write || !cfg->may_write(dest) || ((in->flags & AVA1_JF_MOVE) && !cfg->may_write(src))) {
        snprintf(msg, cap, "cannot write %s", dest);
        return NULL;
    }
    /* Reject both directions by disk identity, before any job can stage or delete. */
    rc = path_has_inode_ancestor(dest, &st);
    if (rc < 0 || rc > 0 ||
        (stat(dest, &dst_st) == 0 && path_has_inode_ancestor(src, &dst_st) != 0)) {
        snprintf(msg, cap, "the source and destination overlap");
        return NULL;
    }
    memset(&s, 0, sizeof s);
    memcpy(s.id, in->job_id, 16);
    memcpy(s.owner, owner, 32);
    s.kind = AVA1_JOB_COPY;
    s.policy = AVA1_POLICY_REPLACE;
    s.flags = S_ISDIR(st.st_mode) ? 0 : AVA1_JF_SINGLE_FILE;
    s.root = dest;
    s.emit = ava1_copy_emit;
    s.emit_ctx = NULL;
    s.sid = NULL; /* a local job has no session; any paired device may poll it */
    j = ava1_recv_open(&s, &ack, msg, cap);
    if (!j) {
        *status = ack.status;
        return NULL;
    }
    /* A failed copy's Done marker says where the previous attempt stopped; it is not
     * a terminal answer to the reissued copy. The manifest and durable ranges stay. */
    if (j->replay_done && j->replay_status != AVA1_STATUS_OK) j->replay_done = 0;
    c = calloc(1, sizeof *c);
    /* CORRECTED: `j->src` must be set before anything can start the reader (cp_tick waits
     * for the map, which needs the prepare the job thread runs after the manifest hash is
     * checked) — and the walk must not hold j->mu (it mallocs and does I/O). */
    snprintf(j->src, sizeof j->src, "%s", src);
    if (c && prepared) {
        j->m_in = *prepared;
        memset(prepared, 0, sizeof *prepared);
        rc = 0;
    } else {
        rc = c ? (S_ISDIR(st.st_mode) ? ava1_mstore_walk_deny(&j->m_in, src, 0, cfg->refuse_link) : ava1_mstore_single(&j->m_in, src))
               : -ENOMEM;
    }
    if (rc != 0 || ava1_bits_init(&c->skip, j->m_in.n) != 0 ||
        !(c->durable = calloc((size_t)j->m_in.n + 1, sizeof *c->durable))) {
        free(c);
        *status = AVA1_ERR_IO; /* CORRECTED: an unwalkable source is I/O, not a path error */
        snprintf(msg, cap, "cannot list %s", src);
        ava1_job_free_one(in->job_id); /* no role was set: free_one + put destroys it */
        ava1_job_put(j);
        return NULL;
    }
    /* C14: without JF_OVERWRITE, refuse an existing destination with ERR_EXISTS before
     * anything is written (the receiver writes nothing until the manifest is adopted, which
     * needs the ev_end set at the end of this function, so this check is the last word).
     * The residual TOCTOU window — a path created after this walk but before the write — is
     * accepted for v1: the receiver then replaces that file (policy REPLACE), because a
     * partial copy followed by a refusal is the worse failure. With the flag, colliding
     * destination files are replaced per file and destination-only entries are left alone
     * (a copy never wipes a tree it was not asked to wipe). A re-issued job.copy that
     * resumed the same job from its journal is not refused: the paths found are its own
     * partials, which the receiver reconciles. */
    if (!(in->flags & AVA1_JF_OVERWRITE)) {
        int resumed;
        pthread_mutex_lock(&j->mu);
        resumed = j->have_manifest;
        pthread_mutex_unlock(&j->mu);
        if (!resumed && dest_collides(dest)) {
            ava1_bits_free(&c->skip);
            free(c->durable);
            free(c);
            *status = AVA1_ERR_EXISTS;
            snprintf(msg, cap, "the destination already exists");
            ava1_job_free_one(in->job_id); /* no role was set: free_one + put destroys it */
            ava1_job_put(j);
            return NULL;
        }
    }
    c->move = (in->flags & AVA1_JF_MOVE) != 0;
    pthread_mutex_lock(&j->mu);
    j->role = c;
    __atomic_store_n(&j->copy_move, c->move, __ATOMIC_RELEASE);
    j->copy_flags = in->flags;
    __atomic_store_n(&j->copy_delete_done, !c->move, __ATOMIC_RELEASE);
    j->role_free = cp_free;
    j->on_tick = cp_tick;
    j->end_files = j->m_in.files;
    j->end_bytes = j->m_in.bytes;
    ava1_mstore_hash(&j->m_in, j->end_hash);
    j->ev_end = 1; /* the receiver prepares and "sends" its map to cp_emit */
    pthread_mutex_unlock(&j->mu);
    *status = AVA1_STATUS_OK;
    return j;
}
