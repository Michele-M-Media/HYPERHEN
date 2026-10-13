#include <onion/client.h>
#include <onion/plugin.h>
#include <onion/status.h>
#include <onion/transport.h>
#include <onion/ui.h>

#include <ps5/kernel.h>

#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "hyperdpi_i18n.h"
#include "hyperdpi_service.h"
#include "hyperdpi_config.h"
#include "hyperdpi_settings.h"
#include "hyperdpi_ui.h"

#define CONNECT_ATTEMPTS 30
#define CONNECT_RETRY_US (250 * 1000)
#define EVENT_POLL_US (100 * 1000)
#define HYPERDPI_AUTH_ID UINT64_C(0x4801000000000013)

extern const onion_plugin_descriptor_v1 onion_plugin_descriptor;

typedef struct hyperdpi_app {
    onion_transport transport;
    onion_socket_transport socket;
    onion_client client;
    onion_host_services_v1 services;
    onion_ui_document *document;
    onion_ui_handle ui_handle;
    hyperdpi_service installer;
    hyperdpi_settings settings;
    FILE *log_file;
    int transport_connected;
    int client_initialized;
    int installer_initialized;
} hyperdpi_app;

static volatile sig_atomic_t running = 1;

static void request_stop(int signal_number) {
    (void)signal_number;
    running = 0;
}

static void log_message(hyperdpi_app *app, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    vprintf(format, arguments);
    va_end(arguments);
    fflush(stdout);

    if (!app->log_file) return;
    va_start(arguments, format);
    vfprintf(app->log_file, format, arguments);
    va_end(arguments);
    fflush(app->log_file);
}

static onion_status connect_to_daemon(hyperdpi_app *app) {
    for (int attempt = 1; attempt <= CONNECT_ATTEMPTS && running; ++attempt) {
        onion_status status = onion_socket_transport_connect(
            &app->transport, &app->socket, ONION_PLUGIN_IPC_SOCKET_PATH);
        if (status == ONION_OK) {
            app->transport_connected = 1;
            return ONION_OK;
        }
        if (attempt < CONNECT_ATTEMPTS) usleep(CONNECT_RETRY_US);
    }
    return ONION_E_IO;
}

static void save_settings(hyperdpi_app *app) {
    if (hyperdpi_settings_save(HYPERDPI_SETTINGS_PATH, &app->settings) != 0) {
        log_message(app, "[%s] failed to save %s\n", HYPERDPI_PLUGIN_ID,
                    HYPERDPI_SETTINGS_PATH);
    }
}

static onion_status start_plugin(hyperdpi_app *app) {
    if (kernel_set_ucred_authid(getpid(), HYPERDPI_AUTH_ID) != 0) {
        return ONION_E_PERMISSION;
    }

    onion_status status = connect_to_daemon(app);
    if (status != ONION_OK) return status;
    status = onion_client_init(&app->client, &app->transport);
    if (status != ONION_OK) return status;
    app->client_initialized = 1;

    status = onion_client_open_session(&app->client, &onion_plugin_descriptor);
    if (status == ONION_OK) {
        status = onion_client_make_services(&app->client, &app->services);
    }
    if (status != ONION_OK) return status;

    hyperdpi_i18n_initialize();
    if (!hyperdpi_service_init(&app->installer, app->settings.api_port,
                               app->settings.webui_port)) {
        return ONION_E_IO;
    }
    app->installer_initialized = 1;
    if (app->settings.enabled && !hyperdpi_service_start(&app->installer)) {
        app->settings.enabled = 0;
        save_settings(app);
        log_message(app, "[%s] failed to start HyperDPI on TCP %u/%u\n",
                    HYPERDPI_PLUGIN_ID, (unsigned)app->settings.api_port,
                    (unsigned)app->settings.webui_port);
    }

    status = hyperdpi_ui_create(&app->settings, &app->document);
    if (status == ONION_OK) {
        status = onion_ui_register(
            &app->services, app->document, &app->ui_handle);
    }
    return status;
}

static void stop_plugin(hyperdpi_app *app) {
    if (app->ui_handle != 0) {
        (void)onion_ui_unregister(&app->services, app->ui_handle);
        app->ui_handle = 0;
    }
    onion_ui_document_destroy(app->document);
    app->document = NULL;
    if (app->installer_initialized) {
        hyperdpi_service_destroy(&app->installer);
        app->installer_initialized = 0;
    }
    if (app->client_initialized) {
        onion_client_deinit(&app->client);
        app->client_initialized = 0;
    }
    if (app->transport_connected) {
        onion_socket_transport_deinit(&app->transport);
        app->transport_connected = 0;
    }
}

