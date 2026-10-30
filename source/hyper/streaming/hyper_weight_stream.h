#pragma once

#include "ggml-backend.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Owned by HyperDialogueImpl; destroy only after llama_model_free().
 * The override buffer uses CPU host layout while pinned, and owns source FDs.
 * Weight bytes use one lazily allocated arena of cap_bytes, reused until
 * destruction. Eviction changes non-owning ranges, never frees/reallocates
 * per-weight storage. No aggregate model allocation is reserved.
 * tensor_buft_overrides { ".*", hyper_weight_stream_buft(stream) } is sufficient
 * for dense Qwen2 weights, with use_mmap=false and use_extra_bufts=false. */
struct hyper_weight_stream;
typedef bool (*hyper_weight_stream_cancel_fn)(void * user);
struct hyper_weight_stream_stats {
    /* resident/peak count occupied arena ranges, not total process RSS.
     * The arena allocation itself is at most cap_bytes and never grows. */
    uint64_t cap_bytes, resident_bytes, peak_bytes, bytes_read;
    uint64_t tensors_registered, unique_weights, max_weight_bytes;
    uint64_t cache_hits, cache_misses, evictions, nodes_computed;
    uint64_t open_files, error_code;
};
enum hyper_weight_stream_error {
    HYPER_STREAM_OK = 0,
    HYPER_STREAM_CANCELLED = 1,
    HYPER_STREAM_TOO_LARGE = 2,
    HYPER_STREAM_IO = 3,
    HYPER_STREAM_INVALID = 4,
    HYPER_STREAM_ALLOC = 5,
    HYPER_STREAM_UNSUPPORTED = 6,
    HYPER_STREAM_WRITE = 7
};

struct hyper_weight_stream * hyper_weight_stream_create(size_t cap_bytes,
    hyper_weight_stream_cancel_fn cancel, void * user);
void hyper_weight_stream_destroy(struct hyper_weight_stream * stream);
ggml_backend_buffer_type_t hyper_weight_stream_buft(struct hyper_weight_stream * stream);
/* Call after the runtime resets its cancellation state for a new request.
 * Clears cancellation only; persistent integrity/I/O/unsupported errors remain.
 * Synchronous compute has already restored aliases and unpinned all operands. */
void hyper_weight_stream_begin_request(struct hyper_weight_stream * stream);
bool hyper_weight_stream_is_buft(ggml_backend_buffer_type_t buft);
bool hyper_weight_stream_is_tensor(const struct ggml_tensor * tensor);

/* Call instead of loader's read_raw for each tensor in this buffer. Caller FD
 * can close afterwards. Bounds are verified by fstat, including both shards.
 * Tied tensors at the same (file, offset, bytes) share one cache entry. */
bool hyper_weight_stream_register(struct ggml_tensor * tensor, int fd,
    uint64_t offset, size_t bytes);
/* Bounded read + actual ggml_validate_row_data, then unpin. SUCCESS plus
 * valid=false means malformed tensor contents, not an I/O/cancellation error. */
enum ggml_status hyper_weight_stream_validate(struct ggml_tensor * tensor, bool * valid);
void hyper_weight_stream_get_stats(const struct hyper_weight_stream * stream,
    struct hyper_weight_stream_stats * out);

/* CPU backend hook: ordinary_compute must synchronously finish all CPU workers
 * before returning. Only node operands/view chains are pinned, one node at a
 * time. false handled means the original full-graph path should run unchanged.
 * All failures occur before computation can dereference weight placeholders. */
typedef enum ggml_status (*hyper_weight_stream_compute_fn)(void * user,
    struct ggml_cgraph * graph);
bool hyper_weight_stream_graph_uses(const struct ggml_cgraph * graph);
enum ggml_status hyper_weight_stream_compute(struct ggml_cgraph * graph,
    hyper_weight_stream_compute_fn ordinary_compute, void * user, bool * handled);

#ifdef __cplusplus
}
#endif
