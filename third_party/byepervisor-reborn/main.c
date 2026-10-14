#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/mman.h>

#include <ps5/kernel.h>
#include <sys/syscall.h>

#include "kdlsym.h"
#include "khook.h"
#include "util.h"
#include "paging.h"
#include "hyperhen_integration.h"

#include "break_hv_payload/break_hv.h"
#include "break_hv_payload/hypervisor_protocol.h"

#define STATE_ARMED               0x4B484F4F4B454401ULL // 'KHOOKED.'
#define HOOK_CORE                 0
#define HOOK_IST                  6
#define CAVE_STATE_OFF            0x600
#define CAVE_SAVE_OFF             0x620
#define TSS_STRIDE                0x68
#define STARTUP_MP_PAYLOAD_PAGE   0x3FFFF000
#define HV_TRIGGER_TIMEOUT_POLLS  12

#define HV_LOADER_PA_REGULAR      0x062800000
#define HV_LOADER_PA_PRO          0x8e0800000

uint64_t g_hv_base_pa;

static int install_test_hook(int core_id, uint64_t cave)
{
  khook_t    hook;
  kh_chain_t chain;
  uint8_t    chain_buf[0x400];
  int        ret;

  printf("[+] install_test_hook addr = %p\n", &install_test_hook);

  ret = kh_init(&hook, kread, kwrite, ktext(0), cave, HOOK_IST);
  if (ret) return ret;

  hook.tss_stride      = TSS_STRIDE;
  kh_set_tables(&hook, kdlsym(KERNEL_SYM_IDT_ARRAY), kdlsym(KERNEL_SYM_COMMON_TSS));
  kh_set_core(&hook, core_id);
  hook.oneshot   = 1;

  // kh_set_hook_target(&hook, kdlsym(KERNEL_SYM_SYS_GETUID) + 0xB);    // 8b4008             mov     eax, dword [rax+0x8]
  // kh_set_resume_rax(&hook, ktext(0x12F61C6));
  // kh_set_resume_skip(&hook, 3);
  // kh_set_resume_rip(&hook, kdlsym(KERNEL_SYM_SYS_GETUID) + 0xB + 3); // skip ^^^

  kh_set_hook_target(&hook, kdlsym(KERNEL_SYM_SYS_GETUID) + 0xE);       // 48898708040000     mov     qword [rdi+0x408], rax
  kh_set_resume_rax(&hook, 0x12345678);
  //kh_set_resume_skip(&hook, 7);
  //kh_set_resume_rip(&hook, kdlsym(KERNEL_SYM_SYS_GETUID) + 0xE + 7); // skip ^^^

  uint64_t oracle = kh_user_data_addr(&hook);
  kh_chain_init(&chain, chain_buf, sizeof chain_buf);
  kh_chain_store(&chain, oracle, HV_TRIGGER_COOKIE);

  ret = kh_install(&hook, &chain);
  if (ret) return ret;

  kh_trigger(&hook);

  kwrite64(cave + CAVE_SAVE_OFF + 0x00, hook.saved_gate_lo);
  kwrite64(cave + CAVE_SAVE_OFF + 0x08, hook.saved_gate_hi);
  kwrite64(cave + CAVE_SAVE_OFF + 0x10, hook.saved_ist);
  kwrite64(cave + CAVE_STATE_OFF, STATE_ARMED);
  return 0;
}

