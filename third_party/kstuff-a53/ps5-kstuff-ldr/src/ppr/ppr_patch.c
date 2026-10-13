#include "ppr_patch.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PPR_CALL_COUNT 10U
#define PPR_AES_SITE_COUNT 2U
#define PPR_LEGACY_SITE_COUNT 2U
#define PPR_SITE_COUNT (PPR_CALL_COUNT + PPR_AES_SITE_COUNT + 1U)
#define PPR_RUNTIME_SIZE 0x4cU
#define PPR_PLAINTEXT_SIZE 0xa0U
#define PPR_LAYOUT_HEADER_SIZE 0x148U
#define PPR_LAYOUT_MAX_SEGMENTS 32U
#define PPR_LAYOUT_VISITED_MASK 0x00030000U
#define PPR_LAYOUT_IO_FIXED_INDEX_OFF 0x54U
#define PPR_LAYOUT_IO_DEV_INDEX_OFF 0x74U
#define PPR_MIXED_WRITE_MAX 8U
#define PPR_INSTALL_MAX_ATTEMPTS 2U

struct ppr_layout_record {
    uint32_t id;
    uint32_t flags;
    uint64_t g6_base;
    uint64_t g6_size;
    uint64_t sram_base;
    uint64_t sram_size;
    uint64_t unknown_28;
    uint64_t mapped_base;
    uint64_t unknown_38;
    uint64_t mapped_size;
    uint64_t name_va;
};

_Static_assert(sizeof(struct ppr_layout_record) == 0x50,
               "A53 layout record size");

static int ppr_layout_flags_match(uint32_t flags, uint32_t expected) {
    /* mp4_show_layout_info() uses bits 16 and 17 as traversal markers after
     * printing the G6 and SRAM sides of a record.  Early releases can expose
     * the table before that diagnostic has run, so those two bits are state,
     * not part of the segment's semantic flags. */
    return (flags & ~PPR_LAYOUT_VISITED_MASK) == expected;
}

static int ppr_layout_dev_flags_match(uint32_t flags) {
    /* Both semantic encodings occur in valid DEV layout records.  Keep them
     * firmware-independent while ignoring only the two traversal bits. */
    return ppr_layout_flags_match(flags, 0x00000001U) ||
           ppr_layout_flags_match(flags, 0x00000011U);
}

struct ppr_profile {
    uint32_t firmware;
    const char *name;

    enum {
        PPR_ABI_LEGACY,
        PPR_ABI_CURRENT,
        PPR_ABI_LATE,
    } abi;
    int merged_text;
    int cave_in_dev;

    uint64_t io_mapped_base;
    uint64_t io_file_size;
    uint64_t io_mapped_size;
    uint64_t io_sram_base;
    uint64_t dev_mapped_base;
    uint64_t dev_file_size;
    uint64_t dev_mapped_size;

    uint64_t runtime_cave_va;
    uint64_t plaintext_cave_va;
    uint64_t helper_va;
    uint64_t precheck_normal_va;
    uint64_t precheck_special_va;
    uint64_t common_error_va;
    uint64_t dispatch_native_va;
    uint64_t aes_descriptor_va;
    uint64_t plaintext_descriptor_va;
    uint64_t plaintext_direct_va;
    uint64_t plaintext_idma_va;
    uint64_t sha_wait_idma_aes_va;
    uint64_t sha_authenticate_va;
    uint64_t submit_idma_va;
    uint64_t common_return_va;

    uint64_t call_va[PPR_CALL_COUNT];
    uint32_t call_stock[PPR_CALL_COUNT];
    uint64_t aes_site_va[PPR_AES_SITE_COUNT];
    uint32_t aes_site_stock[PPR_AES_SITE_COUNT];
    uint64_t sha_auth_site_va;
    uint32_t sha_auth_site_stock;
    uint64_t precheck_va;
    uint32_t precheck_stock;
    uint64_t dispatch_va;
    uint32_t dispatch_stock;
};

struct ppr_target_profile {
    uint32_t firmware;
    enum ppr_target_type target;
    uint16_t profile_index;
};

/* Generated from the complete MP4 A53 ELF set. */
#include "ppr_profiles.inc"

static const char *const ppr_call_names[PPR_CALL_COUNT] = {
    "PackageRead call 1",
    "PackageRead call 2",
    "FlashReadDecryptAndVerifySha",
    "ExtFs_UnpackPackage1Block",
    "ExtFs_UnpackPackage2Block A",
    "ExtFs_UnpackPackage2Block B",
    "ExtFs_DecryptAndVerifySha",
    "NSID2_FlashAppendFromDeltaSource",
    "NSID2_FlashReadPackageFile A",
    "NSID2_FlashReadPackageFile B",
};

struct ppr_images {
    uint32_t runtime[PPR_RUNTIME_SIZE / 4U];
    uint32_t plaintext[PPR_PLAINTEXT_SIZE / 4U];
    uint32_t site_hook[PPR_SITE_COUNT];
    uint32_t legacy_hook[PPR_LEGACY_SITE_COUNT];
};

struct ppr_resolved {
    uint64_t runtime_cave;
    uint64_t plaintext_cave;
    uint64_t site[PPR_SITE_COUNT];
    uint64_t legacy_site[PPR_LEGACY_SITE_COUNT];
};

struct ppr_state {
    uint8_t runtime[PPR_RUNTIME_SIZE];
    uint8_t plaintext[PPR_PLAINTEXT_SIZE];
    uint32_t site[PPR_SITE_COUNT];
    uint32_t legacy_site[PPR_LEGACY_SITE_COUNT];
    int runtime_exact;
    int plaintext_exact;
    int sites_known;
    int sites_stock;
    int sites_hooked;
    int legacy_sites_known;
    int legacy_sites_stock;
    int legacy_hook_present;
    int legacy_reboot_required;
    int native;
    int dynamic;
    int recoverable;
};

struct ppr_context {
    const struct ppr_patch_transport *transport;
    const struct ppr_profile *profile;
    struct ppr_images images;
};

static void ppr_logf(const struct ppr_context *ctx, const char *format, ...) {
    char message[512];
    va_list args;
    va_start(args, format);
    (void)vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    if (ctx->transport->log)
        ctx->transport->log(ctx->transport->context, message);
}

static void ppr_log_layout_record(const struct ppr_context *ctx,
                                  const char *role, uint32_t index,
                                  const struct ppr_layout_record *record) {
    ppr_logf(ctx,
             "[*] PPR layout %s[%u]: id=%u flags=0x%08x "
             "g6=0x%lx+0x%lx sram=0x%lx+0x%lx map=0x%lx+0x%lx",
             role, index, record->id, record->flags,
             (unsigned long)record->g6_base,
             (unsigned long)record->g6_size,
             (unsigned long)record->sram_base,
             (unsigned long)record->sram_size,
             (unsigned long)record->mapped_base,
             (unsigned long)record->mapped_size);
}

