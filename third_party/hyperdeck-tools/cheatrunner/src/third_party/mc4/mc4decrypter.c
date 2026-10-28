/* Copyright (C) 2025 etaHEN / LightningMods

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 3, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING. If not, see
<http://www.gnu.org/licenses/>.  */

#include "mc4decrypter.h"
#include <stdarg.h>
#include <stdio.h>
#include "../../cr_log.h"

const unsigned char MC4_AES256CBC_KEY[] = "304c6528f659c766110239a51cl5dd9c";
const unsigned char MC4_AES256CBC_IV[]  = "u@}kzW2u[u(8DWar";

/* Forward etaHEN log calls to CheatRunner's cr_log (dashboard + klog). */
static void etaHEN_log(const char *fmt, ...) {
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  cr_log("debug", "mc4", "%s", buf);
}

uint8_t* decrypt_data(uint8_t* data, size_t* size)
{
        uint8_t* bin_data;
        size_t bin_size;
        struct AES_ctx ctx;

        etaHEN_log("b64 input: %zu bytes", *size);

        bin_data = base64_decode(data, *size, &bin_size);
        if (!bin_data) {
                cr_log("error", "mc4", "base64_decode failed — input may be corrupt or not a valid mc4 file");
                return NULL;
        }

        etaHEN_log("b64 decoded: %zu bytes (AES-256-CBC ciphertext)", bin_size);

        /* Extra headroom: AES_CBC_decrypt_buffer requires the buffer to be
         * a multiple of AES_BLOCKLEN. bin_size from base64_decode is already
         * correct, but we pad to the next block boundary for safety. */
        size_t new_buff_size = bin_size + AES_BLOCKLEN;
        uint8_t* bin_data_2 = calloc(new_buff_size, sizeof(uint8_t));
        if (!bin_data_2) {
                cr_log("error", "mc4", "calloc(%zu) failed during decrypt", new_buff_size);
                free(bin_data);
                return NULL;
        }
        memcpy(bin_data_2, bin_data, bin_size);
        free(bin_data);
        bin_data = bin_data_2;

        AES_init_ctx_iv(&ctx, MC4_AES256CBC_KEY, MC4_AES256CBC_IV);
        AES_CBC_decrypt_buffer(&ctx, bin_data, bin_size);

        /* Strip PKCS7 padding (1–AES_BLOCKLEN bytes, all equal to pad length). */
        if (bin_size > 0) {
                uint8_t pad = bin_data[bin_size - 1];
                if (pad >= 1 && pad <= AES_BLOCKLEN) {
                        int valid = 1;
                        for (size_t i = bin_size - pad; i < bin_size; i++)
                                if (bin_data[i] != pad) { valid = 0; break; }
                        if (valid) {
                                etaHEN_log("pkcs7 stripped: %u padding byte(s)", (unsigned)pad);
                                bin_size -= pad;
                        } else {
                                cr_log("warn", "mc4", "pkcs7 pad byte=0x%02x but bytes don't match — skipping strip", pad);
                        }
                }
        }

        *size = bin_size;
        etaHEN_log("decrypt ok: plaintext %zu bytes, first char='%c' (0x%02x)",
                   bin_size,
                   (bin_size > 0 && bin_data[0] >= 0x20) ? bin_data[0] : '?',
                   bin_size > 0 ? bin_data[0] : 0);
        return bin_data;
}

uint8_t* encrypt_data(uint8_t* data, size_t* size)
{
        uint8_t* b64_data;
        size_t b64_size;
        struct AES_ctx ctx;

        etaHEN_log("[*] Total XML Size: %zu bytes", *size);

        AES_init_ctx_iv(&ctx, MC4_AES256CBC_KEY, MC4_AES256CBC_IV);
        AES_CBC_encrypt_buffer(&ctx, data, *size);

        b64_data = base64_encode(data, *size, &b64_size);
        if (!b64_data)
        {
                etaHEN_log("Base64 Error!");
                return data;
        }

        *size = b64_size;

        etaHEN_log("[*] Total Encrypted Size: %zu bytes", b64_size);
        etaHEN_log("[*] Encrypted File Successfully!");
        return b64_data;
}
