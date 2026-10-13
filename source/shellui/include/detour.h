/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Compatibility shim — implementation lives in libonion_detour.
 * Historical detour.h also pulled C APIs used by prx.cpp.
 */
#pragma once

#include <onion/detour.h>

extern "C" {
#include "ucred.h"
#include "external_symbols.hpp"
#include <ps5/libmprotect.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#include "ps5/mdbg.h"
}
