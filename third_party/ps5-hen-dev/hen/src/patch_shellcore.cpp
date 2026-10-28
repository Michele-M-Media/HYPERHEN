#include <sys/types.h>
#include <sys/param.h>
#include <sys/uio.h>

#include "kdlsym.h"
#include "util.h"
#include "patch_shellcore.h"
#include "proc.h"

#include "shellcore_patches/1_00.h"
#include "shellcore_patches/1_02.h"
#include "shellcore_patches/1_12.h"
#include "shellcore_patches/1_14.h"
#include "shellcore_patches/2_00.h"
#include "shellcore_patches/2_20.h"
#include "shellcore_patches/2_25.h"
#include "shellcore_patches/2_26.h"
#include "shellcore_patches/2_30.h"
#include "shellcore_patches/2_50.h"
#include "shellcore_patches/2_70.h"
#include "shellcore_patches/3_00.h"
#include "shellcore_patches/3_10.h"
#include "shellcore_patches/3_20.h"
#include "shellcore_patches/3_21.h"
#include "shellcore_patches/4_00.h"
#include "shellcore_patches/4_02.h"
#include "shellcore_patches/4_03.h"
#include "shellcore_patches/4_50.h"
#include "shellcore_patches/4_51.h"

/**
 * @brief Implementation of read/write memory for a process (from kernel)
 * 
 * @param p struct proc* Process to read/write to/from
 * @param procAddr off_t Address to read/write to/from
 * @param sz size_t Size to read/write
 * @param kAddr void* Kernel buffer
 * @param ioSz size_t io size
 * @param write int32_t 1 for write, 0 for read
 * @return int 0 on success, error otherwise
 */
 
 bool sceKernelIsTestKit(void) {
    return if_exists("/system/priv/lib/libSceDeci5Ttyp.sprx");
}

bool sceKernelIsDevKit(void) {
    return if_exists("/system/priv/lib/libSceDeci5Dtracep.sprx");
}

enum kit_type {
    KIT_RETAIL,
    KIT_TESTKIT,
    KIT_DEVKIT
};

static enum kit_type get_kit_type(void) {
    if (sceKernelIsDevKit()) return KIT_DEVKIT;
    if (sceKernelIsTestKit()) return KIT_TESTKIT;
    return KIT_RETAIL;
}

int proc_rw_mem(void *p, off_t procAddr, size_t sz, void *kAddr, size_t *ioSz, int write)
{
    // Assign kdlsym
    auto printf = (void (*)(const char *fmt, ...)) kdlsym(KERNEL_SYM_PRINTF);
    auto debug_rwmem = (int (*)(void *proc, struct uio *uio)) kdlsym(KERNEL_SYM_RW_MEM);

    // Debug logging
    // printf("proc_rw_mem(%p, 0x%lx, %lx, %p, %p, %d)\n", p, procAddr, sz, kAddr, ioSz, write);

    // Validate process
    if (!p) {
        printf("no proc\n");
        return -1;
    }

    // Validate process address, and kernel address
    if (!procAddr || !kAddr) {
        printf("no addrs\n");
        return -1;
    }

    // Validate size
    if (!sz) {
        if (ioSz) {
            *ioSz = 0;
        }
        return 0;
    }

    struct iovec _iov{};
    struct uio _uio{};

    _iov.iov_base = kAddr;
    _iov.iov_len = sz;

    _uio.uio_iov = &_iov;
    _uio.uio_iovcnt = 1;
    _uio.uio_offset = procAddr;
    _uio.uio_resid = sz;
    _uio.uio_segflg = UIO_SYSSPACE;
    _uio.uio_rw = (write) ? UIO_WRITE : UIO_READ;
    _uio.uio_td = (struct thread *)curthread;

    // Read/Write memory (ignoring faults)
    int ret = debug_rwmem(p, &_uio);

    if (ioSz) {
        *ioSz = (sz - _uio.uio_resid);
    }

    if (ret != 0) {
        printf("[HyperHEN] [SHELLCORE] proc_rw_mem %s failed ret=0x%x proc=%p addr=0x%lx sz=0x%lx done=0x%lx\n",
               write ? "write" : "read",
               ret,
               p,
               procAddr,
               sz,
               sz - _uio.uio_resid);
    }

    return ret;
}

