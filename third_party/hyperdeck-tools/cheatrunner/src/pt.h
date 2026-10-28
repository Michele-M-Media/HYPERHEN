/* Copyright (C) 2024 John Törnblom

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

#include <stdint.h>
#include <sys/types.h>
#include <machine/reg.h>

int pt_attach(pid_t pid);
/* Like pt_attach but fails with -2 if the traced process doesn't stop within timeout_ms */
int pt_attach_timed(pid_t pid, int timeout_ms);
int pt_detach(pid_t pid, int sig);
int pt_step(pid_t pid);
int pt_continue(pid_t pid, int sig);



int pt_getregs(pid_t pid, struct reg *r);
int pt_setregs(pid_t pid, const struct reg *r);

/* LWP id ptrace currently reports for pid - pass back as pid to pin
 * GETREGS/SETREGS/CONTINUE/STEP to one thread instead of "whatever's selected", which drifts on a busy target. */
pid_t pt_get_lwpid(pid_t pid);

/* struct fpreg has no userspace header; PT_FPREGS_SIZE is its known size
 * (the legacy FXSAVE area) — pass a buffer of at least this many bytes. */
#define PT_FPREGS_SIZE 512
int pt_getfpregs(pid_t pid, void *fpregs);
int pt_setfpregs(pid_t pid, void *fpregs);

int pt_copyin(pid_t pid, const void* buf, intptr_t addr, size_t len);
int pt_copyout(pid_t pid, intptr_t addr, void* buf, size_t len);

/* Raises ucred once for a run of pt_* calls instead of per-call (cheap for bulk
 * patches). Caller must hold the ptrace gate; begin/end must balance. */
int  pt_batch_begin(void);
void pt_batch_end(void);

/* mdbg_copyin/copyout have the same ucred race as ptrace — cr_mdbg.c holds this
 * lock around them. Don't call while pt_batch_begin() is open (same mutex, deadlocks). */
void pt_ucred_lock(void);
void pt_ucred_unlock(void);

int pt_setint(pid_t pid, intptr_t addr, int val);
int pt_setlong(pid_t pid, intptr_t addr, long val);

int   pt_getint(pid_t pid, intptr_t addr);

long pt_syscall(pid_t pid, int sysno, ...);
intptr_t pt_resolve(pid_t pid, const char* nid);

/* Calls a function at addr in the target with up to 6 args, single-stepping
 * until it returns; GP registers are saved/restored around the call. */
long pt_call(pid_t pid, intptr_t addr, ...);

intptr_t pt_mmap(pid_t pid, intptr_t addr, size_t len, int prot, int flags,
		 int fd, off_t off);
int pt_msync(pid_t, intptr_t addr, size_t len, int flags);
int pt_munmap(pid_t pid, intptr_t addr, size_t len);
int pt_mprotect(pid_t pid, intptr_t addr, size_t len, int prot);

int pt_socket(pid_t pid, int domain, int type, int protocol);
int pt_setsockopt(pid_t pid, int fd, int level, int optname, intptr_t optval,
		  uint32_t optlen);


int pt_close(pid_t pid, int fd);

int pt_dup2(pid_t pid, int oldfd, int newfd);
int pt_rdup(pid_t pid, pid_t other_pid, int fd);
int pt_pipe(pid_t pid, intptr_t pipefd);
void pt_perror(pid_t pid, const char *s);

