#include "hyper_weight_stream.h"
#include "ggml-backend-impl.h"
#include "ggml-cpu.h"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "ggml-impl.h"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {
constexpr size_t alignment = 64;
constexpr size_t maximum_cap = size_t(768) * 1024 * 1024;
constexpr size_t io_chunk = 1024 * 1024;
struct source_file {
    int fd;
    dev_t dev;
    ino_t ino;
    uint64_t size;
    timespec mtime;
    ~source_file() { if (fd >= 0) close(fd); }
};
struct weight_key {
    size_t file;
    uint64_t offset;
    size_t bytes;
    bool operator<(const weight_key & b) const {
        if (file != b.file) return file < b.file;
        if (offset != b.offset) return offset < b.offset;
        return bytes < b.bytes;
    }
};
struct weight {
    weight_key key;
    void * data = nullptr;
    size_t arena_offset = 0;
    size_t allocated = 0;
    size_t pins = 0;
    uint64_t used = 0;
};
struct tensor_record {
    void * placeholder = nullptr;
    weight * entry = nullptr;
};
struct buffer_state;
static const char * stream_name(ggml_backend_buffer_type_t) { return "Hyper-CPU-Stream"; }
}

struct hyper_weight_stream {
    ggml_backend_buffer_type buft = {};
    mutable std::mutex mutex;
    hyper_weight_stream_cancel_fn cancel = nullptr;
    void * user = nullptr;
    hyper_weight_stream_stats stats = {};
    uint64_t clock = 0;
    size_t refs = 1;
    // One retained allocation bounds actual weight storage, not just the sum
    // of live per-tensor allocations. Eviction never calls the native heap.
    void * arena = nullptr;
    std::vector<std::unique_ptr<source_file>> files;
    std::map<weight_key, std::unique_ptr<weight>> weights;
    std::map<const ggml_tensor *, tensor_record> tensors;
    ~hyper_weight_stream() { free(arena); }
};

