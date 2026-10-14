/* Host tests for onion_ready protocol + runtime flags. */
#include "test_harness.h"
#include <onion/ready.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int test_ready_signal_wait_clear(void) {
  const char *name = "host_test_marker";
  onion_ready_clear(name);
  TEST_ASSERT_TRUE(!onion_ready_is_set(name));
  TEST_ASSERT_TRUE(onion_ready_signal(name));
  TEST_ASSERT_TRUE(onion_ready_is_set(name));
  TEST_ASSERT_TRUE(onion_ready_wait(name, 100, 50));
  TEST_ASSERT_TRUE(onion_ready_clear(name));
  TEST_ASSERT_TRUE(!onion_ready_is_set(name));
  return 0;
}

static int test_ready_reject_slash(void) {
  TEST_ASSERT_TRUE(!onion_ready_signal("../evil"));
  TEST_ASSERT_TRUE(!onion_ready_is_set("../evil"));
  TEST_ASSERT_TRUE(!onion_ready_signal("a/b"));
  TEST_ASSERT_TRUE(!onion_ready_signal("dot.name"));
  TEST_ASSERT_TRUE(!onion_ready_signal(""));
  TEST_ASSERT_TRUE(!onion_ready_signal(NULL));
  return 0;
}

static int test_ready_timeout(void) {
  onion_ready_clear("never_set_xyz");
  TEST_ASSERT_TRUE(!onion_ready_wait("never_set_xyz", 100, 50));
  return 0;
}

static int test_ready_path_builder(void) {
  char buf[128];
  TEST_ASSERT_TRUE(onion_ready_path(ONION_FLAG_FPS_OVERLAY, buf, sizeof(buf)));
  TEST_ASSERT_TRUE(strstr(buf, "fps_overlay") != NULL);
  TEST_ASSERT_TRUE(strstr(buf, "/tmp/hyperhen/ready/") != NULL);
  /* buffer too small */
  TEST_ASSERT_TRUE(!onion_ready_path(ONION_FLAG_FPS_OVERLAY, buf, 8));
  TEST_ASSERT_TRUE(!onion_ready_path(ONION_FLAG_FPS_OVERLAY, NULL, 64));
  return 0;
}

static int test_runtime_flag_fps_overlay(void) {
  onion_ready_clear(ONION_FLAG_FPS_OVERLAY);
  TEST_ASSERT_TRUE(!onion_ready_is_set(ONION_FLAG_FPS_OVERLAY));
  TEST_ASSERT_TRUE(onion_ready_signal(ONION_FLAG_FPS_OVERLAY));
  TEST_ASSERT_TRUE(onion_ready_is_set(ONION_FLAG_FPS_OVERLAY));
  TEST_ASSERT_TRUE(onion_ready_clear(ONION_FLAG_FPS_OVERLAY));
  TEST_ASSERT_TRUE(!onion_ready_is_set(ONION_FLAG_FPS_OVERLAY));
  return 0;
}

static int test_runtime_flag_util_booted(void) {
  onion_ready_clear(ONION_FLAG_UTIL_BOOTED);
  TEST_ASSERT_TRUE(onion_ready_signal(ONION_FLAG_UTIL_BOOTED));
  TEST_ASSERT_TRUE(onion_ready_is_set(ONION_FLAG_UTIL_BOOTED));
  onion_ready_clear(ONION_FLAG_UTIL_BOOTED);
  return 0;
}

static int test_toolbox_marker_under_runtime_root(void) {
  char path[128];
  onion_ready_clear(ONION_READY_TOOLBOX);
  TEST_ASSERT_TRUE(onion_ready_path(ONION_READY_TOOLBOX, path, sizeof(path)));
  TEST_ASSERT_TRUE(strstr(path, "/tmp/hyperhen/ready/toolbox") != NULL);
  TEST_ASSERT_TRUE(!onion_ready_is_set(ONION_READY_TOOLBOX));
  TEST_ASSERT_TRUE(onion_ready_signal(ONION_READY_TOOLBOX));
  TEST_ASSERT_TRUE(onion_ready_is_set(ONION_READY_TOOLBOX));
  onion_ready_clear(ONION_READY_TOOLBOX);
  TEST_ASSERT_TRUE(!onion_ready_is_set(ONION_READY_TOOLBOX));
  return 0;
}

