/* Host tests for hde64 (x86_64 length disassembler). */
#include "test_harness.h"

#include <onion/hde64.h>

#include <string.h>

static int test_nop(void) {
  const unsigned char code[] = {0x90};
  hde64s hs;
  unsigned int n = hde64_disasm(code, &hs);
  TEST_ASSERT_EQ_INT(1, (int)n);
  TEST_ASSERT_EQ_INT(1, (int)hs.len);
  TEST_ASSERT_EQ_INT(0x90, (int)hs.opcode);
  TEST_ASSERT_TRUE((hs.flags & F_ERROR) == 0);
  return 0;
}

static int test_ret(void) {
  const unsigned char code[] = {0xC3};
  hde64s hs;
  unsigned int n = hde64_disasm(code, &hs);
  TEST_ASSERT_EQ_INT(1, (int)n);
  TEST_ASSERT_EQ_INT(0xC3, (int)hs.opcode);
  return 0;
}

static int test_mov_rax_imm64(void) {
  /* 48 B8 imm64 — mov rax, imm64 */
  const unsigned char code[] = {0x48, 0xB8, 0x01, 0x02, 0x03, 0x04,
                                0x05, 0x06, 0x07, 0x08};
  hde64s hs;
  unsigned int n = hde64_disasm(code, &hs);
  TEST_ASSERT_EQ_INT(10, (int)n);
  TEST_ASSERT_EQ_INT(10, (int)hs.len);
  TEST_ASSERT_EQ_INT(0xB8, (int)hs.opcode);
  /* REX.W present (field name varies by hde version — check W bit or prefix) */
  TEST_ASSERT_TRUE(hs.rex_w != 0 || (hs.flags & F_PREFIX_REX) != 0 ||
                   hs.rex == 0x48);
  TEST_ASSERT_TRUE((hs.flags & F_IMM64) != 0 ||
                   (hs.flags & F_IMM32) != 0); /* some builds fold imm */
  /* Length is the contract Detour relies on */
  TEST_ASSERT_TRUE(hs.len == 10);
  return 0;
}

static int test_jmp_rel32(void) {
  /* E9 rel32 */
  const unsigned char code[] = {0xE9, 0x00, 0x00, 0x00, 0x00};
  hde64s hs;
  unsigned int n = hde64_disasm(code, &hs);
  TEST_ASSERT_EQ_INT(5, (int)n);
  TEST_ASSERT_EQ_INT(0xE9, (int)hs.opcode);
  TEST_ASSERT_TRUE((hs.flags & F_RELATIVE) != 0);
  return 0;
}

static int test_multi_insn_lengths(void) {
  /* sequence: nop; ret; int3 — decode first only */
  const unsigned char code[] = {0x90, 0xC3, 0xCC};
  hde64s hs;
  unsigned int n = hde64_disasm(code, &hs);
  TEST_ASSERT_EQ_INT(1, (int)n);
  n = hde64_disasm(code + 1, &hs);
  TEST_ASSERT_EQ_INT(1, (int)n);
  n = hde64_disasm(code + 2, &hs);
  TEST_ASSERT_EQ_INT(1, (int)n);
  TEST_ASSERT_EQ_INT(0xCC, (int)hs.opcode);
  return 0;
}

int test_hde64_suite(void) {
  int failures = 0;
  failures += onion_test_run("hde64.nop", test_nop);
  failures += onion_test_run("hde64.ret", test_ret);
  failures += onion_test_run("hde64.mov_rax_imm64", test_mov_rax_imm64);
  failures += onion_test_run("hde64.jmp_rel32", test_jmp_rel32);
  failures += onion_test_run("hde64.multi_lengths", test_multi_insn_lengths);
  return failures;
}