static int ppr_target_valid(enum ppr_target_type target) {
    switch (target) {
    case PPR_TARGET_ANY:
    case PPR_TARGET_RETAIL:
    case PPR_TARGET_TESTKIT:
    case PPR_TARGET_DEVKIT:
        return 1;
    }
    return 0;
}

static const struct ppr_profile *ppr_find_profile(
        uint32_t firmware, enum ppr_target_type target) {
    const struct ppr_profile *generic = NULL;
    const struct ppr_profile *retail = NULL;
    const struct ppr_profile *first = NULL;

    if (!ppr_target_valid(target))
        return NULL;

    for (size_t i = 0;
         i < sizeof(ppr_target_profiles) / sizeof(ppr_target_profiles[0]);
         i++) {
        const struct ppr_target_profile *entry = &ppr_target_profiles[i];
        if (entry->firmware != firmware ||
            entry->profile_index >=
                sizeof(ppr_profiles) / sizeof(ppr_profiles[0]))
            continue;
        const struct ppr_profile *profile =
            &ppr_profiles[entry->profile_index];
        if (!first)
            first = profile;
        if (entry->target == PPR_TARGET_ANY)
            generic = profile;
        if (entry->target == PPR_TARGET_RETAIL)
            retail = profile;
        if (target != PPR_TARGET_ANY && entry->target == target)
            return profile;
    }
    if (generic)
        return generic;
    if (target == PPR_TARGET_ANY)
        return retail ? retail : first;
    return NULL;
}

int ppr_patch_firmware_supported(uint32_t firmware) {
    return ppr_find_profile(firmware, PPR_TARGET_ANY) != NULL;
}

int ppr_patch_target_supported(uint32_t firmware,
                               enum ppr_target_type target) {
    return ppr_find_profile(firmware, target) != NULL;
}

enum ppr_target_type ppr_patch_parse_target(const char *version) {
    if (!version)
        return PPR_TARGET_ANY;
    if (strstr(version, "devkit(") || strstr(version, "build.rev devkit"))
        return PPR_TARGET_DEVKIT;
    if (strstr(version, "testkit(") || strstr(version, "build.rev testkit"))
        return PPR_TARGET_TESTKIT;
    if (strstr(version, "cex(") || strstr(version, "build.rev cex"))
        return PPR_TARGET_RETAIL;
    return PPR_TARGET_ANY;
}

const char *ppr_patch_target_name(enum ppr_target_type target) {
    switch (target) {
    case PPR_TARGET_RETAIL:  return "retail";
    case PPR_TARGET_TESTKIT: return "testkit";
    case PPR_TARGET_DEVKIT:  return "devkit";
    case PPR_TARGET_ANY:     return "unmarked";
    }
    return "invalid";
}

static int ppr_encode_branch26(uint32_t opcode, uint64_t from, uint64_t to,
                               uint32_t *result) {
    int64_t delta = (int64_t)to - (int64_t)from;
    if (!result || (delta & 3) != 0 || delta < -0x08000000LL ||
        delta > 0x07fffffcLL)
        return -1;
    *result = opcode | ((uint32_t)(delta / 4) & 0x03ffffffU);
    return 0;
}

static int ppr_build_images(struct ppr_context *ctx) {
    const struct ppr_profile *p = ctx->profile;

    static const uint32_t runtime_template[PPR_RUNTIME_SIZE / 4U] = {
        0xb9401bf0U, 0x7103fe1fU, 0x54000101U, 0xb94023f0U,
        0x7103fa1fU, 0x540000a1U, 0x5280fff0U, 0xb9001bf0U,
        0x5280ffd0U, 0xb90023f0U, 0U, 0x711ffcffU,
        0x54000061U, 0x528000a5U, 0U, 0U,
        0U, 0U, 0U,
    };
    static const uint32_t plaintext_template[PPR_PLAINTEXT_SIZE / 4U] = {
        0x711ff8ffU, 0x54000401U, 0xd10083ffU, 0xa90017e0U,
        0xf9000bfeU, 0x2a0303e2U, 0xf9402fe3U, 0U,
        0x340002c0U, 0xa94017e0U, 0x79402808U, 0x51000508U,
        0xb6f801e5U, 0xb9430809U, 0x53083d2aU, 0x0b290149U,
        0x531060abU, 0x6b0b013fU, 0x54000129U, 0x8b2b440aU,
        0x7906c148U, 0x5280002aU, 0xf9422c09U, 0x9acb2148U,
        0x2a090108U, 0xb3405d09U, 0xf9022c09U, 0x52800021U,
        0U, 0x52800020U, 0xf9400bfeU, 0x910083ffU,
        0xd65f03c0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U,
    };
    memcpy(ctx->images.runtime, runtime_template, sizeof(runtime_template));
    memcpy(ctx->images.plaintext, plaintext_template,
           sizeof(ctx->images.plaintext));

    if (p->merged_text) {
        ctx->images.plaintext[10] = 0x79404408U; /* q1 cycle +0x22 */
        ctx->images.plaintext[13] = 0xb9440809U; /* ZCN config +0x408 */
        ctx->images.plaintext[20] = 0x7908c148U; /* release +0x460 */
        ctx->images.plaintext[22] = 0xf9430009U; /* mask +0x600 */
        ctx->images.plaintext[26] = 0xf9030009U;
    }

#define B26(array, base, index, opcode, target)                                \
    do {                                                                        \
        if (ppr_encode_branch26((opcode),                                       \
                (base) + (uint64_t)(index) * 4U,                                \
                (target), &(array)[index]) != 0)                                \
            return -1;                                                          \
    } while (0)
    B26(ctx->images.runtime, p->runtime_cave_va, 10,
        0x14000000U, p->helper_va);
    B26(ctx->images.runtime, p->runtime_cave_va, 14,
        0x14000000U, p->plaintext_descriptor_va);
    B26(ctx->images.runtime, p->runtime_cave_va, 15,
        0x14000000U, p->aes_descriptor_va);
    B26(ctx->images.plaintext, p->plaintext_cave_va, 7,
        0x94000000U, p->sha_wait_idma_aes_va);
    B26(ctx->images.plaintext, p->plaintext_cave_va, 28,
        0x94000000U, p->submit_idma_va);
    B26(ctx->images.plaintext, p->plaintext_cave_va, 33,
        0x14000000U, p->sha_authenticate_va);

    for (size_t i = 0; i < PPR_CALL_COUNT; i++) {
        if (ppr_encode_branch26(0x94000000U, p->call_va[i],
                                p->runtime_cave_va,
                                &ctx->images.site_hook[i]) != 0)
            return -1;
    }
    for (size_t i = 0; i < PPR_AES_SITE_COUNT; i++) {
        if (ppr_encode_branch26(
                0x94000000U, p->aes_site_va[i],
                p->runtime_cave_va + 0x2cU,
                &ctx->images.site_hook[PPR_CALL_COUNT + i]) != 0)
            return -1;
    }
    if (ppr_encode_branch26(
            0x94000000U, p->sha_auth_site_va, p->plaintext_cave_va,
            &ctx->images.site_hook[PPR_CALL_COUNT + PPR_AES_SITE_COUNT]) != 0)
        return -1;

    /* Exact hooks emitted by the previous dynamic-patch generation.  They are
     * read-only guards outside the current site array: an exact old target is
     * reported as LEGACY_REBOOT_REQUIRED, and no current action writes either
     * retired site. */
    if (ppr_encode_branch26(0x94000000U, p->precheck_va,
                            p->runtime_cave_va + 0x24U,
                            &ctx->images.legacy_hook[0]) != 0 ||
        ppr_encode_branch26(0x94000000U, p->dispatch_va,
                            p->runtime_cave_va + 0x3cU,
                            &ctx->images.legacy_hook[1]) != 0)
        return -1;

#undef B26
    return 0;
}

