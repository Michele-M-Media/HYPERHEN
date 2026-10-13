/* Host tests for MC4/shared base64 codec. */
#include "test_harness.h"

#include "mc4/base64.h"

#include <stdlib.h>
#include <string.h>

static int test_encode_known(void) {
  size_t out_len = 0;
  unsigned char *enc =
      base64_encode((const unsigned char *)"hello", 5, &out_len);
  TEST_ASSERT_TRUE(enc != NULL);
  /* RFC base64: "hello" → aGVsbG8= ; implementation may append '\n' */
  TEST_ASSERT_TRUE(strncmp((const char *)enc, "aGVsbG8=", 8) == 0);
  free(enc);
  return 0;
}

static int test_decode_known(void) {
  size_t out_len = 0;
  unsigned char *dec =
      base64_decode((const unsigned char *)"aGVsbG8=", 8, &out_len);
  TEST_ASSERT_TRUE(dec != NULL);
  TEST_ASSERT_EQ_U64(5, out_len);
  TEST_ASSERT_MEMEQ("hello", dec, 5);
  free(dec);
  return 0;
}

static int test_roundtrip(void) {
  const unsigned char src[] = {0x00, 0x01, 0xFE, 0xFF, 'A', 'z'};
  size_t elen = 0, dlen = 0;
  unsigned char *enc = base64_encode(src, sizeof(src), &elen);
  TEST_ASSERT_TRUE(enc != NULL);
  unsigned char *dec = base64_decode(enc, elen, &dlen);
  TEST_ASSERT_TRUE(dec != NULL);
  TEST_ASSERT_EQ_U64(sizeof(src), dlen);
  TEST_ASSERT_MEMEQ(src, dec, sizeof(src));
  free(enc);
  free(dec);
  return 0;
}

static int test_empty(void) {
  size_t out_len = 0;
  unsigned char *enc = base64_encode((const unsigned char *)"", 0, &out_len);
  TEST_ASSERT_TRUE(enc != NULL);
  free(enc);

  unsigned char *dec =
      base64_decode((const unsigned char *)"", 0, &out_len);
  /* empty decode may return empty alloc or NULL depending on impl */
  free(dec);
  return 0;
}

int test_base64_suite(void) {
  int failures = 0;
  failures += onion_test_run("base64.encode_known", test_encode_known);
  failures += onion_test_run("base64.decode_known", test_decode_known);
  failures += onion_test_run("base64.roundtrip", test_roundtrip);
  failures += onion_test_run("base64.empty", test_empty);
  return failures;
}
