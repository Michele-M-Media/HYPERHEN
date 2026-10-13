/* Host tests for PID-bound, serialized Toolbox inject. */
#include "test_harness.h"
#include "toolbox_injection.hpp"

#include <atomic>
#include <thread>

using onion::ToolboxInject;
using onion::ToolboxInjectionResult;

namespace {

void reset_toolbox_marker() {
  onion_ready_clear(ONION_READY_TOOLBOX);
}

int test_injects_and_publishes_current_pid() {
  reset_toolbox_marker();
  ToolboxInject injector;
  int inject_count = 0;
  constexpr pid_t kPid = 4100;

  const auto outcome = injector.inject(
      []() -> pid_t { return kPid; },
      [&](pid_t pid) -> bool {
        ++inject_count;
        return onion_ready_signal_pid(ONION_READY_TOOLBOX, pid);
      },
      100, 50);

  TEST_ASSERT_TRUE(outcome.result == ToolboxInjectionResult::Injected);
  TEST_ASSERT_EQ_INT((int)kPid, (int)outcome.pid);
  TEST_ASSERT_EQ_INT(1, inject_count);
  TEST_ASSERT_TRUE(injector.is_ready_for(kPid));
  reset_toolbox_marker();
  return 0;
}

int test_same_pid_skips_duplicate_injection() {
  reset_toolbox_marker();
  ToolboxInject injector;
  constexpr pid_t kPid = 4200;
  TEST_ASSERT_TRUE(onion_ready_signal_pid(ONION_READY_TOOLBOX, kPid));

  int inject_count = 0;
  const auto outcome = injector.inject(
      []() -> pid_t { return kPid; },
      [&](pid_t) -> bool {
        ++inject_count;
        return true;
      },
      100, 50);

  TEST_ASSERT_TRUE(outcome.result == ToolboxInjectionResult::AlreadyReady);
  TEST_ASSERT_EQ_INT(0, inject_count);
  reset_toolbox_marker();
  return 0;
}

int test_new_pid_replaces_stale_marker() {
  reset_toolbox_marker();
  ToolboxInject injector;
  constexpr pid_t kOldPid = 4300;
  constexpr pid_t kNewPid = 4301;
  TEST_ASSERT_TRUE(onion_ready_signal_pid(ONION_READY_TOOLBOX, kOldPid));

  int inject_count = 0;
  const auto outcome = injector.inject(
      []() -> pid_t { return kNewPid; },
      [&](pid_t pid) -> bool {
        ++inject_count;
        return onion_ready_signal_pid(ONION_READY_TOOLBOX, pid);
      },
      100, 50);

  TEST_ASSERT_TRUE(outcome.result == ToolboxInjectionResult::Injected);
  TEST_ASSERT_EQ_INT(1, inject_count);
  TEST_ASSERT_TRUE(!injector.is_ready_for(kOldPid));
  TEST_ASSERT_TRUE(injector.is_ready_for(kNewPid));
  reset_toolbox_marker();
  return 0;
}

int test_expected_pid_overrides_resolver() {
  reset_toolbox_marker();
  ToolboxInject injector;
  constexpr pid_t kExpectedPid = 4351;
  int resolved = 0;
  int injected_pid = 0;

  const auto outcome = injector.inject(
      [&]() -> pid_t {
        ++resolved;
        return resolved == 1 ? 0 : kExpectedPid;
      },
      [&](pid_t pid) -> bool {
        injected_pid = static_cast<int>(pid);
        return onion_ready_signal_pid(ONION_READY_TOOLBOX, pid);
      },
      100, 50, kExpectedPid);

  TEST_ASSERT_TRUE(outcome.result == ToolboxInjectionResult::Injected);
  TEST_ASSERT_EQ_INT((int)kExpectedPid, injected_pid);
  TEST_ASSERT_EQ_INT((int)kExpectedPid, (int)outcome.pid);
  TEST_ASSERT_EQ_INT(2, resolved);
  reset_toolbox_marker();
  return 0;
}

int test_failed_or_timed_out_injection_does_not_stick() {
  reset_toolbox_marker();
  ToolboxInject injector;
  constexpr pid_t kPid = 4400;

  auto failed = injector.inject(
      []() -> pid_t { return kPid; }, [](pid_t) -> bool { return false; }, 100,
      50);
  TEST_ASSERT_TRUE(failed.result == ToolboxInjectionResult::InjectFailed);
  TEST_ASSERT_TRUE(!onion_ready_is_set(ONION_READY_TOOLBOX));

  auto timed_out = injector.inject(
      []() -> pid_t { return kPid; }, [](pid_t) -> bool { return true; }, 100,
      50);
  TEST_ASSERT_TRUE(timed_out.result == ToolboxInjectionResult::ReadyTimeout);
  TEST_ASSERT_TRUE(!onion_ready_is_set(ONION_READY_TOOLBOX));
  return 0;
}

int test_concurrent_requests_inject_once() {
  reset_toolbox_marker();
  ToolboxInject injector;
  constexpr pid_t kPid = 4500;
  std::atomic<int> inject_count{0};
  std::atomic<int> ready_count{0};

  auto request = [&]() {
    const auto outcome = injector.inject(
        []() -> pid_t { return kPid; },
        [&](pid_t pid) -> bool {
          ++inject_count;
          return onion_ready_signal_pid(ONION_READY_TOOLBOX, pid);
        },
        100, 50);
    if (outcome.ready()) {
      ++ready_count;
    }
  };

  std::thread first(request);
  std::thread second(request);
  first.join();
  second.join();

  TEST_ASSERT_EQ_INT(1, inject_count.load());
  TEST_ASSERT_EQ_INT(2, ready_count.load());
  reset_toolbox_marker();
  return 0;
}

} // namespace

extern "C" int test_toolbox_injection_suite(void) {
  int failures = 0;
  failures += onion_test_run("toolbox_injection.inject",
                             test_injects_and_publishes_current_pid);
  failures += onion_test_run("toolbox_injection.same_pid_skip",
                             test_same_pid_skips_duplicate_injection);
  failures += onion_test_run("toolbox_injection.new_pid",
                             test_new_pid_replaces_stale_marker);
  failures += onion_test_run("toolbox_injection.expected_pid",
                             test_expected_pid_overrides_resolver);
  failures += onion_test_run("toolbox_injection.failure_cleanup",
                             test_failed_or_timed_out_injection_does_not_stick);
  failures += onion_test_run("toolbox_injection.serialized",
                             test_concurrent_requests_inject_once);
  return failures;
}