static int ppr_read(const struct ppr_context *ctx, uint64_t pa, void *dst,
                    uint32_t size) {
    return ctx->transport->read(ctx->transport->context, pa, dst, size) ==
                   (int)size
               ? 0 : -1;
}

static int ppr_write(const struct ppr_context *ctx, uint64_t pa,
                     const void *src, uint32_t size) {
    return ctx->transport->write(ctx->transport->context, pa, src, size);
}

static int ppr_read_layout(const struct ppr_context *ctx,
                           struct ppr_layout_record *io,
                           struct ppr_layout_record *dev) {
    uint8_t header[PPR_LAYOUT_HEADER_SIZE] = {0};
    uint8_t *snapshot = NULL;
    struct ppr_layout_record *records = NULL;
    uint32_t segment_count = 0;
    uint32_t io_index = UINT32_MAX;
    uint32_t dev_index = UINT32_MAX;
    int rc = -1;

    if (ctx->transport->fast_mode) {
        size_t snapshot_size = PPR_LAYOUT_HEADER_SIZE +
            PPR_LAYOUT_MAX_SEGMENTS * sizeof(struct ppr_layout_record);
        snapshot = calloc(1, snapshot_size);
        if (!snapshot || ppr_read(ctx, PPR_PATCH_LAYOUT_PA, snapshot,
                                  (uint32_t)snapshot_size) != 0)
            goto out;
        memcpy(header, snapshot, sizeof(header));
    } else if (ppr_read(ctx, PPR_PATCH_LAYOUT_PA, header,
                        sizeof(header)) != 0) {
        goto out;
    }

    memcpy(&segment_count, header + 0x0c, sizeof(segment_count));
    memcpy(&io_index, header + PPR_LAYOUT_IO_FIXED_INDEX_OFF,
           sizeof(io_index));
    memcpy(&dev_index, header + PPR_LAYOUT_IO_DEV_INDEX_OFF,
           sizeof(dev_index));
    if (segment_count == 0 || segment_count > PPR_LAYOUT_MAX_SEGMENTS) {
        ppr_logf(ctx,
                 "[!] PPR layout header mismatch: segments=%u io_index=%u dev_index=%u",
                 segment_count, io_index, dev_index);
        goto out;
    }

    size_t records_size =
        (size_t)segment_count * sizeof(struct ppr_layout_record);
    records = calloc(1, records_size);
    if (!records)
        goto out;
    if (snapshot) {
        memcpy(records, snapshot + PPR_LAYOUT_HEADER_SIZE, records_size);
    } else if (ppr_read(ctx, PPR_PATCH_LAYOUT_PA + PPR_LAYOUT_HEADER_SIZE,
                        records, (uint32_t)records_size) != 0) {
        goto out;
    }

    const struct ppr_profile *p = ctx->profile;
    int have_io = p->merged_text, have_dev = 0;
    for (uint32_t i = 0; i < segment_count; i++) {
        if (!p->merged_text &&
            ppr_layout_flags_match(records[i].flags, 0x00000001U) &&
            records[i].g6_base != 0 &&
            (records[i].g6_base & 0xfffU) == 0 &&
            records[i].g6_size == p->io_file_size &&
            records[i].sram_base == p->io_sram_base &&
            records[i].sram_size >= p->io_mapped_size &&
            records[i].mapped_base == p->io_mapped_base &&
            records[i].mapped_size == p->io_mapped_size) {
            if (have_io)
                goto out;
            *io = records[i];
            have_io = 1;
        }
        if (ppr_layout_dev_flags_match(records[i].flags) &&
            records[i].g6_base != 0 &&
            (records[i].g6_base & 0xfffU) == 0 &&
            records[i].g6_size == p->dev_file_size &&
            records[i].sram_base == 0 && records[i].sram_size == 0 &&
            records[i].mapped_base == p->dev_mapped_base &&
            records[i].mapped_size == p->dev_mapped_size) {
            if (have_dev)
                goto out;
            *dev = records[i];
            have_dev = 1;
        }
    }
    if (!have_io || !have_dev) {
        ppr_logf(ctx,
                 "[!] PPR layout record mismatch: segments=%u io=%d dev=%d io_index=%u dev_index=%u",
                 segment_count, have_io, have_dev, io_index, dev_index);
        if (io_index < segment_count)
            ppr_log_layout_record(ctx, "header-io", io_index,
                                  &records[io_index]);
        if (dev_index < segment_count && dev_index != io_index)
            ppr_log_layout_record(ctx, "header-dev", dev_index,
                                  &records[dev_index]);
        for (uint32_t i = 0; i < segment_count; i++) {
            if (i == io_index || i == dev_index)
                continue;
            if (records[i].mapped_base == p->io_mapped_base ||
                records[i].mapped_base == p->dev_mapped_base ||
                records[i].g6_size == p->io_file_size ||
                records[i].g6_size == p->dev_file_size ||
                records[i].sram_base == p->io_sram_base)
                ppr_log_layout_record(ctx, "candidate", i, &records[i]);
        }
        goto out;
    }

    rc = 0;

out:
    free(records);
    free(snapshot);
    return rc;
}

static int ppr_map_sram(const struct ppr_layout_record *segment, uint64_t va,
                        uint32_t size, uint64_t *pa) {
    if (!segment || !pa || va < segment->mapped_base ||
        size > segment->mapped_size ||
        va - segment->mapped_base > segment->mapped_size - size ||
        size > segment->sram_size ||
        va - segment->mapped_base > segment->sram_size - size)
        return -1;
    *pa = segment->sram_base + (va - segment->mapped_base);
    return 0;
}

static int ppr_map_g6(const struct ppr_layout_record *segment, uint64_t va,
                      uint32_t size, uint64_t *pa) {
    if (!segment || !pa || va < segment->mapped_base ||
        size > segment->mapped_size ||
        va - segment->mapped_base > segment->mapped_size - size ||
        size > segment->g6_size ||
        va - segment->mapped_base > segment->g6_size - size)
        return -1;
    *pa = segment->g6_base + (va - segment->mapped_base);
    return 0;
}

