#include <stdint.h>
#include <stddef.h>

#include "kdlsym.h"
#include "proc.h"
#include "util.h"

uint64_t g_dmap_base = 0;
uint64_t g_proc_comm_offset = 0;
void *curthread;

static void *get_curproc()
{
    if (!curthread) {
        return NULL;
    }

    return (void *) *(uint64_t *) ((char *) curthread + THREAD_OFFSET_TD_PROC);
}

static int is_printable_ascii(char c)
{
    return (c >= 0x20 && c <= 0x7E);
}

static int is_reasonable_proc_name(const char *name)
{
    if (!name || !name[0]) {
        return 0;
    }

    for (size_t i = 0; i < 32; i++) {
        char c = name[i];

        if (c == '\0') {
            return 1;
        }

        if (!is_printable_ascii(c)) {
            return 0;
        }
    }

    return 1;
}

static uint64_t resolve_proc_comm_offset()
{
    static const uint8_t proc_comm_sign[] = { 0xCE, 0xFA, 0xEF, 0xBE, 0xCC, 0xBB };
    auto printf = (void (*)(const char *fmt, ...)) kdlsym(KERNEL_SYM_PRINTF);

    if (g_proc_comm_offset != 0) {
        return g_proc_comm_offset;
    }

    void *curproc = get_curproc();
    if (curproc != NULL) {
        auto proc_data = (uint8_t *) curproc;

        for (size_t i = 0; i <= (0x1000 - sizeof(proc_comm_sign)); i++) {
            size_t j = 0;
            for (; j < sizeof(proc_comm_sign); j++) {
                if (proc_data[i + j] != proc_comm_sign[j]) {
                    break;
                }
            }

            if (j == sizeof(proc_comm_sign)) {
                uint64_t candidate = i + 0x8;
                auto proc_name = (char *) ((char *) curproc + candidate);

                if (is_reasonable_proc_name(proc_name)) {
                    g_proc_comm_offset = candidate;
                    printf("[HyperHEN] [PROC] resolved runtime p_comm = 0x%lx\n", g_proc_comm_offset);
                    return g_proc_comm_offset;
                }
            }
        }

        printf("[HyperHEN] [PROC] runtime p_comm scan failed, falling back to 0x%x\n", PROC_OFFSET_P_COMM);
    } else {
        printf("[HyperHEN] [PROC] curthread missing, falling back to 0x%x\n", PROC_OFFSET_P_COMM);
    }

    g_proc_comm_offset = PROC_OFFSET_P_COMM;
    return g_proc_comm_offset;
}

static void dump_proc_candidates(void *allproc, uint64_t proc_comm_offset, int max_entries)
{
    auto printf = (void (*)(const char *fmt, ...)) kdlsym(KERNEL_SYM_PRINTF);
    void *p = allproc;

    for (int i = 0; p && i < max_entries; i++) {
        auto proc_name = (char *) ((char *) p + proc_comm_offset);
        int proc_pid = *(int *) ((char *) p + PROC_OFFSET_P_PID);

        if (is_reasonable_proc_name(proc_name)) {
            printf("[HyperHEN] [PROC] candidate[%d] proc=%p pid=%d name='%s'\n", i, p, proc_pid, proc_name);
        } else {
            printf("[HyperHEN] [PROC] candidate[%d] proc=%p pid=%d name=<invalid>\n", i, p, proc_pid);
        }

        p = (void *) *(uint64_t *) p;
    }
}

void init_dmap_resolve()
{
    uint32_t DMPML4I;
    uint32_t DMPDPI;

    DMPML4I = *(uint32_t *) (kdlsym(KERNEL_SYM_DMPML4I));
    DMPDPI  = *(uint32_t *) (kdlsym(KERNEL_SYM_DMPDPI));

    g_dmap_base = ((uint64_t) (DMPDPI) << 30) | ((uint64_t ) (DMPML4I) << 39) | 0xFFFF800000000000;
}

uint64_t get_dmap_addr(uint64_t pa)
{
    // Init dmap resolve if it's not initialized already
    if (g_dmap_base == 0)
        init_dmap_resolve();

    return g_dmap_base + pa;
}

void *find_proc_by_name(const char *name)
{
    void *p;
    char *proc_name;
    uint64_t proc_comm_offset;
    int proc_pid;
    int scanned = 0;

    auto printf = (void (*)(const char *fmt, ...)) kdlsym(KERNEL_SYM_PRINTF);
    auto allproc = (void *) *(uint64_t *) kdlsym(KERNEL_SYM_ALLPROC);

    if (!name) {
        return NULL;
    }

    proc_comm_offset = resolve_proc_comm_offset();

    if (!allproc) {
        printf("[HyperHEN] [PROC] allproc head is null while looking for '%s'\n", name);
        return NULL;
    }

    p = allproc;
    while (p) {
        proc_name = (char *) ((char *) (p) + proc_comm_offset);
        proc_pid  = *(int *) ((char *) (p) + PROC_OFFSET_P_PID);
        scanned++;

        if (!strncmp(proc_name, name, strlen(name))) {
            printf("[HyperHEN] [PROC] matched '%s' at %p pid=%d after %d entries\n", name, p, proc_pid, scanned);
            return p;
        }

        p = (void *) *(uint64_t *) p;
    }

    printf("[HyperHEN] [PROC] failed to find '%s' after %d entries (allproc=%p p_comm=0x%lx)\n",
           name, scanned, allproc, proc_comm_offset);
    dump_proc_candidates(allproc, proc_comm_offset, 12);
    return NULL;
}

void *get_proc_vmmap(void *p)
{
    return (void *) *(uint64_t *) ((char *) (p) + PROC_OFFSET_P_VMSPACE);
}

void memcpy(void *dest, const void *src, size_t n)
{
    char *csrc = (char *) src;
    char *cdest = (char *) dest;

    for (size_t i = 0; i < n; i++) {
        cdest[i] = csrc[i];
    }
}

size_t strlen(const char *str)
{
    const char *s;

    for (s = str; *s; s++) ;
    return (s - str);
}

char *strstr(const char *str, const char *substring)
{
    const char *a;
    const char *b;

    b = substring;

    if (*b == 0) {
        return (char *) str;
    }

    for ( ; *str != 0; str += 1) {
        if (*str != *b) {
            continue;
        }

        a = str;
        while (1) {
            if (*b == 0) {
                return (char *) str;
            }
            if (*a++ != *b++) {
                break;
            }
        }
        b = substring;
    }

    return NULL;
}

int strncmp(const char * s1, const char * s2, size_t n)
{
    while (n && *s1 && (*s1 == *s2)) {
        ++s1;
        ++s2;
        --n;
    }
    if (n == 0) {
        return 0;
    } else {
        return (*(unsigned char *) s1 - *(unsigned char *) s2);
    }
}

bool if_exists(const char *path) {
    auto kernel_fopen =
        (void *(*)(const char*, unsigned int, unsigned int, int*))
        kdlsym(KERNEL_SYM_SYS_FOPEN);

    if (!kernel_fopen)
        return false;

    int error = 0;
    void* file_ptr = kernel_fopen(path, 0, 0, &error);

    return (file_ptr != nullptr && error == 0);
}