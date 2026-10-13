#pragma once

#include <stddef.h>
#include <stdbool.h>

#include "onion/ipc.h"
#include "onion/status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef onion_status (*onion_transport_send_fn)(void *context,
                                                const void *data, size_t size);
typedef onion_status (*onion_transport_recv_fn)(void *context,
                                                void *data, size_t size);
typedef void (*onion_transport_close_fn)(void *context);

/* Dependency inversion point: IPC, shared memory, or a test double can implement this. */
typedef struct onion_transport {
    void *context;
    onion_transport_send_fn send;
    onion_transport_recv_fn recv;
    onion_transport_close_fn close;
} onion_transport;

onion_status onion_transport_send_frame(const onion_transport *transport,
                                        const onion_plugin_ipc_frame *frame);
onion_status onion_transport_recv_frame(const onion_transport *transport,
                                        onion_plugin_ipc_frame *frame);

/* Adapt an already-connected stream socket. The fd is borrowed unless owns_fd is true. */
typedef struct onion_socket_transport {
    int fd;
    bool owns_fd;
} onion_socket_transport;

onion_status onion_socket_transport_init(onion_transport *transport,
                                         onion_socket_transport *state,
                                         int fd, bool owns_fd);
/* Connect to a Unix stream socket and transfer ownership to the transport. */
onion_status onion_socket_transport_connect(onion_transport *transport,
                                            onion_socket_transport *state,
                                            const char *path);
void onion_socket_transport_deinit(onion_transport *transport);

#ifdef __cplusplus
}
#endif