static int ppr_map_g6_padding(const struct ppr_layout_record *segment,
                              uint64_t va, uint32_t size, uint64_t *pa) {
    if (!segment || !pa || segment->g6_base == 0 ||
        va < segment->mapped_base || size > segment->mapped_size ||
        va - segment->mapped_base > segment->mapped_size - size)
        return -1;
    *pa = segment->g6_base + (va - segment->mapped_base);
    return 0;
}

static int ppr_resolve(const struct ppr_context *ctx,
                       struct ppr_resolved *resolved) {
    struct ppr_layout_record io = {0}, dev = {0};
    const struct ppr_profile *p = ctx->profile;
    if (ppr_read_layout(ctx, &io, &dev) != 0) {
        ppr_logf(ctx, "[!] PPR exact runtime-layout validation failed");
        return -1;
    }

    const struct ppr_layout_record *cave = p->cave_in_dev ? &dev : &io;
    int runtime_mapped = p->cave_in_dev
        ? ppr_map_g6_padding(cave, p->runtime_cave_va, PPR_RUNTIME_SIZE,
                             &resolved->runtime_cave)
        : ppr_map_sram(cave, p->runtime_cave_va, PPR_RUNTIME_SIZE,
                       &resolved->runtime_cave);
    int plaintext_mapped = p->cave_in_dev
        ? ppr_map_g6_padding(cave, p->plaintext_cave_va, PPR_PLAINTEXT_SIZE,
                             &resolved->plaintext_cave)
        : ppr_map_sram(cave, p->plaintext_cave_va, PPR_PLAINTEXT_SIZE,
                       &resolved->plaintext_cave);
    if (runtime_mapped != 0 || plaintext_mapped != 0)
        return -1;

    for (size_t i = 0; i < PPR_CALL_COUNT; i++) {
        const struct ppr_layout_record *segment =
            !p->merged_text && i < 2 ? &io : &dev;
        int mapped = !p->merged_text && i < 2
            ? ppr_map_sram(segment, p->call_va[i], 4,
                           &resolved->site[i])
            : ppr_map_g6(segment, p->call_va[i], 4,
                         &resolved->site[i]);
        if (mapped != 0) {
            ppr_logf(ctx, "[!] %s is outside its validated runtime segment",
                     ppr_call_names[i]);
            return -1;
        }
    }
    for (size_t i = 0; i < PPR_AES_SITE_COUNT; i++) {
        int mapped = p->merged_text
            ? ppr_map_g6(&dev, p->aes_site_va[i], 4,
                         &resolved->site[PPR_CALL_COUNT + i])
            : ppr_map_sram(&io, p->aes_site_va[i], 4,
                           &resolved->site[PPR_CALL_COUNT + i]);
        if (mapped != 0)
            return -1;
    }
    int sha_mapped = p->merged_text
        ? ppr_map_g6(&dev, p->sha_auth_site_va, 4,
                     &resolved->site[PPR_CALL_COUNT + PPR_AES_SITE_COUNT])
        : ppr_map_sram(&io, p->sha_auth_site_va, 4,
                       &resolved->site[PPR_CALL_COUNT + PPR_AES_SITE_COUNT]);
    if (sha_mapped != 0)
        return -1;

    const uint64_t legacy_va[PPR_LEGACY_SITE_COUNT] = {
        p->precheck_va, p->dispatch_va,
    };
    for (size_t i = 0; i < PPR_LEGACY_SITE_COUNT; i++) {
        int mapped = p->merged_text
            ? ppr_map_g6(&dev, legacy_va[i], 4,
                         &resolved->legacy_site[i])
            : ppr_map_sram(&io, legacy_va[i], 4,
                           &resolved->legacy_site[i]);
        if (mapped != 0)
            return -1;
    }

    ppr_logf(ctx, "[+] PPR %s addresses resolved from exact runtime layout",
             p->name);
    if (p->merged_text) {
        ppr_logf(ctx, "[*] merged DEV code: G6 base 0x%lx, mapped VA 0x%lx",
                 (unsigned long)dev.g6_base,
                 (unsigned long)dev.mapped_base);
    } else {
        ppr_logf(ctx, "[*] IO fixed code: executable SRAM base 0x%lx, mapped VA 0x%lx",
                 (unsigned long)io.sram_base,
                 (unsigned long)io.mapped_base);
    }
    return 0;
}

static uint32_t ppr_stock_site(const struct ppr_profile *p, size_t index) {
    if (index < PPR_CALL_COUNT)
        return p->call_stock[index];
    index -= PPR_CALL_COUNT;
    if (index < PPR_AES_SITE_COUNT)
        return p->aes_site_stock[index];
    return p->sha_auth_site_stock;
}

static const char *ppr_site_name(size_t index) {
    static const char *const internal_names[] = {
        "segmented AES descriptor",
        "full-block AES descriptor",
        "terminal SHA authenticate",
    };
    return index < PPR_CALL_COUNT
        ? ppr_call_names[index]
        : internal_names[index - PPR_CALL_COUNT];
}

static uint32_t ppr_legacy_stock_site(const struct ppr_profile *p,
                                      size_t index) {
    return index == 0 ? p->precheck_stock : p->dispatch_stock;
}

static void ppr_classify_entry_state(const struct ppr_context *ctx,
                                     struct ppr_state *state) {
    state->sites_known = 1;
    state->sites_stock = 1;
    state->sites_hooked = 1;
    for (size_t i = 0; i < PPR_SITE_COUNT; i++) {
        int stock = state->site[i] == ppr_stock_site(ctx->profile, i);
        int hooked = state->site[i] == ctx->images.site_hook[i];
        state->sites_known &= stock || hooked;
        state->sites_stock &= stock;
        state->sites_hooked &= hooked;
    }

    state->legacy_sites_known = 1;
    state->legacy_sites_stock = 1;
    state->legacy_hook_present = 0;
    for (size_t i = 0; i < PPR_LEGACY_SITE_COUNT; i++) {
        int stock = state->legacy_site[i] ==
                    ppr_legacy_stock_site(ctx->profile, i);
        int hooked = state->legacy_site[i] == ctx->images.legacy_hook[i];
        state->legacy_sites_known &= stock || hooked;
        state->legacy_sites_stock &= stock;
        state->legacy_hook_present |= hooked;
    }
    state->legacy_reboot_required = state->legacy_sites_known &&
                                    state->legacy_hook_present;
}

static void ppr_classify_patch_state(struct ppr_state *state) {
    /* Cave padding is deliberately irrelevant while every entry is stock. */
    state->native = state->sites_stock && state->legacy_sites_stock;
    state->dynamic = state->runtime_exact && state->plaintext_exact &&
                     state->sites_hooked && state->legacy_sites_stock;
    state->recoverable = !state->native && !state->dynamic &&
                         state->runtime_exact && state->plaintext_exact &&
                         state->sites_known && state->legacy_sites_stock;
}

