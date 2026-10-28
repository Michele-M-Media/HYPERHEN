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

#include <elf.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/mman.h>
#include <sys/ptrace.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/sysctl.h>

#include <sys/event.h>
#include <sys/un.h>
#include <sys/user.h>
#include <sys/wait.h>

#include <ps5/kernel.h>

#include <onion/elfldr.h>
#include <onion/elfldr_log.h>
#include <onion/pt.h>
#include "elf-image.h"
extern intptr_t hhp_elf_map(pid_t, const uint8_t *, size_t);


#ifndef IPV6_2292PKTOPTIONS
#define IPV6_2292PKTOPTIONS 25
#endif


/**
 * Convenient macros.
 **/
#define ROUND_PG(x) (((x) + (PAGE_SIZE - 1)) & ~(PAGE_SIZE - 1))
#define TRUNC_PG(x) ((x) & ~(PAGE_SIZE - 1))
#define PFLAGS(x)   ((((x) & PF_R) ? PROT_READ  : 0) | \
		     (((x) & PF_W) ? PROT_WRITE : 0) | \
		     (((x) & PF_X) ? PROT_EXEC  : 0))


/**
 * Context structure for the ELF loader.
 **/
typedef struct elfldr_ctx {
  uint8_t* elf;
  pid_t    pid;

  intptr_t base_addr;
  size_t   base_size;
  void*    base_mirror;
} elfldr_ctx_t;

/**
 * Known-good system eboot used as the donor process for bare ELF payloads.
 */
static const char *SceSpZeroConf = "/system/vsh/app/NPXS40112/eboot.bin";

static int
size_add_overflow(size_t a, size_t b, size_t *out) {
  if(!out) {
    return -1;
  }
  if(a > (size_t)-1 - b) {
    return -1;
  }
  *out = a + b;
  return 0;
}

static int
size_mul_overflow(size_t a, size_t b, size_t *out) {
  if(!out) {
    return -1;
  }
  if(a != 0 && b > (size_t)-1 / a) {
    return -1;
  }
  *out = a * b;
  return 0;
}

static int
range_end_overflow(size_t off, size_t len, size_t *end) {
  return size_add_overflow(off, len, end);
}

static int
range_count_end_overflow(size_t off, size_t count, size_t elem_size,
                         size_t *end) {
  size_t len;
  if(size_mul_overflow(count, elem_size, &len)) {
    return -1;
  }
  return range_end_overflow(off, len, end);
}


int
elfldr_sanity_check(uint8_t *elf, size_t elf_size) {
  if(!elf || elf_size < sizeof(Elf64_Ehdr)) {
    return -1;
  }

  Elf64_Ehdr *ehdr = (Elf64_Ehdr*)elf;
  if(ehdr->e_ident[0] != 0x7f || ehdr->e_ident[1] != 'E' ||
     ehdr->e_ident[2] != 'L'  || ehdr->e_ident[3] != 'F') {
    return -1;
  }
  if(ehdr->e_phentsize != sizeof(Elf64_Phdr) ||
     ehdr->e_shentsize != sizeof(Elf64_Shdr)) {
    return -1;
  }

  size_t end = 0;
  if(range_count_end_overflow(ehdr->e_phoff, ehdr->e_phnum,
                              sizeof(Elf64_Phdr), &end) ||
     end > elf_size) {
    return -1;
  }
  if(range_count_end_overflow(ehdr->e_shoff, ehdr->e_shnum,
                              sizeof(Elf64_Shdr), &end) ||
     end > elf_size) {
    return -1;
  }

  Elf64_Phdr *phdr = (Elf64_Phdr*)(elf + ehdr->e_phoff);
  for(int i=0; i<ehdr->e_phnum; i++) {
    if(range_end_overflow(phdr[i].p_offset, phdr[i].p_filesz, &end) ||
       end > elf_size) {
      return -1;
    }
  }

  Elf64_Shdr *shdr = (Elf64_Shdr*)(elf + ehdr->e_shoff);
  for(int i=0; i<ehdr->e_shnum; i++) {
    if(shdr[i].sh_type == SHT_NOBITS) {
      continue;
    }
    if(range_end_overflow(shdr[i].sh_offset, shdr[i].sh_size, &end) ||
       end > elf_size) {
      return -1;
    }
  }

  return 0;
}

