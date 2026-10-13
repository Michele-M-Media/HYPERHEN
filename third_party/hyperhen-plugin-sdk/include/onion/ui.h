#pragma once

#include <stddef.h>
#include <stdint.h>

#include "onion/plugin.h"
#include "onion/services.h"
#include "onion/status.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ONION_UI_ABI_VERSION 1u
#define ONION_UI_SERVICE_NAME "onion.ui"

#define ONION_UI_CONTRIBUTION_ID_MAX 64u
#define ONION_UI_NODE_ID_MAX 64u
#define ONION_UI_TITLE_MAX 128u
#define ONION_UI_DESCRIPTION_MAX 256u
#define ONION_UI_VALUE_MAX 256u
#define ONION_UI_BINDING_KEY_MAX 64u
#define ONION_UI_MAX_NODES 256u
#define ONION_UI_MAX_DEPTH 8u

typedef uint64_t onion_ui_handle;

typedef enum onion_ui_node_kind {
    ONION_UI_NODE_PAGE = 1,
    ONION_UI_NODE_MENU = 2,
    ONION_UI_NODE_GROUP = 3,
    ONION_UI_NODE_LABEL = 4,
    ONION_UI_NODE_ACTION = 5,
    ONION_UI_NODE_TOGGLE = 6,
    ONION_UI_NODE_LIST = 7,
    ONION_UI_NODE_LIST_ITEM = 8,
    ONION_UI_NODE_INPUT = 9
} onion_ui_node_kind;

typedef enum onion_ui_value_type {
    ONION_UI_VALUE_NONE = 0,
    ONION_UI_VALUE_BOOL = 1,
    ONION_UI_VALUE_INT = 2,
    ONION_UI_VALUE_STRING = 3
} onion_ui_value_type;

typedef enum onion_ui_binding_kind {
    ONION_UI_BINDING_NONE = 0,
    ONION_UI_BINDING_CONFIG = 1,
    ONION_UI_BINDING_EVENT = 2
} onion_ui_binding_kind;

enum onion_ui_node_flags {
    ONION_UI_NODE_FLAG_NONE = 0,
    ONION_UI_NODE_FLAG_DISABLED = 1u << 0,
    ONION_UI_NODE_FLAG_CONFIRM = 1u << 1,
    ONION_UI_NODE_FLAG_SECRET = 1u << 2
};

typedef struct onion_ui_document_desc_v1 {
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t flags;
    int32_t priority;
    char plugin_id[ONION_PLUGIN_ID_MAX];
    char contribution_id[ONION_UI_CONTRIBUTION_ID_MAX];
    char title[ONION_UI_TITLE_MAX];
    char description[ONION_UI_DESCRIPTION_MAX];
    char root_page_id[ONION_UI_NODE_ID_MAX];
} onion_ui_document_desc_v1;

typedef struct onion_ui_node_desc_v1 {
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t kind;
    uint32_t flags;
    uint32_t value_type;
    uint32_t binding;
    int64_t min_value;
    int64_t max_value;
    uint32_t min_length;
    uint32_t max_length;
    char id[ONION_UI_NODE_ID_MAX];
    char parent_id[ONION_UI_NODE_ID_MAX];
    char title[ONION_UI_TITLE_MAX];
    char description[ONION_UI_DESCRIPTION_MAX];
    char target_id[ONION_UI_NODE_ID_MAX];
    char binding_key[ONION_UI_BINDING_KEY_MAX];
    char value[ONION_UI_VALUE_MAX];
} onion_ui_node_desc_v1;

typedef struct onion_ui_event_v1 {
    uint32_t struct_size;
    uint32_t abi_version;
    uint64_t sequence;
    onion_ui_handle handle;
    uint32_t value_type;
    char contribution_id[ONION_UI_CONTRIBUTION_ID_MAX];
    char page_id[ONION_UI_NODE_ID_MAX];
    char node_id[ONION_UI_NODE_ID_MAX];
    char value[ONION_UI_VALUE_MAX];
} onion_ui_event_v1;

typedef struct onion_ui_document onion_ui_document;

onion_status onion_ui_document_create(
    const onion_ui_document_desc_v1 *desc, onion_ui_document **out_document);
void onion_ui_document_destroy(onion_ui_document *document);
onion_status onion_ui_document_add_node(
    onion_ui_document *document, const onion_ui_node_desc_v1 *node);
onion_status onion_ui_document_validate(const onion_ui_document *document);
size_t onion_ui_document_node_count(const onion_ui_document *document);
onion_status onion_ui_document_encoded_size(
    const onion_ui_document *document, size_t *out_size);
onion_status onion_ui_document_encode(
    const onion_ui_document *document, void *buffer, size_t buffer_size,
    size_t *out_size);

typedef onion_status (*onion_ui_register_fn)(
    void *context, const void *document, size_t document_size,
    onion_ui_handle *out_handle);
typedef onion_status (*onion_ui_unregister_fn)(
    void *context, onion_ui_handle handle);
typedef onion_status (*onion_ui_set_value_fn)(
    void *context, onion_ui_handle handle, const char *node_id,
    onion_ui_value_type value_type, const char *value);

typedef struct onion_ui_services_v1 {
    uint32_t struct_size;
    uint32_t abi_version;
    void *context;
    onion_ui_register_fn register_document;
    onion_ui_unregister_fn unregister_document;
    onion_ui_set_value_fn set_value;
} onion_ui_services_v1;

onion_status onion_ui_get_services(
    const onion_host_services_v1 *host, onion_ui_services_v1 *out_services);
onion_status onion_ui_register(
    const onion_host_services_v1 *host, const onion_ui_document *document,
    onion_ui_handle *out_handle);
onion_status onion_ui_unregister(
    const onion_host_services_v1 *host, onion_ui_handle handle);
onion_status onion_ui_set_value(
    const onion_host_services_v1 *host, onion_ui_handle handle,
    const char *node_id, onion_ui_value_type value_type, const char *value);

#ifdef __cplusplus
}
#endif
