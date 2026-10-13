#include "onion/ui.h"
#include "onion/ui_protocol.h"
#include "onion/client.h"
#include "onion/event_bus.h"
#include "onion/ipc.h"
#include "onion/transport.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

typedef struct mock_ui_host {
    onion_ui_services_v1 ui;
    int registered;
    int unregistered;
    int updated;
} mock_ui_host;

typedef struct mock_transport_state {
    onion_plugin_ipc_frame request;
    unsigned chunks;
    unsigned hellos;
    int hello_valid;
    int event_mode;
} mock_transport_state;

static int check(int condition, const char *message) {
    if (!condition) fprintf(stderr, "FAIL: %s\n", message);
    return condition;
}

static uint32_t read_u32(const unsigned char *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static uint16_t read_u16(const unsigned char *bytes) {
    return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
}

static void write_u32(unsigned char *bytes, uint32_t value) {
    bytes[0] = (unsigned char)value;
    bytes[1] = (unsigned char)(value >> 8);
    bytes[2] = (unsigned char)(value >> 16);
    bytes[3] = (unsigned char)(value >> 24);
}

static void write_u16(unsigned char *bytes, uint16_t value) {
    bytes[0] = (unsigned char)value;
    bytes[1] = (unsigned char)(value >> 8);
}

static void write_u64(unsigned char *bytes, uint64_t value) {
    for (unsigned i = 0; i < 8; ++i) bytes[i] = (unsigned char)(value >> (i * 8));
}

static size_t write_ui_event(unsigned char *bytes) {
    static const char contribution[] = "settings";
    static const char page[] = "main";
    static const char node_id[] = "mode";
    static const char value[] = "fast";
    write_u32(bytes, ONION_EVENT_UI_ACTION);
    write_u32(bytes + 4, ONION_UI_ABI_VERSION);
    write_u64(bytes + 8, 7);
    write_u64(bytes + 16, 99);
    write_u32(bytes + 24, ONION_UI_VALUE_STRING);
    write_u16(bytes + 28, (uint16_t)(sizeof(contribution) - 1));
    write_u16(bytes + 30, (uint16_t)(sizeof(page) - 1));
    write_u16(bytes + 32, (uint16_t)(sizeof(node_id) - 1));
    write_u16(bytes + 34, (uint16_t)(sizeof(value) - 1));
    size_t offset = ONION_PLUGIN_IPC_UI_EVENT_HEADER_SIZE;
    memcpy(bytes + offset, contribution, sizeof(contribution) - 1);
    offset += sizeof(contribution) - 1;
    memcpy(bytes + offset, page, sizeof(page) - 1);
    offset += sizeof(page) - 1;
    memcpy(bytes + offset, node_id, sizeof(node_id) - 1);
    offset += sizeof(node_id) - 1;
    memcpy(bytes + offset, value, sizeof(value) - 1);
    return offset + sizeof(value) - 1;
}

static onion_status transport_send(void *context, const void *data, size_t size) {
    mock_transport_state *state = (mock_transport_state *)context;
    if (size != sizeof(state->request)) return ONION_E_PROTOCOL;
    memcpy(&state->request, data, size);
    if (state->request.command == ONION_PLUGIN_IPC_UI_REGISTER_CHUNK)
        state->chunks++;
    if (state->request.command == ONION_PLUGIN_IPC_HELLO) {
        const unsigned char *payload = state->request.payload;
        const uint16_t id_size = read_u16(payload + 8);
        state->hellos++;
        state->hello_valid =
            state->request.payload_size == ONION_PLUGIN_IPC_HELLO_HEADER_SIZE + 9 &&
            read_u32(payload) == ONION_PLUGIN_ABI_VERSION &&
            read_u32(payload + 4) == (ONION_PLUGIN_CAP_IPC | ONION_PLUGIN_CAP_UI) &&
            id_size == 9 && read_u16(payload + 10) == 0 &&
            memcmp(payload + ONION_PLUGIN_IPC_HELLO_HEADER_SIZE,
                   "TEST00001", id_size) == 0;
    }
    return ONION_OK;
}

static onion_status transport_recv(void *context, void *data, size_t size) {
    mock_transport_state *state = (mock_transport_state *)context;
    if (size != sizeof(onion_plugin_ipc_frame)) return ONION_E_PROTOCOL;
    onion_plugin_ipc_frame *response = (onion_plugin_ipc_frame *)data;
    memset(response, 0, sizeof(*response));
    response->magic = ONION_PLUGIN_IPC_MAGIC;
    response->version = ONION_PLUGIN_IPC_VERSION;
    response->command = ONION_PLUGIN_IPC_RESPONSE;
    response->request_id = state->request.request_id;
    write_u32(response->payload, 0);
    if (state->request.command == ONION_PLUGIN_IPC_EVENT) {
        if (state->event_mode == 1) {
            write_u32(response->payload, (uint32_t)ONION_E_NOT_FOUND);
            write_u32(response->payload + 4, 0);
            response->payload_size = 8;
        } else {
            const size_t event_size = write_ui_event(response->payload + 8);
            if (state->event_mode == 2)
                response->payload[8] = 0;
            write_u32(response->payload + 4, (uint32_t)event_size);
            response->payload_size = (uint32_t)(8 + event_size);
        }
    } else if (state->request.command == ONION_PLUGIN_IPC_UI_REGISTER_BEGIN) {
        write_u32(response->payload + 4, 4);
        write_u32(response->payload + 8, 7);
        response->payload_size = 12;
    } else if (state->request.command == ONION_PLUGIN_IPC_UI_REGISTER_COMMIT) {
        write_u32(response->payload + 4, 8);
        write_u64(response->payload + 8, 99);
        response->payload_size = 16;
    } else {
        write_u32(response->payload + 4, 0);
        response->payload_size = 8;
    }
    return ONION_OK;
}

static onion_status mock_register(void *context, const void *document,
                                  size_t document_size,
                                  onion_ui_handle *out_handle) {
    mock_ui_host *host = (mock_ui_host *)context;
    if (!document || document_size < ONION_UI_DOCUMENT_HEADER_SIZE ||
        read_u32((const unsigned char *)document) != ONION_UI_DOCUMENT_MAGIC) {
        return ONION_E_PROTOCOL;
    }
    host->registered++;
    *out_handle = 42;
    return ONION_OK;
}

static onion_status mock_unregister(void *context, onion_ui_handle handle) {
    mock_ui_host *host = (mock_ui_host *)context;
    if (handle != 42) return ONION_E_NOT_FOUND;
    host->unregistered++;
    return ONION_OK;
}

static onion_status mock_set_value(void *context, onion_ui_handle handle,
                                   const char *node_id,
                                   onion_ui_value_type value_type,
                                   const char *value) {
    mock_ui_host *host = (mock_ui_host *)context;
    if (handle != 42 || strcmp(node_id, "enabled") != 0 ||
        value_type != ONION_UI_VALUE_BOOL || strcmp(value, "true") != 0) {
        return ONION_E_INVALID_ARGUMENT;
    }
    host->updated++;
    return ONION_OK;
}

static onion_status mock_query(void *context, const char *name,
                               uint32_t min_version, void *out_interface,
                               size_t interface_size) {
    mock_ui_host *host = (mock_ui_host *)context;
    if (strcmp(name, ONION_UI_SERVICE_NAME) != 0 ||
        min_version > ONION_UI_ABI_VERSION ||
        interface_size < sizeof(host->ui)) return ONION_E_NOT_SUPPORTED;
    memcpy(out_interface, &host->ui, sizeof(host->ui));
    return ONION_OK;
}

static int test_socket_connect(void) {
    char path[] = "/tmp/onion-sdk-XXXXXX";
    int placeholder = mkstemp(path);
    if (placeholder < 0) return check(0, "create temporary socket path");
    close(placeholder);
    unlink(path);

    int listener = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listener < 0) return check(0, "create Unix listener");
    struct sockaddr_un address;
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    memcpy(address.sun_path, path, strlen(path) + 1);
    socklen_t address_size = (socklen_t)(
        offsetof(struct sockaddr_un, sun_path) + strlen(path) + 1);
    if (bind(listener, (const struct sockaddr *)&address, address_size) != 0 ||
        listen(listener, 1) != 0) {
        close(listener);
        unlink(path);
        return check(0, "listen on temporary Unix socket");
    }

    onion_transport transport = {0};
    onion_socket_transport state = {0};
    onion_status status = onion_socket_transport_connect(
        &transport, &state, path);
    int accepted = status == ONION_OK ? accept(listener, NULL, NULL) : -1;
    int passed = check(status == ONION_OK && accepted >= 0 && state.owns_fd,
                       "connect owned socket transport");
    onion_socket_transport_deinit(&transport);
    if (accepted >= 0) close(accepted);
    close(listener);
    unlink(path);
    return passed;
}

