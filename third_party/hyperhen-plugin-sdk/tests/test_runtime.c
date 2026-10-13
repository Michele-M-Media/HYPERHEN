#include "onion/event_bus.h"
#include "onion/runtime.h"
#include "onion/services.h"

#include <stdio.h>
#include <string.h>

static int event_count;
static int callback_count;

static onion_status mock_log(void *context, onion_log_level level, const char *message) {
    (void)context;
    (void)level;
    return message ? ONION_OK : ONION_E_INVALID_ARGUMENT;
}

static onion_status mock_notify(void *context, const char *message) {
    (void)context;
    return message ? ONION_OK : ONION_E_INVALID_ARGUMENT;
}

static onion_status mock_get(void *context, const char *key, char *value, size_t size) {
    (void)context;
    if (!key || !value || size < 2) return ONION_E_INVALID_ARGUMENT;
    if (strcmp(key, "enabled") != 0) return ONION_E_NOT_FOUND;
    strcpy(value, "true");
    return ONION_OK;
}

static onion_status mock_set(void *context, const char *key, const char *value) {
    (void)context;
    return key && value ? ONION_OK : ONION_E_INVALID_ARGUMENT;
}

static onion_status plugin_init(void *context, const onion_host_services_v1 *services) {
    (void)context;
    callback_count++;
    return onion_service_log(services, ONION_LOG_INFO, "init");
}

static onion_status plugin_start(void *context) {
    (void)context;
    callback_count++;
    return ONION_OK;
}

static void on_event(onion_event_id event, const void *data, size_t size, void *user_data) {
    (void)event;
    (void)data;
    (void)size;
    (void)user_data;
    event_count++;
}

static int check(int condition, const char *message) {
    if (!condition) fprintf(stderr, "FAIL: %s\n", message);
    return condition;
}

int main(void) {
    onion_host_services_v1 services = {
        sizeof(services), ONION_HOST_SERVICES_ABI_VERSION, NULL,
        mock_log, mock_notify, mock_get, mock_set, NULL
    };
    static const onion_plugin_descriptor_v1 descriptor = {
        sizeof(descriptor), ONION_PLUGIN_ABI_VERSION, ONION_PLUGIN_CAP_NOTIFY, 0,
        "TEST00001", "1.00", "Runtime test"
    };
    onion_plugin_callbacks_v1 callbacks = {
        sizeof(callbacks), ONION_PLUGIN_ABI_VERSION, NULL,
        plugin_init, plugin_start, NULL, NULL
    };
    onion_plugin_runtime runtime = ONION_PLUGIN_RUNTIME_INITIALIZER;
    if (!check(onion_plugin_runtime_init(&runtime, &descriptor, &callbacks, &services) == ONION_OK,
               "runtime init")) return 1;
    if (!check(callback_count == 1 && onion_plugin_runtime_state(&runtime) == ONION_PLUGIN_STATE_INITIALIZED,
               "init callback and state")) return 1;
    if (!check(onion_plugin_runtime_start(&runtime) == ONION_OK && callback_count == 2,
               "runtime start")) return 1;
    if (!check(onion_plugin_runtime_stop(&runtime) == ONION_OK &&
               onion_plugin_runtime_state(&runtime) == ONION_PLUGIN_STATE_STOPPED,
               "runtime stop")) return 1;

    int enabled = 0;
    if (!check(onion_service_config_get_bool(&services, "enabled", &enabled) == ONION_OK && enabled,
               "typed config")) return 1;

    onion_event_bus *bus = NULL;
    onion_event_subscription subscription = 0;
    if (!check(onion_event_bus_create(&bus) == ONION_OK,
               "event bus create")) return 1;
    if (!check(onion_event_subscribe(bus, ONION_EVENT_GAME_STARTED, on_event, NULL, &subscription) == ONION_OK,
               "event subscribe")) return 1;
    if (!check(onion_event_publish(bus, ONION_EVENT_GAME_STARTED, NULL, 0) == ONION_OK && event_count == 1,
               "event publish")) return 1;
    if (!check(onion_event_unsubscribe(bus, subscription) == ONION_OK &&
               onion_event_publish(bus, ONION_EVENT_GAME_STARTED, NULL, 0) == ONION_OK && event_count == 1,
               "event unsubscribe")) return 1;
    onion_event_bus_destroy(bus);
    puts("sdk_runtime_test: PASS");
    return 0;
}
