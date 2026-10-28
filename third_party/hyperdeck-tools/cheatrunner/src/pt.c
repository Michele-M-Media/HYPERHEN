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

#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include <sys/ptrace.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <sys/mman.h>

#include <ps5/kernel.h>

#include "cr_log.h"
#include "pt.h"


/* sys_ptrace elevates this process's ucred process-wide for one syscall, so
 * concurrent callers stomp each other's privileges — serialize with a mutex. */
static pthread_mutex_t g_ptrace_ucred_lock = PTHREAD_MUTEX_INITIALIZER;

/* Per-thread batch-elevation state (see pt_batch_begin) - depth>0 means
 * ucred is already raised and the lock held, so sys_ptrace skips the per-call swap. */
static __thread int      g_pt_batch_depth  = 0;
static __thread uint64_t g_pt_batch_authid = 0;
static __thread uint8_t  g_pt_batch_caps[16];

static int
sys_ptrace(int request, pid_t pid, caddr_t addr, int data) {
  /* Inside an open batch: ucred is already elevated and the lock held -
   * just issue the syscall (the 4 credential ops are the expensive part, not this). */
  if (g_pt_batch_depth > 0) {
    return (int)syscall(SYS_ptrace, request, pid, addr, data);
  }

  uint8_t privcaps[16] = {0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
                          0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff};
  pid_t mypid = getpid();
  uint8_t caps[16];
  uint64_t authid;
  int ret;

  pthread_mutex_lock(&g_ptrace_ucred_lock);

  if(!(authid=kernel_get_ucred_authid(mypid))) {
    pthread_mutex_unlock(&g_ptrace_ucred_lock);
    return -1;
  }
  if(kernel_get_ucred_caps(mypid, caps)) {
    pthread_mutex_unlock(&g_ptrace_ucred_lock);
    return -1;
  }

  if(kernel_set_ucred_authid(mypid, 0x4800000000010003l)) {
    pthread_mutex_unlock(&g_ptrace_ucred_lock);
    return -1;
  }
  if(kernel_set_ucred_caps(mypid, privcaps)) {
    /* authid was already elevated — restore it before bailing out. */
    kernel_set_ucred_authid(mypid, authid);
    pthread_mutex_unlock(&g_ptrace_ucred_lock);
    return -1;
  }

  ret = (int)syscall(SYS_ptrace, request, pid, addr, data);

  /* Restore both fields unconditionally — early return on the first failure
   * would leave the process with elevated privcaps permanently. */
  kernel_set_ucred_authid(mypid, authid);
  kernel_set_ucred_caps(mypid, caps);

  pthread_mutex_unlock(&g_ptrace_ucred_lock);

  return ret;
}


int
pt_batch_begin(void) {
  if (g_pt_batch_depth > 0) {     /* already elevated on this thread — nest */
    g_pt_batch_depth++;
    return 0;
  }
  uint8_t privcaps[16] = {0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
                          0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff};
  pid_t mypid = getpid();

  pthread_mutex_lock(&g_ptrace_ucred_lock);
  if(!(g_pt_batch_authid = kernel_get_ucred_authid(mypid)) ||
     kernel_get_ucred_caps(mypid, g_pt_batch_caps) ||
     kernel_set_ucred_authid(mypid, 0x4800000000010003l) ||
     kernel_set_ucred_caps(mypid, privcaps)) {
    /* Elevation failed — restore authid (in case it was raised) and bail.
     * Callers fall back to the per-call swap path automatically. */
    if(g_pt_batch_authid) {
      kernel_set_ucred_authid(mypid, g_pt_batch_authid);
    }
    pthread_mutex_unlock(&g_ptrace_ucred_lock);
    return -1;
  }
  g_pt_batch_depth = 1;           /* lock stays held until pt_batch_end */
  return 0;
}


