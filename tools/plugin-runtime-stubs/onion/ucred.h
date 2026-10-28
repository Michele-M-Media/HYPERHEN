#pragma once
#include <stdint.h>
#include <sys/types.h>
#ifdef __cplusplus
extern "C" {
#endif
uintptr_t set_ucred_to_ptrace(void);
int set_proc_authid(pid_t,uintptr_t);
#ifdef __cplusplus
}
#endif
