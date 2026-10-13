/* Host regression tests for unique same-page trampoline allocation. */
#include "test_harness.h"

#include <onion/trampoline_arena.hpp>

#include <cstdint>
#include <sys/mman.h>
#include <unistd.h>

namespace {

int test_same_page_allocations_are_unique() {
  onion::TrampolineArena arena;
  void *target_page = mmap(nullptr, 4096, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  TEST_ASSERT_TRUE(target_page != MAP_FAILED);
  const uintptr_t target = reinterpret_cast<uintptr_t>(target_page);
  void *first = arena.allocate_near(256, target);
  void *second = arena.allocate_near(256, target);

  TEST_ASSERT_TRUE(first != nullptr);
  TEST_ASSERT_TRUE(second != nullptr);
  TEST_ASSERT_TRUE(first != second);
  TEST_ASSERT_TRUE(arena.owns(first));
  TEST_ASSERT_TRUE(arena.owns(second));
  TEST_ASSERT_TRUE(reinterpret_cast<uintptr_t>(second) >
                  reinterpret_cast<uintptr_t>(first));
  TEST_ASSERT_TRUE(reinterpret_cast<uintptr_t>(second) -
                       reinterpret_cast<uintptr_t>(first) >=
                   static_cast<uintptr_t>(sysconf(_SC_PAGESIZE)));
  (void)munmap(target_page, 4096);
  return 0;
}

int test_arena_rejects_invalid_requests() {
  onion::TrampolineArena arena;
  TEST_ASSERT_TRUE(arena.allocate_near(0, 1) == nullptr);
  TEST_ASSERT_TRUE(arena.allocate_near(256, 0) == nullptr);
  TEST_ASSERT_TRUE(!arena.owns(nullptr));
  return 0;
}

} // namespace

extern "C" int test_trampoline_arena_suite(void) {
  int failures = 0;
  failures += onion_test_run("trampoline_arena.same_page_unique",
                             test_same_page_allocations_are_unique);
  failures += onion_test_run("trampoline_arena.invalid_requests",
                             test_arena_rejects_invalid_requests);
  return failures;
}