void
pt_batch_end(void) {
  if (g_pt_batch_depth <= 0) {
    return;
  }
  if (--g_pt_batch_depth > 0) {   /* still nested */
    return;
  }
  pid_t mypid = getpid();
  kernel_set_ucred_authid(mypid, g_pt_batch_authid);
  kernel_set_ucred_caps(mypid, g_pt_batch_caps);
  pthread_mutex_unlock(&g_ptrace_ucred_lock);
}


void
pt_ucred_lock(void) {
  pthread_mutex_lock(&g_ptrace_ucred_lock);
}


void
pt_ucred_unlock(void) {
  pthread_mutex_unlock(&g_ptrace_ucred_lock);
}


intptr_t
pt_resolve(pid_t pid, const char* nid) {
  intptr_t addr;

  if((addr=kernel_dynlib_resolve(pid, 0x1, nid))) {
    return addr;
  }

  return kernel_dynlib_resolve(pid, 0x2001, nid);
}



int
pt_attach(pid_t pid) {
  if(sys_ptrace(PT_ATTACH, pid, 0, 0) == -1) {
    return -1;
  }

  if(waitpid(pid, 0, 0) == -1) {
    return -1;
  }

  return 0;
}


int
pt_attach_timed(pid_t pid, int timeout_ms) {
  if(sys_ptrace(PT_ATTACH, pid, 0, 0) == -1) {
    cr_log("warn", "ptrace", "PT_ATTACH failed outright for pid=%d", (int)pid);
    return -1;
  }

  int waited_ms = 0;
  const int step_ms = 10;
  while(waited_ms < timeout_ms) {
    int status = 0;
    pid_t wret = waitpid(pid, &status, WNOHANG);
    if(wret == pid) {
      if(!WIFSTOPPED(status)) {
        cr_log("warn", "ptrace", "pid=%d didn't stop after PT_ATTACH (status=0x%x)", (int)pid, status);
        return -1;
      }
      return 0;
    }
    if(wret == -1) {
      /* errno here is from waitpid() itself, called directly above with nothing
       * in between - reliable, unlike errno after sys_ptrace() (see below). */
      cr_log("warn", "ptrace", "waitpid(pid=%d) failed after %dms: %s", (int)pid, waited_ms, strerror(errno));
      int drc = sys_ptrace(PT_DETACH, pid, 0, 0);
      cr_log(drc == 0 ? "info" : "error", "ptrace",
             "PT_DETACH for pid=%d after waitpid error: %s", (int)pid,
             drc == 0 ? "ok" : "FAILED - process may remain stopped");
      return -1;
    }
    usleep((useconds_t)step_ms * 1000);
    waited_ms += step_ms;
  }

  /* Timed out - detach to leave the process running. sys_ptrace() clobbers
   * errno restoring ucred internally, so only the return code is trustworthy here. */
  int drc = sys_ptrace(PT_DETACH, pid, 0, 0);
  cr_log(drc == 0 ? "warn" : "error", "ptrace",
         "PT_ATTACH: pid=%d timed out after %dms, detach %s", (int)pid, timeout_ms,
         drc == 0 ? "ok" : "FAILED - process may remain stopped");
  return -2;
}


int
pt_detach(pid_t pid, int sig) {
  if(sys_ptrace(PT_DETACH, pid, 0, sig) == -1) {
    return -1;
  }

  return 0;
}





int
pt_step(int pid) {
  if(sys_ptrace(PT_STEP, pid, (caddr_t)1, 0)) {
    return -1;
  }

  if(waitpid(pid, 0, 0) < 0) {
    return -1;
  }

  return 0;
}


int
pt_continue(pid_t pid, int sig) {
  if(sys_ptrace(PT_CONTINUE, pid, (caddr_t)1, sig) == -1) {
    return -1;
  }

  return 0;
}


int
pt_getint(pid_t pid, intptr_t addr) {
  return sys_ptrace(PT_READ_D, pid, (caddr_t)addr, 0);
}


int
pt_setint(pid_t pid, intptr_t addr, int val) {
  return sys_ptrace(PT_WRITE_D, pid, (caddr_t)addr, val);
}