static int test_process_instance_marker(void) {
  const char *name = "host_pid_marker";
  const pid_t expected = (pid_t)4242;
  pid_t actual = -1;

  onion_ready_clear(name);
  TEST_ASSERT_TRUE(onion_ready_signal_pid(name, expected));
  TEST_ASSERT_TRUE(onion_ready_is_set(name));
  TEST_ASSERT_TRUE(onion_ready_read_pid(name, &actual));
  TEST_ASSERT_EQ_INT((int)expected, (int)actual);
  TEST_ASSERT_TRUE(onion_ready_matches_pid(name, expected));
  TEST_ASSERT_TRUE(!onion_ready_matches_pid(name, (pid_t)4243));
  TEST_ASSERT_TRUE(onion_ready_wait_pid(name, expected, 100, 50));
  TEST_ASSERT_TRUE(!onion_ready_wait_pid(name, (pid_t)4243, 100, 50));
  onion_ready_clear(name);
  return 0;
}

static int test_process_instance_rejects_plain_marker_value(void) {
  const char *name = "host_plain_marker";
  pid_t actual = -1;

  onion_ready_clear(name);
  TEST_ASSERT_TRUE(onion_ready_signal(name));
  TEST_ASSERT_TRUE(onion_ready_is_set(name));
  TEST_ASSERT_TRUE(onion_ready_read_pid(name, &actual));
  TEST_ASSERT_EQ_INT(1, (int)actual);
  TEST_ASSERT_TRUE(!onion_ready_matches_pid(name, (pid_t)4242));
  TEST_ASSERT_TRUE(!onion_ready_signal_pid(name, (pid_t)0));
  TEST_ASSERT_TRUE(!onion_ready_read_pid(name, NULL));
  onion_ready_clear(name);
  return 0;
}

static int test_hypercore_result_markers(void) {
  onion_ready_clear(ONION_READY_KSTUFF_STARTING);
  onion_ready_clear(ONION_READY_KSTUFF);
  onion_ready_clear(ONION_READY_KSTUFF_FAILED);

  TEST_ASSERT_TRUE(
      onion_ready_signal_pid(ONION_READY_KSTUFF_STARTING, getpid()));
  TEST_ASSERT_TRUE(
      onion_ready_matches_pid(ONION_READY_KSTUFF_STARTING, getpid()));
  TEST_ASSERT_TRUE(onion_ready_clear(ONION_READY_KSTUFF_STARTING));

  TEST_ASSERT_TRUE(onion_ready_signal(ONION_READY_KSTUFF_FAILED));
  TEST_ASSERT_TRUE(onion_ready_is_set(ONION_READY_KSTUFF_FAILED));
  TEST_ASSERT_TRUE(!onion_ready_is_set(ONION_READY_KSTUFF));

  TEST_ASSERT_TRUE(onion_ready_clear(ONION_READY_KSTUFF_FAILED));
  TEST_ASSERT_TRUE(onion_ready_signal(ONION_READY_KSTUFF));
  TEST_ASSERT_TRUE(onion_ready_is_set(ONION_READY_KSTUFF));
  TEST_ASSERT_TRUE(!onion_ready_is_set(ONION_READY_KSTUFF_FAILED));

  onion_ready_clear(ONION_READY_KSTUFF);
  return 0;
}

int test_ready_suite(void) {
  int failures = 0;
  failures += onion_test_run("ready_signal_wait_clear", test_ready_signal_wait_clear);
  failures += onion_test_run("ready_reject_slash", test_ready_reject_slash);
  failures += onion_test_run("ready_timeout", test_ready_timeout);
  failures += onion_test_run("ready_path_builder", test_ready_path_builder);
  failures += onion_test_run("flag_fps_overlay", test_runtime_flag_fps_overlay);
  failures += onion_test_run("flag_util_booted", test_runtime_flag_util_booted);
  failures += onion_test_run("toolbox_marker_runtime_root",
                             test_toolbox_marker_under_runtime_root);
  failures += onion_test_run("ready_process_instance",
                             test_process_instance_marker);
  failures += onion_test_run("ready_process_plain_marker_value",
                             test_process_instance_rejects_plain_marker_value);
  failures += onion_test_run("ready_hypercore_result_markers",
                             test_hypercore_result_markers);
  return failures;
}
