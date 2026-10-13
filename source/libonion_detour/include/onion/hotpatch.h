/* Copyright (C) 2026 OnionHEN */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ONION_X64_ABSOLUTE_JUMP_SIZE 14u
#define ONION_X64_ATOMIC_PATCH_SIZE 16u

struct onion_x64_atomic_patch {
  uint8_t expected[ONION_X64_ATOMIC_PATCH_SIZE];
  uint8_t desired[ONION_X64_ATOMIC_PATCH_SIZE];
};

/*
 * Build an aligned 16-byte compare/exchange image for:
 *   jmp qword ptr [rip+0]; dq destination
 * Bytes 14..15 are preserved from the original function.
 */
bool onion_x64_build_atomic_patch(
    uintptr_t target_address,
    const uint8_t current[ONION_X64_ATOMIC_PATCH_SIZE],
    uintptr_t destination,
    struct onion_x64_atomic_patch *patch);

#ifdef __cplusplus
}
#endif
