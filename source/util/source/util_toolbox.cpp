/* Copyright (C) 2025 OnionHEN / LightningMods — P0 split. */


#include <onion/platform.h>
#include <onion/ready.h>
#include <onion/ipc_client.hpp>
#include <msg.hpp>
#include "common_utils.h"
#include "util_toolbox.h"
#include <unistd.h>

bool enable_toolbox() {
    // Single client path into crit daemon (replaces hand-rolled Unix socket).
    // Wait briefly for crit socket / ready — bootstrap order is util then daemon.
    for (int wait = 0; wait <= 20; ++wait) {
      const bool daemon_ready = onion_ready_is_set(ONION_READY_DAEMON);
      const bool sock_exists = if_exists(CRIT_IPC_SOC);
      if (daemon_ready || sock_exists) {
        LOG_DEBUG("rest: crit reachable wait=%d ready=%d sock=%d", wait,
                  daemon_ready ? 1 : 0, sock_exists ? 1 : 0);
        break;
      }
      if (wait == 20) {
        LOG_ERROR("rest: crit daemon not reachable after 20s");
        onion_notify(true, "notify.toolbox.load_failed");
        return false;
      }
      if (wait == 0 || wait % 5 == 0) {
        LOG_DEBUG("rest: waiting for crit daemon (%d/20)", wait);
      }
      sleep(1);
    }
    LOG_DEBUG("rest: sending BREW_ENABLE_TOOLBOX to crit");
    const bool ok = IPC_Client::getInstance(/*util=*/false).EnableToolbox();
    LOG_DEBUG("rest: EnableToolbox returned %d", ok ? 1 : 0);
    return ok;
}

/** Re-request toolbox inject via crit daemon (util crash / re-launch). */
bool toolbox_reinject() {
    LOG_DEBUG("toolbox reinject (util restart)");
    if (!enable_toolbox()) {
        LOG_ERROR("toolbox reinject failed");
        onion_notify(true, "notify.toolbox.inject_failed");
        return false;
    }
    LOG_DEBUG("toolbox reinject succeeded");
    return true;
}
