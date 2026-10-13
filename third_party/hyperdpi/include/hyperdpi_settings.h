#pragma once

#include <stdint.h>

typedef struct hyperdpi_settings {
    int enabled;
    uint16_t api_port;
    uint16_t webui_port;
} hyperdpi_settings;

void hyperdpi_settings_defaults(hyperdpi_settings *settings);
int hyperdpi_settings_load(const char *path, hyperdpi_settings *settings);
int hyperdpi_settings_save(const char *path,
                           const hyperdpi_settings *settings);