/**
* Parse a R_X86_64_RELATIVE relocatable.
**/
static int
r_relative(elfldr_ctx_t *ctx, Elf64_Rela* rela) {
  intptr_t* loc = ctx->base_mirror + rela->r_offset;
  intptr_t val = ctx->base_addr + rela->r_addend;

  *loc = val;

  return 0;
}


/**
 * Parse a PT_LOAD program header.
 **/
static int
data_load(elfldr_ctx_t *ctx, Elf64_Phdr *phdr) {
  void* data = ctx->base_mirror + phdr->p_vaddr;

  if(!phdr->p_memsz) {
    return 0;
  }

  if(!phdr->p_filesz) {
    return 0;
  }

  memcpy(data, ctx->elf+phdr->p_offset, phdr->p_filesz);

  return 0;
}



/**
 * Load an ELF into the address space of a process with the given pid.
 **/
intptr_t
elfldr_load(pid_t pid, uint8_t *elf) {
  Elf64_Ehdr *ehdr = (Elf64_Ehdr*)elf;
  Elf64_Phdr *phdr = (Elf64_Phdr*)(elf + ehdr->e_phoff);
  Elf64_Shdr *shdr = (Elf64_Shdr*)(elf + ehdr->e_shoff);

  elfldr_ctx_t ctx = {.elf = elf, .pid=pid};

  size_t min_vaddr = -1;
  size_t max_vaddr = 0;

  int error = 0;

  // Compute size of virtual memory region (PT_LOAD only).
  for(int i=0; i<ehdr->e_phnum; i++) {
    if(phdr[i].p_type != PT_LOAD) {
      continue;
    }
    if(phdr[i].p_vaddr < min_vaddr) {
      min_vaddr = phdr[i].p_vaddr;
    }

    if(max_vaddr < phdr[i].p_vaddr + phdr[i].p_memsz) {
      max_vaddr = phdr[i].p_vaddr + phdr[i].p_memsz;
    }
  }

  min_vaddr = TRUNC_PG(min_vaddr);
  max_vaddr = ROUND_PG(max_vaddr);
  ctx.base_size = max_vaddr - min_vaddr;

  int flags = MAP_PRIVATE | MAP_ANONYMOUS;
  int prot = PROT_READ | PROT_WRITE;
  if(ehdr->e_type == ET_DYN) {
    ctx.base_addr = 0;
  } else if(ehdr->e_type == ET_EXEC) {
    ctx.base_addr = min_vaddr;
    flags |= MAP_FIXED;
  } else {
    LOG_PUTS("elfldr_load: ELF type not supported");
    return 0;
  }

  if(!(ctx.base_mirror=malloc(ctx.base_size))) {
    LOG_PERROR("malloc");
    return 0;
  }

  // Reserve an address space of sufficient size.
  if((ctx.base_addr=pt_mmap(pid, ctx.base_addr, ctx.base_size, prot,
			    flags, -1, 0)) == -1) {
    LOG_PT_PERROR(pid, "pt_mmap");
    free(ctx.base_mirror);
    return 0;
  }

  // Parse program headers.
  for(int i=0; i<ehdr->e_phnum && !error; i++) {
    switch(phdr[i].p_type) {
    case PT_LOAD:
      error = data_load(&ctx, &phdr[i]);
      break;
    }
  }

  // Apply relocations.
  for(int i=0; i<ehdr->e_shnum && !error; i++) {
    if(shdr[i].sh_type != SHT_RELA) {
      continue;
    }

    Elf64_Rela* rela = (Elf64_Rela*)(elf + shdr[i].sh_offset);
    for(int j=0; j<shdr[i].sh_size/sizeof(Elf64_Rela); j++) {
      switch(rela[j].r_info & 0xffffffffl) {
      case R_X86_64_RELATIVE:
	error = r_relative(&ctx, &rela[j]);
	break;
      }
    }
  }

  if(pt_copyin(ctx.pid, ctx.base_mirror, ctx.base_addr, ctx.base_size)) {
    LOG_PERROR("pt_copyin");
    error = 1;
  }

  // Set protection bits on mapped segments.
  for(int i=0; i<ehdr->e_phnum && !error; i++) {
    if(phdr[i].p_type != PT_LOAD || phdr[i].p_memsz == 0) {
      continue;
    }

    if(phdr[i].p_flags & PF_X) {
      if(kernel_mprotect(pid, ctx.base_addr + phdr[i].p_vaddr,
                         ROUND_PG(phdr[i].p_memsz),
                         PFLAGS(phdr[i].p_flags))) {
	LOG_PERROR("kernel_mprotect");
	error = 1;
      }
    } else {
      if(pt_mprotect(pid, ctx.base_addr + phdr[i].p_vaddr,
		     ROUND_PG(phdr[i].p_memsz),
		     PFLAGS(phdr[i].p_flags))) {
	LOG_PT_PERROR(pid, "pt_mprotect");
	error = 1;
      }
    }
  }

  if(pt_msync(pid, ctx.base_addr, ctx.base_size, MS_SYNC)) {
    LOG_PT_PERROR(pid, "pt_msync");
    error = 1;
  }

  free(ctx.base_mirror);

  if(error) {
    pt_munmap(pid, ctx.base_addr, ctx.base_size);
    return 0;
  }

  return ctx.base_addr + ehdr->e_entry;
}


