#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum onion_status {
    ONION_OK = 0,
    ONION_E_INVALID_ARGUMENT = -1,
    ONION_E_INVALID_STATE = -2,
    ONION_E_NOT_INITIALIZED = -3,
    ONION_E_ALREADY_INITIALIZED = -4,
    ONION_E_NOT_SUPPORTED = -5,
    ONION_E_NO_MEMORY = -6,
    ONION_E_IO = -7,
    ONION_E_PROTOCOL = -8,
    ONION_E_TIMEOUT = -9,
    ONION_E_PERMISSION = -10,
    ONION_E_NOT_FOUND = -11,
    ONION_E_BUSY = -12
} onion_status;

const char *onion_status_string(onion_status status);

#ifdef __cplusplus
}
#endif

