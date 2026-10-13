#include "ui_client_internal.h"

#include "client_internal.h"
#include "onion/ipc.h"
#include "onion/ui_protocol.h"

#include <stdlib.h>
#include <string.h>

static void put_u16(uint8_t *out, uint16_t value) {
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
}

static void put_u32(uint8_t *out, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) out[i] = (uint8_t)(value >> (i * 8));
}

static void put_u64(uint8_t *out, uint64_t value) {
    for (unsigned i = 0; i < 8; ++i) out[i] = (uint8_t)(value >> (i * 8));
}

static uint32_t get_u32(const uint8_t *in) {
    return (uint32_t)in[0] | ((uint32_t)in[1] << 8) |
           ((uint32_t)in[2] << 16) | ((uint32_t)in[3] << 24);
}

static uint64_t get_u64(const uint8_t *in) {
    uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i) value |= (uint64_t)in[i] << (i * 8);
    return value;
}

static uint32_t checksum32(const uint8_t *data, size_t size) {
    uint32_t value = 2166136261u;
    for (size_t i = 0; i < size; ++i) {
        value ^= data[i];
        value *= 16777619u;
    }
    return value;
}

static void abort_registration(onion_client *client, uint32_t transfer_id) {
    uint8_t payload[4];
    put_u32(payload, transfer_id);
    (void)onion_client_request(client, ONION_PLUGIN_IPC_UI_REGISTER_ABORT,
                               payload, sizeof(payload), NULL, 0, NULL);
}

static onion_status client_ui_register(void *context, const void *document,
                                       size_t document_size,
                                       onion_ui_handle *out_handle) {
    if (!context || !document || !out_handle || document_size == 0 ||
        document_size > ONION_UI_DOCUMENT_MAX_ENCODED_SIZE) {
        return ONION_E_INVALID_ARGUMENT;
    }
    onion_client *client = (onion_client *)context;
    uint8_t begin[8];
    put_u32(begin, (uint32_t)document_size);
    put_u32(begin + 4, checksum32((const uint8_t *)document, document_size));
    uint8_t transfer_data[4];
    size_t response_size = 0;
    onion_status result = onion_client_request(
        client, ONION_PLUGIN_IPC_UI_REGISTER_BEGIN, begin, sizeof(begin),
        transfer_data, sizeof(transfer_data), &response_size);
    if (result != ONION_OK) return result;
    if (response_size != sizeof(transfer_data)) return ONION_E_PROTOCOL;
    uint32_t transfer_id = get_u32(transfer_data);
    if (transfer_id == 0) return ONION_E_PROTOCOL;

    const size_t chunk_capacity = ONION_PLUGIN_IPC_MAX_PAYLOAD - 8u;
    uint8_t *chunk = (uint8_t *)malloc(ONION_PLUGIN_IPC_MAX_PAYLOAD);
    if (!chunk) {
        abort_registration(client, transfer_id);
        return ONION_E_NO_MEMORY;
    }
    size_t offset = 0;
    while (offset < document_size) {
        size_t chunk_size = document_size - offset;
        if (chunk_size > chunk_capacity) chunk_size = chunk_capacity;
        put_u32(chunk, transfer_id);
        put_u32(chunk + 4, (uint32_t)offset);
        memcpy(chunk + 8, (const uint8_t *)document + offset, chunk_size);
        result = onion_client_request(
            client, ONION_PLUGIN_IPC_UI_REGISTER_CHUNK, chunk, chunk_size + 8,
            NULL, 0, NULL);
        if (result != ONION_OK) break;
        offset += chunk_size;
    }
    free(chunk);
    if (result != ONION_OK) {
        abort_registration(client, transfer_id);
        return result;
    }

    uint8_t commit[4];
    uint8_t handle_data[8];
    put_u32(commit, transfer_id);
    response_size = 0;
    result = onion_client_request(
        client, ONION_PLUGIN_IPC_UI_REGISTER_COMMIT, commit, sizeof(commit),
        handle_data, sizeof(handle_data), &response_size);
    if (result != ONION_OK) {
        abort_registration(client, transfer_id);
        return result;
    }
    if (response_size != sizeof(handle_data)) return ONION_E_PROTOCOL;
    *out_handle = get_u64(handle_data);
    return *out_handle != 0 ? ONION_OK : ONION_E_PROTOCOL;
}

static onion_status client_ui_unregister(void *context, onion_ui_handle handle) {
    if (!context || handle == 0) return ONION_E_INVALID_ARGUMENT;
    uint8_t payload[8];
    put_u64(payload, handle);
    return onion_client_request((onion_client *)context,
                                ONION_PLUGIN_IPC_UI_UNREGISTER,
                                payload, sizeof(payload), NULL, 0, NULL);
}

static onion_status client_ui_set_value(void *context, onion_ui_handle handle,
                                        const char *node_id,
                                        onion_ui_value_type value_type,
                                        const char *value) {
    if (!context || handle == 0 || !node_id || !value) return ONION_E_INVALID_ARGUMENT;
    size_t id_size = strlen(node_id);
    size_t value_size = strlen(value);
    if (id_size == 0 || id_size >= ONION_UI_NODE_ID_MAX ||
        value_size >= ONION_UI_VALUE_MAX || value_type == ONION_UI_VALUE_NONE ||
        value_type > ONION_UI_VALUE_STRING ||
        16u + id_size + value_size > ONION_PLUGIN_IPC_MAX_PAYLOAD) {
        return ONION_E_INVALID_ARGUMENT;
    }
    uint8_t payload[ONION_PLUGIN_IPC_MAX_PAYLOAD];
    put_u64(payload, handle);
    put_u32(payload + 8, (uint32_t)value_type);
    put_u16(payload + 12, (uint16_t)id_size);
    put_u16(payload + 14, (uint16_t)value_size);
    memcpy(payload + 16, node_id, id_size);
    memcpy(payload + 16 + id_size, value, value_size);
    return onion_client_request((onion_client *)context,
                                ONION_PLUGIN_IPC_UI_SET_VALUE,
                                payload, 16 + id_size + value_size,
                                NULL, 0, NULL);
}

onion_status onion_ui_client_make_services(onion_client *client,
                                           onion_ui_services_v1 *out_services) {
    if (!client || !client->initialized || !out_services)
        return ONION_E_INVALID_ARGUMENT;
    memset(out_services, 0, sizeof(*out_services));
    out_services->struct_size = sizeof(*out_services);
    out_services->abi_version = ONION_UI_ABI_VERSION;
    out_services->context = client;
    out_services->register_document = client_ui_register;
    out_services->unregister_document = client_ui_unregister;
    out_services->set_value = client_ui_set_value;
    return ONION_OK;
}
