#pragma once

#include <stdint.h>

typedef struct cr_priv_status {
  int  uid;
  int  euid;
  int  gid;
  int  egid;
  int  uid_ok;              /* uid == 0 && euid == 0 */
  int  rootvnode_ok;        /* kernel_get_root_vnode() returned non-zero */
  int  sandbox_ok;          /* rootdir/jaildir patched (rootvnode_ok serves as proxy) */
  int  authid_ok;           /* ucred authid == JB_AUTHID */
  int  caps_ok;             /* all 16 ucred cap bytes == 0xff */
  int  attrs_ok;            /* ucred attrs & 0x80 set */
  int  kernel_rw_ok;        /* kernel_get_proc(self) returned non-zero */
  int  kernel_mprotect_ok;  /* kernel_mprotect self-test passed */
  int  can_patch_game;      /* uid+kernel+authid+caps+mprotect all ok */
  uint64_t authid;
  /* up to 6 warning strings, zero-terminated */
  char warnings[6][128];
  int  n_warnings;
} cr_priv_status_t;

/* Call once at startup (before services). Logs results to stdout with [priv] prefix. */
void cr_priv_init(void);

/* Returns pointer to the cached status (set after cr_priv_init). */
const cr_priv_status_t *cr_priv_get(void);

/* Quick test: returns 1 if privilege bootstrap confirmed ok, 0 otherwise. */
int cr_priv_can_patch_game_memory(void);
