#include <onion/log.h>
#include <errno.h>
#include <unistd.h>

#include "../include/injector.h"
#include "ps5/klog.h"
#include <ps5/kernel.h>

int attached = false;
intptr_t remote_malloc = 0;
intptr_t remote_pthread_create = 0;
void* remote_pthread_join = NULL;
SCEFunctions sce_functions = {0};

/*
 * Scoped PTRACE_AUTHID for the inject_elf() window (not DEBUG_AUTHID).
 * Elevate once via set_ucred_to_ptrace(); restore on every exit. No per-call
 * flip inside sys_ptrace (see libonion_elfldr pt.c).
 */
static int read_self_authid(uint64_t *authid_out)
{
    if (!authid_out) {
        return -1;
    }

    *authid_out = kernel_get_ucred_authid(getpid());
    return (*authid_out != 0) ? 0 : -1;
}

static int write_self_authid(uint64_t authid)
{
    return kernel_set_ucred_authid(getpid(), authid);
}


int __attribute__((section(".stager_shellcode$1")))  stager(SCEFunctions* functions)
{
    pthread_t thread;
    functions->thread_result = functions->pthread_create_ptr(
        &thread, 0, (void *(*)(void *)) functions->elf_main, functions->payload_args);

    asm volatile("int3" ::: "memory");

    return functions->thread_result;
}

//
// Just used to calculate stager size
//
int __attribute__((section(".stager_shellcode$2"))) stager_end()
{
    return 0;
}

//
// Poor man function size counter, temp stuff
//
uint32_t get_shellcode_size()
{
    return &stager_end - &stager;
}


//
// Init all remote function pointers needed for injection
//
void init_remote_function_pointers(pid_t pid)
{
    if (!attached)
    {
        if (pt_attach(pid) < 0)
        {
            LOG_ERROR("Error attaching PID %d! aborting...", pid);
            return;
        }
    }

    char nid[12] = {0};
    //
    // Injector/loader specifics
    //
    nid_encode("malloc", nid);
    remote_malloc = pt_resolve(pid, nid);
    nid_encode("pthread_create", nid);
    remote_pthread_create = pt_resolve(pid, nid);
    nid_encode("nid_pthread_join", nid);
    remote_pthread_join = (void*) pt_resolve(pid, nid);

    //
    // Shellcode function pointers
    //
    nid_encode("sceKernelDebugOutText", nid);
    sce_functions.sceKernelDebugOutText = (void*) pt_resolve(pid, nid);
    sce_functions.pthread_create_ptr = (void*) remote_pthread_create;

}


