#ifndef HV_DEFEAT_0304_H
#define HV_DEFEAT_0304_H

#include <stddef.h>

int hv_defeat_0304(void *shellcode_kernel, size_t shellcode_kernel_len);
int stage1_tmr_relax(void);
int stage2_patch_vmcbs(void);
int stage3_force_vmcb_reload(void);

#endif
