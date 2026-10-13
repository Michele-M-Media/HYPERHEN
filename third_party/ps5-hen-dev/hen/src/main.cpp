#include <stdint.h>

#include "fpkg.h"
#include "fself.h"
#include "hook.h"
#include "kdlsym.h"
#include "patch_shellcore.h"
#include "util.h"

struct args
{
    uint64_t fptr;
    uint64_t fw;
    uint64_t kernel_base;
};

extern "C" {
    int kernel_main(void *td, struct args *args);
}

/**
 * @brief The kernel sysent entrypoint
 * 
 * @param td struct thread* The calling thread
 * @param args struct args* Syscall arguments
 * @return int 0 on success, error otherwise
 */
int kernel_main(void *td, struct args *args)
{
    int ret;

    curthread = td;
    init_kdlsym(args->fw, args->kernel_base);

    // kdlsym assignments
    auto printf = (void (*)(const char *fmt, ...)) kdlsym(KERNEL_SYM_PRINTF);
    printf("[HyperHEN] kernel_main fw=0x%lx kernel_base=0x%lx curthread=%p\n", args->fw, args->kernel_base, td);
    printf("[HyperHEN] symbols text_end=0x%lx allproc=0x%lx mailbox=0x%lx rw_mem=0x%lx\n",
           kdlsym(KERNEL_SYM_TEXT_END),
           kdlsym(KERNEL_SYM_ALLPROC),
           kdlsym(KERNEL_SYM_SCESBLSERVICEMAILBOX),
           kdlsym(KERNEL_SYM_RW_MEM));

    // Reset hooks before installing new ones
    printf("[HyperHEN] Resetting hooks\n");
    for (int i = 0; i < HOOK_MAX; i++) {
        reset_hook((hook_id) i);
    }

    // Install new hooks
    printf("[HyperHEN] Applying test hook\n");
    ret = apply_test_hook();
    if (ret != 0) {
        printf("[HyperHEN] Failed to apply test hook\n");
        return -1;
    }

    printf("[HyperHEN] Applying fself hooks\n");
    apply_fself_hooks();

    printf("[HyperHEN] Applying fpkg hooks\n");
    apply_fpkg_hooks();

    printf("[HyperHEN] Applying shellcore patches\n");
    apply_shellcore_patches();

    return 0;
}