/**
 * @brief Gets the shellcore base address for patching from kernel->user space
 * 
 * @param shellcore_proc struct proc* Shellcore process
 * @return uint64_t Base address of shellcore, or 0 on error
 */
uint64_t shellcore_get_addr(void *shellcore_proc)
{
    void *vm_map;
    void *first_entry;
    void *entry;
    uint64_t entry_start;
    uint8_t entry_prot;
    char *entry_name;
    uint64_t addr;
    int scanned_entries;

    // kdlsym function pointers
    auto printf                 = (void (*)(const char *fmt, ...)) kdlsym(KERNEL_SYM_PRINTF);
    auto _vm_map_lock_read      = (void (*)(void *map, const char *file, int line)) kdlsym(KERNEL_SYM_VM_MAP_LOCK_READ);
    auto _vm_map_unlock_read    = (void (*)(void *map, const char *file, int line)) kdlsym(KERNEL_SYM_VM_MAP_UNLOCK_READ);
    auto _vm_map_lookup_entry   = (int (*)(void *map, uint64_t offset, void *entry)) kdlsym(KERNEL_SYM_VM_MAP_LOOKUP_ENTRY);

    // Get the process vm map
    vm_map = get_proc_vmmap(shellcore_proc);
    if (!vm_map) {
        printf("[HyperHEN] [SHELLCORE] shellcore vm_map is null\n");
        return 0;
    }
    printf("[HyperHEN] [SHELLCORE] vm_map = %p\n", vm_map);

    // Lock the vm map
    _vm_map_lock_read(vm_map, "", 0);    

    // Lookup the vm map entry
    if (_vm_map_lookup_entry(vm_map, 0, &entry) != 0) {
        // On failure log and unlock
        printf("[HyperHEN] [SHELLCORE] Failed to lookup first entry\n");
        _vm_map_unlock_read(vm_map, "", 0);
        return 0;
    }

    first_entry = entry;
    addr = 0;
    scanned_entries = 0;

    // Iterate over all of the entries and check the name, offset, and protection
    do {
        entry_name  = (char *) ((char *) (entry) + VM_ENTRY_OFFSET_NAME);
        entry_start = *(uint64_t *) ((char *) (entry) + VM_ENTRY_OFFSET_START);
        entry_prot  = *(uint8_t *) ((char *) (entry) + VM_ENTRY_OFFSET_PROT);
        scanned_entries++;

        printf("  vm entry (start=0x%lx, prot=0x%x), '%s'\n", entry_start, entry_prot, entry_name);
        entry = (void *) *(uint64_t *) ((char *) (entry) + VM_ENTRY_OFFSET_NEXT);

        if (!strncmp(entry_name, "executable", strlen("executable")) && entry_prot == 4) {
            // for (int i = 0; i < 0x200; i += 0x8) {
            //     printf("  +%02x: 0x%lx\n", i, *(uint64_t *) ((char *) (entry) + i));
            // }
            addr = entry_start;
            printf("[HyperHEN] [SHELLCORE] selected executable entry after %d vm entries\n", scanned_entries);
            break;
        }
    } while (entry != NULL && entry != first_entry);

    // Unlock the vm map
    _vm_map_unlock_read(vm_map, "", 0);

    if (!addr) {
        printf("[HyperHEN] [SHELLCORE] no executable vm entry found after %d entries\n", scanned_entries);
    }

    // return the found address
    return addr;
}

/**
 * @brief Applies the shellcore patches in memory
 * 
 */