static void ppr_classify_caves(const struct ppr_context *ctx,
                               struct ppr_state *state) {
    state->runtime_exact =
        memcmp(state->runtime, ctx->images.runtime, PPR_RUNTIME_SIZE) == 0;
    state->plaintext_exact =
        memcmp(state->plaintext, ctx->images.plaintext,
               PPR_PLAINTEXT_SIZE) == 0;
}

static int ppr_read_state(const struct ppr_context *ctx,
                          const struct ppr_resolved *resolved,
                          struct ppr_state *state) {
    memset(state, 0, sizeof(*state));

    if (ctx->transport->fast_mode && ctx->transport->batch_enabled &&
        ctx->transport->read_many) {
        uint64_t addresses[2 + PPR_SITE_COUNT];
        uint32_t sizes[2 + PPR_SITE_COUNT];
        void *destinations[2 + PPR_SITE_COUNT];
        size_t n = 0;
#define STATE_READ(address, size, destination) do { \
        addresses[n] = (address);                  \
        sizes[n] = (size);                         \
        destinations[n] = (destination);           \
        n++;                                        \
    } while (0)
        STATE_READ(resolved->runtime_cave, PPR_RUNTIME_SIZE, state->runtime);
        STATE_READ(resolved->plaintext_cave, PPR_PLAINTEXT_SIZE,
                   state->plaintext);
        for (size_t i = 0; i < PPR_SITE_COUNT; i++)
            STATE_READ(resolved->site[i], 4, &state->site[i]);
#undef STATE_READ
        if (ctx->transport->read_many(ctx->transport->context, addresses,
                                      sizes, destinations, (uint32_t)n) != 0)
            return -1;
    } else {
        if (ppr_read(ctx, resolved->runtime_cave, state->runtime,
                     sizeof(state->runtime)) != 0 ||
            ppr_read(ctx, resolved->plaintext_cave, state->plaintext,
                     sizeof(state->plaintext)) != 0)
            return -1;
        for (size_t i = 0; i < PPR_SITE_COUNT; i++) {
            if (ppr_read(ctx, resolved->site[i], &state->site[i], 4) != 0)
                return -1;
        }
    }

    /* Keep the two retired guards outside the 15-command status batch. */
    for (size_t i = 0; i < PPR_LEGACY_SITE_COUNT; i++) {
        if (ppr_read(ctx, resolved->legacy_site[i],
                     &state->legacy_site[i], 4) != 0)
            return -1;
    }

    ppr_classify_caves(ctx, state);
    ppr_classify_entry_state(ctx, state);
    ppr_classify_patch_state(state);
    return 0;
}

static int ppr_read_action_state(const struct ppr_context *ctx,
                                 const struct ppr_resolved *resolved,
                                 struct ppr_state *state) {
    memset(state, 0, sizeof(*state));

    /* One 15-command preflight covers every mutable and retired entry word. */
    if (ctx->transport->fast_mode && ctx->transport->batch_enabled &&
        ctx->transport->read_many) {
        uint64_t addresses[PPR_SITE_COUNT + PPR_LEGACY_SITE_COUNT];
        uint32_t sizes[PPR_SITE_COUNT + PPR_LEGACY_SITE_COUNT];
        void *destinations[PPR_SITE_COUNT + PPR_LEGACY_SITE_COUNT];
        size_t n = 0;
        for (size_t i = 0; i < PPR_SITE_COUNT; i++) {
            addresses[n] = resolved->site[i];
            sizes[n] = 4;
            destinations[n++] = &state->site[i];
        }
        for (size_t i = 0; i < PPR_LEGACY_SITE_COUNT; i++) {
            addresses[n] = resolved->legacy_site[i];
            sizes[n] = 4;
            destinations[n++] = &state->legacy_site[i];
        }
        if (ctx->transport->read_many(ctx->transport->context, addresses,
                                      sizes, destinations, (uint32_t)n) != 0)
            return -1;
    } else {
        for (size_t i = 0; i < PPR_SITE_COUNT; i++) {
            if (ppr_read(ctx, resolved->site[i], &state->site[i], 4) != 0)
                return -1;
        }
        for (size_t i = 0; i < PPR_LEGACY_SITE_COUNT; i++) {
            if (ppr_read(ctx, resolved->legacy_site[i],
                         &state->legacy_site[i], 4) != 0)
                return -1;
        }
    }

    ppr_classify_entry_state(ctx, state);
    ppr_classify_patch_state(state);
    if (state->native || !state->sites_known ||
        !state->legacy_sites_stock)
        return 0;

    /* Only an exact current hook/mix needs cave bytes for classification. */
    if (ctx->transport->fast_mode && ctx->transport->batch_enabled &&
        ctx->transport->read_many) {
        const uint64_t addresses[2] = {
            resolved->runtime_cave, resolved->plaintext_cave,
        };
        const uint32_t sizes[2] = {
            PPR_RUNTIME_SIZE, PPR_PLAINTEXT_SIZE,
        };
        void *const destinations[2] = {
            state->runtime, state->plaintext,
        };
        if (ctx->transport->read_many(ctx->transport->context, addresses,
                                      sizes, destinations, 2) != 0)
            return -1;
    } else if (ppr_read(ctx, resolved->runtime_cave, state->runtime,
                        sizeof(state->runtime)) != 0 ||
               ppr_read(ctx, resolved->plaintext_cave, state->plaintext,
                        sizeof(state->plaintext)) != 0) {
        return -1;
    }
    ppr_classify_caves(ctx, state);
    ppr_classify_patch_state(state);
    return 0;
}

static void ppr_print_state(const struct ppr_context *ctx,
                            const struct ppr_state *state) {
    if (state->native) {
        ppr_logf(ctx, "[+] PPR patch state: STOCK/NATIVE (unused tail ignored)");
    } else if (state->dynamic) {
        ppr_logf(ctx, "[+] PPR patch state: DYNAMIC_INSTALLED (10 callers + 3 internal hooks; FE/FF => ordered PLAINTEXT_NOAUTH)");
    } else if (state->legacy_reboot_required) {
        ppr_logf(ctx, "[!] PPR patch state: LEGACY_REBOOT_REQUIRED (retired hooks remain connected)");
    } else if (state->recoverable) {
        ppr_logf(ctx, "[!] PPR patch state: INTERRUPTED CURRENT IMAGE BUT EXACTLY RECOVERABLE");
    } else {
        ppr_logf(ctx, "[!] PPR patch state: UNKNOWN OR PARTIALLY PATCHED");
        ppr_logf(ctx, "    runtime=%s plaintext=%s sites=%s",
                 state->runtime_exact ? "PATCH" : "UNKNOWN",
                 state->plaintext_exact ? "PATCH" : "UNKNOWN",
                 state->sites_known ? "KNOWN-MIX" : "UNKNOWN");
        ppr_logf(ctx, "    call1=%08x call2=%08x aes0=%08x aes1=%08x sha=%08x",
                 state->site[0], state->site[1],
                 state->site[PPR_CALL_COUNT],
                 state->site[PPR_CALL_COUNT + 1U],
                 state->site[PPR_CALL_COUNT + PPR_AES_SITE_COUNT]);
    }
    if (!state->legacy_sites_stock) {
        ppr_logf(ctx, "    legacy-precheck=%08x legacy-dispatch=%08x (%s)",
                 state->legacy_site[0], state->legacy_site[1],
                 state->legacy_sites_known ? "EXACT OLD/STOCK MIX" : "UNKNOWN");
    }
}

