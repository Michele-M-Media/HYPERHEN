#include <errno.h>
#include <stdint.h>
#include <sys/types.h>

#include "hook.h"
#include "kdlsym.h"

#include "hooks/1_00.h"
#include "hooks/1_01.h"
#include "hooks/1_02.h"
#include "hooks/1_05.h"
#include "hooks/1_10.h"
#include "hooks/1_11.h"
#include "hooks/1_12.h"
#include "hooks/1_13.h"
#include "hooks/1_14.h"
#include "hooks/2_00.h"
#include "hooks/2_20.h"
#include "hooks/2_25.h"
#include "hooks/2_26.h"
#include "hooks/2_30.h"
#include "hooks/2_50.h"
#include "hooks/3_00.h"
#include "hooks/3_10.h"
#include "hooks/3_20.h"
#include "hooks/3_21.h"
#include "hooks/4_00.h"
#include "hooks/4_02.h"
#include "hooks/4_03.h"
#include "hooks/4_50.h"
#include "hooks/4_51.h"

static uint64_t get_call_target(uint64_t call_addr)
{
    if (*(uint8_t *)call_addr != 0xE8) {
        return 0;
    }

    int32_t rel32 = *(int32_t *)(call_addr + 1);
    return call_addr + 5 + rel32;
}

static void log_hook_transition(const char *action, int id, uint64_t call_addr, uint64_t new_target)
{
    auto printf = (void (*)(const char *fmt, ...)) kdlsym(KERNEL_SYM_PRINTF);
    uint8_t op = *(uint8_t *)call_addr;
    uint64_t cur_target = get_call_target(call_addr);

    if (id >= 0) {
        if (cur_target != 0) {
            printf("[HyperHEN] [HOOK] %s id=%d call=0x%lx op=0x%x cur=0x%lx new=0x%lx\n",
                   action, id, call_addr, op, cur_target, new_target);
        } else {
            printf("[HyperHEN] [HOOK] %s id=%d call=0x%lx op=0x%x new=0x%lx (not a direct CALL)\n",
                   action, id, call_addr, op, new_target);
        }
    } else {
        if (cur_target != 0) {
            printf("[HyperHEN] [HOOK] %s raw call=0x%lx op=0x%x cur=0x%lx new=0x%lx\n",
                   action, call_addr, op, cur_target, new_target);
        } else {
            printf("[HyperHEN] [HOOK] %s raw call=0x%lx op=0x%x new=0x%lx (not a direct CALL)\n",
                   action, call_addr, op, new_target);
        }
    }
}

struct hook *find_hook(hook_id id)
{
    uint64_t fw_ver;
    struct hook *hooks;
    struct hook *cur_hook;
    int num_hooks;

    fw_ver = get_fw_version();
    switch (fw_ver) {
    case 0x1000000:
         hooks = (struct hook *) &g_kernel_hooks_100;
         num_hooks = sizeof(g_kernel_hooks_100) / sizeof(struct hook);
         break;
    case 0x1010000:
    case 0x1020000:
         hooks = (struct hook *) &g_kernel_hooks_102;
         num_hooks = sizeof(g_kernel_hooks_102) / sizeof(struct hook);
         break;
    case 0x1050000:
         hooks = (struct hook *) &g_kernel_hooks_105;
         num_hooks = sizeof(g_kernel_hooks_105) / sizeof(struct hook);
         break;
    case 0x1100000:
        hooks = (struct hook *) &g_kernel_hooks_110;
        num_hooks = sizeof(g_kernel_hooks_110) / sizeof(struct hook);
        break;
    case 0x1110000:
        hooks = (struct hook *) &g_kernel_hooks_111;
        num_hooks = sizeof(g_kernel_hooks_111) / sizeof(struct hook);
        break;
    case 0x1120000:
        hooks = (struct hook *) &g_kernel_hooks_112;
        num_hooks = sizeof(g_kernel_hooks_112) / sizeof(struct hook);
        break;
    case 0x1130000:
        hooks = (struct hook *) &g_kernel_hooks_113;
        num_hooks = sizeof(g_kernel_hooks_113) / sizeof(struct hook);
        break;
    case 0x1140000:
        hooks = (struct hook *) &g_kernel_hooks_114;
        num_hooks = sizeof(g_kernel_hooks_114) / sizeof(struct hook);
        break;
    case 0x2000000:
        hooks = (struct hook *) &g_kernel_hooks_200;
        num_hooks = sizeof(g_kernel_hooks_200) / sizeof(struct hook);
        break;
    case 0x2200000:
        hooks = (struct hook *) &g_kernel_hooks_220;
        num_hooks = sizeof(g_kernel_hooks_220) / sizeof(struct hook);
        break;
    case 0x2250000:
        hooks = (struct hook *) &g_kernel_hooks_225;
        num_hooks = sizeof(g_kernel_hooks_225) / sizeof(struct hook);
        break;
    case 0x2260000:
        hooks = (struct hook *) &g_kernel_hooks_226;
        num_hooks = sizeof(g_kernel_hooks_226) / sizeof(struct hook);
        break;
    case 0x2300000:
        hooks = (struct hook *) &g_kernel_hooks_230;
        num_hooks = sizeof(g_kernel_hooks_230) / sizeof(struct hook);
        break;
    case 0x2500000:
    case 0x2700000:
        hooks = (struct hook *) &g_kernel_hooks_250;
        num_hooks = sizeof(g_kernel_hooks_250) / sizeof(struct hook);
        break;
    case 0x3000000:
        hooks = (struct hook *) &g_kernel_hooks_300;
        num_hooks = sizeof(g_kernel_hooks_300) / sizeof(struct hook);
        break;
    case 0x3100000:
        hooks = (struct hook *) &g_kernel_hooks_310;
        num_hooks = sizeof(g_kernel_hooks_310) / sizeof(struct hook);
        break;
    case 0x3200000:
        hooks = (struct hook *) &g_kernel_hooks_320;
        num_hooks = sizeof(g_kernel_hooks_320) / sizeof(struct hook);
        break;
    case 0x3210000:
        hooks = (struct hook *) &g_kernel_hooks_321;
        num_hooks = sizeof(g_kernel_hooks_321) / sizeof(struct hook);
        break;
    case 0x4000000:
        hooks = (struct hook *) &g_kernel_hooks_400;
        num_hooks = sizeof(g_kernel_hooks_400) / sizeof(struct hook);
        break;
    case 0x4020000:
        hooks = (struct hook *) &g_kernel_hooks_402;
        num_hooks = sizeof(g_kernel_hooks_402) / sizeof(struct hook);
        break;
    case 0x4030000:
        hooks = (struct hook *) &g_kernel_hooks_403;
        num_hooks = sizeof(g_kernel_hooks_403) / sizeof(struct hook);
        break;
    case 0x4500000:
        hooks = (struct hook *) &g_kernel_hooks_450;
        num_hooks = sizeof(g_kernel_hooks_450) / sizeof(struct hook);
        break;
    case 0x4510000:
        hooks = (struct hook *) &g_kernel_hooks_451;
        num_hooks = sizeof(g_kernel_hooks_451) / sizeof(struct hook);
        break;
    default: {
        auto printf = (void (*)(const char *fmt, ...)) kdlsym(KERNEL_SYM_PRINTF);
        printf("[HyperHEN] [HOOK] no hook table for fw=0x%lx\n", fw_ver);
        return 0;
    }
    }

