/* libFuzzer: manifest pages into the store, journals through replay, bundles through their
 * record iterators — the parsers that see peer-controlled bytes.
 *
 * The journal record's CRC32C is computed here on purpose: a random record passes the walk's
 * check with probability 2^-32, so raw bytes would leave the replay visitor (the parser under
 * test) unreachable. The raw bytes follow the valid record to exercise the framing checks. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ava1_frame.h"
#include "ava1_gen.h"
#include "ava1_journal.h"
#include "ava1_manifest.h"
#include "ava1_ranges.h"
#include "ava1_wire.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static int visit(void *ctx, uint8_t kind, const uint8_t *body, size_t len) {
    ava1_jnl_batch_t b;
    (void)ctx;
    if (kind == AVA1_JNL_BATCH && ava1_jnl_batch_decode(body, len, &b) == 0) {
        ava1_r_t it;
        ava1_file_range_t g;
        ava1_r_init(&it, b.ranges, b.ranges_len);
        while (ava1_file_range_next(&it, &g) == 1) {
        }
    }
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 1) return 0;
    switch (data[0] % 3) {
    case 0: {
        ava1_manifest_page_t p;
        ava1_mstore_t m;
        memset(&m, 0, sizeof m);
        if (ava1_manifest_page_decode(data + 1, size - 1, &p) == 0) {
            uint8_t h[32];
            (void)ava1_mstore_add_page(&m, &p);
            ava1_mstore_hash(&m, h);
        }
        ava1_mstore_free(&m);
        break;
    }
    case 1: {
        char dir[] = "/tmp/ava1-fuzz-jnl-XXXXXX";
        char p[64];
        ava1_jnl_t j;
        uint8_t *rec;
        size_t body = size - 1;
        FILE *f;
        if (body > 60000) body = 60000; /* keep the length field honest for one record */
        rec = malloc(9 + body);
        if (!rec || !mkdtemp(dir)) {
            free(rec);
            return 0;
        }
        snprintf(p, sizeof p, "%s/journal", dir);
        rec[0] = (uint8_t)(1 + body);
        rec[1] = (uint8_t)((1 + body) >> 8);
        rec[2] = (uint8_t)((1 + body) >> 16);
        rec[3] = (uint8_t)((1 + body) >> 24);
        rec[4] = AVA1_JNL_BATCH;
        memcpy(rec + 5, data + 1, body);
        {
            /* Little-endian like every journal field, whatever the host's byte order. */
            uint32_t crc = ava1_crc32c(rec + 4, 1 + body);
            rec[5 + body] = (uint8_t)crc;
            rec[6 + body] = (uint8_t)(crc >> 8);
            rec[7 + body] = (uint8_t)(crc >> 16);
            rec[8 + body] = (uint8_t)(crc >> 24);
        }
        f = fopen(p, "wb");
        if (f) {
            fwrite("AVA1JNL1", 1, 8, f);
            fwrite(rec, 1, 9 + body, f);      /* CRC-valid: the body reaches the decoder */
            fwrite(data + 1, 1, size - 1, f); /* raw tail: the framing/CRC checks */
            fclose(f);
            if (ava1_jnl_open(&j, dir, visit, NULL) == 0) ava1_jnl_close(&j);
        }
        unlink(p);
        rmdir(dir);
        free(rec);
        break;
    }
    default: {
        ava1_bundle_t b;
        if (ava1_bundle_decode(data + 1, size - 1, &b) == 0) {
            ava1_r_t it;
            ava1_bundle_record_t r;
            ava1_r_init(&it, b.records, b.records_len);
            while (ava1_bundle_record_next(&it, &r) == 1) {
                if (r.data_len && !r.data) __builtin_trap();
            }
        }
        break;
    }
    }
    return 0;
}
