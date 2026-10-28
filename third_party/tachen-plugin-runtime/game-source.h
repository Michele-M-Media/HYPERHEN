// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
#include <stddef.h>
int hhp_game_identity(int pid,const char *generation,const char *title);
// Open the uniquely matched, non-symlink running sandbox directory. Caller
// closes the descriptor and uses descriptor-relative paths beneath it.
int hhp_game_sandbox_open(int pid,const char *generation,const char *title,char *reason,size_t capacity);
int hhp_game_sandbox_open_path(int pid,const char *generation,const char *title,char *path,size_t pathCapacity,char *reason,size_t capacity);
// Caller frees *bytes. Requires native operation ownership when called from a
// worker. The metadata digest binds JS's exact version to the native operation.
int hhp_game_metadata_read(int pid,const char *generation,const char *title,
  unsigned char **bytes,size_t *length,char *reason,size_t capacity);
// PS4 compatibility games may omit param.sfo from their mounted app0. In that
// case only, use the registered title SFO predating this exact live process.
// The extended call reports this provenance rather than claiming sandbox data.
int hhp_game_metadata_read_ex(int pid,const char *generation,const char *title,
  unsigned char **bytes,size_t *length,char *reason,size_t capacity,int *registered);

#ifdef __cplusplus
}
#endif

