#include "onion/client.h"

#include "client_internal.h"
#include "onion/event_bus.h"
#include "onion/ipc.h"

#include <string.h>

static uint16_t get_u16(const uint8_t *bytes) {
    return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
}

static uint32_t get_u32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static uint64_t get_u64(const uint8_t *bytes) {
    uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i) value |= (uint64_t)bytes[i] << (i * 8);
    return value;
}

static int field_valid(uint16_t size, size_t capacity, int required) {
    return size < capacity && (!required || size != 0);
}

static void copy_text(char *out, const uint8_t *data, uint16_t size) {
    if (size) memcpy(out, data, size);
    out[size] = '\0';
}

onion_status onion_client_poll_ui_event(onion_client *client,
                                        onion_ui_event_v1 *out_event) {
    if (!client || !out_event) return ONION_E_INVALID_ARGUMENT;

    uint8_t encoded[ONION_PLUGIN_IPC_MAX_PAYLOAD];
    size_t encoded_size = 0;
    onion_status status = onion_client_request(
        client, ONION_PLUGIN_IPC_EVENT, NULL, 0, encoded, sizeof(encoded),
        &encoded_size);
    if (status != ONION_OK) return status;
    if (encoded_size < ONION_PLUGIN_IPC_UI_EVENT_HEADER_SIZE ||
        get_u32(encoded) != ONION_EVENT_UI_ACTION ||
        get_u32(encoded + 4) != ONION_UI_ABI_VERSION) {
        return ONION_E_PROTOCOL;
    }

    const uint16_t contribution_size = get_u16(encoded + 28);
    const uint16_t page_size = get_u16(encoded + 30);
    const uint16_t node_size = get_u16(encoded + 32);
    const uint16_t value_size = get_u16(encoded + 34);
    const size_t text_size = (size_t)contribution_size + page_size +
                             node_size + value_size;
    const uint32_t value_type = get_u32(encoded + 24);
    if (encoded_size != ONION_PLUGIN_IPC_UI_EVENT_HEADER_SIZE + text_size ||
        get_u64(encoded + 8) == 0 || get_u64(encoded + 16) == 0 ||
        value_type > ONION_UI_VALUE_STRING ||
        !field_valid(contribution_size, ONION_UI_CONTRIBUTION_ID_MAX, 1) ||
        !field_valid(page_size, ONION_UI_NODE_ID_MAX, 1) ||
        !field_valid(node_size, ONION_UI_NODE_ID_MAX, 1) ||
        !field_valid(value_size, ONION_UI_VALUE_MAX, 0)) {
        return ONION_E_PROTOCOL;
    }

    onion_ui_event_v1 event;
    memset(&event, 0, sizeof(event));
    event.struct_size = sizeof(event);
    event.abi_version = ONION_UI_ABI_VERSION;
    event.sequence = get_u64(encoded + 8);
    event.handle = get_u64(encoded + 16);
    event.value_type = value_type;
    const uint8_t *text = encoded + ONION_PLUGIN_IPC_UI_EVENT_HEADER_SIZE;
    copy_text(event.contribution_id, text, contribution_size);
    text += contribution_size;
    copy_text(event.page_id, text, page_size);
    text += page_size;
    copy_text(event.node_id, text, node_size);
    text += node_size;
    copy_text(event.value, text, value_size);
    *out_event = event;
    return ONION_OK;
}