static int install_resume_hook(int core_id, uint64_t cave, uint64_t cr3)
{
  khook_t hook;
  kh_chain_t chain;
  uint8_t chain_buf[0x800];
  uint64_t set_rsi;
  uint64_t oracle;
  int ret;

  ret = kh_init(&hook, kread, kwrite, ktext(0), cave, HOOK_IST);
  if (ret) return ret;

  hook.tss_stride      = TSS_STRIDE;
  kh_set_tables(&hook, kdlsym(KERNEL_SYM_IDT_ARRAY), kdlsym(KERNEL_SYM_COMMON_TSS));
  kh_set_core(&hook, core_id);
  hook.oneshot   = 1;

  set_rsi = kdldmap(STARTUP_MP_PAYLOAD_PAGE); // our payload
  // set_rsi = ktext(0x269FB8); // original vector (13.60)

  printf("[+] install_resume_hook: setting hook target = 0x%lx (base: 0x%lx), set rsi=0x%lx\n", kdlsym(KERNEL_SYM_HV_MP_STARTUP_CALL), ktext(0), set_rsi);
  kh_set_hook_target(&hook, kdlsym(KERNEL_SYM_HV_MP_STARTUP_CALL));
  kh_set_resume_rsi(&hook, set_rsi);
  kh_set_resume_rdx(&hook, cr3);

  oracle = kh_user_data_addr(&hook);
  kh_chain_init(&chain, chain_buf, sizeof chain_buf);
  kh_chain_store(&chain, oracle, HV_TRIGGER_COOKIE);

  kh_chain_g(&chain, KERNEL_GADGET_POP_RDX);
  kh_chain_q(&chain, cr3);

  kh_chain_g(&chain, KERNEL_GADGET_POP_RCX);
  kh_chain_q(&chain, oracle + 0x08);
  kh_chain_g(&chain, KERNEL_GADGET_ST_RCX_RDX);

  kh_chain_memcpy(&chain, kdldmap(STARTUP_MP_PAYLOAD_PAGE), kdlsym(KERNEL_SYM_DATA_CAVE) + 0x1000, 0xF00);

  kh_chain_store(&chain, kdldmap(STARTUP_MP_PAYLOAD_PAGE + 0xF00), kdlsym(KERNEL_SYM_MP_STARTUP_INIT_VECTOR));
  kh_chain_store(&chain, kdldmap(STARTUP_MP_PAYLOAD_PAGE + 0xF08), kdldmap(0));
  kh_chain_store(&chain, kdldmap(STARTUP_MP_PAYLOAD_PAGE + 0xF10), oracle);

  // carveout addr
  if (g_hv_base_pa == HV_LOADER_PA_PRO)
    kh_chain_store(&chain, kdldmap(STARTUP_MP_PAYLOAD_PAGE + 0xF18), g_hv_base_pa - 0x800000);
  else
    kh_chain_store(&chain, kdldmap(STARTUP_MP_PAYLOAD_PAGE + 0xF18), g_hv_base_pa);

  kh_chain_store(&chain, kdldmap(STARTUP_MP_PAYLOAD_PAGE + 0xF20), g_hv_base_pa + rawoff(HV_SYM_HEAP_BASE));
  kh_chain_store(&chain, kdldmap(STARTUP_MP_PAYLOAD_PAGE + 0xF28), g_hv_base_pa + rawoff(HV_SYM_PCPUS));

  ret = kh_install(&hook, &chain);
  if (ret) return ret;

  kh_trigger(&hook);
  
  kwrite64(cave + CAVE_SAVE_OFF + 0x00, hook.saved_gate_lo);
  kwrite64(cave + CAVE_SAVE_OFF + 0x08, hook.saved_gate_hi);
  kwrite64(cave + CAVE_SAVE_OFF + 0x10, hook.saved_ist);
  kwrite64(cave + CAVE_STATE_OFF, STATE_ARMED);
  return 0;
}

static void uninstall_hook(int core_id, uint64_t cave)
{
  khook_t hook;
  kh_init(&hook, kread, kwrite, ktext(0), cave, HOOK_IST);
  kh_set_tables(&hook, kdlsym(KERNEL_SYM_IDT_ARRAY), kdlsym(KERNEL_SYM_COMMON_TSS));
  kh_set_core(&hook, core_id);
  hook.saved_gate_lo = kread64(cave + CAVE_SAVE_OFF + 0x00);
  hook.saved_gate_hi = kread64(cave + CAVE_SAVE_OFF + 0x08);
  hook.saved_ist     = kread64(cave + CAVE_SAVE_OFF + 0x10);
  hook.installed     = 1;
  kh_disarm(&hook);
  kh_restore(&hook);
  kwrite64(cave + CAVE_STATE_OFF, 0);
}

