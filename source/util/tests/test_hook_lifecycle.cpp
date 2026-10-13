/* Host tests for the Installing -> Ready hook barrier. */
#include "test_harness.h"
#include "hook_lifecycle.hpp"

static int test_hook_lifecycle_barrier() {
  shellui_hooks_publish_failed();
  TEST_ASSERT_TRUE(!shellui_hooks_are_ready());
  shellui_hooks_begin_install();
  TEST_ASSERT_TRUE(!shellui_hooks_are_ready());
  shellui_hooks_publish_ready();
  TEST_ASSERT_TRUE(shellui_hooks_are_ready());
  shellui_hooks_publish_failed();
  TEST_ASSERT_TRUE(!shellui_hooks_are_ready());
  return 0;
}

extern "C" int test_hook_lifecycle_suite(void) {
  return onion_test_run("hook_lifecycle.barrier", test_hook_lifecycle_barrier);
}
