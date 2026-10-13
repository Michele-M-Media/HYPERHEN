/* Copyright (C) 2024–2025 John Törnblom / OnionHEN
 *
 * Util-facing ptrace API. Implementation: libonion_elfldr (no per-call authid).
 * Process is elevated once with set_ucred_to_ptrace() (PTRACE_AUTHID) at util
 * main; cave map uses shared pt_*.
 */

#pragma once

#include <onion/pt.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Historical util names → shared attach/detach. */
static inline int pt_attach_proc(pid_t pid) { return pt_attach(pid); }
static inline int pt_detach_proc(pid_t pid, int sig) {
  return pt_detach(pid, sig);
}

#ifdef __cplusplus
}
#endif