    for (int i = 0; i < num_hooks; i++) {
        cur_hook = &hooks[i];
        if (cur_hook->id == id) {
            return cur_hook;
        }
    }

    auto printf = (void (*)(const char *fmt, ...)) kdlsym(KERNEL_SYM_PRINTF);
    printf("[HyperHEN] [HOOK] missing id=%d for fw=0x%lx\n", id, fw_ver);
    return 0;
}

int install_raw_hook(uint64_t call_addr, void *func)
{
    uint64_t call_install;
    int32_t call_rel32;

    // Calculate rel32
    call_rel32  = (int32_t) ((uint64_t) (func) - call_addr) - 5; // Subtract 5 for call opcodes
    log_hook_transition("install", -1, call_addr, (uint64_t)func);

    call_install = call_addr + 1;
    *(uint32_t *) (call_install) = call_rel32;
    return 0;
}

int install_hook(hook_id id, void *func)
{
    struct hook *hook_info;
    uint64_t call_addr;
    uint64_t call_install;
    int32_t call_rel32;

    // Find info for this hook
    hook_info = find_hook(id);
    if (hook_info == 0)
        return -ENOENT;

    // Calculate rel32
    call_addr   = ktext(hook_info->call_offset);
    call_rel32  = (int32_t) ((uint64_t) (func) - call_addr) - 5; // Subtract 5 for call opcodes
    log_hook_transition("install", id, call_addr, (uint64_t)func);

    call_install = call_addr + 1;
    *(uint32_t *) (call_install) = call_rel32;
    return 0;
}

void reset_hook(hook_id id)
{
    struct hook *hook_info;
    uint64_t call_addr;
    uint64_t call_install;
    int32_t call_rel32;
    void *func;

    // Find info for this hook
    hook_info = find_hook(id);
    if (hook_info == 0)
        return;

    // Calculate rel32
    func        = (void *) ktext(hook_info->orig_func_offset);
    call_addr   = ktext(hook_info->call_offset);
    call_rel32  = (int32_t) ((uint64_t) (func) - call_addr) - 5; // Subtract 5 for call opcodes
    log_hook_transition("reset", id, call_addr, (uint64_t)func);

    call_install = call_addr + 1;
    *(uint32_t *) (call_install) = call_rel32;
}

int hook_is_development_mode()
{
    return 0xc001;
}

int apply_test_hook()
{
    auto printf = (void (*)(const char *fmt, ...)) kdlsym(KERNEL_SYM_PRINTF);

    printf("[HyperHEN] [HOOK] sys_is_development_mode() -> isDevelopmentMode()\n");
    return install_hook(HOOK_TEST_SYS_IS_DEVELOPMENT_MODE, (void *) &hook_is_development_mode);
}
