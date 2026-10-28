// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
#include <stddef.h>
#include <stdint.h>

// Fixed ELF64 file layouts, independent of host operating-system headers.
typedef struct { uint8_t ident[16]; uint16_t type, machine; uint32_t version;
  uint64_t entry, phoff, shoff; uint32_t flags; uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx; } HhpElfHeader;
typedef struct { uint32_t type, flags; uint64_t offset, vaddr, paddr, filesz, memsz, align; } HhpElfProgram;
typedef struct { uint32_t name, type; uint64_t flags, addr, offset, size; uint32_t link, info; uint64_t align, entsize; } HhpElfSection;
typedef struct { uint64_t offset, info; int64_t addend; } HhpElfRela;
typedef struct { int64_t tag; uint64_t value; } HhpElfDynamic;
typedef struct { uint64_t offset, count; } HhpRelocationTable;
typedef struct {
  HhpElfHeader header;
  uint64_t lowest, highest;
  HhpRelocationTable relocations[128];
  unsigned relocation_tables;
} HhpElfImage;

// No pointers to unvalidated input escape this function. Zero means accepted.
int hhp_elf_inspect(const void *data, size_t size, HhpElfImage *image, char *error, size_t error_size);
// Materialize the validated image into a bounded local mirror before any remote write.
int hhp_elf_materialize(const void *data, size_t size, const HhpElfImage *image,
                        uint64_t mapped_base, void *mirror, size_t mirror_size);

#ifdef __cplusplus
}
#endif

