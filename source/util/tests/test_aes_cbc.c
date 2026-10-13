/* Host tests for MC4 AES-256-CBC round-trip. */
#include "test_harness.h"

#include "mc4/aes.h"

#include <string.h>

/* Same key/IV as Mc4CheatParser / test_support. */
static const uint8_t MC4_KEY[] = "304c6528f659c766110239a51cl5dd9c";
static const uint8_t MC4_IV[] = "u@}kzW2u[u(8DWar";

static int test_roundtrip_block(void) {
  uint8_t plain[32];
  uint8_t work[32];
  uint8_t iv_enc[16];
  uint8_t iv_dec[16];
  struct AES_ctx ctx;

  memset(plain, 0, sizeof(plain));
  memcpy(plain, "hello AES-CBC MC4!!", 19); /* pad zeros to 32 */

  memcpy(work, plain, sizeof(work));
  memcpy(iv_enc, MC4_IV, 16);
  AES_init_ctx_iv(&ctx, MC4_KEY, iv_enc);
  AES_CBC_encrypt_buffer(&ctx, work, sizeof(work));

  TEST_ASSERT_TRUE(memcmp(work, plain, sizeof(work)) != 0);

  memcpy(iv_dec, MC4_IV, 16);
  AES_init_ctx_iv(&ctx, MC4_KEY, iv_dec);
  AES_CBC_decrypt_buffer(&ctx, work, sizeof(work));

  TEST_ASSERT_MEMEQ(plain, work, sizeof(plain));
  return 0;
}

static int test_known_changes(void) {
  uint8_t a[16] = {0};
  uint8_t b[16] = {0};
  struct AES_ctx ctx;
  uint8_t iv[16];

  memcpy(iv, MC4_IV, 16);
  AES_init_ctx_iv(&ctx, MC4_KEY, iv);
  AES_CBC_encrypt_buffer(&ctx, a, 16);

  memcpy(iv, MC4_IV, 16);
  AES_init_ctx_iv(&ctx, MC4_KEY, iv);
  b[0] = 1;
  AES_CBC_encrypt_buffer(&ctx, b, 16);

  TEST_ASSERT_TRUE(memcmp(a, b, 16) != 0);
  return 0;
}

int test_aes_cbc_suite(void) {
  int failures = 0;
  failures += onion_test_run("aes_cbc.roundtrip", test_roundtrip_block);
  failures += onion_test_run("aes_cbc.plaintext_diff", test_known_changes);
  return failures;
}