static int ppr_write_checked(const struct ppr_context *ctx, uint64_t pa,
                             const void *bytes, uint32_t size,
                             const char *name) {
    uint8_t verify[PPR_PLAINTEXT_SIZE];
    if (size > sizeof(verify))
        return -1;

    int write_result = ppr_write(ctx, pa, bytes, size);
    int read_result = ppr_read(ctx, pa, verify, size);
    if (read_result == 0 && memcmp(verify, bytes, size) == 0) {
        if (write_result != 0)
            ppr_logf(ctx,
                     "[*] reconciled an unconfirmed write for %s @ PA 0x%lx",
                     name, (unsigned long)pa);
        return 0;
    }
    {
        ppr_logf(ctx, "[!] write/readback failed for %s @ PA 0x%lx",
                 name, (unsigned long)pa);
        return -1;
    }
}

static int ppr_write_many_available(const struct ppr_context *ctx) {
    return ctx->transport->fast_mode && ctx->transport->batch_enabled &&
           ctx->transport->mixed_io_enabled &&
           ctx->transport->write_many_read_many;
}

static int ppr_verify_expected_many(
        const struct ppr_context *ctx, const uint64_t *addresses,
        const void *const *expected, const uint32_t *sizes,
        const char *const *names, uint32_t count) {
    uint8_t verify[PPR_CALL_COUNT][PPR_PLAINTEXT_SIZE];
    void *destinations[PPR_CALL_COUNT];

    if (!addresses || !expected || !sizes || !names || count == 0 ||
        count > PPR_CALL_COUNT)
        return -1;
    for (uint32_t i = 0; i < count; i++) {
        if (!expected[i] || !names[i] || sizes[i] == 0 ||
            sizes[i] > sizeof(verify[i]))
            return -1;
        destinations[i] = verify[i];
    }

    if (ctx->transport->fast_mode && ctx->transport->batch_enabled &&
        ctx->transport->read_many) {
        if (ctx->transport->read_many(
                ctx->transport->context, addresses, sizes, destinations,
                count) != 0)
            return -1;
    } else {
        for (uint32_t i = 0; i < count; i++) {
            if (ppr_read(ctx, addresses[i], destinations[i], sizes[i]) != 0)
                return -1;
        }
    }
    for (uint32_t i = 0; i < count; i++) {
        if (memcmp(verify[i], expected[i], sizes[i]) != 0) {
            ppr_logf(ctx, "[!] exact verification mismatch for %s @ PA 0x%lx",
                     names[i], (unsigned long)addresses[i]);
            return -1;
        }
    }
    return 0;
}

static int ppr_write_many_checked(
        const struct ppr_context *ctx, const uint64_t *addresses,
        const void *const *sources, const uint32_t *sizes,
        const char *const *names, uint32_t count) {
    uint8_t verify[PPR_MIXED_WRITE_MAX][PPR_PLAINTEXT_SIZE];
    void *destinations[PPR_MIXED_WRITE_MAX];

    if (!ppr_write_many_available(ctx) || !addresses || !sources || !sizes ||
        !names || count == 0 || count > PPR_MIXED_WRITE_MAX)
        return -1;
    for (uint32_t i = 0; i < count; i++) {
        if (!sources[i] || !names[i] || sizes[i] == 0 ||
            sizes[i] > sizeof(verify[i]))
            return -1;
        destinations[i] = verify[i];
    }
    int transport_result = ctx->transport->write_many_read_many(
            ctx->transport->context, addresses, sources, sizes,
            destinations, count);
    if (transport_result == 0) {
        uint32_t i = 0;
        for (; i < count; i++) {
            if (memcmp(verify[i], sources[i], sizes[i]) != 0)
                break;
        }
        if (i == count)
            return 0;
    }

    /* A timeout means "completion unknown", not "the write did not run".
     * Reopen the transport and reconcile the intended bytes before deciding
     * whether this phase needs retry or failure handling. */
    if (ppr_verify_expected_many(ctx, addresses, sources, sizes, names,
                                 count) == 0) {
        ppr_logf(ctx,
                 "[*] reconciled an unconfirmed grouped write for %u items (%s .. %s)",
                 count, names[0], names[count - 1U]);
        return 0;
    }
    ppr_logf(ctx,
             "[!] grouped write/readback failed for %u items (%s .. %s)",
             count, names[0], names[count - 1U]);
    return -1;
}

static int ppr_write_pair_checked(const struct ppr_context *ctx,
                                  uint64_t pa0, const void *bytes0,
                                  uint32_t size0, const char *name0,
                                  uint64_t pa1, const void *bytes1,
                                  uint32_t size1, const char *name1) {
    if (ppr_write_many_available(ctx)) {
        const uint64_t addresses[2] = {pa0, pa1};
        const void *sources[2] = {bytes0, bytes1};
        const uint32_t sizes[2] = {size0, size1};
        const char *names[2] = {name0, name1};
        return ppr_write_many_checked(ctx, addresses, sources, sizes, names,
                                      2);
    }
    if (!ctx->transport->fast_mode || !ctx->transport->batch_enabled ||
        !ctx->transport->mixed_io_enabled ||
        !ctx->transport->write_pair_read_pair) {
        if (ppr_write_checked(ctx, pa0, bytes0, size0, name0) != 0)
            return -1;
        return ppr_write_checked(ctx, pa1, bytes1, size1, name1);
    }

    uint8_t verify0[PPR_PLAINTEXT_SIZE];
    uint8_t verify1[PPR_PLAINTEXT_SIZE];
    if (size0 > sizeof(verify0) || size1 > sizeof(verify1))
        return -1;
    int transport_result = ctx->transport->write_pair_read_pair(
            ctx->transport->context,
            pa0, bytes0, size0, verify0,
            pa1, bytes1, size1, verify1);
    if (transport_result == 0 &&
        memcmp(verify0, bytes0, size0) == 0 &&
        memcmp(verify1, bytes1, size1) == 0)
        return 0;

    const uint64_t addresses[2] = {pa0, pa1};
    const void *expected[2] = {bytes0, bytes1};
    const uint32_t sizes[2] = {size0, size1};
    const char *names[2] = {name0, name1};
    if (ppr_verify_expected_many(ctx, addresses, expected, sizes, names, 2) == 0) {
        ppr_logf(ctx, "[*] reconciled an unconfirmed paired write for %s / %s",
                 name0, name1);
        return 0;
    }
    ppr_logf(ctx, "[!] paired write/readback failed for %s / %s @ PA 0x%lx / 0x%lx",
             name0, name1, (unsigned long)pa0, (unsigned long)pa1);
    return -1;
}

