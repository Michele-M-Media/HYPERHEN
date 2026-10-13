#include "onion/runtime.h"

#include <string.h>

static int descriptor_valid(const onion_plugin_descriptor_v1 *descriptor) {
    return descriptor && descriptor->struct_size >= sizeof(*descriptor) &&
           descriptor->abi_version == ONION_PLUGIN_ABI_VERSION &&
           descriptor->plugin_id[0] != '\0' && descriptor->version[0] != '\0';
}

onion_status onion_plugin_runtime_init(
    onion_plugin_runtime *runtime,
    const onion_plugin_descriptor_v1 *descriptor,
    const onion_plugin_callbacks_v1 *callbacks,
    const onion_host_services_v1 *services) {
    if (!runtime || !descriptor || !callbacks || !services || !descriptor_valid(descriptor) ||
        callbacks->struct_size < sizeof(*callbacks) || callbacks->abi_version != ONION_PLUGIN_ABI_VERSION) {
        return ONION_E_INVALID_ARGUMENT;
    }
    if (runtime->struct_size == sizeof(*runtime) &&
        runtime->abi_version == ONION_PLUGIN_ABI_VERSION &&
        runtime->state != ONION_PLUGIN_STATE_CREATED &&
        runtime->state != ONION_PLUGIN_STATE_STOPPED) {
        return ONION_E_ALREADY_INITIALIZED;
    }
    onion_status result = onion_services_validate(services);
    if (result != ONION_OK) {
        return result;
    }
    memset(runtime, 0, sizeof(*runtime));
    runtime->struct_size = sizeof(*runtime);
    runtime->abi_version = ONION_PLUGIN_ABI_VERSION;
    runtime->descriptor = descriptor;
    runtime->callbacks = *callbacks;
    size_t services_size = services->struct_size;
    if (services_size > sizeof(runtime->services)) services_size = sizeof(runtime->services);
    memcpy(&runtime->services, services, services_size);
    runtime->state = ONION_PLUGIN_STATE_INITIALIZED;
    if (runtime->callbacks.on_init) {
        onion_status callback_result = runtime->callbacks.on_init(runtime->callbacks.context, services);
        if (callback_result != ONION_OK) {
            runtime->state = ONION_PLUGIN_STATE_FAILED;
            return callback_result;
        }
    }
    return ONION_OK;
}

onion_status onion_plugin_runtime_start(onion_plugin_runtime *runtime) {
    if (!runtime) return ONION_E_INVALID_ARGUMENT;
    if (runtime->state != ONION_PLUGIN_STATE_INITIALIZED &&
        runtime->state != ONION_PLUGIN_STATE_STOPPED) return ONION_E_INVALID_STATE;
    runtime->state = ONION_PLUGIN_STATE_RUNNING;
    if (runtime->callbacks.on_start) {
        onion_status result = runtime->callbacks.on_start(runtime->callbacks.context);
        if (result != ONION_OK) {
            runtime->state = ONION_PLUGIN_STATE_FAILED;
            return result;
        }
    }
    return ONION_OK;
}

onion_status onion_plugin_runtime_stop(onion_plugin_runtime *runtime) {
    if (!runtime) return ONION_E_INVALID_ARGUMENT;
    if (runtime->state != ONION_PLUGIN_STATE_RUNNING &&
        runtime->state != ONION_PLUGIN_STATE_FAILED) return ONION_E_INVALID_STATE;
    runtime->state = ONION_PLUGIN_STATE_STOPPING;
    if (runtime->callbacks.on_stop) {
        onion_status result = runtime->callbacks.on_stop(runtime->callbacks.context);
        if (result != ONION_OK) {
            runtime->state = ONION_PLUGIN_STATE_FAILED;
            return result;
        }
    }
    runtime->state = ONION_PLUGIN_STATE_STOPPED;
    return ONION_OK;
}

onion_status onion_plugin_runtime_shutdown(onion_plugin_runtime *runtime) {
    if (!runtime) return ONION_E_INVALID_ARGUMENT;
    if (runtime->state == ONION_PLUGIN_STATE_RUNNING || runtime->state == ONION_PLUGIN_STATE_FAILED) {
        onion_status result = onion_plugin_runtime_stop(runtime);
        if (result != ONION_OK) return result;
    }
    if (runtime->state != ONION_PLUGIN_STATE_STOPPED &&
        runtime->state != ONION_PLUGIN_STATE_INITIALIZED) return ONION_E_INVALID_STATE;
    if (runtime->callbacks.on_shutdown) {
        onion_status result = runtime->callbacks.on_shutdown(runtime->callbacks.context);
        if (result != ONION_OK) return result;
    }
    runtime->state = ONION_PLUGIN_STATE_SHUTDOWN;
    return ONION_OK;
}

onion_status onion_plugin_runtime_fail(onion_plugin_runtime *runtime) {
    if (!runtime) return ONION_E_INVALID_ARGUMENT;
    if (runtime->state == ONION_PLUGIN_STATE_CREATED ||
        runtime->state == ONION_PLUGIN_STATE_STOPPED) return ONION_E_INVALID_STATE;
    runtime->state = ONION_PLUGIN_STATE_FAILED;
    return ONION_OK;
}

onion_plugin_state onion_plugin_runtime_state(const onion_plugin_runtime *runtime) {
    return runtime ? runtime->state : ONION_PLUGIN_STATE_FAILED;
}
