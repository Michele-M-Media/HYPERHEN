#pragma once

#include <stddef.h>
#include <stdint.h>

#include "onion/status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t onion_event_id;
typedef uint64_t onion_event_subscription;
typedef void (*onion_event_callback)(onion_event_id event, const void *data,
                                     size_t data_size, void *user_data);

typedef struct onion_event_bus onion_event_bus;

enum onion_system_event {
    ONION_EVENT_SYSTEM_READY = 0x100,
    ONION_EVENT_SHELLUI_READY = 0x101,
    ONION_EVENT_GAME_STARTED = 0x102,
    ONION_EVENT_GAME_STOPPED = 0x103,
    ONION_EVENT_REST_MODE = 0x104,
    ONION_EVENT_RESUME = 0x105,
    ONION_EVENT_LANGUAGE_CHANGED = 0x106,
    ONION_EVENT_HOST_DISCONNECTING = 0x107,
    ONION_EVENT_UI_ACTION = 0x108
};

onion_status onion_event_bus_create(onion_event_bus **out_bus);
void onion_event_bus_destroy(onion_event_bus *bus);
onion_status onion_event_subscribe(onion_event_bus *bus, onion_event_id event,
                                   onion_event_callback callback, void *user_data,
                                   onion_event_subscription *out_subscription);
onion_status onion_event_unsubscribe(onion_event_bus *bus,
                                     onion_event_subscription subscription);
onion_status onion_event_publish(onion_event_bus *bus, onion_event_id event,
                                 const void *data, size_t data_size);

#ifdef __cplusplus
}
#endif