void apply_shellcore_patches()
{
    uint64_t fw_ver;
    struct patch *patches;
    struct patch *cur_patch;
    void *shellcore_proc;
    uint64_t shellcore_base_addr;
    int num_patches;
    int shellcore_pid;
    int patches_ok;
    int patches_failed;

    // Get kdlsym function pointers
    auto printf = (void (*)(const char *fmt, ...)) kdlsym(KERNEL_SYM_PRINTF);

    // Resolve patches for this fw
    fw_ver = get_fw_version() >> 16;
    printf("apply_shellcore_patches: fw_ver = 0x%lx\n", fw_ver);

    enum kit_type kit = get_kit_type();
	
	if (kit == KIT_RETAIL)
	    printf("[HyperHEN] [SHELLCORE] kit=RETAIL\n");
	else if (kit == KIT_TESTKIT)
	    printf("[HyperHEN] [SHELLCORE] kit=TESTKIT\n");
	else if (kit == KIT_DEVKIT)
	    printf("[HyperHEN] [SHELLCORE] kit=DEVKIT\n");

	#define FW(x) \
		case 0x ## x:\
			switch (kit) { \
				case KIT_DEVKIT: \
					patches = (struct patch*)&g_shellcore_patches_##x##_devkit; \
					num_patches = sizeof(g_shellcore_patches_##x##_devkit)/sizeof(struct patch); \
					break; \
				case KIT_TESTKIT: \
					patches = (struct patch*)&g_shellcore_patches_##x##_testkit; \
					num_patches = sizeof(g_shellcore_patches_##x##_testkit)/sizeof(struct patch); \
					break; \
				case KIT_RETAIL: \
					patches = (struct patch*)&g_shellcore_patches_##x##_retail; \
					num_patches = sizeof(g_shellcore_patches_##x##_retail)/sizeof(struct patch); \
					break; \
				} \
				break


    switch (fw_ver) {
    FW(100);
    FW(102);
    FW(112);
    FW(114);
    FW(200);
    FW(220);
    FW(225);
    FW(226);
    FW(230);
    FW(250);
    FW(270);
    FW(300);
    FW(310);
    FW(320);
    FW(321);
    FW(400);
	    FW(402);
    FW(403);
    FW(450);
    FW(451);

    default:
        printf("apply_shellcore_patches: don't have offsets for this firmware\n");
        return;
    }
	#undef FW

    printf("[HyperHEN] [SHELLCORE] selected %d patches for fw=0x%lx\n", num_patches, fw_ver);

    // Get shellcore proc
    printf("[HyperHEN] [SHELLCORE] Finding shellcore\n");
    shellcore_proc = find_proc_by_name("SceShellCore");
    if (shellcore_proc == NULL) {
        printf("[HyperHEN] [SHELLCORE] Failed to find shellcore\n");
        return;
    }
    shellcore_pid = *(int *) ((char *) shellcore_proc + PROC_OFFSET_P_PID);
    printf("[HyperHEN] [SHELLCORE] shellcore proc = %p pid=%d vmspace=%p\n",
           shellcore_proc,
           shellcore_pid,
           get_proc_vmmap(shellcore_proc));

    // Resolve shellcore base address
    shellcore_base_addr = shellcore_get_addr(shellcore_proc);
    printf("[HyperHEN] [SHELLCORE] Found shellcore base = 0x%lx\n", shellcore_base_addr);
    if (!shellcore_base_addr) {
        printf("[HyperHEN] [SHELLCORE] WARNING: shellcore base is zero, patch writes will likely fail\n");
    }

    printf("[HyperHEN] [SHELLCORE] Applying shellcore patches...\n");
    patches_ok = 0;
    patches_failed = 0;
    for (int i = 0; i < num_patches; i++) {
        cur_patch = &patches[i];
        printf("  offset=0x%lx, size=0x%x, data=%p\n", cur_patch->offset, cur_patch->size, &cur_patch->data);

        int ret = proc_rw_mem(shellcore_proc, (shellcore_base_addr + cur_patch->offset), cur_patch->size, (void *) &cur_patch->data, NULL, 1);
        if (ret == 0) {
            patches_ok++;
        } else {
            patches_failed++;
            printf("[HyperHEN] [SHELLCORE] patch[%d] failed offset=0x%lx ret=0x%x\n", i, cur_patch->offset, ret);
        }
    }

    printf("[HyperHEN] [SHELLCORE] patch summary ok=%d failed=%d\n", patches_ok, patches_failed);
}
