/* Host regression tests for relocation-aware x86-64 trampolines. */
#include "test_harness.h"

#include <onion/x64_relocator.h>

#include <cstdint>
#include <cstring>

namespace {

uint64_t read_u64(const uint8_t *p) {
  uint64_t value = 0;
  std::memcpy(&value, p, sizeof(value));
  return value;
}

int32_t read_i32(const uint8_t *p) {
  int32_t value = 0;
  std::memcpy(&value, p, sizeof(value));
  return value;
}

bool relocate(const uint8_t *source, size_t minimum, uintptr_t source_address,
              uintptr_t destination_address, uint8_t *destination,
              onion_x64_relocate_result &result) {
  std::memset(destination, 0xcc, ONION_X64_TRAMPOLINE_CAPACITY);
  return onion_x64_relocate(
      source, source_address, destination, destination_address, minimum,
      ONION_X64_TRAMPOLINE_CAPACITY, &result);
}

int test_plain_copy_and_jump_back() {
  const uint8_t source[14] = {0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
                              0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
  uint8_t destination[ONION_X64_TRAMPOLINE_CAPACITY];
  onion_x64_relocate_result result{};
  TEST_ASSERT_TRUE(relocate(source, sizeof(source), 0x10000000, 0x20000000,
                            destination, result));
  TEST_ASSERT_EQ_INT(14, static_cast<int>(result.source_size));
  TEST_ASSERT_EQ_INT(28, static_cast<int>(result.trampoline_size));
  TEST_ASSERT_MEMEQ(source, destination, sizeof(source));
  const uint8_t absolute_jump[] = {0xff, 0x25, 0, 0, 0, 0};
  TEST_ASSERT_MEMEQ(absolute_jump, destination + 14, sizeof(absolute_jump));
  TEST_ASSERT_EQ_U64(0x1000000e, read_u64(destination + 20));
  return 0;
}

int test_rip_relative_memory() {
  /* mov rax,[rip+0x1234], followed by enough nops to steal 14 bytes. */
  const uint8_t source[14] = {0x48, 0x8b, 0x05, 0x34, 0x12, 0x00, 0x00,
                              0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
  constexpr uintptr_t source_address = 0x30000000;
  constexpr uintptr_t destination_address = 0x31000000;
  const uintptr_t expected_target = source_address + 7 + 0x1234;
  uint8_t destination[ONION_X64_TRAMPOLINE_CAPACITY];
  onion_x64_relocate_result result{};
  TEST_ASSERT_TRUE(relocate(source, sizeof(source), source_address,
                            destination_address, destination, result));
  const int32_t relocated_disp = read_i32(destination + 3);
  TEST_ASSERT_EQ_U64(expected_target,
                     destination_address + 7 + relocated_disp);
  return 0;
}

int test_rip_relative_indirect_call() {
  /* call qword ptr [rip+0x200] — representative Mono JIT prologue pattern. */
  const uint8_t source[14] = {0xff, 0x15, 0x00, 0x02, 0x00, 0x00, 0x90,
                              0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
  constexpr uintptr_t source_address = 0x32000000;
  constexpr uintptr_t destination_address = 0x33000000;
  const uintptr_t expected_slot = source_address + 6 + 0x200;
  uint8_t destination[ONION_X64_TRAMPOLINE_CAPACITY];
  onion_x64_relocate_result result{};
  TEST_ASSERT_TRUE(relocate(source, sizeof(source), source_address,
                            destination_address, destination, result));
  const int32_t relocated_disp = read_i32(destination + 2);
  TEST_ASSERT_EQ_U64(expected_slot,
                     destination_address + 6 + relocated_disp);
  return 0;
}

int test_call_rel32_external() {
  const uint8_t source[14] = {0xe8, 0x00, 0x01, 0x00, 0x00, 0x90, 0x90,
                              0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
  uint8_t destination[ONION_X64_TRAMPOLINE_CAPACITY];
  onion_x64_relocate_result result{};
  TEST_ASSERT_TRUE(relocate(source, sizeof(source), 0x40000000, 0x50000000,
                            destination, result));
  const uint8_t absolute_call[] = {0xff, 0x15, 0x02, 0x00,
                                   0x00, 0x00, 0xeb, 0x08};
  TEST_ASSERT_MEMEQ(absolute_call, destination, sizeof(absolute_call));
  TEST_ASSERT_EQ_U64(0x40000105, read_u64(destination + 8));
  return 0;
}

int test_jmp_rel8_external() {
  const uint8_t source[14] = {0xeb, 0x7e, 0x90, 0x90, 0x90, 0x90, 0x90,
                              0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
  uint8_t destination[ONION_X64_TRAMPOLINE_CAPACITY];
  onion_x64_relocate_result result{};
  TEST_ASSERT_TRUE(relocate(source, sizeof(source), 0x60000000, 0x61000000,
                            destination, result));
  const uint8_t absolute_jump[] = {0xff, 0x25, 0, 0, 0, 0};
  TEST_ASSERT_MEMEQ(absolute_jump, destination, sizeof(absolute_jump));
  TEST_ASSERT_EQ_U64(0x60000080, read_u64(destination + 6));
  return 0;
}

int test_jmp_rel32_external() {
  const uint8_t source[14] = {0xe9, 0x00, 0x10, 0x00, 0x00, 0x90, 0x90,
                              0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
  uint8_t destination[ONION_X64_TRAMPOLINE_CAPACITY];
  onion_x64_relocate_result result{};
  TEST_ASSERT_TRUE(relocate(source, sizeof(source), 0x62000000, 0x63000000,
                            destination, result));
  TEST_ASSERT_EQ_U64(0x62001005, read_u64(destination + 6));
  return 0;
}

int test_jcc_short_internal_target() {
  const uint8_t source[14] = {0x75, 0x02, 0x90, 0x90, 0x90, 0x90, 0x90,
                              0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
  constexpr uintptr_t destination_address = 0x71000000;
  uint8_t destination[ONION_X64_TRAMPOLINE_CAPACITY];
  onion_x64_relocate_result result{};
  TEST_ASSERT_TRUE(relocate(source, sizeof(source), 0x70000000,
                            destination_address, destination, result));
  TEST_ASSERT_EQ_INT(0x74, destination[0]); /* inverted JNE -> JE */
  TEST_ASSERT_EQ_INT(14, destination[1]);
  /* source+4 maps after rewritten jcc and two one-byte nops. */
  TEST_ASSERT_EQ_U64(destination_address + 18, read_u64(destination + 8));
  return 0;
}

int test_jcc_near_external() {
  const uint8_t source[14] = {0x0f, 0x84, 0x20, 0x00, 0x00, 0x00, 0x90,
                              0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
  uint8_t destination[ONION_X64_TRAMPOLINE_CAPACITY];
  onion_x64_relocate_result result{};
  TEST_ASSERT_TRUE(relocate(source, sizeof(source), 0x72000000, 0x73000000,
                            destination, result));
  TEST_ASSERT_EQ_INT(0x75, destination[0]); /* inverted JE -> JNE */
  TEST_ASSERT_EQ_INT(14, destination[1]);
  TEST_ASSERT_EQ_U64(0x72000026, read_u64(destination + 8));
  return 0;
}

int test_rip_relative_out_of_range_fails() {
  const uint8_t source[14] = {0x48, 0x8b, 0x05, 0x00, 0x10, 0x00, 0x00,
                              0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
  uint8_t destination[ONION_X64_TRAMPOLINE_CAPACITY];
  onion_x64_relocate_result result{};
  TEST_ASSERT_TRUE(!relocate(source, sizeof(source), 0x10000000,
                             0x9000000000ULL, destination, result));
  TEST_ASSERT_EQ_INT(ONION_X64_RELOCATE_RIP_DISPLACEMENT_OUT_OF_RANGE,
                     result.error);
  return 0;
}

int test_loop_relative_rejected() {
  const uint8_t source[14] = {0xe2, 0xfe, 0x90, 0x90, 0x90, 0x90, 0x90,
                              0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
  uint8_t destination[ONION_X64_TRAMPOLINE_CAPACITY];
  onion_x64_relocate_result result{};
  TEST_ASSERT_TRUE(!relocate(source, sizeof(source), 0x10000000, 0x11000000,
                             destination, result));
  TEST_ASSERT_EQ_INT(ONION_X64_RELOCATE_UNSUPPORTED_RELATIVE, result.error);
  return 0;
}

} // namespace

extern "C" int test_x64_relocator_suite(void) {
  int failures = 0;
  failures += onion_test_run("x64reloc.plain", test_plain_copy_and_jump_back);
  failures += onion_test_run("x64reloc.rip_memory", test_rip_relative_memory);
  failures += onion_test_run("x64reloc.rip_indirect_call",
                             test_rip_relative_indirect_call);
  failures += onion_test_run("x64reloc.call_rel32", test_call_rel32_external);
  failures += onion_test_run("x64reloc.jmp_rel8", test_jmp_rel8_external);
  failures += onion_test_run("x64reloc.jmp_rel32", test_jmp_rel32_external);
  failures += onion_test_run("x64reloc.jcc_internal", test_jcc_short_internal_target);
  failures += onion_test_run("x64reloc.jcc_near", test_jcc_near_external);
  failures += onion_test_run("x64reloc.rip_range", test_rip_relative_out_of_range_fails);
  failures += onion_test_run("x64reloc.loop_reject", test_loop_relative_rejected);
  return failures;
}
