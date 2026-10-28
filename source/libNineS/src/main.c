#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <elf.h>
#include <signal.h>
#include <onion/notify_i18n.h>

#include "../include/proc.h"
#include "../include/ucred.h"
#include "../include/injector.h"
#include "../include/notify.h"
#include "../include/server.h"

#include "ps5/mdbg.h"

#include <dlfcn.h>

bool Inject_Toolbox_Guarded(int pid, uint8_t * elf, bool (*pre_attach)(int))
{                                  
    if(pid < 0 || !elf){
        notify_send("%s", onion_notify_tr("notify.toolbox.invalid_arguments"));
        return false;
    } 
    bool success = true;
    struct proc* target_proc = get_proc_by_pid(pid);//find_proc_by_name("SceShellUI");
    if (target_proc)
    {
        if (!(success = inject_elf_guarded(target_proc, elf, pre_attach)))
            notify_send("%s", onion_notify_tr("notify.toolbox.inject_failed"));
        
        free(target_proc);
    }
    else{
        notify_send("%s", onion_notify_tr("notify.toolbox.no_shellui_pid"));
        return false;
    }

    return success;
}

/* Preserve the entry point used by the independent HyperShell loader. */
bool Inject_Toolbox(int pid, uint8_t *elf)
{
    return Inject_Toolbox_Guarded(pid, elf, NULL);
}
