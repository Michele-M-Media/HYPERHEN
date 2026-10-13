#include "test_harness.h"

#include <onion/hyperlinux_profiles.h>

static int test_exact_supported_profiles(void) {
  static const uint32_t supported[] = {
      0x03000000U, 0x03100000U, 0x03200000U, 0x03210000U, 0x04000000U,
      0x04020000U, 0x04030000U, 0x04500000U, 0x04510000U, 0x05000000U,
      0x05020000U, 0x05100000U, 0x05500000U, 0x06000000U, 0x06020000U,
      0x06500000U, 0x07000000U, 0x07010000U, 0x07200000U, 0x07400000U,
      0x07600000U, 0x07610000U, 0x13200000U,
  };

  for (size_t i = 0; i < sizeof(supported) / sizeof(supported[0]); ++i) {
    TEST_ASSERT_TRUE(onion_hyperlinux_firmware_supported(supported[i]));
    TEST_ASSERT_TRUE(
        onion_hyperlinux_firmware_supported(supported[i] | 0x00000006U));
  }
  return 0;
}

static int test_unverified_profiles_fail_closed(void) {
  static const uint32_t unsupported[] = {
      0x01000000U, 0x02500000U, 0x03010000U, 0x04100000U,
      0x07620000U, 0x08000000U, 0x13000000U, 0x13600000U,
  };

  for (size_t i = 0; i < sizeof(unsupported) / sizeof(unsupported[0]); ++i)
    TEST_ASSERT_TRUE(!onion_hyperlinux_firmware_supported(unsupported[i]));
  return 0;
}

int test_hyperlinux_profiles_suite(void) {
  int failures = 0;
  failures += onion_test_run("hyperlinux.exact_profiles",
                             test_exact_supported_profiles);
  failures += onion_test_run("hyperlinux.fail_closed",
                             test_unverified_profiles_fail_closed);
  return failures;
}
