#pragma once
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#ifdef __cplusplus
extern "C" {
#endif
intptr_t kernel_dynlib_dlsym(pid_t,uint32_t,const char*);
int kernel_proc_copyout(pid_t,intptr_t,void*,size_t);
#ifdef __cplusplus
}
#endif
