#include "test_harness.h"

#include <onion/firmware_capabilities.h>

static int test_exact_routes(void) {
  static const uint32_t legacy[] = {
      0x01000000U, 0x01010000U, 0x01020000U, 0x01050000U, 0x01100000U,
      0x01110000U, 0x01120000U, 0x01130000U, 0x01140000U, 0x02000000U,
      0x02200000U, 0x02250000U, 0x02260000U, 0x02300000U, 0x02500000U,
      0x02700000U, 0x03000000U, 0x03100000U, 0x03200000U, 0x03210000U,
      0x04000000U, 0x04020000U, 0x04030000U, 0x04500000U, 0x04510000U,
  };
  static const uint32_t hypervisor_5xx[] = {
      0x05000000U, 0x05020000U, 0x05100000U, 0x05500000U,
  };
  static const uint32_t a53_ppr[] = {
      0x06000000U, 0x06020000U, 0x06500000U, 0x07000000U, 0x07010000U,
      0x07200000U, 0x07400000U, 0x07600000U, 0x07610000U, 0x08000000U,
      0x08200000U, 0x08400000U, 0x08600000U, 0x09000000U, 0x09050000U,
      0x09200000U, 0x09400000U, 0x09600000U, 0x10000000U, 0x10010000U,
      0x10200000U, 0x10400000U, 0x10600000U, 0x11000000U, 0x11200000U,
      0x11400000U,
  };
  static const uint32_t degraded[] = {
      0x11600000U, 0x12000000U, 0x12020000U, 0x12200000U,
      0x12400000U, 0x12600000U, 0x12700000U,
  };
  static const uint32_t hypervisor_13xx[] = {
      0x13000000U, 0x13200000U, 0x13600000U,
  };

  for (size_t i = 0; i < sizeof(legacy) / sizeof(legacy[0]); ++i) {
    TEST_ASSERT_EQ_U64(ONION_FIRMWARE_ROUTE_LEGACY_HEN,
                       onion_firmware_route(legacy[i]));
  }
  for (size_t i = 0;
       i < sizeof(hypervisor_5xx) / sizeof(hypervisor_5xx[0]); ++i) {
    TEST_ASSERT_EQ_U64(ONION_FIRMWARE_ROUTE_HYPERVISOR_5XX,
                       onion_firmware_route(hypervisor_5xx[i]));
  }
  for (size_t i = 0; i < sizeof(a53_ppr) / sizeof(a53_ppr[0]); ++i) {
    TEST_ASSERT_EQ_U64(ONION_FIRMWARE_ROUTE_A53_PPR,
                       onion_firmware_route(a53_ppr[i]));
  }

  for (size_t i = 0; i < sizeof(degraded) / sizeof(degraded[0]); ++i) {
    TEST_ASSERT_EQ_U64(ONION_FIRMWARE_ROUTE_HYPERCORE_NO_PPR,
                       onion_firmware_route(degraded[i]));
    TEST_ASSERT_EQ_U64(ONION_FIRMWARE_ROUTE_HYPERCORE_NO_PPR,
                       onion_firmware_route(degraded[i] | 0x23U));
  }
  for (size_t i = 0;
       i < sizeof(hypervisor_13xx) / sizeof(hypervisor_13xx[0]); ++i) {
    TEST_ASSERT_EQ_U64(ONION_FIRMWARE_ROUTE_HYPERVISOR_13XX,
                       onion_firmware_route(hypervisor_13xx[i]));
  }

  TEST_ASSERT_EQ_U64(ONION_FIRMWARE_ROUTE_USERLAND_ONLY,
                     onion_firmware_route(0x13400000U));
  TEST_ASSERT_EQ_U64(ONION_FIRMWARE_ROUTE_USERLAND_ONLY,
                     onion_firmware_route(0x13420000U));
  return 0;
}