/**
 * Create payload args in the address space of the process with the given pid.
 **/
static intptr_t
elfldr_payload_args_impl(pid_t pid, int plugin_abi) {
  int victim_sock;
  int master_sock;
  intptr_t buf;
  int pipe0;
  int pipe1;

  if((buf=pt_mmap(pid, 0, PAGE_SIZE, PROT_READ | PROT_WRITE,
		  MAP_ANONYMOUS | MAP_PRIVATE, -1, 0)) == -1) {
    LOG_PT_PERROR(pid, "pt_mmap");
    return 0;
  }

  if((master_sock=pt_socket(pid, AF_INET6, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
    LOG_PT_PERROR(pid, "pt_socket");
    return 0;
  }

  pt_setint(pid, buf+0x00, 20);
  pt_setint(pid, buf+0x04, IPPROTO_IPV6);
  pt_setint(pid, buf+0x08, IPV6_TCLASS);
  pt_setint(pid, buf+0x0c, 0);
  pt_setint(pid, buf+0x10, 0);
  pt_setint(pid, buf+0x14, 0);
  if(pt_setsockopt(pid, master_sock, IPPROTO_IPV6, IPV6_2292PKTOPTIONS, buf, 24)) {
    LOG_PT_PERROR(pid, "pt_setsockopt");
    return 0;
  }

  if((victim_sock=pt_socket(pid, AF_INET6, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
    LOG_PT_PERROR(pid, "pt_socket");
    return 0;
  }

  pt_setint(pid, buf+0x00, 0);
  pt_setint(pid, buf+0x04, 0);
  pt_setint(pid, buf+0x08, 0);
  pt_setint(pid, buf+0x0c, 0);
  pt_setint(pid, buf+0x10, 0);
  if(pt_setsockopt(pid, victim_sock, IPPROTO_IPV6, IPV6_PKTINFO, buf, 20)) {
    LOG_PT_PERROR(pid, "pt_setsockopt");
    return 0;
  }

  if(kernel_overlap_sockets(pid, master_sock, victim_sock)) {
    LOG_PUTS("kernel_overlap_sockets failed");
    return 0;
  }

  if(pt_pipe(pid, buf)) {
    LOG_PT_PERROR(pid, "pt_pipe");
    return 0;
  }
  pipe0 = pt_getint(pid, buf);
  pipe1 = pt_getint(pid, buf+4);

  intptr_t args       = buf;
  intptr_t rwpipe     = buf + 0x100;
  intptr_t rwpair     = buf + 0x200;
  intptr_t kpipe_addr = kernel_get_proc_file(pid, pipe0);
  intptr_t payloadout = buf + 0x300;
  intptr_t getpid      = pt_resolve(pid, "HoLVWNanBBc");

  if(plugin_abi) {
    /* TacHen/SDK v0.43 plugin ABI. Keep legacy internal payloads unchanged. */
    intptr_t dlsym = pt_resolve(pid, "LwG8g3niqwA");
    uint8_t call[5];
    int32_t relative;
    if(!dlsym || pt_copyout(pid, dlsym + 4, call, sizeof(call)) ||
       call[0] != 0xe8) return 0;
    memcpy(&relative, call + 1, sizeof(relative));
    getpid = dlsym + 9 + relative;
  }
  if(plugin_abi && (!getpid || !kpipe_addr)) return 0;

  pt_setlong(pid, args + 0x00, getpid);
  pt_setlong(pid, args + 0x08, rwpipe);
  pt_setlong(pid, args + 0x10, rwpair);
  pt_setlong(pid, args + 0x18, kpipe_addr);
  pt_setlong(pid, args + 0x20, KERNEL_ADDRESS_DATA_BASE);
  pt_setlong(pid, args + 0x28, payloadout);
  pt_setint(pid, rwpipe + 0, pipe0);
  pt_setint(pid, rwpipe + 4, pipe1);
  pt_setint(pid, rwpair + 0, master_sock);
  pt_setint(pid, rwpair + 4, victim_sock);
  pt_setint(pid, payloadout, 0);

  return args;
}

intptr_t elfldr_payload_args(pid_t pid) {
  return elfldr_payload_args_impl(pid, 0);
}


/**
 * Prepare registers of a process for execution of an ELF.
 **/
static int
elfldr_prepare_exec(pid_t pid, uint8_t *elf, size_t size, int plugin_abi) {
  intptr_t entry;
  intptr_t args;
  struct reg r;

  if(pt_getregs(pid, &r)) {
    LOG_PERROR("pt_getregs");
    return -1;
  }

  if(!(entry=plugin_abi ? hhp_elf_map(pid, elf, size) : elfldr_load(pid, elf))) {
    LOG_PUTS("elfldr_load failed");
    return -1;
  }

  if(!(args=elfldr_payload_args_impl(pid, plugin_abi))) {
    LOG_PUTS("elfldr_payload_args failed");
    return -1;
  }

  if(plugin_abi) r.r_rsp &= ~0xfl;
  if(pt_setlong(pid, r.r_rsp - 8, r.r_rip) && plugin_abi) return -1;
  r.r_rsp -= 8;
  r.r_rip = entry;
  r.r_rdi = args;

  if(pt_setregs(pid, &r)) {
    LOG_PERROR("pt_setregs");
    return -1;
  }

  return 0;
}


int
elfldr_set_procname(pid_t pid, const char* name) {
  intptr_t buf;

  if(!name || !name[0]) {
    name = "payload.elf";
  }

  if((buf=pt_mmap(pid, 0, PAGE_SIZE, PROT_READ | PROT_WRITE,
		  MAP_ANONYMOUS | MAP_PRIVATE, -1, 0)) == -1) {
    LOG_PT_PERROR(pid, "pt_mmap");
    return -1;
  }

  pt_copyin(pid, name, buf, strlen(name)+1);
  pt_syscall(pid, SYS_thr_set_name, -1, buf);
  pt_msync(pid, buf, PAGE_SIZE, MS_SYNC);
  pt_munmap(pid, buf, PAGE_SIZE);

  return 0;
}


static int
elfldr_exec_impl(pid_t pid, int stdio, uint8_t* elf, size_t size, int plugin_abi) {
  uint8_t caps[16];
  intptr_t jaildir;
  intptr_t rootdir;
  uint64_t authid;
  int error = 0;

  if(pid <= 1 || !elf) {
    return -1;
  }

  jaildir = kernel_get_proc_jaildir(pid);
  if(!(rootdir=kernel_get_proc_rootdir(pid))) {
    LOG_PUTS("kernel_get_proc_rootdir failed");
    pt_detach(pid, 0);
    return -1;
  }
  if(kernel_get_ucred_caps(pid, caps)) {
    LOG_PUTS("kernel_get_ucred_caps failed");
    pt_detach(pid, 0);
    return -1;
  }
  if(!(authid=kernel_get_ucred_authid(pid))) {
    LOG_PUTS("kernel_get_ucred_authid failed");
    pt_detach(pid, 0);
    return -1;
  }

  if(elfldr_raise_privileges(pid)) {
    LOG_PUTS("Unable to raise privileges");
    pt_detach(pid, 0);
    return -1;
  }

  if(stdio > 0) {
    stdio = pt_rdup(pid, getpid(), stdio);

    pt_close(pid, STDERR_FILENO);
    pt_close(pid, STDOUT_FILENO);
    pt_close(pid, STDIN_FILENO);

    pt_dup2(pid, stdio, STDIN_FILENO);
    pt_dup2(pid, stdio, STDOUT_FILENO);
    pt_dup2(pid, stdio, STDERR_FILENO);

    pt_close(pid, stdio);
  }

  if(elfldr_prepare_exec(pid, elf, size, plugin_abi)) {
    error = -1;
  }

  if(kernel_set_proc_jaildir(pid, jaildir)) {
    LOG_PUTS("kernel_set_proc_jaildir failed");
    error = -1;
  }
  if(kernel_set_proc_rootdir(pid, rootdir)) {
    LOG_PUTS("kernel_set_proc_rootdir failed");
    error = -1;
  }

  if(kernel_set_ucred_caps(pid, caps)) {
    LOG_PUTS("kernel_set_ucred_caps failed");
    error = -1;
  }
  if(kernel_set_ucred_authid(pid, authid)) {
    LOG_PUTS("kernel_set_ucred_authid failed");
    error = -1;
  }

  if(pt_detach(pid, plugin_abi && error ? SIGKILL : 0)) {
    LOG_PERROR("pt_detach");
    error = -1;
  }

  return error;
}

int elfldr_exec(pid_t pid, int stdio, uint8_t *elf) {
  return elfldr_exec_impl(pid, stdio, elf, 0, 0);
}


static int
elfldr_set_heap_size(pid_t pid, ssize_t size) {
  intptr_t sceLibcHeapSize;
  intptr_t sceLibcParam;
  intptr_t sceProcParam;
  intptr_t Need_sceLibc;

  if(!(sceProcParam=pt_sceKernelGetProcParam(pid))) {
    LOG_PT_PERROR(pid, "pt_sceKernelGetProcParam");
    return -1;
  }

  if(pt_copyout(pid, sceProcParam+56, &sceLibcParam,
		sizeof(sceLibcParam))) {
    LOG_PERROR("pt_copyout");
    return -1;
  }

  if(pt_copyout(pid, sceLibcParam+16, &sceLibcHeapSize,
		sizeof(sceLibcHeapSize))) {
    LOG_PERROR("pt_copyout");
    return -1;
  }

  if(pt_setlong(pid, sceLibcHeapSize, size)) {
    LOG_PERROR("pt_setlong");
    return -1;
  }

  if(size != -1) {
    return 0;
  }

  if(pt_copyout(pid, sceLibcParam+72, &Need_sceLibc,
		sizeof(Need_sceLibc))) {
    LOG_PERROR("pt_copyout");
    return -1;
  }

  return pt_setlong(pid, sceLibcParam+32, Need_sceLibc);
}


static int
sys_budget_set(long budget) {
  return __syscall(0x23b, budget);
}


static int
elfldr_rfork_entry(void* argv) {
  if(sys_budget_set(0)) {
    LOG_PERROR("sys_budget_set");
    return -1;
  }
  if(open("/dev/deci_stdin", O_RDONLY) < 0) {
    LOG_PERROR("open");
    return -1;
  }
  if(open("/dev/deci_stdout", O_WRONLY) < 0) {
    LOG_PERROR("open");
    return -1;
  }
  if(open("/dev/deci_stderr", O_WRONLY) < 0) {
    LOG_PERROR("open");
    return -1;
  }

  if(ptrace(PT_TRACE_ME, 0, 0, 0)) {
    LOG_PERROR("ptrace");
    return -1;
  }

  execve(SceSpZeroConf, argv, 0);
  LOG_PERROR("execve");
  return -1;
}

/* Dedicated plugin spawn follows TacHen's bounded stopped-child handshake.
 * Legacy internal payload startup keeps its existing kqueue route. */
static int plugin_wait_stop(pid_t pid, int signal_number, unsigned timeout_ms) {
  for(unsigned elapsed=0; elapsed<timeout_ms; elapsed+=10) {
    int status=0;
    pid_t waited=waitpid(pid, &status, WNOHANG | WUNTRACED);
    if(waited==pid)
      return WIFSTOPPED(status) && WSTOPSIG(status)==signal_number ? 0 : -1;
    if(waited<0 && errno!=EINTR) return -1;
    usleep(10000);
  }
  return -1;
}

static void plugin_abort_child(pid_t pid, void *stack) {
  (void)kill(pid, SIGKILL);
  for(unsigned attempt=0;attempt<100;++attempt) {
    int status=0; pid_t waited=waitpid(pid,&status,WNOHANG | WUNTRACED);
    if((waited==pid && (WIFEXITED(status)||WIFSIGNALED(status))) ||
       (kill(pid,0)<0 && errno==ESRCH)) { free(stack); return; }
    usleep(1000);
  }
  /* An uncertain rfork child still owns the shared stack: never free it. */
}

#define ELFLDR_PTRACE_AUTHID 0x4800000000010003ULL

typedef struct elfldr_service_ptrace_scope {
  uint64_t authid;
  uint8_t caps[16];
  int restore_needed;
} elfldr_service_ptrace_scope_t;

static int
elfldr_service_ptrace_leave(elfldr_service_ptrace_scope_t *scope) {
  int error = 0;
  if(!scope->restore_needed) {
    return 0;
  }
  if(kernel_set_ucred_authid(getpid(), scope->authid)) {
    error = -1;
  }
  if(kernel_set_ucred_caps(getpid(), scope->caps)) {
    error = -1;
  }
  if(!error) {
    scope->restore_needed = 0;
  }
  return error;
}

/* The package helper is born while HyperDeck is SYSTEM so AppInstUtil keeps
 * the same context as websrv. Only its parent's remote-loader window needs
 * PTRACE, and it must always be restored before returning to the web server. */
static int
elfldr_service_ptrace_enter(elfldr_service_ptrace_scope_t *scope) {
  static const uint8_t caps[16] = {0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
                                   0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff};
  memset(scope, 0, sizeof(*scope));
  if(!(scope->authid=kernel_get_ucred_authid(getpid())) ||
     kernel_get_ucred_caps(getpid(), scope->caps)) {
    return -1;
  }
  scope->restore_needed = 1;
  if(kernel_set_ucred_authid(getpid(), ELFLDR_PTRACE_AUTHID) ||
     kernel_set_ucred_caps(getpid(), caps)) {
    (void)elfldr_service_ptrace_leave(scope);
    return -1;
  }
  return 0;
}

static void
elfldr_abort_child(pid_t pid) {
  if(pt_detach(pid, SIGKILL)) {
    (void)kill(pid, SIGKILL);
  }
}


static pid_t
elfldr_spawn_impl(int stdio, char* const argv[], uint8_t* elf,
                  size_t payload_size, int plugin_abi,
                  int prepare_before_relocation) {
  uint8_t int3instr = 0xcc;
  struct kevent evt;
  intptr_t brkpoint;
  uint8_t orginstr;
  void *stack;
  pid_t pid;
  int kq;
  int result = -1;
  elfldr_service_ptrace_scope_t service_scope = {0};

  HhpElfImage checked;
  if(!argv || !argv[0] || !elf || payload_size < sizeof(Elf64_Ehdr) ||
     (plugin_abi ? hhp_elf_inspect(elf, payload_size, &checked, NULL, 0)
                 : elfldr_sanity_check(elf, payload_size))) {
    return -1;
  }

  kq = plugin_abi ? -1 : kqueue();
  if(!plugin_abi && kq < 0) {
    LOG_PERROR("kqueue");
    return -1;
  }

  if(!(stack=malloc(PAGE_SIZE))) {
    LOG_PERROR("malloc");
    close(kq);
    return -1;
  }

  if((pid=rfork_thread(RFPROC | RFCFDG | RFMEM,
		       (uint8_t*)stack + PAGE_SIZE - 8,
		       elfldr_rfork_entry, (void*)argv)) < 0) {
    LOG_PERROR("rfork_thread");
    free(stack);
    close(kq);
    return -1;
  }

  if(plugin_abi) {
    if(plugin_wait_stop(pid,SIGTRAP,10000)) {
      plugin_abort_child(pid,stack); return -1;
    }
  } else {
    EV_SET(&evt, pid, EVFILT_PROC, EV_ADD, NOTE_EXEC, 0, 0);
    if(kevent(kq, &evt, 1, &evt, 1, 0) < 0) {
      LOG_PERROR("kevent"); free(stack); close(kq); return -1;
    }
    if(waitpid(pid, 0, 0) < 0) {
      LOG_PERROR("waitpid"); free(stack); close(kq); return -1;
    }
  }

  free(stack);
  if(kq >= 0) close(kq);

  /* websrv prepares its fixed package-installer child before the platform's
   * process-parameter relocation. Keep this ordering isolated to callers
   * that explicitly request the service path; normal payload and plugin
   * launches retain their established behavior. */
  if(prepare_before_relocation && elfldr_raise_privileges(pid)) {
    LOG_PUTS("Unable to prepare service child");
    (void)kill(pid, SIGKILL);
    (void)waitpid(pid, 0, 0);
    goto done;
  }
  if(prepare_before_relocation && elfldr_service_ptrace_enter(&service_scope)) {
    LOG_PUTS("Unable to enter service ptrace context");
    (void)kill(pid, SIGKILL);
    (void)waitpid(pid, 0, 0);
    goto done;
  }

  if(pt_syscall(pid, 599)) {
    LOG_PT_PERROR(pid, "sys_dynlib_process_needed_and_relocate");
    elfldr_abort_child(pid);
    goto done;
  }

  elfldr_set_heap_size(pid, -1);

  if(!(brkpoint=kernel_dynlib_entry_addr(pid, 0))) {
    LOG_PUTS("kernel_dynlib_entry_addr failed");
    elfldr_abort_child(pid);
    goto done;
  }
  brkpoint += 58;

  if(kernel_mprotect(pid, brkpoint, PAGE_SIZE,
                     PROT_READ | PROT_WRITE | PROT_EXEC)) {
    LOG_PUTS("kernel_mprotect failed");
    elfldr_abort_child(pid);
    goto done;
  }

  if(pt_copyout(pid, brkpoint, &orginstr, sizeof(orginstr))) {
    LOG_PERROR("pt_copyout");
    elfldr_abort_child(pid);
    goto done;
  }
  if(pt_copyin(pid, &int3instr, brkpoint, sizeof(int3instr))) {
    LOG_PERROR("pt_copyin");
    elfldr_abort_child(pid);
    goto done;
  }

  if(pt_continue(pid, SIGCONT)) {
    LOG_PERROR("pt_continue");
    elfldr_abort_child(pid);
    goto done;
  }
  if(plugin_abi ? plugin_wait_stop(pid,SIGTRAP,2000) : waitpid(pid, 0, 0) == -1) {
    LOG_PERROR("waitpid");
    elfldr_abort_child(pid);
    goto done;
  }
  if(plugin_abi) {
    struct reg stopped;
    if(pt_getregs(pid,&stopped) || stopped.r_rip != (uint64_t)brkpoint+1) {
      elfldr_abort_child(pid); goto done;
    }
  }
  if(pt_copyin(pid, &orginstr, brkpoint, sizeof(orginstr))) {
    LOG_PERROR("pt_copyin");
    elfldr_abort_child(pid);
    goto done;
  }

  elfldr_set_procname(pid, argv[0]);
  if(elfldr_exec_impl(pid, stdio, elf, payload_size, plugin_abi)) {
    kill(pid, SIGKILL);
    goto done;
  }

  result = pid;
done:
  if(elfldr_service_ptrace_leave(&service_scope)) {
    LOG_PUTS("Unable to restore service ptrace context");
    return -1;
  }
  return result;
}

pid_t elfldr_spawn(int stdio, char *const argv[], uint8_t *elf, size_t size) {
  return elfldr_spawn_impl(stdio, argv, elf, size, 0, 0);
}

pid_t elfldr_spawn_service(int stdio, char *const argv[], uint8_t *elf,
                           size_t size) {
  return elfldr_spawn_impl(stdio, argv, elf, size, 0, 1);
}

pid_t elfldr_spawn_plugin(int stdio, char *const argv[], uint8_t *elf, size_t size) {
  return elfldr_spawn_impl(stdio, argv, elf, size, 1, 0);
}


int
elfldr_read(int fd, uint8_t** elf, size_t* elf_size) {
  Elf64_Ehdr ehdr;
  Elf64_Shdr *shdr;
  uint8_t* buf;
  uint8_t* bak;
  size_t table_size;
  size_t size;
  size_t shend;
  size_t rem;

  if(!elf || !elf_size) {
    return -1;
  }
  *elf = NULL;
  *elf_size = 0;

  if(recv(fd, &ehdr, sizeof(ehdr), MSG_WAITALL) != sizeof(ehdr)) {
    return -1;
  }

  if(ehdr.e_ident[0] != 0x7f || ehdr.e_ident[1] != 'E' ||
     ehdr.e_ident[2] != 'L'  || ehdr.e_ident[3] != 'F' ||
     ehdr.e_shentsize != sizeof(Elf64_Shdr)) {
    errno = ENOEXEC;
    return -1;
  }

  if(range_count_end_overflow(ehdr.e_shoff, ehdr.e_shnum,
                              sizeof(Elf64_Shdr), &table_size) ||
     table_size < sizeof(ehdr)) {
    errno = ENOEXEC;
    return -1;
  }
  size = table_size;

  if(!(buf=(uint8_t*)malloc(size))) {
    return -1;
  }

  memcpy(buf, &ehdr, sizeof(ehdr));
  rem = size - sizeof(ehdr);
  if(recv(fd, buf + sizeof(ehdr), rem, MSG_WAITALL) != (ssize_t)rem) {
    free(buf);
    return -1;
  }

  shend = 0;
  shdr = (Elf64_Shdr*)(buf + ehdr.e_shoff);
  for(int i=0; i<ehdr.e_shnum; i++) {
    if(shdr[i].sh_type == SHT_NOBITS) {
      continue;
    }
    size_t end = 0;
    if(range_end_overflow(shdr[i].sh_offset, shdr[i].sh_size, &end)) {
      free(buf);
      errno = ENOEXEC;
      return -1;
    }
    if(end > shend) {
      shend = end;
    }
  }

  if(shend <= size) {
    *elf = buf;
    *elf_size = size;
    return 0;
  }

  bak = buf;
  if(!(buf=(uint8_t*)realloc(buf, shend))) {
    free(bak);
    return -1;
  }

  rem = shend - size;
  if(recv(fd, buf + size, rem, MSG_WAITALL) != (ssize_t)rem) {
    free(buf);
    return -1;
  }

  if(elfldr_sanity_check(buf, shend)) {
    free(buf);
    errno = ENOEXEC;
    return -1;
  }

  *elf = buf;
  *elf_size = shend;
  return 0;
}


/**
 * Escape jail and raise privileges (bootstrapper / self-elevate path).
 * Does not use ptrace — safe to call on self without attach.
 **/
int
elfldr_raise_privileges(pid_t pid) {
  static const uint8_t caps[16] = {0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
				   0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff};
  intptr_t vnode;

  if(!(vnode=kernel_get_root_vnode())) {
    return -1;
  }
  if(kernel_set_proc_rootdir(pid, vnode)) {
    return -1;
  }
  if(kernel_set_proc_jaildir(pid, 0)) {
    return -1;
  }
  if(kernel_set_ucred_uid(pid, 0)) {
    return -1;
  }
  if(kernel_set_ucred_caps(pid, caps)) {
    return -1;
  }

  return 0;
}
