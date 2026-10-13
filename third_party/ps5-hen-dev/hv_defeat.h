#pragma once
#include <cstdint>
#include "offsets.h"

// ecam / pci config (hardware, not fw-dependent)
#define ECAM_BASE_PA        0xF0000000ULL
#define ECAM_B0D18F2        (ECAM_BASE_PA + 0x18ULL * 0x8000 + 2 * 0x1000)
#define TMR_INDEX_OFF       0x80
#define TMR_DATA_OFF        0x84

// tmr layout (hardware)
#define TMR_BASE(n)         ((n) * 0x10 + 0x00)
#define TMR_LIMIT(n)        ((n) * 0x10 + 0x04)
#define TMR_CONFIG(n)       ((n) * 0x10 + 0x08)
#define TMR_REQUESTORS(n)   ((n) * 0x10 + 0x0C)
#define TMR_CFG_PERMISSIVE  0x3F07
#define MAX_TMR             22
#define MAX_SAVED_TMRS      8

// vmcb offsets (amd svm spec)
#define VMCB_INTERCEPT_CR   0x00
#define VMCB_INTERCEPT_DR   0x04
#define VMCB_INTERCEPT_EXC  0x08
#define VMCB_INTERCEPT_MISC 0x0C
#define VMCB_INTERCEPT_VMXX 0x10
#define VMCB_TLB_CONTROL    0x58
#define VMCB_VMCB_CLEAN     0x5C
#define VMCB_NP_ENABLE      0x90

#define VMCB_PATCH_SIZE     0x94

// VMCB struct
// Covers offsets 0x00–0x93
struct vmcb_control_patch {
    /* 0x00 */ uint32_t intercept_cr;
    /* 0x04 */ uint32_t intercept_dr;
    /* 0x08 */ uint32_t intercept_exc;
    /* 0x0C */ uint32_t intercept_misc;
    /* 0x10 */ uint32_t intercept_vmxx;
    /* 0x14 */ uint8_t  _rsvd0[0x58 - 0x14];
    /* 0x58 */ uint32_t tlb_control;
    /* 0x5C */ uint32_t vmcb_clean;
    /* 0x60 */ uint8_t  _rsvd1[0x90 - 0x60];
    /* 0x90 */ uint32_t np_enable;
};
static_assert(sizeof(vmcb_control_patch) == VMCB_PATCH_SIZE);

#define MAX_VMCBS           16
#define PAGE_SIZE           4096
#define INKERNEL(va)        (va & 0xFFFF000000000000)

// context
struct hv_defeat_ctx {
    uint32_t fw;
    uint64_t kbase;
    uint64_t dmap_base;
    uint64_t hv_data_va;

    uint32_t saved_tmrs[MAX_SAVED_TMRS * 2];
    int saved_tmr_count;
    int tmr_disabled;

    uint64_t hv_data_pa;
    uint64_t vmcb_pas[MAX_VMCBS];
    int vmcb_count;
    int vmcbs_patched;

    uint64_t mp4_softc;
    uint64_t zcn_bar2;
};

int stage0_discover(hv_defeat_ctx *ctx);
int stage1_tmr_relax(hv_defeat_ctx *ctx);
int stage2_find_vmcbs(hv_defeat_ctx *ctx);
int stage3_patch_vmcbs(hv_defeat_ctx *ctx, struct iommu_ctx *iommu);
int iommu_selftest(struct iommu_ctx *iommu, uint64_t dmap);
int stage3b_remove_xotext(hv_defeat_ctx *ctx);
int stage4_verify(hv_defeat_ctx *ctx);
int stage5_patch_kernel(hv_defeat_ctx *ctx);
int stage6_install_kexec(hv_defeat_ctx *ctx);
int stage7_run_hen(hv_defeat_ctx *ctx, struct iommu_ctx *iommu);
int kexec(uint64_t fptr);
int run_hv_defeat(void);
int kernel_pmap_invalidate_all(void);