int inject_elf_guarded(struct proc* proc, void* elf, bool (*pre_attach)(int))
{
    int status = true;
    uint64_t original_authid = 0;
    int ptrace_authid_changed = 0;
    uint64_t sce_ptr_mem;
    uint64_t shellcode_size = get_shellcode_size();

    LOG_DEBUG("[+] Elevating for ptrace (PTRACE_AUTHID)...[+]");

    if (proc == NULL || elf == NULL)
    {
        LOG_ERROR("[-] inject_elf: invalid args proc=%p elf=%p",
                    (void*)proc, elf);
        status = false;
        goto exit;
    }

    /* Scoped PTRACE_AUTHID for attach/load only (not DEBUG_AUTHID). */
    if (read_self_authid(&original_authid) == 0)
    {
        ptrace_authid_changed = 1;
        LOG_DEBUG("[+] inject: backup authid=0x%lx",
                    (unsigned long)original_authid);
    }
    else
    {
        LOG_ERROR("[-] inject: authid backup failed (continuing)");
    }
    set_ucred_to_ptrace();

    /* Recheck browser/target at the last boundary before PT_ATTACH. The
     * caller's preceding quiet window may have been invalidated meanwhile. */
    if (pre_attach && !pre_attach(proc->pid))
    {
        LOG_WARN("Toolbox R29 stage=pre_attach_cancelled pid=%d", proc->pid);
        status = false;
        goto exit;
    }
    LOG_INFO("Toolbox R29 stage=attach pid=%d", proc->pid);
    if (pt_attach(proc->pid) < 0)
    {
        LOG_ERROR("Error attaching into PID: %d errno=%d", proc->pid, errno);
        status = false;
        goto exit;
    }

    LOG_DEBUG("[+] Attached to %d! [+]", proc->pid);
    attached = true;

    init_remote_function_pointers(proc->pid);
    if (!remote_pthread_create || !shellcode_size || shellcode_size > 4096)
    {
        LOG_ERROR("Toolbox R29 invalid stager prerequisites pid=%d", proc->pid);
        status = false;
        goto detach;
    }

    LOG_DEBUG("[+] Loading ELF on %d...[+]", proc->pid);
    LOG_INFO("Toolbox R29 stage=load pid=%d", proc->pid);
    /* Crash window: remote mmap/mprotect via pt_syscall while ShellUI is stopped. */
    intptr_t entry = elfldr_load(proc->pid, (uint8_t*) elf);

    if (entry <= 0)
    {
        LOG_ERROR("[-] Failed to load ELF! [-]");
        status = false;
        goto detach;
    }
    LOG_DEBUG("[+] elfldr_load ok entry=%#lx", (unsigned long)entry);
    LOG_INFO("Toolbox R29 stage=load_complete pid=%d", proc->pid);

    LOG_DEBUG("[+] Allocating payload args on %d...[+]", proc->pid);
    LOG_INFO("Toolbox R29 stage=args pid=%d", proc->pid);
    intptr_t args = elfldr_payload_args(proc->pid);
    LOG_DEBUG("[+] ELF entrypoint: %#02lx [+]\n[+] Payload Args: %#02lx [+]", entry, args);
    if (args <= 0)
    {
        LOG_ERROR("[-] Failed to allocate payload args! [-]");
        status = false;
        goto detach;
    }

    //
    // Copy shellcode thread parameters & boot code
    //
    sce_functions.elf_main = (void*) entry;
    sce_functions.payload_args = (void*) args;
    sce_functions.thread_result = -1;

    uint64_t bootstrap = pt_mmap(proc->pid, 0, shellcode_size, PROT_READ|PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);

    /* MAP_FAILED is (void*)-1; also reject NULL. (kylin-core) */
    if (!bootstrap || bootstrap == (uint64_t)-1)
    {
        LOG_ERROR("Unable to allocate bootstrap code, injection aborted!");
        status = false;
        goto detach;
    }

    //
    // Make it executable
    //
    if (kernel_mprotect(proc->pid, bootstrap, shellcode_size, PROT_EXEC|PROT_WRITE|PROT_READ) != 0)
    {
        LOG_ERROR("[-] bootstrap mprotect failed pid=%d addr=%#lx",
                    proc->pid, (unsigned long)bootstrap);
        status = false;
        goto detach;
    }
    if (pt_copyin(proc->pid, stager, bootstrap, shellcode_size) != 0)
    {
        LOG_ERROR("[-] bootstrap copyin failed pid=%d addr=%#lx",
                    proc->pid, (unsigned long)bootstrap);
        status = false;
        goto detach;
    }

    LOG_DEBUG("[+] Bootstrap code allocated at %#02lx [+]", bootstrap);
    //
    // Write the sce functions data
    //
    sce_ptr_mem = pt_mmap(proc->pid, 0, sizeof(sce_functions), PROT_READ|PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (!sce_ptr_mem || sce_ptr_mem == (uint64_t)-1)
    {
        LOG_ERROR("[-] sce_functions mmap failed pid=%d", proc->pid);
        status = false;
        goto detach;
    }
    if (pt_copyin(proc->pid, &sce_functions, sce_ptr_mem, sizeof(SCEFunctions)) != 0)
    {
        LOG_ERROR("[-] sce_functions copyin failed pid=%d", proc->pid);
        status = false;
        goto detach;
    }

    LOG_DEBUG("[+] Triggering entrypoint... [+]");
    LOG_INFO("Toolbox R29 stage=stager pid=%d", proc->pid);
    //
    // Call until hit a breakpoint (stager ends with int3; pt_call2 waits)
    //
    if (pt_call2(proc->pid, bootstrap, sce_ptr_mem) == -1)
    {
        LOG_ERROR("[-] pt_call2 failed pid=%d errno=%d", proc->pid, errno);
        status = false;
        goto detach;
    }
    /* pt_call2 synchronizes at INT3 but does not return the final tracee RAX.
     * Read an explicit remote result rather than treating that RAX as the
     * pthread_create status. */
    SCEFunctions completed = {0};
    if (pt_copyout(proc->pid, sce_ptr_mem, &completed, sizeof(completed)) != 0 ||
        completed.thread_result != 0)
    {
        LOG_ERROR("Toolbox R29 stage=stager_failed pid=%d pthread_result=%d",
                  proc->pid, completed.thread_result);
        status = false;
        goto detach;
    }
    LOG_INFO("Toolbox R29 stage=stager_complete pid=%d", proc->pid);

detach:
    if (pt_detach(proc->pid, 0) == 0)
    {
        attached = false;
    }
    else
    {
        LOG_ERROR("[-] pt_detach failed pid=%d errno=%d", proc->pid, errno);
        status = false;
    }

    LOG_DEBUG("[+] ELF injection finished! [+]");
    LOG_DEBUG("[+] Detached [+]");
exit:
    if (ptrace_authid_changed)
    {
        if (write_self_authid(original_authid) == 0)
        {
            LOG_DEBUG("[+] inject: restored authid=0x%lx",
                        (unsigned long)original_authid);
        }
        else
        {
            LOG_ERROR("[-] inject: authid restore failed authid=0x%lx",
                        (unsigned long)original_authid);
        }
    }
    LOG_INFO("Toolbox R29 stage=finish pid=%d success=%d",
             proc ? proc->pid : -1, status);
    return status;
}

int inject_elf(struct proc* proc, void* elf)
{
    return inject_elf_guarded(proc, elf, NULL);
}

//
// We can't stuck sceshellui for too long or the system will kill it's process, so we will load the library in a separated thread
//
module_info_t* load_remote_library(pid_t pid, const char* library_path, const char* library_name)
{
    if (!attached)
    {
        if (pt_attach(pid) < 0)
        {
            LOG_ERROR("load_remote_library: Failed to attach PID %d", pid);
            return NULL;
        }
    }

    // intptr_t library_str = pt_call(pid, 0, 0x100, PROT_WRITE | PROT_READ, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    intptr_t library_str = pt_call(pid, remote_malloc, strlen(library_name) + 1);
    mdbg_copyin(pid, library_path, library_str, strlen(library_path) + 1);

    //
    // Run the module loading in a separated thread
    //
    intptr_t sce_kernel_load_start_module = pt_resolve(pid, nid_sce_kernel_load_start_module);
    create_remote_thread(pid, sce_kernel_load_start_module, library_str);

    LOG_DEBUG("sce_kernel_load_start_module: %#02lx", sce_kernel_load_start_module);
    //
    // Now we detach, sleep a little and attach again
    //
    pt_detach(pid, 0);

    int retries = 0;
    int max_retries = 100;
    module_info_t* module = NULL;

    while (retries <= max_retries)
    {
        module = get_module_info(pid, library_name);
        if (!module)
        {
            usleep(500);
        } else
        {
            break;
        }
        retries++;
    }

    if (!module)
    {
        LOG_ERROR("Unable to load %s into PID %d!", library_name, pid);
    }

    pt_attach(pid);

    return module;
}


int create_remote_thread(pid_t pid, uintptr_t target_address, uintptr_t parameters)
{
    if (!attached)
    {
        if (pt_attach(pid) < 0)
        {
            LOG_ERROR("Unable to attach into the remote process!");
            return false;
        }
    }

    intptr_t pthread = pt_call(pid, remote_malloc, sizeof(pthread_t));
    if (!pthread)
    {
        LOG_ERROR("Unable to allocate memory for pthread pointer!");
        return false;
    }

    //
    // We don't have to wait (join), otherwise we would block the whole target
    //
    return pt_call(pid, remote_pthread_create, pthread, 0, target_address, parameters);
}