static onion_status apply_action(hyperdpi_app *app,
                                 const hyperdpi_ui_action *action) {
    switch (action->kind) {
    case HYPERDPI_UI_ACTION_SET_ENABLED:
        if (action->enabled == app->settings.enabled) return ONION_OK;
        if (action->enabled) {
            hyperdpi_i18n_initialize();
            if (!hyperdpi_service_start(&app->installer)) {
                (void)hyperdpi_ui_set_enabled(
                    &app->services, app->ui_handle, 0);
                return ONION_E_IO;
            }
        } else {
            hyperdpi_service_stop(&app->installer);
        }
        app->settings.enabled = action->enabled;
        save_settings(app);
        return ONION_OK;

    case HYPERDPI_UI_ACTION_SET_API_PORT:
    case HYPERDPI_UI_ACTION_SET_WEBUI_PORT: {
        const uint16_t previous_api_port = app->settings.api_port;
        const uint16_t previous_webui_port = app->settings.webui_port;
        const uint16_t api_port =
            action->kind == HYPERDPI_UI_ACTION_SET_API_PORT
                ? action->port
                : previous_api_port;
        const uint16_t webui_port =
            action->kind == HYPERDPI_UI_ACTION_SET_WEBUI_PORT
                ? action->port
                : previous_webui_port;
        const char *node_id = action->kind == HYPERDPI_UI_ACTION_SET_API_PORT
                                  ? "api_port"
                                  : "webui_port";
        const uint16_t previous_value =
            action->kind == HYPERDPI_UI_ACTION_SET_API_PORT
                ? previous_api_port
                : previous_webui_port;
        if (api_port == webui_port ||
            !hyperdpi_service_reconfigure(&app->installer, api_port,
                                          webui_port)) {
            (void)hyperdpi_ui_set_port(&app->services, app->ui_handle, node_id,
                                       previous_value);
            return ONION_E_IO;
        }
        app->settings.api_port = api_port;
        app->settings.webui_port = webui_port;
        save_settings(app);
        return ONION_OK;
    }

    case HYPERDPI_UI_ACTION_RESTART:
        hyperdpi_i18n_initialize();
        if (!hyperdpi_service_restart(&app->installer)) {
            app->settings.enabled = 0;
            (void)hyperdpi_ui_set_enabled(&app->services, app->ui_handle, 0);
            save_settings(app);
            return ONION_E_IO;
        }
        app->settings.enabled = 1;
        save_settings(app);
        return hyperdpi_ui_set_enabled(&app->services, app->ui_handle, 1);

    case HYPERDPI_UI_ACTION_NONE:
        return ONION_E_NOT_FOUND;
    }
    return ONION_E_NOT_FOUND;
}

static int run_event_loop(hyperdpi_app *app) {
    unsigned int language_refresh_ticks = 0;
    while (running) {
        if (++language_refresh_ticks >= 50) {
            hyperdpi_i18n_refresh();
            language_refresh_ticks = 0;
        }
        if (app->settings.enabled && !hyperdpi_service_running(&app->installer)) {
            app->settings.enabled = 0;
            (void)hyperdpi_ui_set_enabled(&app->services, app->ui_handle, 0);
            save_settings(app);
            log_message(app, "[%s] installer service stopped\n",
                        HYPERDPI_PLUGIN_ID);
        }

        onion_ui_event_v1 event;
        onion_status status = onion_client_poll_ui_event(&app->client, &event);
        if (status == ONION_E_NOT_FOUND) {
            usleep(EVENT_POLL_US);
            continue;
        }
        if (status != ONION_OK) {
            log_message(app, "[%s] UI event poll failed: %s\n",
                        HYPERDPI_PLUGIN_ID, onion_status_string(status));
            return 1;
        }

        hyperdpi_ui_action action;
        status = hyperdpi_ui_decode_action(app->ui_handle, &event, &action);
        if (status == ONION_OK) status = apply_action(app, &action);
        if (status != ONION_OK && status != ONION_E_NOT_FOUND) {
            log_message(app, "[%s] action %s failed: %s\n",
                        HYPERDPI_PLUGIN_ID, event.node_id,
                        onion_status_string(status));
        }
    }
    return 0;
}

int main(void) {
    hyperdpi_app app = {0};
    hyperdpi_settings_defaults(&app.settings);
    if (hyperdpi_settings_load(HYPERDPI_SETTINGS_PATH, &app.settings) != 0) {
        fprintf(stderr, "[%s] failed to read %s; using defaults\n",
                HYPERDPI_PLUGIN_ID, HYPERDPI_SETTINGS_PATH);
    }
    app.log_file = fopen(HYPERDPI_LOG_PATH, "a");

    signal(SIGINT, request_stop);
    signal(SIGTERM, request_stop);
    signal(SIGPIPE, SIG_IGN);
    (void)syscall(SYS_thr_set_name, -1, "hyperdpi.elf");
    log_message(&app, "[%s] starting %s %s\n", HYPERDPI_PLUGIN_ID,
                HYPERDPI_PLUGIN_NAME, HYPERDPI_PLUGIN_VERSION);

    const onion_status status = start_plugin(&app);
    int exit_code = 1;
    if (status == ONION_OK) {
        log_message(&app,
                    "[%s] ready; API=%u WebUI=%u language=%s handle=%llu\n",
                    HYPERDPI_PLUGIN_ID, (unsigned)app.settings.api_port,
                    (unsigned)app.settings.webui_port,
                    hyperdpi_i18n_language_code(),
                    (unsigned long long)app.ui_handle);
        exit_code = run_event_loop(&app);
    } else {
        log_message(&app, "[%s] startup failed: %s\n", HYPERDPI_PLUGIN_ID,
                    onion_status_string(status));
    }

    stop_plugin(&app);
    log_message(&app, "[%s] stopped with exit_code=%d\n", HYPERDPI_PLUGIN_ID,
                exit_code);
    if (app.log_file) fclose(app.log_file);
    return exit_code;
}