namespace {
struct buffer_state {
    hyper_weight_stream * stream;
    void * placeholders = nullptr;
    std::vector<ggml_tensor *> tensors;
    ~buffer_state() { free(placeholders); }
};

static void set_error(hyper_weight_stream * p, hyper_weight_stream_error e) {
    if (p->stats.error_code == HYPER_STREAM_OK ||
        (p->stats.error_code == HYPER_STREAM_CANCELLED && e != HYPER_STREAM_CANCELLED)) p->stats.error_code = e;
}
static bool cancelled(hyper_weight_stream * p) {
    if (p->cancel && p->cancel(p->user)) {
        set_error(p, HYPER_STREAM_CANCELLED);
        return true;
    }
    return p->stats.error_code != HYPER_STREAM_OK;
}
static ggml_status failure_status(const hyper_weight_stream * p) {
    if (p->stats.error_code == HYPER_STREAM_CANCELLED) return GGML_STATUS_ABORTED;
    if (p->stats.error_code == HYPER_STREAM_TOO_LARGE ||
        p->stats.error_code == HYPER_STREAM_ALLOC) return GGML_STATUS_ALLOC_FAILED;
    return GGML_STATUS_FAILED;
}
static bool round_size(size_t bytes, size_t * out) {
    if (bytes == 0 || bytes > std::numeric_limits<size_t>::max() - (alignment - 1)) return false;
    *out = (bytes + alignment - 1) & ~(alignment - 1);
    return true;
}
static bool is_stream_buffer(ggml_backend_buffer_t buffer) {
    return buffer && buffer->buft && buffer->buft->iface.get_name == stream_name;
}
static ggml_tensor * root_of(const ggml_tensor * tensor, size_t * offset = nullptr) {
    size_t sum = 0;
    const ggml_tensor * cur = tensor;
    for (size_t depth = 0; cur && depth < 64; ++depth) {
        if (!cur->view_src) {
            if (offset) *offset = sum;
            return const_cast<ggml_tensor *>(cur);
        }
        if (cur->view_offs > std::numeric_limits<size_t>::max() - sum) return nullptr;
        sum += cur->view_offs;
        cur = cur->view_src;
    }
    return nullptr;
}
static hyper_weight_stream * provider_of(const ggml_tensor * tensor) {
    ggml_tensor * root = root_of(tensor);
    if (!root || !is_stream_buffer(root->buffer)) return nullptr;
    return static_cast<buffer_state *>(root->buffer->context)->stream;
}
static void evict(hyper_weight_stream * p, weight * w) {
    if (!w->data || w->pins) return;
    w->data = nullptr;
    p->stats.resident_bytes -= w->allocated;
    w->arena_offset = 0;
    w->allocated = 0;
    ++p->stats.evictions;
}
// First fit over occupied ranges. Metadata is already owned by the weight
// registry: no allocation is needed to track or coalesce free spans. Every
// eviction automatically joins adjacent free spans. Never relocate a live
// range, particularly an operand pinned by the current CPU operation.
static bool arena_range(hyper_weight_stream * p, size_t bytes, size_t * offset) {
    const size_t cap = static_cast<size_t>(p->stats.cap_bytes);
    if (bytes > cap) return false;
    size_t candidate = 0;
    for (;;) {
        if (candidate > cap - bytes) return false;
        size_t next = candidate;
        for (const auto & item : p->weights) {
            const weight * w = item.second.get();
            if (!w->data) continue;
            // All stored ranges were admitted against cap with checked
            // subtraction below; candidate + bytes is likewise bounded.
            const size_t end = w->arena_offset + w->allocated;
            if (candidate < end && w->arena_offset < candidate + bytes)
                next = std::max(next, end);
        }
        if (next == candidate) { *offset = candidate; return true; }
        candidate = next;
    }
}
static bool source_unchanged(hyper_weight_stream * p, const source_file & file) {
    struct stat st;
    // Compare the owned FD, not its pathname: rename/unlink is safe. Replacing
    // model contents in place while the worker runs is an I/O failure instead
    // of silently mixing originally validated and changed tensor data.
    if (fstat(file.fd, &st) != 0 || st.st_size < 0 ||
        static_cast<uint64_t>(st.st_size) != file.size ||
        st.st_dev != file.dev || st.st_ino != file.ino ||
        st.st_mtim.tv_sec != file.mtime.tv_sec || st.st_mtim.tv_nsec != file.mtime.tv_nsec) {
        set_error(p, HYPER_STREAM_IO); return false;
    }
    return true;
}
static bool read_bytes(hyper_weight_stream * p, const weight_key & key,
                       void * out, size_t offset, size_t count) {
    if (key.file >= p->files.size() || offset > key.bytes || count > key.bytes - offset) {
        set_error(p, HYPER_STREAM_INVALID); return false;
    }
    auto & file = p->files[key.file];
    if (!source_unchanged(p, *file)) return false;
    size_t done = 0;
    while (done < count) {
        if (cancelled(p)) return false;
        size_t amount = std::min(io_chunk, count - done);
        ssize_t n = pread(file->fd, static_cast<char *>(out) + done, amount,
                         static_cast<off_t>(key.offset + offset + done));
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0 || static_cast<size_t>(n) > amount) {
            set_error(p, HYPER_STREAM_IO); return false;
        }
        done += static_cast<size_t>(n);
        p->stats.bytes_read += static_cast<size_t>(n);
    }
    return source_unchanged(p, *file);
}
static bool pin(hyper_weight_stream * p, weight * w) {
    if (cancelled(p)) return false;
    if (w->data) {
        ++p->stats.cache_hits;
        ++w->pins; w->used = ++p->clock; return true;
    }
    size_t allocation;
    if (!round_size(w->key.bytes, &allocation) || allocation > p->stats.cap_bytes) {
        set_error(p, HYPER_STREAM_TOO_LARGE); return false;
    }
    // Allocate once, including during initial validation. Later cache misses
    // reuse the same bounded storage instead of relying on free() to release
    // hundreds of MiB to the OS under flexible-memory pressure.
    if (!p->arena &&
        (posix_memalign(&p->arena, alignment, static_cast<size_t>(p->stats.cap_bytes)) != 0 || !p->arena)) {
        p->arena = nullptr;
        set_error(p, HYPER_STREAM_ALLOC); return false;
    }
    size_t offset = 0;
    while (!arena_range(p, allocation, &offset)) {
        weight * victim = nullptr;
        for (auto & item : p->weights) {
            weight * candidate = item.second.get();
            if (candidate->data && !candidate->pins &&
                (!victim || candidate->used < victim->used)) victim = candidate;
        }
        if (!victim) { set_error(p, HYPER_STREAM_TOO_LARGE); return false; }
        evict(p, victim);
    }
    void * data = static_cast<char *>(p->arena) + offset;
    w->data = data; w->arena_offset = offset; w->allocated = allocation;
    p->stats.resident_bytes += allocation;
    p->stats.peak_bytes = std::max(p->stats.peak_bytes, p->stats.resident_bytes);
    ++p->stats.cache_misses;
    if (!read_bytes(p, w->key, data, 0, w->key.bytes)) {
        evict(p, w); return false;
    }
    ++w->pins; w->used = ++p->clock;
    return true;
}
static void unpin(weight * w) { if (w->pins) --w->pins; }
static void unref(hyper_weight_stream * p) {
    bool last;
    { std::lock_guard<std::mutex> lock(p->mutex); last = --p->refs == 0; }
    if (last) delete p;
}