static onion_ui_node_desc_v1 node(uint32_t kind, const char *id,
                                  const char *parent, const char *title) {
    onion_ui_node_desc_v1 result;
    memset(&result, 0, sizeof(result));
    result.struct_size = sizeof(result);
    result.abi_version = ONION_UI_ABI_VERSION;
    result.kind = kind;
    snprintf(result.id, sizeof(result.id), "%s", id);
    snprintf(result.parent_id, sizeof(result.parent_id), "%s", parent ? parent : "");
    snprintf(result.title, sizeof(result.title), "%s", title);
    return result;
}

int main(void) {
    if (!test_socket_connect()) return 1;
    if (!check(strcmp(ONION_PLUGIN_IPC_SOCKET_PATH,
                      "/system_tmp/hyperhen/ipc/plugin_service") == 0,
               "default plugin IPC path")) return 1;
    onion_ui_document_desc_v1 desc;
    memset(&desc, 0, sizeof(desc));
    desc.struct_size = sizeof(desc);
    desc.abi_version = ONION_UI_ABI_VERSION;
    snprintf(desc.plugin_id, sizeof(desc.plugin_id), "TEST00001");
    snprintf(desc.contribution_id, sizeof(desc.contribution_id), "settings");
    snprintf(desc.title, sizeof(desc.title), "Test settings");
    snprintf(desc.root_page_id, sizeof(desc.root_page_id), "main");

    onion_ui_document *document = NULL;
    if (!check(onion_ui_document_create(&desc, &document) == ONION_OK,
               "create document")) return 1;

    onion_ui_node_desc_v1 root = node(ONION_UI_NODE_PAGE, "main", NULL, "Main");
    onion_ui_node_desc_v1 advanced =
        node(ONION_UI_NODE_PAGE, "advanced", NULL, "Advanced");
    onion_ui_node_desc_v1 menu =
        node(ONION_UI_NODE_MENU, "advanced_menu", "main", "Advanced");
    snprintf(menu.target_id, sizeof(menu.target_id), "advanced");
    onion_ui_node_desc_v1 toggle =
        node(ONION_UI_NODE_TOGGLE, "enabled", "main", "Enabled");
    toggle.value_type = ONION_UI_VALUE_BOOL;
    toggle.binding = ONION_UI_BINDING_CONFIG;
    snprintf(toggle.binding_key, sizeof(toggle.binding_key), "enabled");
    snprintf(toggle.value, sizeof(toggle.value), "true");
    onion_ui_node_desc_v1 invalid_toggle = toggle;
    snprintf(invalid_toggle.id, sizeof(invalid_toggle.id), "invalid_toggle");
    snprintf(invalid_toggle.value, sizeof(invalid_toggle.value), "maybe");
    if (!check(onion_ui_document_add_node(document, &invalid_toggle) ==
                   ONION_E_INVALID_ARGUMENT,
               "reject invalid initial value")) return 1;
    onion_ui_node_desc_v1 list =
        node(ONION_UI_NODE_LIST, "mode", "advanced", "Mode");
    list.value_type = ONION_UI_VALUE_STRING;
    list.binding = ONION_UI_BINDING_EVENT;
    snprintf(list.binding_key, sizeof(list.binding_key), "mode_changed");
    snprintf(list.value, sizeof(list.value), "safe");
    onion_ui_node_desc_v1 item =
        node(ONION_UI_NODE_LIST_ITEM, "mode_safe", "mode", "Safe");
    item.value_type = ONION_UI_VALUE_STRING;
    snprintf(item.value, sizeof(item.value), "safe");

    if (!check(onion_ui_document_add_node(document, &root) == ONION_OK &&
               onion_ui_document_add_node(document, &advanced) == ONION_OK &&
               onion_ui_document_add_node(document, &menu) == ONION_OK &&
               onion_ui_document_add_node(document, &toggle) == ONION_OK &&
               onion_ui_document_add_node(document, &list) == ONION_OK &&
               onion_ui_document_add_node(document, &item) == ONION_OK,
               "add nodes")) return 1;
    if (!check(onion_ui_document_add_node(document, &toggle) ==
                   ONION_E_ALREADY_INITIALIZED,
               "reject duplicate id")) return 1;
    if (!check(onion_ui_document_validate(document) == ONION_OK &&
               onion_ui_document_node_count(document) == 6,
               "validate document")) return 1;

    size_t encoded_size = 0;
    if (!check(onion_ui_document_encoded_size(document, &encoded_size) == ONION_OK &&
               encoded_size > ONION_UI_DOCUMENT_HEADER_SIZE,
               "encoded size")) return 1;
    unsigned char *encoded = (unsigned char *)malloc(encoded_size);
    if (!check(encoded != NULL &&
               onion_ui_document_encode(document, encoded, encoded_size, NULL) == ONION_OK &&
               read_u32(encoded) == ONION_UI_DOCUMENT_MAGIC,
               "encode document")) return 1;
    free(encoded);

    mock_ui_host mock;
    memset(&mock, 0, sizeof(mock));
    mock.ui.struct_size = sizeof(mock.ui);
    mock.ui.abi_version = ONION_UI_ABI_VERSION;
    mock.ui.context = &mock;
    mock.ui.register_document = mock_register;
    mock.ui.unregister_document = mock_unregister;
    mock.ui.set_value = mock_set_value;
    onion_host_services_v1 services;
    memset(&services, 0, sizeof(services));
    services.struct_size = sizeof(services);
    services.abi_version = ONION_HOST_SERVICES_ABI_VERSION;
    services.context = &mock;
    services.query_interface = mock_query;

    onion_ui_handle handle = 0;
    if (!check(onion_ui_register(&services, document, &handle) == ONION_OK &&
               handle == 42 && mock.registered == 1,
               "register contribution")) return 1;
    if (!check(onion_ui_set_value(&services, handle, "enabled",
                                  ONION_UI_VALUE_BOOL, "true") == ONION_OK &&
               mock.updated == 1,
               "update value")) return 1;
    if (!check(onion_ui_unregister(&services, handle) == ONION_OK &&
               mock.unregistered == 1,
               "unregister contribution")) return 1;

    for (unsigned i = 0; i < 24; ++i) {
        char id[32];
        snprintf(id, sizeof(id), "label_%u", i);
        onion_ui_node_desc_v1 label =
            node(ONION_UI_NODE_LABEL, id, "advanced", "Additional status");
        memset(label.description, 'x', sizeof(label.description) - 1);
        label.description[sizeof(label.description) - 1] = '\0';
        if (!check(onion_ui_document_add_node(document, &label) == ONION_OK,
                   "grow document past one IPC chunk")) return 1;
    }

    mock_transport_state transport_state;
    memset(&transport_state, 0, sizeof(transport_state));
    onion_transport transport = {
        &transport_state, transport_send, transport_recv, NULL
    };
    onion_client client = ONION_CLIENT_INITIALIZER;
    onion_host_services_v1 client_services;
    if (!check(onion_client_init(&client, &transport) == ONION_OK &&
               onion_client_make_services(&client, &client_services) == ONION_OK,
               "create IPC-backed UI services")) return 1;
    onion_ui_services_v1 queried_ui;
    if (!check(onion_ui_get_services(&client_services, &queried_ui) ==
                   ONION_E_NOT_SUPPORTED,
               "hide UI services before HELLO")) return 1;
    onion_plugin_descriptor_v1 descriptor;
    memset(&descriptor, 0, sizeof(descriptor));
    descriptor.struct_size = sizeof(descriptor);
    descriptor.abi_version = ONION_PLUGIN_ABI_VERSION;
    descriptor.capabilities = ONION_PLUGIN_CAP_IPC | ONION_PLUGIN_CAP_UI;
    snprintf(descriptor.plugin_id, sizeof(descriptor.plugin_id), "TEST00001");
    if (!check(onion_client_open_session(&client, &descriptor) == ONION_OK &&
               transport_state.hellos == 1 && transport_state.hello_valid &&
               onion_ui_get_services(&client_services, &queried_ui) == ONION_OK,
               "open cooperative plugin session")) return 1;
    if (!check(onion_client_open_session(&client, &descriptor) ==
                   ONION_E_ALREADY_INITIALIZED &&
               transport_state.hellos == 1,
               "keep connection identity immutable")) return 1;
    onion_ui_handle client_handle = 0;
    if (!check(onion_ui_register(&client_services, document, &client_handle) == ONION_OK &&
               client_handle == 99 && transport_state.chunks > 1,
               "chunked UI registration")) return 1;
    if (!check(onion_ui_set_value(&client_services, client_handle, "enabled",
                                  ONION_UI_VALUE_BOOL, "false") == ONION_OK &&
               onion_ui_unregister(&client_services, client_handle) == ONION_OK,
               "IPC-backed UI update and unregister")) return 1;
    onion_ui_event_v1 ui_event;
    memset(&ui_event, 0xa5, sizeof(ui_event));
    if (!check(onion_client_poll_ui_event(&client, &ui_event) == ONION_OK &&
               ui_event.struct_size == sizeof(ui_event) &&
               ui_event.abi_version == ONION_UI_ABI_VERSION &&
               ui_event.sequence == 7 && ui_event.handle == 99 &&
               ui_event.value_type == ONION_UI_VALUE_STRING &&
               strcmp(ui_event.contribution_id, "settings") == 0 &&
               strcmp(ui_event.page_id, "main") == 0 &&
               strcmp(ui_event.node_id, "mode") == 0 &&
               strcmp(ui_event.value, "fast") == 0,
               "decode UI action event")) return 1;
    transport_state.event_mode = 1;
    if (!check(onion_client_poll_ui_event(&client, &ui_event) ==
                   ONION_E_NOT_FOUND,
               "empty UI event queue")) return 1;
    transport_state.event_mode = 2;
    if (!check(onion_client_poll_ui_event(&client, &ui_event) ==
                   ONION_E_PROTOCOL,
               "reject malformed UI action event")) return 1;
    onion_client_deinit(&client);

    onion_ui_document_destroy(document);
    puts("sdk_ui_test: PASS");
    return 0;
}
