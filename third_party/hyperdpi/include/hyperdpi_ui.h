#pragma once

#include <onion/services.h>
#include <onion/status.h>
#include <onion/ui.h>

#include "hyperdpi_settings.h"

typedef enum hyperdpi_ui_action_kind {
    HYPERDPI_UI_ACTION_NONE = 0,
    HYPERDPI_UI_ACTION_SET_ENABLED,
    HYPERDPI_UI_ACTION_SET_API_PORT,
    HYPERDPI_UI_ACTION_SET_WEBUI_PORT,
    HYPERDPI_UI_ACTION_RESTART
} hyperdpi_ui_action_kind;

typedef struct hyperdpi_ui_action {
    hyperdpi_ui_action_kind kind;
    int enabled;
    uint16_t port;
} hyperdpi_ui_action;

onion_status hyperdpi_ui_create(const hyperdpi_settings *settings,
                                onion_ui_document **out_document);
onion_status hyperdpi_ui_decode_action(onion_ui_handle handle,
                                       const onion_ui_event_v1 *event,
                                       hyperdpi_ui_action *out_action);
onion_status hyperdpi_ui_set_enabled(const onion_host_services_v1 *services,
                                     onion_ui_handle handle, int enabled);
onion_status hyperdpi_ui_set_port(const onion_host_services_v1 *services,
                                  onion_ui_handle handle, const char *node_id,
                                  uint16_t port);
