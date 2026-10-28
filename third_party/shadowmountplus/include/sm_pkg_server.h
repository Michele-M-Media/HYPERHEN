#ifndef SM_PKG_SERVER_H
#define SM_PKG_SERVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "sm_pkg.h"
#include "sm_pkg_stream.h"

typedef struct sm_pkg_server sm_pkg_server_t;

int sm_pkg_server_start(sm_pkg_server_t **out, const char *pkg_path,
                        const char *attempt, uint16_t *port_out);
int sm_pkg_server_start_reader(sm_pkg_server_t **out, const char *name, uint64_t size,
    sm_pkg_read_fn read, void *ctx, const char *attempt, uint16_t *port_out);
int sm_pkg_server_start_browser(sm_pkg_server_t **out, const char *name, uint64_t size,
    sm_pkg_stream_t *stream, const char *bind_address, const char *attempt, uint16_t *port_out);
uint64_t sm_pkg_server_bytes_served(sm_pkg_server_t *server);
const char *sm_pkg_server_host(sm_pkg_server_t *server);
uint64_t sm_pkg_server_total(sm_pkg_server_t *server);
bool sm_pkg_server_complete(sm_pkg_server_t *server);
uint64_t sm_pkg_server_last_request_us(sm_pkg_server_t *server);
void sm_pkg_server_stop(sm_pkg_server_t *server);
bool sm_pkg_server_source_matches(sm_pkg_server_t *server, const sm_pkg_info_t *info);

#endif
