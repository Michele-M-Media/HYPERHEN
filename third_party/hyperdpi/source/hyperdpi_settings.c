#include "hyperdpi_settings.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "hyperdpi_config.h"

void hyperdpi_settings_defaults(hyperdpi_settings *settings) {
    if (!settings) return;
    settings->enabled = 0;
    settings->api_port = HYPERDPI_API_PORT;
    settings->webui_port = HYPERDPI_WEBUI_PORT;
}

static void trim_line_end(char *value) {
    size_t end = strlen(value);
    while (end > 0 && (value[end - 1] == '\n' || value[end - 1] == '\r' ||
                       value[end - 1] == ' ' || value[end - 1] == '\t')) {
        value[--end] = '\0';
    }
}

static void parse_port(const char *value, uint16_t *out_port) {
    char *end = NULL;
    errno = 0;
    const long port = strtol(value, &end, 10);
    if (errno == 0 && end != value && *end == '\0' && port >= 1 &&
        port <= 65535) {
        *out_port = (uint16_t)port;
    }
}

int hyperdpi_settings_load(const char *path, hyperdpi_settings *settings) {
    if (!path || !settings) return -1;
    hyperdpi_settings_defaults(settings);

    FILE *file = fopen(path, "r");
    if (!file) return errno == ENOENT ? 0 : -1;

    char line[128];
    while (fgets(line, sizeof(line), file)) {
        trim_line_end(line);
        if (strncmp(line, "enabled=", 8) == 0) {
            const char *value = line + 8;
            if (strcmp(value, "true") == 0) settings->enabled = 1;
            if (strcmp(value, "false") == 0) settings->enabled = 0;
        } else if (strncmp(line, "api_port=", 9) == 0) {
            parse_port(line + 9, &settings->api_port);
        } else if (strncmp(line, "webui_port=", 11) == 0) {
            parse_port(line + 11, &settings->webui_port);
        }
    }
    const int result = fclose(file) == 0 ? 0 : -1;
    if (settings->api_port == settings->webui_port) {
        settings->api_port = HYPERDPI_API_PORT;
        settings->webui_port = HYPERDPI_WEBUI_PORT;
    }
    return result;
}

int hyperdpi_settings_save(const char *path,
                           const hyperdpi_settings *settings) {
    if (!path || !settings || settings->api_port == 0 ||
        settings->webui_port == 0 ||
        settings->api_port == settings->webui_port) {
        return -1;
    }

    char temporary_path[512];
    const int path_length = snprintf(
        temporary_path, sizeof(temporary_path), "%s.tmp", path);
    if (path_length < 0 || (size_t)path_length >= sizeof(temporary_path)) {
        return -1;
    }

    FILE *file = fopen(temporary_path, "w");
    if (!file) return -1;

    int result = fprintf(file,
                         "enabled=%s\napi_port=%u\nwebui_port=%u\n",
                         settings->enabled ? "true" : "false",
                         (unsigned)settings->api_port,
                         (unsigned)settings->webui_port) < 0
                     ? -1
                     : 0;
    if (result == 0 && fflush(file) != 0) result = -1;
    if (result == 0 && fsync(fileno(file)) != 0) result = -1;
    if (fclose(file) != 0) result = -1;
    if (result == 0 && rename(temporary_path, path) != 0) result = -1;
    if (result != 0) unlink(temporary_path);
    return result;
}