static int write_page_entry_checked(uint64_t address, uint64_t value)
{
  if (address == 0xFFFFFFFFFFFFFFFFull)
    return -1;
  kwrite64(address, value);
  return kread64(address) == value ? 0 : -1;
}

static int prepare_startup_payload_mapping(uint64_t kernel_pmap)
{
  uint64_t address;
  uint64_t entry;

  address = find_pml4e(kernel_pmap, kdldmap(STARTUP_MP_PAYLOAD_PAGE), &entry);
  if (address == 0xFFFFFFFFFFFFFFFFull)
    return -1;
  CLEAR_PDE_BIT(entry, EXECUTE_DISABLE);
  if (write_page_entry_checked(address, entry))
    return -1;

  address = find_pdpe(kernel_pmap, kdldmap(STARTUP_MP_PAYLOAD_PAGE), &entry);
  if (address == 0xFFFFFFFFFFFFFFFFull)
    return -1;
  CLEAR_PDE_BIT(entry, EXECUTE_DISABLE);
  if (write_page_entry_checked(address, entry))
    return -1;
  if (PDE_FIELD(entry, PS))
    return 0;

  address = find_pde(kernel_pmap, kdldmap(STARTUP_MP_PAYLOAD_PAGE), &entry);
  if (address == 0xFFFFFFFFFFFFFFFFull)
    return -1;
  CLEAR_PDE_BIT(entry, EXECUTE_DISABLE);
  if (write_page_entry_checked(address, entry))
    return -1;
  if (PDE_FIELD(entry, PS))
    return 0;

  address = find_pte(kernel_pmap, kdldmap(STARTUP_MP_PAYLOAD_PAGE), &entry);
  if (address == 0xFFFFFFFFFFFFFFFFull)
    return -1;
  CLEAR_PDE_BIT(entry, EXECUTE_DISABLE);
  return write_page_entry_checked(address, entry);
}

static int unlock_kernel_text(uint64_t kernel_pmap)
{
  uint64_t last_pdpe = 0xFFFFFFFFFFFFFFFFull;
  uint64_t last_pde = 0xFFFFFFFFFFFFFFFFull;
  unsigned int verified_entries = 0;

  for (uint64_t addr = ktext(0); addr < KERNEL_ADDRESS_DATA_BASE;
       addr += 0x1000) {
    uint64_t entry;
    uint64_t entry_addr = find_pdpe(kernel_pmap, addr, &entry);
    if (entry_addr == 0xFFFFFFFFFFFFFFFFull)
      continue;
    if (entry_addr != last_pdpe) {
      last_pdpe = entry_addr;
      CLEAR_PDE_BIT(entry, XOTEXT);
      SET_PDE_BIT(entry, RW);
      if (write_page_entry_checked(entry_addr, entry))
        return -1;
      verified_entries++;
    }
    if (PDE_FIELD(entry, PS))
      continue;

    entry_addr = find_pde(kernel_pmap, addr, &entry);
    if (entry_addr == 0xFFFFFFFFFFFFFFFFull)
      continue;
    if (entry_addr != last_pde) {
      last_pde = entry_addr;
      CLEAR_PDE_BIT(entry, XOTEXT);
      SET_PDE_BIT(entry, RW);
      if (write_page_entry_checked(entry_addr, entry))
        return -1;
      verified_entries++;
    }
    if (PDE_FIELD(entry, PS))
      continue;

    entry_addr = find_pte(kernel_pmap, addr, &entry);
    if (entry_addr == 0xFFFFFFFFFFFFFFFFull)
      continue;
    CLEAR_PDE_BIT(entry, XOTEXT);
    SET_PDE_BIT(entry, RW);
    if (write_page_entry_checked(entry_addr, entry))
      return -1;
    verified_entries++;
  }

  return verified_entries ? 0 : -1;
}

