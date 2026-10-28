#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>

#include <ps5/kernel.h>

#include "priv_bootstrap.h"

#define JB_AUTHID 0x4801000000000013ULL

static cr_priv_status_t g_priv = {0};

static void add_warning(const char *msg) {
  if (g_priv.n_warnings < 6) {
    snprintf(g_priv.warnings[g_priv.n_warnings++], 128, "%s", msg);
  }
}

void
cr_priv_init(void) {
  pid_t mypid = getpid();

  printf("[priv] initializing CheatRunner privilege bootstrap\n");

  /* UID/GID check */
  g_priv.uid  = (int)getuid();
  g_priv.euid = (int)geteuid();
  g_priv.gid  = (int)getgid();
  g_priv.egid = (int)getegid();
  g_priv.uid_ok = (g_priv.uid == 0 && g_priv.euid == 0);

  if (g_priv.uid_ok) {
    printf("[priv] uid=%d gid=%d euid=%d egid=%d ok\n",
           g_priv.uid, g_priv.gid, g_priv.euid, g_priv.egid);
  } else {
    printf("[priv] warn: uid=%d gid=%d euid=%d egid=%d (expected 0)\n",
           g_priv.uid, g_priv.gid, g_priv.euid, g_priv.egid);
    add_warning("process is not root");
  }

  /* Rootvnode / sandbox escape */
  intptr_t rootvnode = kernel_get_root_vnode();
  g_priv.rootvnode_ok = (rootvnode != 0);
  g_priv.sandbox_ok = g_priv.rootvnode_ok;

  if (g_priv.sandbox_ok) {
    printf("[priv] sandbox/rootdir escape ok rootvnode=0x%lx\n", (long)rootvnode);
  } else {
    printf("[priv] warn: kernel_get_root_vnode() returned 0; sandbox escape uncertain\n");
    add_warning("sandbox escape not confirmed");
  }

  /* SCE auth/caps/attrs */
  g_priv.authid = kernel_get_ucred_authid(mypid);
  g_priv.authid_ok = (g_priv.authid == JB_AUTHID);

  uint8_t caps[16] = {0};
  int caps_rc = kernel_get_ucred_caps(mypid, caps);
  g_priv.caps_ok = (caps_rc == 0);
  if (g_priv.caps_ok) {
    for (int i = 0; i < 16; i++) {
      if (caps[i] != 0xff) { g_priv.caps_ok = 0; break; }
    }
  }

  uint8_t attrs[32] = {0};
  kernel_get_ucred_attrs(mypid, attrs);
  g_priv.attrs_ok = ((attrs[3] & 0x80) != 0); /* ptrace attribute, see sdk-master crt/patch.c */

  if (g_priv.authid_ok && g_priv.caps_ok && g_priv.attrs_ok) {
    printf("[priv] sce auth/caps ok authid=0x%llx\n", (unsigned long long)g_priv.authid);
  } else {
    printf("[priv] warn: sce authid=0x%llx authid_ok=%d caps_ok=%d attrs_ok=%d\n",
           (unsigned long long)g_priv.authid, g_priv.authid_ok, g_priv.caps_ok, g_priv.attrs_ok);
    if (!g_priv.authid_ok) add_warning("sce authid not set to ShellCore class");
    if (!g_priv.caps_ok)   add_warning("sce caps not fully set");
  }

  /* Kernel R/W check */
  intptr_t selfproc = kernel_get_proc(mypid);
  g_priv.kernel_rw_ok = (selfproc != 0);

  if (g_priv.kernel_rw_ok) {
    printf("[priv] kernel helpers ok proc=0x%lx\n", (long)selfproc);
  } else {
    printf("[priv] warn: kernel_get_proc(self) failed; kernel R/W unavailable\n");
    add_warning("kernel helpers unavailable");
  }

  /* kernel_mprotect self-test: change our own mapping to RWX and back */
  g_priv.kernel_mprotect_ok = 0;
  void *test_page = mmap(NULL, 0x4000, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (test_page != MAP_FAILED) {
    int mrc = kernel_mprotect(mypid, (intptr_t)test_page, 0x4000,
                              PROT_READ | PROT_WRITE | PROT_EXEC);
    if (mrc == 0) {
      g_priv.kernel_mprotect_ok = 1;
      kernel_mprotect(mypid, (intptr_t)test_page, 0x4000, PROT_READ | PROT_WRITE);
    }
    munmap(test_page, 0x4000);
  }

  if (g_priv.kernel_mprotect_ok) {
    printf("[priv] kernel_mprotect ok\n");
  } else {
    printf("[priv] warn: kernel_mprotect self-test failed\n");
    add_warning("kernel_mprotect test failed");
  }

  /* Memory patch capability summary */
  g_priv.can_patch_game = (g_priv.uid_ok && g_priv.kernel_rw_ok &&
                            g_priv.authid_ok && g_priv.caps_ok &&
                            g_priv.kernel_mprotect_ok);

  if (g_priv.can_patch_game) {
    printf("[priv] memory patch capability ok\n");
  } else {
    printf("[priv] warn: privilege bootstrap incomplete; cheat memory patch may fail\n");
    add_warning("canPatchGameMemory=false");
  }
}

const cr_priv_status_t *
cr_priv_get(void) {
  return &g_priv;
}

int
cr_priv_can_patch_game_memory(void) {
  return g_priv.can_patch_game;
}
