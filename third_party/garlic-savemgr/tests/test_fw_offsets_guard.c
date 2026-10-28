#define GARLIC_FW_GUARD_HOST_TEST 1
#include "../src/fw_offsets_guard.h"
#include <stdio.h>
#define REQUIRE(expr) do { if (!(expr)) { \
  fprintf(stderr,"ASSERTION FAILED at line %d: %s\n", __LINE__,#expr);return 1; } } while(0)
int main(void) {
  const uintptr_t base = UINTPTR_MAX - UINT64_C(0x7fffffff);
  REQUIRE(GARLIC_PROFILE_COUNT == 67);
  int original = 0, added = 0;
  for (size_t i = 0; i < GARLIC_PROFILE_COUNT; ++i) {
    const garlic_fw_offsets_profile *p = &garlic_fw_profiles[i];
    REQUIRE(garlic_fw_find(p->version) == p);
    REQUIRE(garlic_fw_validate(p->version, base, base - p->text_minus,
             base + p->allproc_plus, base + p->security_plus,
             base + p->rootvnode_plus, base + p->bus_plus));
    REQUIRE(!garlic_fw_validate(p->version, base, base-p->text_minus,
             base + p->allproc_plus+1, base + p->security_plus,
             base + p->rootvnode_plus, base + p->bus_plus));
    REQUIRE(!garlic_fw_validate(p->version, 0,0,0,0,0,0));
    if (p->version < 0x11000000u) original++; else added++;
  }
  REQUIRE(original == 52 && added == 15);
  REQUIRE(!garlic_fw_find(0x11100000u)); /* 11.10 not in SDK v0.44 */
  REQUIRE(!garlic_fw_find(0x12500000u)); /* 12.50 not in SDK v0.44 */
  REQUIRE(!garlic_fw_find(0x13500000u)); /* 13.50 not in SDK v0.44 */
  REQUIRE(!garlic_fw_find(0x13610000u)); /* 13.61 not in SDK v0.44 */
  REQUIRE(!garlic_fw_validate(0x99990000u,base,base,base,base,base,base));
  puts("PASS: 67 exact profiles 1.00-13.60; 52 in supplied ELF + 15 newly covered; mismatched and unknown rejected");
  return 0;
}