static void buffer_free(ggml_backend_buffer_t buffer) {
    auto * b = static_cast<buffer_state *>(buffer->context);
    hyper_weight_stream * p = b->stream;
    {
        std::lock_guard<std::mutex> lock(p->mutex);
        for (ggml_tensor * t : b->tensors) p->tensors.erase(t);
    }
    delete b;
    unref(p);
}
static void * buffer_base(ggml_backend_buffer_t buffer) {
    return static_cast<buffer_state *>(buffer->context)->placeholders;
}
static ggml_status buffer_init(ggml_backend_buffer_t buffer, ggml_tensor * tensor) {
    auto * b = static_cast<buffer_state *>(buffer->context);
    std::lock_guard<std::mutex> lock(b->stream->mutex);
    size_t offset = 0;
    ggml_tensor * root = root_of(tensor, &offset);
    auto record = b->stream->tensors.find(root);
    if (record == b->stream->tensors.end() || offset > ggml_nbytes(root) ||
        ggml_nbytes(tensor) > ggml_nbytes(root) - offset) {
        set_error(b->stream, HYPER_STREAM_INVALID); return GGML_STATUS_FAILED;
    }
    // The scheduler initializes transient graph views that are not part of the
    // model's metadata context. They share the root descriptor; never insert
    // their short-lived pointers into the provider registry.
    tensor->data = record->second.placeholder;
    return GGML_STATUS_SUCCESS;
}
static void reject_write(ggml_backend_buffer_t buffer) {
    auto * p = static_cast<buffer_state *>(buffer->context)->stream;
    std::lock_guard<std::mutex> lock(p->mutex);
    set_error(p, HYPER_STREAM_WRITE);
}
static void buffer_memset(ggml_backend_buffer_t b, ggml_tensor *, uint8_t, size_t, size_t) { reject_write(b); }
static void buffer_set(ggml_backend_buffer_t b, ggml_tensor *, const void *, size_t, size_t) { reject_write(b); }
static void buffer_clear(ggml_backend_buffer_t b, uint8_t) { reject_write(b); }
static void buffer_get(ggml_backend_buffer_t buffer, const ggml_tensor * tensor,
                       void * out, size_t offset, size_t count) {
    auto * p = static_cast<buffer_state *>(buffer->context)->stream;
    std::lock_guard<std::mutex> lock(p->mutex);
    size_t view_offset = 0;
    ggml_tensor * root = root_of(tensor, &view_offset);
    auto it = p->tensors.find(root);
    if (!out || !root || it == p->tensors.end() || !it->second.entry ||
        view_offset > std::numeric_limits<size_t>::max() - offset) {
        set_error(p, HYPER_STREAM_INVALID);
        if (out && count) memset(out, 0, count);
        return;
    }
    if (!read_bytes(p, it->second.entry->key, out, view_offset + offset, count) && count) {
        memset(out, 0, count);
    }
}
static ggml_backend_buffer_i buffer_iface() {
    ggml_backend_buffer_i i = {};
    i.free_buffer = buffer_free;
    i.get_base = buffer_base;
    i.init_tensor = buffer_init;
    i.memset_tensor = buffer_memset;
    i.set_tensor = buffer_set;
    i.get_tensor = buffer_get;
    i.clear = buffer_clear;
    return i;
}
static size_t buft_alignment(ggml_backend_buffer_type_t) { return alignment; }
static size_t buft_max_size(ggml_backend_buffer_type_t) { return std::numeric_limits<size_t>::max(); }
static bool buft_host(ggml_backend_buffer_type_t) {
    // GGML's generic tensor copy dereferences data directly for host buffers.
    // Only the patched CPU device accepts this non-host buffer explicitly.
    return false;
}
static size_t buft_size_n(ggml_backend_buffer_type_t, ggml_tensor **, int n) {
    if (n < 0 || static_cast<size_t>(n) > std::numeric_limits<size_t>::max() / alignment) return 0;
    return std::max(alignment, static_cast<size_t>(n) * alignment);
}
static ggml_backend_buffer_t buft_alloc(ggml_backend_buffer_type_t buft, size_t) {
    // A logical byte allocator cannot safely initialize streamed weights.
    auto * p = static_cast<hyper_weight_stream *>(buft->context);
    std::lock_guard<std::mutex> lock(p->mutex);
    set_error(p, HYPER_STREAM_UNSUPPORTED);
    return nullptr;
}
static ggml_backend_buffer_t buft_alloc_n(ggml_backend_buffer_type_t buft,
                                         ggml_tensor ** tensors, int n) {
    auto * p = static_cast<hyper_weight_stream *>(buft->context);
    std::lock_guard<std::mutex> lock(p->mutex);
    if (n < 0 || (n && !tensors) || cancelled(p)) return nullptr;
    std::unique_ptr<buffer_state> b(new (std::nothrow) buffer_state());
    if (!b) { set_error(p, HYPER_STREAM_ALLOC); return nullptr; }
    b->stream = p;
    const size_t size = buft_size_n(buft, tensors, n);
    if (!size || posix_memalign(&b->placeholders, alignment, size) != 0 || !b->placeholders) {
        set_error(p, HYPER_STREAM_ALLOC); return nullptr;
    }
    memset(b->placeholders, 0, size);
    try { b->tensors.assign(tensors, tensors + n); }
    catch (...) { set_error(p, HYPER_STREAM_ALLOC); return nullptr; }
    ggml_backend_buffer_t result = ggml_backend_buffer_init(buft, buffer_iface(), b.get(), size);
    if (!result) { set_error(p, HYPER_STREAM_ALLOC); return nullptr; }
    try {
        for (int i = 0; i < n; ++i) {
            void * placeholder = static_cast<char *>(b->placeholders) + size_t(i) * alignment;
            p->tensors.emplace(tensors[i], tensor_record{placeholder, nullptr});
        }
    } catch (...) {
        for (int i = 0; i < n; ++i) p->tensors.erase(tensors[i]);
        // The GGML buffer object has not acquired its buffer_state reference.
        result->iface.free_buffer = nullptr;
        ggml_backend_buffer_free(result);
        set_error(p, HYPER_STREAM_ALLOC); return nullptr;
    }
    for (int i = 0; i < n; ++i) {
        tensors[i]->buffer = result;
        tensors[i]->data = p->tensors.at(tensors[i]).placeholder;
    }
    ++p->refs;
    b.release();
    return result;
}
static ggml_backend_buffer_type_i buft_iface() {
    ggml_backend_buffer_type_i i = {};
    i.get_name = stream_name;
    i.alloc_buffer = buft_alloc;
    i.alloc_buffer_n = buft_alloc_n;
    i.get_alignment = buft_alignment;
    i.get_max_size = buft_max_size;
    // Per-tensor get_alloc_size must remain >= ggml_nbytes per GGML contract.
    // The explicit list allocator bypasses aggregate logical pointer arithmetic.
    i.get_alloc_size_n = buft_size_n;
    i.is_host = buft_host;
    return i;
}

