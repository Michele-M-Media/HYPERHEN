#include "onion/status.h"

const char *onion_status_string(onion_status status) {
    switch (status) {
    case ONION_OK: return "ok";
    case ONION_E_INVALID_ARGUMENT: return "invalid argument";
    case ONION_E_INVALID_STATE: return "invalid state";
    case ONION_E_NOT_INITIALIZED: return "not initialized";
    case ONION_E_ALREADY_INITIALIZED: return "already initialized";
    case ONION_E_NOT_SUPPORTED: return "not supported";
    case ONION_E_NO_MEMORY: return "out of memory";
    case ONION_E_IO: return "io error";
    case ONION_E_PROTOCOL: return "protocol error";
    case ONION_E_TIMEOUT: return "timeout";
    case ONION_E_PERMISSION: return "permission denied";
    case ONION_E_NOT_FOUND: return "not found";
    case ONION_E_BUSY: return "busy";
    default: return "unknown error";
    }
}

