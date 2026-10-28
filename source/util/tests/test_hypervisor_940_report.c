#include "test_harness.h"

#include <onion/hypervisor_940_report.h>

static int test_report_requires_exact_success(void) {
  static const char success[] =
      "PHU-HV940 result\n>>> HV DEFEATED\n";
  static const char partial[] =
      "PHU-HV940 result\nstatus: PARTIAL\n";
  static const char near_match[] =
      "PHU-HV940 result\n>> HV DEFEATED\n";
  static const char notification_only[] =
      "PHU-HV940 result\nHV DEFEATED !!\n";

  TEST_ASSERT_TRUE(onion_hypervisor_940_report_ready(
      success, sizeof(success) - 1));
  TEST_ASSERT_TRUE(!onion_hypervisor_940_report_ready(
      partial, sizeof(partial) - 1));
  TEST_ASSERT_TRUE(!onion_hypervisor_940_report_ready(
      near_match, sizeof(near_match) - 1));
  TEST_ASSERT_TRUE(!onion_hypervisor_940_report_ready(
      notification_only, sizeof(notification_only) - 1));
  TEST_ASSERT_TRUE(!onion_hypervisor_940_report_ready(NULL, 0));
  return 0;
}

int test_hypervisor_940_report_suite(void) {
  return onion_test_run("hypervisor_940_report.exact_success",
                        test_report_requires_exact_success);
}
