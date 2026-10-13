#include <onion/client.h>
#include <onion/event_bus.h>
#include <onion/ipc.h>
#include <onion/plugin.h>
#include <onion/status.h>
#include <onion/transport.h>
#include <onion/ui.h>

#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

ONION_PLUGIN_DEFINE(
    "ONIO00003", "1.00", "Dynamic UI smoke test",
    ONION_PLUGIN_CAP_IPC | ONION_PLUGIN_CAP_UI,
    ONION_PLUGIN_FLAG_AUTO_START | ONION_PLUGIN_FLAG_LONG_RUNNING |
        ONION_PLUGIN_FLAG_STOP_SUPPORTED);

static volatile sig_atomic_t running = 1;
static FILE *log_file;

static void log_message(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    fflush(stdout);
    if (!log_file) return;
    va_start(args, format);
    vfprintf(log_file, format, args);
    va_end(args);
    fflush(log_file);
}

static void request_stop(int signal_number) {
    (void)signal_number;
    running = 0;
}

static onion_ui_node_desc_v1 make_node(uint32_t kind, const char *id,
                                       const char *parent_id,
                                       const char *title) {
    onion_ui_node_desc_v1 node;
    memset(&node, 0, sizeof(node));
    node.struct_size = sizeof(node);
    node.abi_version = ONION_UI_ABI_VERSION;
    node.kind = kind;
    snprintf(node.id, sizeof(node.id), "%s", id);
    snprintf(node.parent_id, sizeof(node.parent_id), "%s",
             parent_id ? parent_id : "");
    snprintf(node.title, sizeof(node.title), "%s", title);
    return node;
}

static onion_status add_nodes(onion_ui_document *document) {
    onion_ui_node_desc_v1 node =
        make_node(ONION_UI_NODE_PAGE, "main", NULL, "Dynamic UI Test");
    onion_status status = onion_ui_document_add_node(document, &node);
    if (status != ONION_OK) return status;

    node = make_node(ONION_UI_NODE_PAGE, "controls", NULL, "Controls");
    status = onion_ui_document_add_node(document, &node);
    if (status != ONION_OK) return status;

    node = make_node(ONION_UI_NODE_LABEL, "connection", "main",
                     "Plugin connected");
    snprintf(node.description, sizeof(node.description),
             "ONIO00003 is receiving UI action events");
    status = onion_ui_document_add_node(document, &node);
    if (status != ONION_OK) return status;

    node = make_node(ONION_UI_NODE_MENU, "open_controls", "main", "Controls");
    snprintf(node.description, sizeof(node.description),
             "Open the interactive smoke-test controls");
    snprintf(node.target_id, sizeof(node.target_id), "controls");
    status = onion_ui_document_add_node(document, &node);
    if (status != ONION_OK) return status;

    node = make_node(ONION_UI_NODE_GROUP, "interactive", "controls",
                     "Interactive controls");
    status = onion_ui_document_add_node(document, &node);
    if (status != ONION_OK) return status;

    node = make_node(ONION_UI_NODE_TOGGLE, "enabled", "interactive", "Enabled");
    node.value_type = ONION_UI_VALUE_BOOL;
    node.binding = ONION_UI_BINDING_EVENT;
    snprintf(node.binding_key, sizeof(node.binding_key), "enabled_changed");
    snprintf(node.value, sizeof(node.value), "true");
    status = onion_ui_document_add_node(document, &node);
    if (status != ONION_OK) return status;

    node = make_node(ONION_UI_NODE_LIST, "mode", "interactive", "Mode");
    node.value_type = ONION_UI_VALUE_STRING;
    node.binding = ONION_UI_BINDING_EVENT;
    snprintf(node.binding_key, sizeof(node.binding_key), "mode_changed");
    snprintf(node.value, sizeof(node.value), "safe");
    status = onion_ui_document_add_node(document, &node);
    if (status != ONION_OK) return status;

    node = make_node(ONION_UI_NODE_LIST_ITEM, "mode_safe", "mode", "Safe");
    node.value_type = ONION_UI_VALUE_STRING;
    snprintf(node.value, sizeof(node.value), "safe");
    status = onion_ui_document_add_node(document, &node);
    if (status != ONION_OK) return status;

    node = make_node(ONION_UI_NODE_LIST_ITEM, "mode_fast", "mode", "Fast");
    node.value_type = ONION_UI_VALUE_STRING;
    snprintf(node.value, sizeof(node.value), "fast");
    status = onion_ui_document_add_node(document, &node);
    if (status != ONION_OK) return status;

    node = make_node(ONION_UI_NODE_INPUT, "port", "interactive", "Port");
    node.value_type = ONION_UI_VALUE_INT;
    node.binding = ONION_UI_BINDING_EVENT;
    node.min_value = 1;
    node.max_value = 65535;
    node.min_length = 1;
    node.max_length = 5;
    snprintf(node.binding_key, sizeof(node.binding_key), "port_changed");
    snprintf(node.value, sizeof(node.value), "1337");
    status = onion_ui_document_add_node(document, &node);
    if (status != ONION_OK) return status;

    node = make_node(ONION_UI_NODE_ACTION, "run_action", "interactive",
                     "Run test action");
    node.flags = ONION_UI_NODE_FLAG_CONFIRM;
    node.binding = ONION_UI_BINDING_EVENT;
    snprintf(node.binding_key, sizeof(node.binding_key), "run_action");
    return onion_ui_document_add_node(document, &node);
}