static int ppr_write_sites(const struct ppr_context *ctx,
                           const struct ppr_resolved *resolved,
                           size_t first, size_t end, int install) {
    size_t i = first;
    if (ppr_write_many_available(ctx)) {
        while (i < end) {
            uint64_t addresses[PPR_MIXED_WRITE_MAX];
            uint32_t values[PPR_MIXED_WRITE_MAX];
            const void *sources[PPR_MIXED_WRITE_MAX];
            uint32_t sizes[PPR_MIXED_WRITE_MAX];
            const char *names[PPR_MIXED_WRITE_MAX];
            size_t remaining = end - i;
            uint32_t count = remaining > PPR_MIXED_WRITE_MAX
                ? PPR_MIXED_WRITE_MAX : (uint32_t)remaining;

            for (uint32_t j = 0; j < count; j++) {
                size_t site = i + j;
                addresses[j] = resolved->site[site];
                values[j] = install ? ctx->images.site_hook[site]
                                    : ppr_stock_site(ctx->profile, site);
                sources[j] = &values[j];
                sizes[j] = sizeof(values[j]);
                names[j] = ppr_site_name(site);
            }
            if (ppr_write_many_checked(ctx, addresses, sources, sizes, names,
                                       count) != 0)
                return -1;
            i += count;
        }
        return 0;
    }
    for (; ctx->transport->fast_mode && ctx->transport->batch_enabled &&
           ctx->transport->mixed_io_enabled &&
           ctx->transport->write_pair_read_pair && i + 1 < end; i += 2) {
        uint32_t value0 = install ? ctx->images.site_hook[i]
                                  : ppr_stock_site(ctx->profile, i);
        uint32_t value1 = install ? ctx->images.site_hook[i + 1]
                                  : ppr_stock_site(ctx->profile, i + 1);
        const char *name0 = ppr_site_name(i);
        const char *name1 = ppr_site_name(i + 1);
        if (ppr_write_pair_checked(ctx,
                resolved->site[i], &value0, 4, name0,
                resolved->site[i + 1], &value1, 4, name1) != 0)
            return -1;
    }
    for (; i < end; i++) {
        uint32_t value = install ? ctx->images.site_hook[i]
                                 : ppr_stock_site(ctx->profile, i);
        const char *name = ppr_site_name(i);
        if (ppr_write_checked(ctx, resolved->site[i], &value, 4, name) != 0)
            return -1;
    }
    return 0;
}

static int ppr_install_caves_with_retry(
        const struct ppr_context *ctx, const struct ppr_resolved *resolved) {
    for (unsigned attempt = 1; attempt <= PPR_INSTALL_MAX_ATTEMPTS;
         attempt++) {
        if (ppr_write_pair_checked(
                ctx, resolved->plaintext_cave, ctx->images.plaintext,
                PPR_PLAINTEXT_SIZE, "plaintext executable-tail cave",
                resolved->runtime_cave, ctx->images.runtime,
                PPR_RUNTIME_SIZE, "runtime executable-tail cave") == 0)
            return 0;
        if (attempt < PPR_INSTALL_MAX_ATTEMPTS)
            ppr_logf(ctx,
                     "[!] dormant cave install attempt %u failed; retrying",
                     attempt);
    }
    ppr_logf(ctx,
             "[!] dormant cave install failed after %u attempts; no entry hooks were connected",
             PPR_INSTALL_MAX_ATTEMPTS);
    return -1;
}

static int ppr_install_sites_with_retry(
        const struct ppr_context *ctx, const struct ppr_resolved *resolved,
        size_t first, size_t end, const char *phase) {
    for (unsigned attempt = 1; attempt <= PPR_INSTALL_MAX_ATTEMPTS;
         attempt++) {
        if (ppr_write_sites(ctx, resolved, first, end, 1) == 0)
            return 0;
        if (attempt < PPR_INSTALL_MAX_ATTEMPTS)
            ppr_logf(ctx, "[!] %s install attempt %u failed; retrying",
                     phase, attempt);
    }
    ppr_logf(ctx,
             "[!] %s install failed after %u attempts; leaving forward progress in place for a later install retry",
             phase, PPR_INSTALL_MAX_ATTEMPTS);
    return -1;
}

static int ppr_restore_stock_checked(const struct ppr_context *ctx,
                                     const struct ppr_resolved *resolved) {
    /* Retired legacy sites are read-only guards and are never mutated here. */
    if (ppr_write_sites(ctx, resolved, 0, PPR_CALL_COUNT, 0) != 0 ||
        ppr_write_sites(ctx, resolved, PPR_CALL_COUNT,
                        PPR_SITE_COUNT, 0) != 0)
        return -1;
    return 0;
}

static int ppr_restore_site_range_best_effort(
        const struct ppr_context *ctx, const struct ppr_resolved *resolved,
        size_t first, size_t end) {
    uint64_t addresses[PPR_CALL_COUNT];
    uint32_t values[PPR_CALL_COUNT];
    const void *expected[PPR_CALL_COUNT];
    uint32_t sizes[PPR_CALL_COUNT];
    const char *names[PPR_CALL_COUNT];
    size_t count = end - first;
    if (end < first || count == 0 || count > PPR_CALL_COUNT)
        return -1;

    for (size_t i = 0; i < count; i++) {
        size_t site = first + i;
        addresses[i] = resolved->site[site];
        values[i] = ppr_stock_site(ctx->profile, site);
        expected[i] = &values[i];
        sizes[i] = sizeof(values[i]);
        names[i] = ppr_site_name(site);
    }
    for (size_t i = first; i < end; i++) {
        uint32_t stock = ppr_stock_site(ctx->profile, i);
        if (ppr_write(ctx, resolved->site[i], &stock, 4) != 0) {
            uint32_t observed = 0;
            if (ppr_read(ctx, resolved->site[i], &observed,
                         sizeof(observed)) != 0 || observed != stock) {
                ppr_logf(ctx,
                         "[!] rollback write remains unconfirmed for %s @ PA 0x%lx",
                         ppr_site_name(i),
                         (unsigned long)resolved->site[i]);
                break;
            }
            ppr_logf(ctx,
                     "[*] reconciled an unconfirmed rollback write for %s",
                     ppr_site_name(i));
        }
    }
    return ppr_verify_expected_many(
        ctx, addresses, expected, sizes, names, (uint32_t)count);
}

