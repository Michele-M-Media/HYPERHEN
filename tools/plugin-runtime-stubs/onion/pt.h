#pragma once
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#ifdef __cplusplus
extern "C" {
#endif
int pt_attach(pid_t);
int pt_detach(pid_t,int);
intptr_t pt_mmap(pid_t,intptr_t,size_t,int,int,int,off_t);
int pt_munmap(pid_t,intptr_t,size_t);
int pt_mprotect(pid_t,intptr_t,size_t,int);
int pt_copyin(pid_t,const void*,intptr_t,size_t);
int pt_copyout(pid_t,intptr_t,void*,size_t);
long pt_syscall(pid_t,int,...);
long pt_call(pid_t,intptr_t,...);
intptr_t pt_resolve(pid_t,const char*);
#ifdef __cplusplus
}
#endif
