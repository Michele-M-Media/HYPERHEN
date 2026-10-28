#ifndef CR_MDBG_H
#define CR_MDBG_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

/* mdbg-backed memory I/O: unlike ptrace, it does not attach to or stop the target. Writes fall back to
 * a CR3 walk on FW 8.20+ (mdbg_copyin breaks there); reads are unaffected. */

int mdbg_io_copyout(pid_t pid, intptr_t addr, void *buf, size_t len);
int mdbg_io_copyin(pid_t pid, const void *buf, intptr_t addr, size_t len);

#endif
