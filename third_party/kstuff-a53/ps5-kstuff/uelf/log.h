#pragma once
#include <sys/types.h>
#include "shared_area.h"

enum kstuff_syscall_tag
{
    KSTUFF_SYSCALL_NONE = 0,
    KSTUFF_SYSCALL_EXECVE,
    KSTUFF_SYSCALL_DYNLIB_LOAD_PRX,
    KSTUFF_SYSCALL_GET_SELF_AUTH_INFO,
    KSTUFF_SYSCALL_GET_SDK_COMPILED_VERSION,
    KSTUFF_SYSCALL_GET_PPR_SDK_COMPILED_VERSION,
    KSTUFF_SYSCALL_MMAP,
    KSTUFF_SYSCALL_MLOCK,
    KSTUFF_SYSCALL_NMOUNT,
    KSTUFF_SYSCALL_UNMOUNT,
    KSTUFF_SYSCALL_IOCTL,
    KSTUFF_SYSCALL_MPROTECT,
    KSTUFF_SYSCALL_MDBG_CALL,
};

#if KSTUFF_OBS
void log_word(uint64_t word);
void log_msg(const char* msg);
int copy_shared_area_snapshot(uint64_t dst, uint64_t sz);
void observe_ioctl_com_total(uint64_t com);
void observe_ioctl_com_emulated(uint64_t com, uint32_t cmd);
void observe_syscall_armed(enum kstuff_syscall_tag tag);
void observe_current_syscall_trap(void);
void observe_current_syscall_emulated(void);
void observe_current_syscall_finish(void);
#else
static inline void log_word(uint64_t word) { (void)word; }
static inline void log_msg(const char* msg) { (void)msg; }
static inline int copy_shared_area_snapshot(uint64_t dst, uint64_t sz)
{
    (void)dst;
    (void)sz;
    return -1;
}
static inline void observe_ioctl_com_total(uint64_t com) { (void)com; }
static inline void observe_ioctl_com_emulated(uint64_t com, uint32_t cmd)
{
    (void)com;
    (void)cmd;
}
static inline void observe_syscall_armed(enum kstuff_syscall_tag tag) { (void)tag; }
static inline void observe_current_syscall_trap(void) {}
static inline void observe_current_syscall_emulated(void) {}
static inline void observe_current_syscall_finish(void) {}
#endif
