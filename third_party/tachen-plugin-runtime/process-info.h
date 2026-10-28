// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef TACHEN_PROCESS_INFO_H
#define TACHEN_PROCESS_INFO_H
#ifdef __cplusplus
extern "C" {
#endif
#include <sys/types.h>
#include <sys/user.h>
int hhp_process_info(pid_t pid,struct kinfo_proc* out);
#endif

#ifdef __cplusplus
}
#endif

