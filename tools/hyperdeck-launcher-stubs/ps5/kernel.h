#pragma once
#include <stdint.h>
#include <sys/types.h>
#ifdef __cplusplus
extern "C" {
#endif
uint64_t kernel_get_ucred_authid(pid_t);
int32_t kernel_set_ucred_authid(pid_t,uint64_t);
int kernel_dynlib_handle(pid_t,const char*,uint32_t*);
intptr_t kernel_dynlib_resolve(pid_t,uint32_t,const char*);
intptr_t kernel_dynlib_dlsym(pid_t,uint32_t,const char*);
#ifdef __cplusplus
}
#endif
