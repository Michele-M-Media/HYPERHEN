/* Host tests: jailbreak hijack retry stop condition. */
#include "test_harness.h"

#include <onion/hijack_retry.h>

static int test_alive_retries_until_max(void) {
  /* Process alive → keep retrying until attempt reaches max. */
  TEST_ASSERT_TRUE(!onion_hijack_retry_should_stop(true, 1, 30));
  TEST_ASSERT_TRUE(!onion_hijack_retry_should_stop(true, 29, 30));
  TEST_ASSERT_TRUE(onion_hijack_retry_should_stop(true, 30, 30));
  TEST_ASSERT_TRUE(onion_hijack_retry_should_stop(true, 31, 30));
  return 0;
}

static int test_dead_process_stops_immediately(void) {
  /* Dead process → stop even on first attempt. */
  TEST_ASSERT_TRUE(onion_hijack_retry_should_stop(false, 1, 30));
  TEST_ASSERT_TRUE(onion_hijack_retry_should_stop(false, 5, 30));
  return 0;
}

static int test_inverted_legacy_bug_regression(void) {
  /*
   * Old bug: stop when process_alive (first fail aborts live targets).
   * Correct: alive + under max → do NOT stop.
   */
  const bool process_alive = true;
  const int attempt = 1;
  TEST_ASSERT_TRUE(
      !onion_hijack_retry_should_stop(process_alive, attempt, 30));
  return 0;
}

int test_hijack_retry_suite(void) {
  int failures = 0;
  failures +=
      onion_test_run("hijack_retry.alive_until_max", test_alive_retries_until_max);
  failures +=
      onion_test_run("hijack_retry.dead_stops", test_dead_process_stops_immediately);
  failures += onion_test_run("hijack_retry.alive_not_abort",
                             test_inverted_legacy_bug_regression);
  return failures;
}
