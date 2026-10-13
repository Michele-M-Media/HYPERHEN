#include "onion/services.h"

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

onion_status onion_services_validate(const onion_host_services_v1 *services) {
    const size_t required_size = offsetof(onion_host_services_v1, query_interface);
    if (!services || services->struct_size < required_size ||
        services->abi_version != ONION_HOST_SERVICES_ABI_VERSION) {
        return ONION_E_PROTOCOL;
    }
    return ONION_OK;
}

onion_status onion_service_log(const onion_host_services_v1 *services,
                               onion_log_level level, const char *message) {
    if (onion_services_validate(services) != ONION_OK || !services->log || !message) {
        return ONION_E_INVALID_ARGUMENT;
    }
    return services->log(services->context, level, message);
}

onion_status onion_service_notify(const onion_host_services_v1 *services,
                                  const char *message) {
    if (onion_services_validate(services) != ONION_OK || !services->notify || !message) {
        return ONION_E_INVALID_ARGUMENT;
    }
    return services->notify(services->context, message);
}

onion_status onion_service_config_get(const onion_host_services_v1 *services,
                                      const char *key, char *value,
                                      size_t value_size) {
    if (onion_services_validate(services) != ONION_OK || !services->config_get ||
        !key || !value || value_size == 0) {
        return ONION_E_INVALID_ARGUMENT;
    }
    return services->config_get(services->context, key, value, value_size);
}

onion_status onion_service_config_set(const onion_host_services_v1 *services,
                                      const char *key, const char *value) {
    if (onion_services_validate(services) != ONION_OK || !services->config_set ||
        !key || !value) {
        return ONION_E_INVALID_ARGUMENT;
    }
    return services->config_set(services->context, key, value);
}

onion_status onion_service_config_get_int(const onion_host_services_v1 *services,
                                          const char *key, int64_t *value) {
    char text[64];
    if (!value) return ONION_E_INVALID_ARGUMENT;
    onion_status result = onion_service_config_get(services, key, text, sizeof(text));
    if (result != ONION_OK) return result;
    char *end = NULL;
    long long parsed = strtoll(text, &end, 10);
    if (end == text || *end != '\0') return ONION_E_PROTOCOL;
    *value = (int64_t)parsed;
    return ONION_OK;
}

onion_status onion_service_config_set_int(const onion_host_services_v1 *services,
                                          const char *key, int64_t value) {
    char text[64];
    int written = snprintf(text, sizeof(text), "%lld", (long long)value);
    if (written < 0 || (size_t)written >= sizeof(text)) return ONION_E_INVALID_ARGUMENT;
    return onion_service_config_set(services, key, text);
}

onion_status onion_service_config_get_bool(const onion_host_services_v1 *services,
                                           const char *key, int *value) {
    char text[16];
    if (!value) return ONION_E_INVALID_ARGUMENT;
    onion_status result = onion_service_config_get(services, key, text, sizeof(text));
    if (result != ONION_OK) return result;
    if (strcmp(text, "true") == 0 || strcmp(text, "1") == 0) *value = 1;
    else if (strcmp(text, "false") == 0 || strcmp(text, "0") == 0) *value = 0;
    else return ONION_E_PROTOCOL;
    return ONION_OK;
}

onion_status onion_service_config_set_bool(const onion_host_services_v1 *services,
                                           const char *key, int value) {
    return onion_service_config_set(services, key, value ? "true" : "false");
}

onion_status onion_service_query_interface(
    const onion_host_services_v1 *services, const char *name,
    uint32_t min_version, void *out_interface, size_t interface_size) {
    const size_t required_size = offsetof(onion_host_services_v1, query_interface) +
                                 sizeof(services->query_interface);
    if (onion_services_validate(services) != ONION_OK || !name || !out_interface ||
        interface_size == 0 || services->struct_size < required_size ||
        !services->query_interface) {
        return ONION_E_NOT_SUPPORTED;
    }
    return services->query_interface(services->context, name, min_version,
                                     out_interface, interface_size);
}