int
pt_getregs(pid_t pid, struct reg *r) {
  return sys_ptrace(PT_GETREGS, pid, (caddr_t)r, 0);
}


pid_t
pt_get_lwpid(pid_t pid) {
  struct ptrace_lwpinfo lwpinfo;

  memset(&lwpinfo, 0, sizeof(lwpinfo));
  if(sys_ptrace(PT_LWPINFO, pid, (caddr_t)&lwpinfo, sizeof(lwpinfo)) == -1) {
    return -1;
  }

  return lwpinfo.pl_lwpid;
}


int
pt_setregs(pid_t pid, const struct reg *r) {
  return sys_ptrace(PT_SETREGS, pid, (caddr_t)r, 0);
}


/* struct fpreg isn't exposed in userspace headers; PT_FPREGS_SIZE (pt.h)
 * matches its known size (the legacy FXSAVE area), so callers use a plain buffer. */
int
pt_getfpregs(pid_t pid, void *fpregs) {
  return sys_ptrace(PT_GETFPREGS, pid, (caddr_t)fpregs, 0);
}


int
pt_setfpregs(pid_t pid, void *fpregs) {
  return sys_ptrace(PT_SETFPREGS, pid, (caddr_t)fpregs, 0);
}


int
pt_copyin(pid_t pid, const void* buf, intptr_t addr, size_t len) {
  struct ptrace_io_desc iod = {
    .piod_op = PIOD_WRITE_D,
    .piod_offs = (void*)addr,
    .piod_addr = (void*)buf,
    .piod_len = len};
  return sys_ptrace(PT_IO, pid, (caddr_t)&iod, 0);
}




int
pt_setlong(pid_t pid, intptr_t addr, long val) {
  return pt_copyin(pid, &val, addr, sizeof(val));
}


int
pt_copyout(pid_t pid, intptr_t addr, void* buf, size_t len) {
  struct ptrace_io_desc iod = {
    .piod_op = PIOD_READ_D,
    .piod_offs = (void*)addr,
    .piod_addr = buf,
    .piod_len = len};
  return sys_ptrace(PT_IO, pid, (caddr_t)&iod, 0);
}





long
pt_call(pid_t pid, intptr_t addr, ...) {
  struct reg jmp_reg;
  struct reg bak_reg;
  va_list ap;

  if(pt_getregs(pid, &bak_reg)) {
    return -1;
  }

  memcpy(&jmp_reg, &bak_reg, sizeof(jmp_reg));
  jmp_reg.r_rip = addr;

  va_start(ap, addr);
  jmp_reg.r_rdi = va_arg(ap, int64_t);
  jmp_reg.r_rsi = va_arg(ap, int64_t);
  jmp_reg.r_rdx = va_arg(ap, int64_t);
  jmp_reg.r_rcx = va_arg(ap, int64_t);
  jmp_reg.r_r8  = va_arg(ap, int64_t);
  jmp_reg.r_r9  = va_arg(ap, int64_t);
  va_end(ap);

  if(pt_setregs(pid, &jmp_reg)) {
    return -1;
  }

  /* Single-step until the injected function returns, capped at PT_STEP_MAX -
   * a blocked/looping target would otherwise hang CheatRunner with the game frozen. */
#define PT_STEP_MAX 1000000
  int pt_steps = 0;
  while(jmp_reg.r_rsp <= bak_reg.r_rsp) {
    if(++pt_steps > PT_STEP_MAX) {
      pt_setregs(pid, &bak_reg);
      return -1;
    }
    if(pt_step(pid)) {
      pt_setregs(pid, &bak_reg);
      return -1;
    }
    if(pt_getregs(pid, &jmp_reg)) {
      pt_setregs(pid, &bak_reg);
      return -1;
    }
  }

  if(pt_setregs(pid, &bak_reg)) {
    return -1;
  }

  return jmp_reg.r_rax;
}