struct restore_pointer { ggml_tensor * tensor; void * data; };
struct node_pins {
    hyper_weight_stream * p;
    std::vector<weight *> pinned;
    std::vector<restore_pointer> pointers;
    ~node_pins() {
        for (auto it = pointers.rbegin(); it != pointers.rend(); ++it) it->tensor->data = it->data;
        for (weight * w : pinned) unpin(w);
    }
    bool prepare(ggml_tensor * node) {
        // Admit a multiple-weight operation before pinning any operand. A
        // cached operand in the middle of the arena could otherwise divide
        // enough aggregate free space into unusable holes after it is pinned.
        std::vector<weight *> operands;
        size_t bytes = 0;
        bool missing = false;
        for (int s = -1; s < GGML_MAX_SRC; ++s) {
            ggml_tensor * tensor = s < 0 ? node : node->src[s];
            hyper_weight_stream * owner = provider_of(tensor);
            if (!owner) continue;
            if (owner != p) { set_error(p, HYPER_STREAM_UNSUPPORTED); return false; }
            auto record = p->tensors.find(root_of(tensor));
            if (record == p->tensors.end() || !record->second.entry) {
                set_error(p, HYPER_STREAM_INVALID); return false;
            }
            weight * w = record->second.entry;
            if (std::find(operands.begin(), operands.end(), w) != operands.end()) continue;
            size_t rounded = 0;
            if (!round_size(w->key.bytes, &rounded) || rounded > p->stats.cap_bytes - bytes) {
                set_error(p, HYPER_STREAM_TOO_LARGE); return false;
            }
            operands.push_back(w);
            bytes += rounded;
            missing |= !w->data;
        }
        if (operands.size() > 1 && missing) {
            // All public calls are serialized, and the previous node's pins
            // have been released. Check that invariant before reclaiming any
            // range. No tensor pointer has been changed for this node yet.
            for (const auto & item : p->weights) if (item.second->pins) {
                set_error(p, HYPER_STREAM_INVALID); return false;
            }
            for (auto & item : p->weights) evict(p, item.second.get());
        }
        return true;
    }
    bool materialize(ggml_tensor * tensor) {
        if (!tensor) return true;
        hyper_weight_stream * owner = provider_of(tensor);
        if (!owner) return true;
        if (owner != p) { set_error(p, HYPER_STREAM_UNSUPPORTED); return false; }
        ggml_tensor * root = root_of(tensor);
        auto it = p->tensors.find(root);
        if (it == p->tensors.end() || !it->second.entry) {
            set_error(p, HYPER_STREAM_INVALID); return false;
        }
        weight * w = it->second.entry;
        if (std::find(pinned.begin(), pinned.end(), w) == pinned.end()) {
            // Allocate bookkeeping before taking a pin, so allocation failure
            // cannot leak a pin and permanently prevent eviction.
            pinned.push_back(w);
            if (!pin(p, w)) { pinned.pop_back(); return false; }
        }
        std::vector<ggml_tensor *> chain;
        for (ggml_tensor * t = tensor; t; t = t->view_src) {
            if (chain.size() >= 64) { set_error(p, HYPER_STREAM_INVALID); return false; }
            chain.push_back(t);
        }
        size_t offset = 0;
        for (auto t = chain.rbegin(); t != chain.rend(); ++t) {
            if ((*t)->view_src) {
                if ((*t)->view_offs > w->key.bytes - offset) {
                    set_error(p, HYPER_STREAM_INVALID); return false;
                }
                offset += (*t)->view_offs;
            }
            if (offset > w->key.bytes || ggml_nbytes(*t) > w->key.bytes - offset) {
                set_error(p, HYPER_STREAM_INVALID); return false;
            }
            bool saved = false;
            for (auto & v : pointers) if (v.tensor == *t) { saved = true; break; }
            if (!saved) pointers.push_back({*t, (*t)->data});
            (*t)->data = static_cast<char *>(w->data) + offset;
        }
        return true;
    }
};
static bool weight_operation_allowed(const ggml_tensor * node) {
    switch (node->op) {
        case GGML_OP_NONE: case GGML_OP_VIEW: case GGML_OP_RESHAPE:
        case GGML_OP_TRANSPOSE: case GGML_OP_PERMUTE:
        case GGML_OP_GET_ROWS: case GGML_OP_MUL_MAT:
        case GGML_OP_ADD: case GGML_OP_MUL: case GGML_OP_DIV:
        case GGML_OP_CONT: case GGML_OP_DUP: return true;
        default: return false;
    }
}
static bool metadata_operation(const ggml_tensor * node) {
    switch (node->op) {
        case GGML_OP_NONE: case GGML_OP_VIEW: case GGML_OP_RESHAPE:
        case GGML_OP_TRANSPOSE: case GGML_OP_PERMUTE: return true;
        default: return false;
    }
}
static hyper_weight_stream * graph_provider(const ggml_cgraph * graph, bool * multiple) {
    hyper_weight_stream * found = nullptr;
    *multiple = false;
    if (!graph) return nullptr;
    for (int i = 0; i < graph->n_nodes; ++i) {
        ggml_tensor * node = graph->nodes[i];
        for (int s = -1; s < GGML_MAX_SRC; ++s) {
            hyper_weight_stream * p = provider_of(s < 0 ? node : node->src[s]);
            if (p && found && p != found) *multiple = true;
            if (p) found = p;
        }
    }
    return found;
}
}

