/* Copyright (C) 2025 OnionHEN / LightningMods

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 3, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING. If not, see
<http://www.gnu.org/licenses/>.  */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif


#include <stdint.h>
#include <unistd.h>
#include <onion/proc.h>

/** Sony debug-style authid (kernel R/W helpers). Not sufficient for PT_*. */
#define DEBUG_AUTHID 0x4800000000000006
/** SceTracer-style authid — only value the kernel accepts for ptrace PT_*. */
#define PTRACE_AUTHID 0x4800000000010003
#define UCRED_AUTHID_KERNEL_OFFSET

/**
 * Set this process ucred authid to PTRACE_AUTHID for ptrace inject/attach.
 * Returns previous authid (0 on failure). Caller restores with set_proc_authid.
 */
uintptr_t set_ucred_to_ptrace(void);
uintptr_t set_proc_authid(pid_t pid, uintptr_t new_authid);
uint8_t* jailbreak_process(pid_t pid);
void jail_process(pid_t pid, uint8_t* ucred);

#ifdef __cplusplus
}
#endif
