#include "ppr_install_bridge.h"
#include "a53_transport.h"
#include "notify.h"
#include "ppr_patch.h"

#include <stdint.h>
#include <stdio.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <unistd.h>
#include <onion/firmware_capabilities.h>
#include <onion/ready.h>
#include <ps5/kernel.h>
#include <ps5/klog.h>

static uint64_t startup_now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + (uint64_t)tv.tv_usec / 1000;
}

static void startup_stage(const char *name, uint64_t began_ms) {
    unsigned long long elapsed = (unsigned long long)(startup_now_ms() - began_ms);
    FILE *log = fopen("/data/kstuff-startup.log", "a");
    if (log) {
        fprintf(log, "[TIME] %s: %llu ms\n", name, elapsed);
        fclose(log);
    }
    klog_printf("[TIME] %s: %llu ms\n", name, elapsed);
}

static int hyperhen_hypervisor_route_ready(uint32_t system_fw) {
    switch (onion_firmware_route(system_fw)) {
    case ONION_FIRMWARE_ROUTE_HYPERVISOR_13XX:
        return access(ONION_READY_ROOT "/" ONION_READY_HYPERVISOR, F_OK) == 0;
    case ONION_FIRMWARE_ROUTE_HYPERVISOR_5XX:
        return access(ONION_READY_ROOT "/" ONION_READY_HYPERVISOR_5XX,
                      F_OK) == 0;
    default:
        return 0;
    }
}

static int hyperhen_no_ppr_route_ready(uint32_t system_fw) {
    return onion_firmware_route(system_fw) ==
               ONION_FIRMWARE_ROUTE_HYPERCORE_NO_PPR &&
           access(ONION_READY_ROOT "/" ONION_READY_HYPERCORE_NO_PPR,
                  F_OK) == 0;
}

static int run_ppr_install_impl(int resumed) {
    uint64_t total_ms = startup_now_ms();
    uint64_t stage_ms = total_ms;
    struct rlimit limit;
    if (getrlimit(RLIMIT_NOFILE, &limit) == 0 &&
        limit.rlim_cur < limit.rlim_max) {
        limit.rlim_cur = limit.rlim_max;
        (void)setrlimit(RLIMIT_NOFILE, &limit);
    }
    const struct a53_transport_options conservative = {0, 0, 0};
    const struct a53_transport_options fast = {
        .persistent = 1, .batch = 1, .mixed_io = 1
    };
    int result = -1;
    char version[160];
    uint32_t system_fw = kernel_get_fw_version() & 0xffff0000U;
    ppr_printf("[PPR] System FW: 0x%08x\n", system_fw);
    /*
     * The bootstrapper publishes this marker only for exact firmwares with a
     * complete HyperCore/ShellCore table but no exact PPR installer profile.
     * The firmware check above prevents a stale or manually-created marker
     * from enabling this path on an unrelated release.
     */
    if (hyperhen_no_ppr_route_ready(system_fw)) {
        ppr_printf("[PPR] HyperCore compatibility route ready; PPR install disabled\n");
        startup_stage("HyperCore no-PPR route", total_ms);
        return 0;
    }
    /* Byepervisor Reborn supplies the supported 13.x route. Its offsets do
       not overlap the supplied exact A53/PPR profiles, so once its readiness
       marker is present we continue into kstuff without pretending PPR ran. */
    if (hyperhen_hypervisor_route_ready(system_fw)) {
        ppr_printf("[PPR] HyperHEN hypervisor route ready; skipping unsupported 13.x PPR install\n");
        startup_stage("Hypervisor route", total_ms);
        return 0;
    }
    if (a53_transport_initialize(&conservative) != 0) {
        ppr_puts("[PPR] Transport initialization failed");
        goto done;
    }
    startup_stage("A53 init", stage_ms);
    stage_ms = startup_now_ms();
    /* Use the upstream phase5 accelerator only after its exact kernel clock
       pair has been located and verified.  The accelerator is optional: the
       verified pair-only fast transport is also an upstream-supported install
       path and does not write the global clock. */
    if (!resumed) {
        if (a53_transport_enable_time_acceleration() != 0) {
            ppr_puts("[PPR] Time acceleration unavailable; continuing with verified transport");
            klog_printf("[TIME] A53 clock acceleration skipped; using verified transport\n");
        } else {
            startup_stage("A53 clock scan", stage_ms);
        }
    }
    stage_ms = startup_now_ms();
    if (a53_transport_get_version(version, sizeof(version)) != 0) {
        ppr_puts("[PPR] GET_CONF failed");
        goto done;
    }
    startup_stage("A53 GET_CONF", stage_ms);
    stage_ms = startup_now_ms();
    ppr_printf("[PPR] A53: %s\n", version);
    uint32_t a53_release = a53_transport_parse_release(version);
    uint32_t release = a53_release ? a53_release : system_fw;
    if (a53_release && a53_release != system_fw)
        ppr_printf("[PPR] System FW 0x%08x differs from A53 0x%08x\n",
                   system_fw, a53_release);
    enum ppr_target_type target = ppr_patch_parse_target(version);
    if (!ppr_patch_target_supported(release, target)) {
        ppr_printf("[PPR] No exact profile for FW 0x%08x target %s\n",
                   release, ppr_patch_target_name(target));
        goto done;
    }
    if (a53_transport_verify_and_enable_fast(&fast) < 0) {
        ppr_puts("[PPR] Fast transport verification failed");
        goto done;
    }
    startup_stage("A53 fast probe", stage_ms);
    stage_ms = startup_now_ms();
    struct ppr_patch_transport transport;
    a53_transport_make_ppr(&transport, 1);
    /* Match the dedicated installer's --idle default. Run only after
       package reads, APR binds and mounts have settled. */
    result = ppr_patch_run_target(&transport, release, target,
                                  PPR_PATCH_INSTALL, 1);
    startup_stage("PPR install", stage_ms);
    ppr_printf("[PPR] Install result: %d\n", result);
    klog_printf("[TIME] PPR install result: %d\n", result);
done:
    a53_transport_shutdown();
    if (resumed)
        a53_transport_set_power_guard(NULL);
    ppr_notify_flush();
    startup_stage("A53/PPR total", total_ms);
    return result == 0 ? 0 : -1;
}

int run_ppr_install(void) {
    return run_ppr_install_impl(0);
}

int run_ppr_install_after_resume(void) {
    return run_ppr_install_impl(1);
}
