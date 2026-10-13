#pragma once

#include <pthread.h>
#include <stdint.h>

typedef struct hyperdpi_service {
    pthread_mutex_t mutex;
    pthread_t thread;
    uint16_t api_port;
    uint16_t webui_port;
    int initialized;
    int running;
    int thread_created;
} hyperdpi_service;

int hyperdpi_service_init(hyperdpi_service *service, uint16_t api_port,
                     uint16_t webui_port);
void hyperdpi_service_destroy(hyperdpi_service *service);
int hyperdpi_service_start(hyperdpi_service *service);
void hyperdpi_service_stop(hyperdpi_service *service);
int hyperdpi_service_restart(hyperdpi_service *service);
int hyperdpi_service_reconfigure(hyperdpi_service *service, uint16_t api_port,
                            uint16_t webui_port);
int hyperdpi_service_running(hyperdpi_service *service);
