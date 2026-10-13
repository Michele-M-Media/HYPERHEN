#ifndef BYEPERVISOR_PAGING_H
#define BYEPERVISOR_PAGING_H

#include <stdint.h>

/*
 * Page-table walkers vendored from Byepervisor-Reborn (AGPL-3.0-or-later).
 * The struct offsets that change between firmware (PROC_P_VMSPACE,
 * VMSPACE_VM_PMAP, PMAP_PM_PML4) are read at call time rather than hardcoded.
 */

enum pde_shift {
    PDE_PRESENT = 0,
    PDE_RW,
    PDE_USER,
    PDE_WRITE_THROUGH,
    PDE_CACHE_DISABLE,
    PDE_ACCESSED,
    PDE_DIRTY,
    PDE_PS,
    PDE_GLOBAL,
    PDE_XOTEXT = 58,
    PDE_PROTECTION_KEY = 59,
    PDE_EXECUTE_DISABLE = 63
};

#define PDE_PRESENT_MASK                1UL
#define PDE_RW_MASK                     1UL
#define PDE_USER_MASK                   1UL
#define PDE_WRITE_THROUGH_MASK          1UL
#define PDE_CACHE_DISABLE_MASK          1UL
#define PDE_ACCESSED_MASK               1UL
#define PDE_DIRTY_MASK                  1UL
#define PDE_PS_MASK                     1UL
#define PDE_GLOBAL_MASK                 1UL
#define PDE_XOTEXT_MASK                 1UL
#define PDE_PROTECTION_KEY_MASK         1UL
#define PDE_EXECUTE_DISABLE_MASK        1UL

#define PDE_FIELD(pde, field)           (((pde) >> PDE_##field) & PDE_##field##_MASK)
#define PDE_ADDR(pde)                   ((pde) & 0x000FFFFFFFFFF000ULL)
#define SET_PDE_BIT(pde, field)         ((pde) |= (PDE_##field##_MASK << PDE_##field))
#define CLEAR_PDE_BIT(pde, field)       ((pde) &= ~(PDE_##field##_MASK << PDE_##field))

uint64_t get_proc_pmap(void);
uint64_t pmap_kextract(uint64_t va);
uint64_t find_pml4e(uint64_t pmap, uint64_t va, uint64_t *out_pml4e);
uint64_t find_pdpe(uint64_t pmap, uint64_t va, uint64_t *out_pdpe);
uint64_t find_pde(uint64_t pmap, uint64_t va, uint64_t *out_pde);
uint64_t find_pte(uint64_t pmap, uint64_t va, uint64_t *out_pte);

#endif // BYEPERVISOR_PAGING_H