static onion_status make_document(onion_ui_document **out_document) {
    onion_ui_document_desc_v1 desc;
    memset(&desc, 0, sizeof(desc));
    desc.struct_size = sizeof(desc);
    desc.abi_version = ONION_UI_ABI_VERSION;
    desc.priority = 100;
    snprintf(desc.plugin_id, sizeof(desc.plugin_id), "ONIO00003");
    snprintf(desc.contribution_id, sizeof(desc.contribution_id), "smoke_test");
    snprintf(desc.title, sizeof(desc.title), "Dynamic UI Smoke Test");
    snprintf(desc.description, sizeof(desc.description),
             "HyperHEN SDK dynamic UI validation");
    snprintf(desc.root_page_id, sizeof(desc.root_page_id), "main");

    onion_status status = onion_ui_document_create(&desc, out_document);
    if (status != ONION_OK) return status;
    status = add_nodes(*out_document);
    if (status == ONION_OK) status = onion_ui_document_validate(*out_document);
    if (status != ONION_OK) {
        onion_ui_document_destroy(*out_document);
        *out_document = NULL;
    }
    return status;
}

static void on_ui_action(onion_event_id event_id, const void *data,
                         size_t data_size, void *user_data) {
    (void)user_data;
    if (event_id != ONION_EVENT_UI_ACTION || !data ||
        data_size != sizeof(onion_ui_event_v1)) {
        return;
    }
    const onion_ui_event_v1 *event = (const onion_ui_event_v1 *)data;
    log_message(
        "[dynamic-ui] seq=%llu handle=%llu contribution=%s page=%s node=%s "
        "type=%u value=%s\n",
        (unsigned long long)event->sequence,
        (unsigned long long)event->handle, event->contribution_id,
        event->page_id, event->node_id, event->value_type, event->value);
}

int main(void) {
    onion_transport transport = {0};
    onion_socket_transport socket_state = {0};
    onion_client client = ONION_CLIENT_INITIALIZER;
    onion_host_services_v1 host_services;
    onion_ui_document *document = NULL;
    onion_ui_handle handle = 0;
    onion_event_bus *event_bus = NULL;
    onion_event_subscription subscription = 0;
    int exit_code = 1;

    log_file = fopen("/data/HyperHEN/dynamic_ui_smoke.log", "a");
    log_message("[dynamic-ui] starting ONIO00003\n");
    signal(SIGINT, request_stop);
    signal(SIGTERM, request_stop);

    onion_status status = onion_socket_transport_connect(
        &transport, &socket_state, ONION_PLUGIN_IPC_SOCKET_PATH);
    if (status == ONION_OK) status = onion_client_init(&client, &transport);
    if (status == ONION_OK)
        status = onion_client_open_session(&client, &onion_plugin_descriptor);
    if (status == ONION_OK)
        status = onion_client_make_services(&client, &host_services);
    if (status == ONION_OK) status = make_document(&document);
    if (status == ONION_OK)
        status = onion_ui_register(&host_services, document, &handle);
    if (status == ONION_OK) status = onion_event_bus_create(&event_bus);
    if (status == ONION_OK)
        status = onion_event_subscribe(event_bus, ONION_EVENT_UI_ACTION,
                                       on_ui_action, NULL, &subscription);
    if (status != ONION_OK) {
        log_message("[dynamic-ui] startup failed: %s\n",
                    onion_status_string(status));
        goto cleanup;
    }

    log_message("[dynamic-ui] registered handle=%llu\n",
                (unsigned long long)handle);
    exit_code = 0;
    while (running) {
        onion_ui_event_v1 event;
        status = onion_client_poll_ui_event(&client, &event);
        if (status == ONION_OK) {
            status = onion_event_publish(event_bus, ONION_EVENT_UI_ACTION,
                                         &event, sizeof(event));
            if (status != ONION_OK) break;
            continue;
        }
        if (status == ONION_E_NOT_FOUND) {
            usleep(100 * 1000);
            continue;
        }
        log_message("[dynamic-ui] event poll failed: %s\n",
                    onion_status_string(status));
        exit_code = 1;
        break;
    }

cleanup:
    if (subscription != 0)
        (void)onion_event_unsubscribe(event_bus, subscription);
    if (event_bus) onion_event_bus_destroy(event_bus);
    if (handle != 0)
        (void)onion_ui_unregister(&host_services, handle);
    onion_ui_document_destroy(document);
    onion_client_deinit(&client);
    onion_socket_transport_deinit(&transport);
    log_message("[dynamic-ui] stopped exit_code=%d\n", exit_code);
    if (log_file) fclose(log_file);
    return exit_code;
}