long
pt_syscall(pid_t pid, int sysno, ...) {
  intptr_t addr = pt_resolve(pid, "HoLVWNanBBc");
  struct reg jmp_reg;
  struct reg bak_reg;
  va_list ap;

  if(!addr) {
    return -1;
  } else {
    addr += 0xa;
  }

  if(pt_getregs(pid, &bak_reg)) {
    return -1;
  }

  memcpy(&jmp_reg, &bak_reg, sizeof(jmp_reg));
  jmp_reg.r_rip = addr;
  jmp_reg.r_rax = sysno;

  va_start(ap, sysno);
  jmp_reg.r_rdi = va_arg(ap, int64_t);
  jmp_reg.r_rsi = va_arg(ap, int64_t);
  jmp_reg.r_rdx = va_arg(ap, int64_t);
  jmp_reg.r_r10 = va_arg(ap, int64_t);
  jmp_reg.r_r8  = va_arg(ap, int64_t);
  jmp_reg.r_r9  = va_arg(ap, int64_t);
  va_end(ap);

  if(pt_setregs(pid, &jmp_reg)) {
    return -1;
  }

  int pt_steps2 = 0;
  while(jmp_reg.r_rsp <= bak_reg.r_rsp) {
    if(++pt_steps2 > PT_STEP_MAX) {
      pt_setregs(pid, &bak_reg);
      return -1;
    }
    if(pt_step(pid)) {
      pt_setregs(pid, &bak_reg);
      return -1;
    }
    if(pt_getregs(pid, &jmp_reg)) {
      pt_setregs(pid, &bak_reg);
      return -1;
    }
  }

  if(pt_setregs(pid, &bak_reg)) {
    return -1;
  }

  return jmp_reg.r_rax;
}


intptr_t
pt_mmap(pid_t pid, intptr_t addr, size_t len, int prot, int flags,
	int fd, off_t off) {
  return pt_syscall(pid, SYS_mmap, addr, len, prot, flags, fd, off);
}


int
pt_msync(pid_t pid, intptr_t addr, size_t len, int flags) {
  return (int)pt_syscall(pid, SYS_msync, addr, len, flags);
}


int
pt_munmap(pid_t pid, intptr_t addr, size_t len) {
  return (int)pt_syscall(pid, SYS_munmap, addr, len);
}


int
pt_mprotect(pid_t pid, intptr_t addr, size_t len, int prot) {
  return (int)pt_syscall(pid, SYS_mprotect, addr, len, prot);
}


int
pt_socket(pid_t pid, int domain, int type, int protocol) {
  return (int)pt_syscall(pid, SYS_socket, domain, type, protocol);
}


int
pt_setsockopt(pid_t pid, int fd, int level, int optname, intptr_t optval,
	      socklen_t optlen) {
  return (int)pt_syscall(pid, SYS_setsockopt, fd, level, optname, optval,
			 optlen, 0);
}



int
pt_close(pid_t pid, int fd) {
  return (int)pt_syscall(pid, SYS_close, fd);
}


int
pt_dup2(pid_t pid, int oldfd, int newfd) {
  return (int)pt_syscall(pid, SYS_dup2, oldfd, newfd);
}


int
pt_rdup(pid_t pid, pid_t other_pid, int fd) {
  return (int)pt_syscall(pid, 0x25b, other_pid, fd);
}


int
pt_pipe(pid_t pid, intptr_t pipefd) {
  intptr_t faddr = pt_resolve(pid, "-Jp7F+pXxNg");
  return (int)pt_call(pid, faddr, pipefd);
}


void
pt_perror(pid_t pid, const char *s) {
  intptr_t faddr = pt_resolve(pid, "9BcDykPmo1I");
  intptr_t addr = pt_call(pid, faddr);
  int err = pt_getint(pid, addr);
  /* Goes through klog because plain stdout is invisible there, and every LOG_PT_PERROR call site relies on it. */
  cr_log("warn", "ptrace", "%s: %s", s, strerror(err));
}
