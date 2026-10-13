#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ONION_PLUGIN_ABI_VERSION 1u
#define ONION_PLUGIN_ID_MAX 32u
#define ONION_PLUGIN_VERSION_MAX 16u
#define ONION_PLUGIN_NAME_MAX 64u

enum onion_plugin_capability {
    ONION_PLUGIN_CAP_NONE = 0,
    ONION_PLUGIN_CAP_NOTIFY = 1u << 0,
    ONION_PLUGIN_CAP_IPC = 1u << 1,
    ONION_PLUGIN_CAP_PROCESS = 1u << 2,
    ONION_PLUGIN_CAP_INJECT = 1u << 3,
    ONION_PLUGIN_CAP_KERNEL = 1u << 4,
    ONION_PLUGIN_CAP_UI = 1u << 5
};

enum onion_plugin_flags {
    ONION_PLUGIN_FLAG_NONE = 0,
    ONION_PLUGIN_FLAG_AUTO_START = 1u << 0,
    ONION_PLUGIN_FLAG_LONG_RUNNING = 1u << 1,
    ONION_PLUGIN_FLAG_STOP_SUPPORTED = 1u << 2
};

/* This descriptor is an ELF section marker, not a host function table. */
typedef struct onion_plugin_descriptor_v1 {
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t capabilities;
    uint32_t flags;
    char plugin_id[ONION_PLUGIN_ID_MAX];
    char version[ONION_PLUGIN_VERSION_MAX];
    char name[ONION_PLUGIN_NAME_MAX];
} onion_plugin_descriptor_v1;

#if defined(__ELF__)
#define ONION_PLUGIN_SECTION __attribute__((used, section(".onion_plugin"), aligned(8)))
#else
#define ONION_PLUGIN_SECTION
#endif

#define ONION_PLUGIN_DEFINE(ID, VERSION, NAME, CAPS, FLAGS) \
    const onion_plugin_descriptor_v1 onion_plugin_descriptor \
        ONION_PLUGIN_SECTION = { \
            sizeof(onion_plugin_descriptor_v1), ONION_PLUGIN_ABI_VERSION, \
            (CAPS), (FLAGS), (ID), (VERSION), (NAME) }

#ifdef __cplusplus
}
#endif