int main(void)
{
  uint64_t test_val;
  uint8_t *test_val_bytes;

  uint32_t fw;
  uint64_t cave;
  uint64_t oracle;
  uint64_t kernel_pmap;
  uint64_t kernel_pm_cr3;
  uint64_t o;
  int trigger_polls = 0;

  hyperhen_hv_begin();
  printf("[+] HyperHEN Hypervisor (Byepervisor Reborn core)\n");

  // Do a quick firmware version detect, if we don't have offsets for it, goodbye!
  fw = kernel_get_fw_version();
  printf("[+] Detected firmware: %x.%x\n", (fw & 0xFF000000) >> 0x18, (fw & 0x00FF0000) >> 0x10);

  if (kdlsym(KERNEL_SYM_HV_MP_STARTUP_CALL) == 0) {
    printf("[!] Undetected firmware, exiting!\n");
    notifyf("[HyperHEN Hypervisor]\nFirmware is not supported\n");
    hyperhen_hv_mark_failed();
    return EXIT_FAILURE;
  }

  // The hypervisor loader's base physical address changes depending on if its a regular or a pro unit
  int soc_id = sceKernelGetMainSocId();
  if (soc_id == 0x840fd0) {
    g_hv_base_pa = HV_LOADER_PA_PRO;
    printf("[+] Pro model detected, HV loader PA = 0x%lx\n", g_hv_base_pa);
    notifyf("HyperHEN Hypervisor\nDetected Pro model (0x%x)\n", soc_id);
  } else {
    g_hv_base_pa = HV_LOADER_PA_REGULAR;
    printf("[+] Regular model detected, HV loader PA = 0x%lx\n", g_hv_base_pa);
    notifyf("HyperHEN Hypervisor\nDetected regular model (0x%x)\n", soc_id);
  }

  // We need system ucred to pin to core 0
  kernel_set_ucred_authid(getpid(), 0x4800000000000007);

  cave   = kdlsym(KERNEL_SYM_DATA_CAVE);
  oracle = cave + KH_OFF_USER;

  pin_to_core(HOOK_CORE);

  // This is so we can send the same payload after resume to experiment with post-HV hax (or not if we want to debug the resume payload)
#define DEBUG_MP_STARTUP_PAYLOAD 0
#if DEBUG_MP_STARTUP_PAYLOAD == 0
  o = kread64(oracle);
  if (o == HV_SUCCESS_COOKIE) {
    printf("Hypervisor route is already active\n");
    test_val = kread64(ktext(0));
    test_val_bytes = (uint8_t *) &test_val;
    hyperhen_hv_mark_ready();
    notifyf("HyperHEN Hypervisor ready\nKernel .text: %02x %02x %02x %02x %02x %02x %02x %02x",
      test_val_bytes[0], test_val_bytes[1], test_val_bytes[2], test_val_bytes[3], 
      test_val_bytes[4], test_val_bytes[5], test_val_bytes[6], test_val_bytes[7]);

    return 0;
  }
  if (o == HV_FAILURE_COOKIE) {
    printf("[!] Previous hypervisor attempt reported failure\n");
    notifyf("[HyperHEN Hypervisor]\nPrevious resume attempt did not complete");
    hyperhen_hv_mark_failed();
    return EXIT_FAILURE;
  }
  if (o == HV_TRIGGER_COOKIE &&
      kread64(cave + CAVE_STATE_OFF) == STATE_ARMED) {
    printf("[!] Previous resume hook did not complete\n");
    uninstall_hook(HOOK_CORE, cave);
    notifyf("[HyperHEN Hypervisor]\nPrevious resume hook did not complete");
    hyperhen_hv_mark_failed();
    return EXIT_FAILURE;
  }
#endif

#if defined(KHOOK_TEST)
  int ret;

  ret = install_test_hook(HOOK_CORE, cave);
  printf("installed test khook = %d\n", ret);

  printf("1\n");
  sceKernelSleep(1);

  kwrite64(oracle, 0);
  printf("2\n");
  sceKernelSleep(1);

  pin_to_core(HOOK_CORE);
  printf("core = %d\n", get_cpu_core());

  ret = syscall(SYS_getuid);

  o = kread64(oracle);
  printf("test khook oracle=0x%lx (ret = 0x%x)\n", o, ret);
  kwrite64(oracle, 0);
  sceKernelSleep(1);

  ret = syscall(SYS_getuid);

  o = kread64(oracle);
  printf("test khook oracle=0x%lx (ret = 0x%x)\n", o, ret);

  sceKernelSleep(1);

  uninstall_hook(HOOK_CORE, cave);
  notifyf("uninstalled test khook\n");
  return 0;

#else // !KHOOK_TEST

  // Copy our payload that runs from startup_mp into kernel cave
  kwrite(kdlsym(KERNEL_SYM_DATA_CAVE) + 0x1000, break_hv, break_hv_len);

  // Make the physical region executable in guest page tables
  kernel_pmap   = kdlsym(KERNEL_SYM_PMAP_STORE);
  if (kernel_pmap == 0) {
    notifyf("[HyperHEN Hypervisor]\nKernel paging state unavailable");
    hyperhen_hv_mark_failed();
    return EXIT_FAILURE;
  }
  kernel_pm_cr3 = kread64(kernel_pmap + 0x28);
  if (kernel_pm_cr3 == 0) {
    notifyf("[HyperHEN Hypervisor]\nKernel CR3 unavailable");
    hyperhen_hv_mark_failed();
    return EXIT_FAILURE;
  }

  if (prepare_startup_payload_mapping(kernel_pmap)) {
    notifyf("[HyperHEN Hypervisor]\nStartup payload mapping is invalid");
    hyperhen_hv_mark_failed();
    return EXIT_FAILURE;
  }
  if (unlock_kernel_text(kernel_pmap)) {
    notifyf("[HyperHEN Hypervisor]\nKernel text mapping verification failed");
    hyperhen_hv_mark_failed();
    return EXIT_FAILURE;
  }

  // Install hook on startup_mp hypercall
  printf("[+] Installing hook...\n");
  if (kread64(cave + CAVE_STATE_OFF) != STATE_ARMED) {
    int r = install_resume_hook(HOOK_CORE, cave, kernel_pm_cr3);
    if (r) {
      printf("[!] Failed to install hook (likely too large)\n");
      notifyf("[HyperHEN Hypervisor]\nResume hook installation failed");
      hyperhen_hv_mark_failed();
      return r;
    }

    printf("[+] Hook installed, enter sleep and resume!\n");
    notifyf("HyperHEN Hypervisor armed\nEnter Rest Mode, then resume\n");
  } else {
    printf("khook: hook already ARMED (state survived)\n");
  }

  hyperhen_hv_mark_armed();

  // Poll for resume and hook trigger
  for (;;) {
    pin_to_core(HOOK_CORE);

    o = kread64(oracle);
    if (o == HV_SUCCESS_COOKIE) {
      printf("[+] Uninstalling hook\n");
      uninstall_hook(HOOK_CORE, cave);

      test_val = kread64(ktext(0));
      test_val_bytes = (uint8_t *) &test_val;
      hyperhen_hv_mark_ready();
      notifyf("HyperHEN Hypervisor ready\nKernel .text: %02x %02x %02x %02x %02x %02x %02x %02x",
      test_val_bytes[0], test_val_bytes[1], test_val_bytes[2], test_val_bytes[3], 
      test_val_bytes[4], test_val_bytes[5], test_val_bytes[6], test_val_bytes[7]);

      // TODO: Patch kernel and all that fun stuff, we don't feel like doing this so someone else can do it :)

      return 0;
    }
    if (o == HV_FAILURE_COOKIE) {
      printf("[!] Hypervisor payload did not complete\n");
      uninstall_hook(HOOK_CORE, cave);
      notifyf("[HyperHEN Hypervisor]\nResume payload did not complete");
      hyperhen_hv_mark_failed();
      return EXIT_FAILURE;
    }
    if (o == HV_TRIGGER_COOKIE) {
      if (++trigger_polls >= HV_TRIGGER_TIMEOUT_POLLS) {
        printf("[!] Hypervisor completion timed out after hook trigger\n");
        uninstall_hook(HOOK_CORE, cave);
        notifyf("[HyperHEN Hypervisor]\nResume completion timed out");
        hyperhen_hv_mark_failed();
        return EXIT_FAILURE;
      }
    } else {
      trigger_polls = 0;
    }

    sceKernelSleep(5);
  }
  return 0;
#endif
}
