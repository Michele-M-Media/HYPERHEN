/* libFuzzer: every generated decoder plus the header parser, under ASan/UBSan. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "ava1_frame.h"
#include "ava1_gen.h"
#include "ava1_noise.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    static uint8_t out[1 << 17], out2[1 << 17];
    size_t n = 0, n2 = 0;
    const char *name;
    if (size == 0) return 0;
    name = ava1_message_names[data[0] % ava1_message_count];
    if (ava1_roundtrip(name, data + 1, size - 1, out, sizeof out, &n) == 0) {
        if (ava1_roundtrip(name, out, n, out2, sizeof out2, &n2) != 0 || n2 != n || memcmp(out, out2, n) != 0)
            __builtin_trap();
    }
    {
        /* Noise message 1 as the responder sees it: the first bytes an attacker controls. */
        ava1_noise_t ns;
        ava1_identity_t st, eph;
        uint8_t k[32], pl[1024];
        size_t pn;
        memset(k, 1, sizeof k);
        ava1_identity_from_secret(&st, k);
        k[0] = 2;
        ava1_identity_from_secret(&eph, k);
        ava1_noise_init(&ns, 0, &st, &eph, NULL, 0);
        (void)ava1_noise_read(&ns, data, size, pl, sizeof pl, &pn);
    }
    if (size >= AVA1_HEADER_LEN) {
        ava1_header_t h;
        uint8_t back[AVA1_HEADER_LEN];
        if (ava1_header_decode(data, &h) == 0) {
            ava1_header_encode(&h, back);
            if (memcmp(back, data, AVA1_HEADER_LEN) != 0) __builtin_trap();
        }
    }
    return 0;
}
