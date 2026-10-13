#pragma once

#include <stdint.h>

#include "onion/plugin.h"
#include "onion/services.h"
#include "onion/status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum onion_plugin_state {
    ONION_PLUGIN_STATE_CREATED = 0,
    ONION_PLUGIN_STATE_INITIALIZED = 1,
    ONION_PLUGIN_STATE_RUNNING = 2,
    ONION_PLUGIN_STATE_STOPPING = 3,
    ONION_PLUGIN_STATE_STOPPED = 4,
    ONION_PLUGIN_STATE_FAILED = 5,
    ONION_PLUGIN_STATE_SHUTDOWN = 6
} onion_plugin_state;

typedef onion_status (*onion_plugin_init_fn)(
    void *context, const onion_host_services_v1 *services);
typedef onion_status (*onion_plugin_lifecycle_fn)(void *context);

typedef struct onion_plugin_callbacks_v1 {
    uint32_t struct_size;
    uint32_t abi_version;
    void *context;
    onion_plugin_init_fn on_init;
    onion_plugin_lifecycle_fn on_start;
    onion_plugin_lifecycle_fn on_stop;
    onion_plugin_lifecycle_fn on_shutdown;
} onion_plugin_callbacks_v1;

typedef struct onion_plugin_runtime {
    uint32_t struct_size;
    uint32_t abi_version;
    const onion_plugin_descriptor_v1 *descriptor;
    onion_plugin_callbacks_v1 callbacks;
    onion_host_services_v1 services;
    onion_plugin_state state;
} onion_plugin_runtime;

#define ONION_PLUGIN_RUNTIME_INITIALIZER {0}

onion_status onion_plugin_runtime_init(
    onion_plugin_runtime *runtime,
    const onion_plugin_descriptor_v1 *descriptor,
    const onion_plugin_callbacks_v1 *callbacks,
    const onion_host_services_v1 *services);
onion_status onion_plugin_runtime_start(onion_plugin_runtime *runtime);
onion_status onion_plugin_runtime_stop(onion_plugin_runtime *runtime);
onion_status onion_plugin_runtime_fail(onion_plugin_runtime *runtime);
onion_status onion_plugin_runtime_shutdown(onion_plugin_runtime *runtime);
onion_plugin_state onion_plugin_runtime_state(const onion_plugin_runtime *runtime);

#ifdef __cplusplus
}
#endif