extern "C" hyper_weight_stream * hyper_weight_stream_create(size_t cap,
    hyper_weight_stream_cancel_fn cancel, void * user) {
    if (!cap || cap > maximum_cap) return nullptr;
    std::unique_ptr<hyper_weight_stream> p(new (std::nothrow) hyper_weight_stream());
    if (!p) return nullptr;
    p->stats.cap_bytes = cap; p->cancel = cancel; p->user = user;
    p->buft.iface = buft_iface();
    p->buft.device = ggml_backend_reg_dev_get(ggml_backend_cpu_reg(), 0);
    p->buft.context = p.get();
    return p.release();
}
extern "C" void hyper_weight_stream_destroy(hyper_weight_stream * p) { if (p) unref(p); }
extern "C" ggml_backend_buffer_type_t hyper_weight_stream_buft(hyper_weight_stream * p) {
    return p ? &p->buft : nullptr;
}
extern "C" void hyper_weight_stream_begin_request(hyper_weight_stream * p) {
    if (!p) return;
    std::lock_guard<std::mutex> lock(p->mutex);
    if (p->stats.error_code == HYPER_STREAM_CANCELLED) p->stats.error_code = HYPER_STREAM_OK;
}
extern "C" bool hyper_weight_stream_is_buft(ggml_backend_buffer_type_t buft) {
    return buft && buft->iface.get_name == stream_name;
}
extern "C" bool hyper_weight_stream_is_tensor(const ggml_tensor * tensor) { return provider_of(tensor) != nullptr; }

