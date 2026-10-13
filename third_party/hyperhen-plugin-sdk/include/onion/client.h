#pragma once

#include <pthread.h>
#include <stdint.h>

#include "onion/plugin.h"
#include "onion/services.h"
#include "onion/transport.h"
#include "onion/ui.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct onion_client {
    onion_transport *transport;
    pthread_mutex_t mutex;
    uint32_t next_request_id;
    uint32_t session_capabilities;
    int initialized;
    int session_open;
} onion_client;

#define ONION_CLIENT_INITIALIZER {0}

onion_status onion_client_init(onion_client *client, onion_transport *transport);
/* Bind one cooperative plugin identity to this transport connection. */
onion_status onion_client_open_session(
    onion_client *client, const onion_plugin_descriptor_v1 *descriptor);
void onion_client_deinit(onion_client *client);
onion_status onion_client_make_services(onion_client *client,
                                        onion_host_services_v1 *out_services);
onion_status onion_client_ping(onion_client *client);
/* Non-blocking poll; returns ONION_E_NOT_FOUND when no UI action is queued. */
onion_status onion_client_poll_ui_event(onion_client *client,
                                        onion_ui_event_v1 *out_event);

#ifdef __cplusplus
}
#endif
