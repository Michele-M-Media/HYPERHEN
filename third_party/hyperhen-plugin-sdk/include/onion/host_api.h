#pragma once

#include <stdint.h>

#include "onion/ipc.h"
#include "onion/status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum onion_log_level {
    ONION_LOG_DEBUG = 0,
    ONION_LOG_INFO = 1,
    ONION_LOG_WARN = 2,
    ONION_LOG_ERROR = 3
} onion_log_level;

/* Host calls are transported over IPC for standalone plugin processes. */
typedef struct onion_plugin_host_context_v1 {
    uint32_t struct_size;
    uint32_t abi_version;
    int control_fd;
    const char *plugin_id;
} onion_plugin_host_context_v1;

onion_status onion_plugin_ipc_send(int fd, onion_plugin_ipc_frame *frame);
onion_status onion_plugin_ipc_recv(int fd, onion_plugin_ipc_frame *frame);
onion_status onion_plugin_log(int fd, onion_log_level level, const char *message);
onion_status onion_plugin_notify(int fd, const char *message);

#ifdef __cplusplus
}
#endif