extern "C" bool hyper_weight_stream_register(ggml_tensor * tensor, int fd, uint64_t offset, size_t bytes) {
    hyper_weight_stream * p = provider_of(tensor);
    if (!p) return false;
    std::lock_guard<std::mutex> lock(p->mutex);
    try {
        size_t rounded;
        if (cancelled(p)) return false;
        if (tensor->view_src || bytes != ggml_nbytes(tensor) || !round_size(bytes, &rounded)) {
            set_error(p, HYPER_STREAM_INVALID); return false;
        }
        if (rounded > p->stats.cap_bytes) { set_error(p, HYPER_STREAM_TOO_LARGE); return false; }
        auto record = p->tensors.find(tensor);
        if (record == p->tensors.end()) { set_error(p, HYPER_STREAM_INVALID); return false; }
        struct stat st;
        if (fd < 0 || fstat(fd, &st) != 0 || st.st_size < 0 || !S_ISREG(st.st_mode)) {
            set_error(p, HYPER_STREAM_IO); return false;
        }
        const uint64_t file_size = static_cast<uint64_t>(st.st_size);
        if (offset > file_size || bytes > file_size - offset ||
            offset > static_cast<uint64_t>(std::numeric_limits<off_t>::max()) ||
            bytes > static_cast<uint64_t>(std::numeric_limits<off_t>::max()) - offset) {
            set_error(p, HYPER_STREAM_INVALID); return false;
        }
        size_t file = 0;
        for (; file < p->files.size(); ++file) {
            if (p->files[file]->dev == st.st_dev && p->files[file]->ino == st.st_ino) break;
        }
        if (file == p->files.size()) {
            std::unique_ptr<source_file> source(new source_file{-1, st.st_dev, st.st_ino, file_size, st.st_mtim});
            source->fd = dup(fd);
            if (source->fd < 0) { set_error(p, HYPER_STREAM_IO); return false; }
            p->files.emplace_back(std::move(source));
            p->stats.open_files = p->files.size();
        } else if (!source_unchanged(p, *p->files[file])) {
            return false;
        }
        weight_key key{file, offset, bytes};
        auto entry = p->weights.find(key);
        if (record->second.entry) {
            if (entry != p->weights.end() && record->second.entry == entry->second.get()) return true;
            set_error(p, HYPER_STREAM_INVALID); return false;
        }
        if (entry == p->weights.end()) {
            std::unique_ptr<weight> w(new weight()); w->key = key;
            entry = p->weights.emplace(key, std::move(w)).first;
        }
        record->second.entry = entry->second.get();
        ++p->stats.tensors_registered;
        p->stats.unique_weights = p->weights.size();
        p->stats.max_weight_bytes = std::max(p->stats.max_weight_bytes, static_cast<uint64_t>(bytes));
        return true;
    } catch (...) { set_error(p, HYPER_STREAM_ALLOC); return false; }
}
extern "C" ggml_status hyper_weight_stream_validate(ggml_tensor * tensor, bool * valid) {
    if (valid) *valid = false;
    hyper_weight_stream * p = provider_of(tensor);
    if (!p || !valid) return GGML_STATUS_FAILED;
    std::lock_guard<std::mutex> lock(p->mutex);
    auto found = p->tensors.find(tensor);
    if (found == p->tensors.end() || !found->second.entry) {
        set_error(p, HYPER_STREAM_INVALID); return GGML_STATUS_FAILED;
    }
    weight * w = found->second.entry;
    if (!pin(p, w)) return failure_status(p);
    *valid = ggml_validate_row_data(tensor->type, w->data, w->key.bytes);
    unpin(w);
    if (cancelled(p)) return failure_status(p);
    return GGML_STATUS_SUCCESS;
}
extern "C" void hyper_weight_stream_get_stats(const hyper_weight_stream * p, hyper_weight_stream_stats * out) {
    if (!out) return;
    *out = {};
    if (!p) return;
    std::lock_guard<std::mutex> lock(p->mutex);
    *out = p->stats;
}
extern "C" bool hyper_weight_stream_graph_uses(const ggml_cgraph * graph) {
    bool multiple;
    return graph_provider(graph, &multiple) != nullptr;
}
extern "C" ggml_status hyper_weight_stream_compute(ggml_cgraph * graph,
    hyper_weight_stream_compute_fn compute, void * user, bool * handled) {
    if (!handled) return GGML_STATUS_FAILED;
    bool multiple;
    hyper_weight_stream * p = graph_provider(graph, &multiple);
    *handled = p != nullptr;
    if (!p) return GGML_STATUS_SUCCESS;
    std::lock_guard<std::mutex> lock(p->mutex);
    if (multiple || !compute) { set_error(p, HYPER_STREAM_UNSUPPORTED); return GGML_STATUS_FAILED; }
    try {
        for (int i = 0; i < graph->n_nodes; ++i) {
            if (cancelled(p)) return failure_status(p);
            ggml_tensor * node = graph->nodes[i];
            // Weights are immutable, even if a caller constructs an in-place op.
            if (provider_of(node) && !metadata_operation(node)) {
                set_error(p, HYPER_STREAM_UNSUPPORTED); return GGML_STATUS_FAILED;
            }
            bool uses_weight = provider_of(node) != nullptr;
            for (ggml_tensor * source : node->src) uses_weight |= provider_of(source) != nullptr;
            if (uses_weight && !weight_operation_allowed(node)) {
                set_error(p, HYPER_STREAM_UNSUPPORTED); return GGML_STATUS_FAILED;
            }
            node_pins pins{p, {}, {}};
            if (!pins.prepare(node)) return failure_status(p);
            // Only immediate sources and their view_src chains. Never traverse
            // src ancestors: doing so would eagerly pin the entire model.
            for (ggml_tensor * source : node->src) {
                if (!pins.materialize(source)) return failure_status(p);
            }
            if (!pins.materialize(node)) return failure_status(p);
            ggml_cgraph one = ggml_graph_view(graph, i, i + 1);
            const ggml_status status = compute(user, &one);
            if (status != GGML_STATUS_SUCCESS) return status;
            ++p->stats.nodes_computed;
            // pins destructor restores all aliases and releases entries only
            // after the synchronous CPU graph-compute barrier has completed.
        }
        return GGML_STATUS_SUCCESS;
    } catch (...) { set_error(p, HYPER_STREAM_ALLOC); return GGML_STATUS_ALLOC_FAILED; }
}