static int ppr_restore_stock_best_effort(const struct ppr_context *ctx,
                                         const struct ppr_resolved *resolved) {
    /* Do not restore internal helpers while any public caller may still
     * branch through them. Retired legacy sites remain read-only guards. */
    if (ppr_restore_site_range_best_effort(
            ctx, resolved, 0, PPR_CALL_COUNT) != 0) {
        ppr_logf(ctx,
                 "[!] ROLLBACK_REBOOT_REQUIRED: public hook restore is unconfirmed; internal hooks were left intact");
        return -1;
    }
    if (ppr_restore_site_range_best_effort(
            ctx, resolved, PPR_CALL_COUNT, PPR_SITE_COUNT) != 0) {
        ppr_logf(ctx,
                 "[!] ROLLBACK_REBOOT_REQUIRED: internal hook restore is unconfirmed");
        return -1;
    }
    return 0;
}

static void ppr_print_stats(const struct ppr_context *ctx) {
    if (!ctx->transport->fast_mode)
        return;
    uint64_t transactions = ctx->transport->transaction_count
        ? ctx->transport->transaction_count(ctx->transport->context) : 0;
    uint64_t opens = ctx->transport->transport_open_count
        ? ctx->transport->transport_open_count(ctx->transport->context) : 0;
    uint64_t ticks = ctx->transport->elapsed_ticks
        ? ctx->transport->elapsed_ticks(ctx->transport->context) : 0;
    ppr_logf(ctx, "[+] PPR FAST stats: %lu transactions, %lu transport opens, 0x%lx ticks",
             (unsigned long)transactions, (unsigned long)opens,
             (unsigned long)ticks);
}

int ppr_patch_run_target(const struct ppr_patch_transport *transport,
                         uint32_t firmware, enum ppr_target_type target,
                         enum ppr_patch_action action,
                         int idle_acknowledged) {
    if (!transport || !transport->read || !transport->write)
        return -1;
    switch (action) {
    case PPR_PATCH_STATUS:
    case PPR_PATCH_INSTALL:
    case PPR_PATCH_UNINSTALL:
    case PPR_PATCH_MODE_NATIVE:
    case PPR_PATCH_MODE_PLAINTEXT_NOAUTH:
        break;
    default:
        if (transport->log)
            transport->log(transport->context,
                           "[!] invalid PPR patch action");
        return -1;
    }
    const struct ppr_profile *profile = ppr_find_profile(firmware, target);
    if (!profile) {
        if (transport->log) {
            char message[128];
            (void)snprintf(message, sizeof(message),
                           "[!] unsupported PPR A53 firmware 0x%08x target %s",
                           firmware, ppr_patch_target_name(target));
            transport->log(transport->context, message);
        }
        return -1;
    }

    struct ppr_context ctx = {
        .transport = transport,
        .profile = profile,
    };
    if (ppr_build_images(&ctx) != 0) {
        ppr_logf(&ctx, "[!] internal AArch64 branch generation failed for %s",
                 profile->name);
        return -1;
    }
    if (action != PPR_PATCH_STATUS && !idle_acknowledged) {
        ppr_logf(&ctx, "[!] refusing A53 mutation: halt/drain PackageRead and APR rings, then pass --i-know-ppr-idle");
        return -1;
    }
    struct ppr_resolved resolved = {0};
    struct ppr_state before;
    if (ppr_resolve(&ctx, &resolved) != 0)
        return -1;
    int state_result = action == PPR_PATCH_STATUS
        ? ppr_read_state(&ctx, &resolved, &before)
        : ppr_read_action_state(&ctx, &resolved, &before);
    if (state_result != 0)
        return -1;
    ppr_print_state(&ctx, &before);

    if (action == PPR_PATCH_STATUS) {
        ppr_logf(&ctx, "[*] runtime cave VA=0x%lx PA=0x%lx",
                 (unsigned long)profile->runtime_cave_va,
                 (unsigned long)resolved.runtime_cave);
        ppr_logf(&ctx, "[*] plaintext cave VA=0x%lx PA=0x%lx",
                 (unsigned long)profile->plaintext_cave_va,
                 (unsigned long)resolved.plaintext_cave);
        ppr_print_stats(&ctx);
        return before.native || before.dynamic ? 0 : -1;
    }

    int install = action == PPR_PATCH_INSTALL ||
                  action == PPR_PATCH_MODE_PLAINTEXT_NOAUTH;
    if (install) {
        if (before.dynamic) {
            ppr_logf(&ctx, "[+] dynamic Native/PLAINTEXT_NOAUTH patch is already installed");
            ppr_print_stats(&ctx);
            return 0;
        }
        if (!before.native && !before.recoverable) {
            ppr_logf(&ctx, "[!] install refused: exact stock or recoverable signatures are required");
            return -1;
        }
        if (before.recoverable)
            ppr_logf(&ctx,
                     "[*] resuming recognized interrupted install without stock rollback");

        /* A native image has no connected current hooks, so its caves can be
         * installed and retried safely.  A recoverable image already has the
         * exact cave bytes and may have live public callers; do not rewrite
         * executable code underneath them. */
        if (before.native &&
            ppr_install_caves_with_retry(&ctx, &resolved) != 0)
            return -1;

        /* Connect internal helpers before public callers.  A failed phase is
         * retried forward; install never rolls already-applied sites to stock. */
        if (ppr_install_sites_with_retry(
                &ctx, &resolved, PPR_CALL_COUNT, PPR_SITE_COUNT,
                "internal-hook") != 0)
            return -1;
        if (ppr_install_sites_with_retry(
                &ctx, &resolved, 0, PPR_CALL_COUNT,
                "public-hook") != 0)
            return -1;
    } else {
        if (before.native) {
            ppr_logf(&ctx, "[+] patch is already absent");
            return 0;
        }
        if (!before.dynamic && !before.recoverable) {
            ppr_logf(&ctx, "[!] uninstall refused: state is neither installed nor exactly recoverable");
            return -1;
        }
        if (before.recoverable)
            ppr_logf(&ctx, "[*] rolling back a recognized interrupted current image");

        /* Disconnect public callers before current internal hooks. */
        if (ppr_restore_stock_checked(&ctx, &resolved) != 0) {
            ppr_logf(&ctx, "[!] stock restore failed; leaving best-effort stock entry points");
            (void)ppr_restore_stock_best_effort(&ctx, &resolved);
            return -1;
        }
    }

    ppr_logf(&ctx, install
             ? "[+] dynamic Native/PLAINTEXT_NOAUTH patch installed and read back"
             : "[+] current PPR entry points restored to stock and read back");
    ppr_print_stats(&ctx);
    return 0;
}

int ppr_patch_run(const struct ppr_patch_transport *transport,
                  uint32_t firmware, enum ppr_patch_action action,
                  int idle_acknowledged) {
    return ppr_patch_run_target(transport, firmware, PPR_TARGET_ANY, action,
                                idle_acknowledged);
}
