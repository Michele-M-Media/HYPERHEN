#pragma once
#include <cstdint>
#include <cstring>
#include <cstdio>

#include "util.h"

// Command buffer MMIO offsets
#define IIOMMU_MMIO_CB3_HEAD  0xe000
#define IOMMU_MMIO_CB3_TAIL   0xe008

// Queue constants
#define IOMMU_CB_SIZE        0x2000
#define IOMMU_CB_MASK        (IOMMU_CB_SIZE - 1)
#define IOMMU_CMD_ENTRY_SIZE 0x10

// IOMMU softc field offsets
#define IOMMU_SC_MMIO_VA     0x40
#define IOMMU_SC_CB3_PTR     0x80
#define IOMMU_SC_CB3_INDEX   0x88

struct iommu_ctx {
    uint64_t cb_base;   // kernel VA of command buffer
    uint64_t mmio_va;   // DMAP VA of IOMMU MMIO base
    uint64_t cb_index;  // kernel internal command buffer index
};

#define print(fmt, ...) printf(fmt, ##__VA_ARGS__)

static inline int iommu_init(iommu_ctx *ctx, uint64_t dmap, uint64_t kbase, uint32_t fw) {
    uint64_t softc_off = fw_off(fw, "IOMMU_SOFTC");
    if (!softc_off) {
        print("[iommu] no IOMMU_SOFTC offset for fw 0x%04x\n", fw);
        return -1;
    }

    uint64_t softc = kr8(kbase + softc_off);
    if (!softc) {
        print("[iommu] softc is NULL\n");
        return -2;
    }

    ctx->mmio_va = kr8(softc + IOMMU_SC_MMIO_VA);
    ctx->cb_base = kr8(softc + IOMMU_SC_CB3_PTR);
    ctx->cb_index = softc + IOMMU_SC_CB3_INDEX;

    if (!ctx->cb_base || !ctx->mmio_va) {
        print("[iommu] cb_base=0x%lx mmio=0x%lx - not initialized\n",
            ctx->cb_base, ctx->mmio_va);
        return -3;
    }

    print("[iommu] softc=0x%lx cb=0x%lx mmio=0x%lx\n",
        softc, ctx->cb_base, ctx->mmio_va);
    return 0;
}

// Submit a single 16-byte command and wait for completion
static inline void iommu_submit_cmd(iommu_ctx *ctx, const void *cmd) {
    uint64_t curr_tail = kr8(ctx->mmio_va + IOMMU_MMIO_CB3_TAIL);
    uint64_t next_tail = (curr_tail + IOMMU_CMD_ENTRY_SIZE) & IOMMU_CB_MASK;

    kernel_copyin(cmd, ctx->cb_base + curr_tail, IOMMU_CMD_ENTRY_SIZE);
    kw8(ctx->mmio_va + IOMMU_MMIO_CB3_TAIL, next_tail);
    kw8(ctx->cb_index, next_tail);

    while (kr8(ctx->mmio_va + IIOMMU_MMIO_CB3_HEAD) !=
           kr8(ctx->mmio_va + IOMMU_MMIO_CB3_TAIL))
        ;
}

// Write 8 bytes to a physical address using IOMMU completion wait store
static inline void iommu_write8_pa(iommu_ctx *ctx, uint64_t pa, uint64_t val) {
    uint32_t cmd[4];
    cmd[0] = (uint32_t)(pa & 0xFFFFFFF8) | 0x05;
    cmd[1] = ((uint32_t)(pa >> 32) & 0xFFFFF) | 0x10000000;
    cmd[2] = (uint32_t)(val);
    cmd[3] = (uint32_t)(val >> 32);
    iommu_submit_cmd(ctx, cmd);
}

// Write 4 bytes to a physical address
static inline void iommu_write4_pa(iommu_ctx *ctx, uint64_t pa, uint32_t val, uint64_t dmap) {
    uint64_t aligned = pa & ~7ULL;
    uint64_t existing = kr8(dmap + aligned);
    uint32_t off = (uint32_t)(pa & 7);
    memcpy((uint8_t *)&existing + off, &val, 4);
    iommu_write8_pa(ctx, aligned, existing);
}

// Write arbitrary length to a physical address in 8-byte chunks
static inline void iommu_write_pa(iommu_ctx *ctx, uint64_t pa, const void *data, uint32_t len, uint64_t dmap) {
    const uint8_t *src = (const uint8_t *)data;
    uint32_t off = 0;

    if (pa & 7) {
        uint32_t head = 8 - (uint32_t)(pa & 7);
        if (head > len) head = len;
        uint64_t aligned = pa & ~7ULL;
        uint64_t existing = kr8(dmap + aligned);
        memcpy((uint8_t *)&existing + (pa & 7), src, head);
        iommu_write8_pa(ctx, aligned, existing);
        off += head;
    }

    while (off + 8 <= len) {
        uint64_t val;
        memcpy(&val, src + off, 8);
        iommu_write8_pa(ctx, pa + off, val);
        off += 8;
    }

    if (off < len) {
        uint64_t aligned = pa + off;
        uint64_t existing = kr8(dmap + aligned);
        memcpy(&existing, src + off, len - off);
        iommu_write8_pa(ctx, aligned, existing);
    }
}