static int test_capabilities_fail_closed(void) {
  const uint32_t legacy_caps =
      onion_firmware_capabilities(ONION_FIRMWARE_ROUTE_LEGACY_HEN);
  TEST_ASSERT_TRUE((legacy_caps & ONION_FIRMWARE_CAP_LEGACY_HEN) != 0);
  TEST_ASSERT_TRUE((legacy_caps & ONION_FIRMWARE_CAP_AUTOMOUNT) != 0);
  TEST_ASSERT_TRUE((legacy_caps & ONION_FIRMWARE_CAP_BACKPORT) != 0);
  TEST_ASSERT_TRUE((legacy_caps & ONION_FIRMWARE_CAP_HYPERCORE) == 0);

  const uint32_t ppr_caps =
      onion_firmware_capabilities(ONION_FIRMWARE_ROUTE_A53_PPR);
  TEST_ASSERT_TRUE((ppr_caps & ONION_FIRMWARE_CAP_HYPERCORE) != 0);
  TEST_ASSERT_TRUE((ppr_caps & ONION_FIRMWARE_CAP_PPR) != 0);
  TEST_ASSERT_TRUE((ppr_caps & ONION_FIRMWARE_CAP_AUTOMOUNT) != 0);
  TEST_ASSERT_TRUE((ppr_caps & ONION_FIRMWARE_CAP_BACKPORT) != 0);

  const uint32_t hypervisor_caps =
      onion_firmware_capabilities(ONION_FIRMWARE_ROUTE_HYPERVISOR_13XX);
  TEST_ASSERT_TRUE((hypervisor_caps & ONION_FIRMWARE_CAP_HYPERCORE) != 0);
  TEST_ASSERT_TRUE((hypervisor_caps & ONION_FIRMWARE_CAP_HYPERVISOR) != 0);
  TEST_ASSERT_TRUE((hypervisor_caps & ONION_FIRMWARE_CAP_PPR) == 0);
  TEST_ASSERT_TRUE((hypervisor_caps & ONION_FIRMWARE_CAP_AUTOMOUNT) != 0);
  TEST_ASSERT_TRUE((hypervisor_caps & ONION_FIRMWARE_CAP_BACKPORT) != 0);

  const uint32_t degraded_caps = onion_firmware_capabilities(
      ONION_FIRMWARE_ROUTE_HYPERCORE_NO_PPR);
  TEST_ASSERT_TRUE((degraded_caps & ONION_FIRMWARE_CAP_HYPERCORE) != 0);
  TEST_ASSERT_TRUE((degraded_caps & ONION_FIRMWARE_CAP_AUTOMOUNT) != 0);
  TEST_ASSERT_TRUE((degraded_caps & ONION_FIRMWARE_CAP_BACKPORT) != 0);
  TEST_ASSERT_TRUE((degraded_caps & ONION_FIRMWARE_CAP_TOOLBOX) != 0);
  TEST_ASSERT_TRUE((degraded_caps & ONION_FIRMWARE_CAP_PPR) == 0);
  TEST_ASSERT_TRUE((degraded_caps & ONION_FIRMWARE_CAP_HYPERVISOR) == 0);

  const uint32_t userland_caps = onion_firmware_capabilities(
      ONION_FIRMWARE_ROUTE_USERLAND_ONLY);
  TEST_ASSERT_EQ_U64(ONION_FIRMWARE_CAP_UTIL, userland_caps);
  TEST_ASSERT_TRUE((userland_caps & ONION_FIRMWARE_CAP_AUTOMOUNT) == 0);
  TEST_ASSERT_TRUE((userland_caps & ONION_FIRMWARE_CAP_BACKPORT) == 0);

  TEST_ASSERT_EQ_U64(ONION_FIRMWARE_ROUTE_UNSUPPORTED,
                     onion_firmware_route(0x11500000U));
  TEST_ASSERT_EQ_U64(ONION_FIRMWARE_ROUTE_UNSUPPORTED,
                     onion_firmware_route(0x11700000U));
  TEST_ASSERT_EQ_U64(ONION_FIRMWARE_ROUTE_UNSUPPORTED,
                     onion_firmware_route(0x12800000U));
  TEST_ASSERT_EQ_U64(ONION_FIRMWARE_ROUTE_UNSUPPORTED,
                     onion_firmware_route(0x14000000U));
  TEST_ASSERT_EQ_U64(0, onion_firmware_capabilities(
                            ONION_FIRMWARE_ROUTE_UNSUPPORTED));
  return 0;
}

int test_firmware_capabilities_suite(void) {
  int failures = 0;
  failures += onion_test_run("firmware_capabilities.exact_routes",
                             test_exact_routes);
  failures += onion_test_run("firmware_capabilities.fail_closed",
                             test_capabilities_fail_closed);
  return failures;
}
