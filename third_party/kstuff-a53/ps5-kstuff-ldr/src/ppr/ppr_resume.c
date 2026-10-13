#include "ppr_resume.h"
#include "ppr_install_bridge.h"
#include "a53_transport.h"

#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <sys/user.h>
#include <ps5/klog.h>

#define POWER_STATE_WORKING 1000u
#define POWER_STATE_POWER_SAVING 200u
#define POWER_STATE_SUSPENDING 300u
#define POWER_STATE_STANDBY 500u
#define POWER_POLL_US 250000u
#define STABLE_WORKING_POLLS 20u
#define IDLE_POLLS 12u

typedef intptr_t power_event_t;
extern int sceKernelOpenEventFlag(power_event_t *event, const char *name);
extern int sceKernelPollEventFlag(power_event_t event, uint64_t bits,
                                  unsigned int mode, uint64_t *result);
extern int sceKernelCloseEventFlag(power_event_t event);
extern void backpork_request_resume_rearm(void);

typedef struct app_info {
    uint32_t app_id;
    uint64_t unknown1;
    char title_id[14];
    char unknown2[0x3c];
} app_info_t;
extern int sceKernelGetAppInfo(pid_t pid, app_info_t *info);

/* Only the monitor thread uses this handle, including the transport guard. */
static power_event_t power_event = -1;

static int read_power_state(unsigned *state) {
    uint64_t result = 0;
    if (power_event < 0 ||
        sceKernelPollEventFlag(power_event, UINT64_MAX, 2u, &result) < 0)
        return -1;
    *state = (unsigned)(result & 0xffffu);
    return 0;
}

static int power_is_working(void) {
    unsigned state = 0;
    return read_power_state(&state) == 0 && state == POWER_STATE_WORKING;
}

/* A failed or truncated process-list read must not authorize PPR writes. */
static int no_game_processes(void) {
    int mib[4] = {1, 14, 8, 0};
    size_t size = 0;
    if (sysctl(mib, 4, NULL, &size, NULL, 0) != 0 ||
        size == 0 || size > 1024 * 1024)
        return 0;
    uint8_t *buffer = malloc(size);
    if (!buffer)
        return 0;
    if (sysctl(mib, 4, buffer, &size, NULL, 0) != 0) {
        free(buffer);
        return 0;
    }

    int idle = 1;
    for (size_t offset = 0; offset < size;) {
        int entry_size = 0;
        if (size - offset < sizeof(entry_size)) {
            idle = 0;
            break;
        }
        memcpy(&entry_size, buffer + offset, sizeof(entry_size));
        if (entry_size < 447 + TDNAMLEN + 1 ||
            (size_t)entry_size > size - offset) {
            idle = 0;
            break;
        }
        pid_t pid = 0;
        memcpy(&pid, buffer + offset + 72, sizeof(pid));
        const char *name = (const char *)buffer + offset + 447;
        if (strncmp(name, "eboot.bin", TDNAMLEN + 1) == 0 ||
            strncmp(name, "eboot", TDNAMLEN + 1) == 0) {
            idle = 0;
            break;
        }
        if (pid > 0 && pid != getpid()) {
            app_info_t info = {0};
            if (sceKernelGetAppInfo(pid, &info) == 0 &&
                (!memcmp(info.title_id, "PPSA", 4) ||
                 !memcmp(info.title_id, "CUSA", 4))) {
                idle = 0;
                break;
            }
        }
        offset += (size_t)entry_size;
    }
    free(buffer);
    return idle;
}

static void *power_monitor(void *unused) {
    (void)unused;
    unsigned previous = 0;
    unsigned saw_working = 0;
    unsigned saw_sleep = 0;
    unsigned pending = 0;
    unsigned stable = 0;
    unsigned idle = 0;

    for (;;) {
        if (power_event < 0) {
            if (sceKernelOpenEventFlag(&power_event,
                                       "SceSystemStateMgrInfo") < 0) {
                power_event = -1;
                sleep(1);
                continue;
            }
            klog_printf("[PPR] power monitor: event opened\n");
        }

        unsigned state = 0;
        if (read_power_state(&state) != 0) {
            klog_printf("[PPR] power monitor: poll failed; reopening\n");
            sceKernelCloseEventFlag(power_event);
            power_event = -1;
            pending = stable = idle = 0;
            sleep(1);
            continue;
        }

        if (state != previous)
            klog_printf("[PPR] power state %u -> %u\n", previous, state);

        if (state == POWER_STATE_WORKING) {
            if (!saw_working)
                saw_working = 1;
            if (saw_sleep && previous != POWER_STATE_WORKING) {
                pending = 1;
                stable = idle = 0;
                saw_sleep = 0;
                klog_printf("[PPR] resume: WORKING observed; waiting for idle\n");
                backpork_request_resume_rearm();
            }
            if (pending) {
                if (stable < STABLE_WORKING_POLLS)
                    ++stable;
                else if (no_game_processes())
                    ++idle;
                else
                    idle = 0;
                if (idle >= IDLE_POLLS) {
                    pending = 0;
                    if (power_is_working() && no_game_processes()) {
                        klog_printf("[PPR] resume: installing/checking PPR\n");
                        a53_transport_set_power_guard(power_is_working);
                        int result = run_ppr_install_after_resume();
                        klog_printf(result == 0
                            ? "[PPR] resume: patch verified\n"
                            : "[PPR] resume: patch failed; no automatic retry\n");
                    }
                }
            }
        } else {
            pending = stable = idle = 0;
            if (saw_working &&
                (state == POWER_STATE_POWER_SAVING ||
                 state == POWER_STATE_SUSPENDING ||
                 state == POWER_STATE_STANDBY))
                saw_sleep = 1;
        }

        previous = state;
        usleep(POWER_POLL_US);
    }
    return NULL;
}

int ppr_resume_start(void) {
    pthread_t thread;
    if (pthread_create(&thread, NULL, power_monitor, NULL) != 0)
        return -1;
    pthread_detach(thread);
    return 0;
}
