#pragma once
#include <stddef.h>
#define CTL_KERN 1
#define KERN_BOOTTIME 21
#ifdef __cplusplus
extern "C" {
#endif
int sysctl(const int*,unsigned,void*,size_t*,const void*,size_t);
#ifdef __cplusplus
}
#endif
