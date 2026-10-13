#include "onion/ui.h"
#include "onion/ui_protocol.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

struct onion_ui_document {
    onion_ui_document_desc_v1 desc;
    onion_ui_node_desc_v1 *nodes;
    size_t node_count;
    size_t node_capacity;
};

static int bounded_length(const char *text, size_t capacity, size_t *out_length) {
    const char *end = text ? (const char *)memchr(text, '\0', capacity) : NULL;
    if (!end) return 0;
    if (out_length) *out_length = (size_t)(end - text);
    return 1;
}

static int valid_id(const char *id, size_t capacity) {
    size_t length = 0;
    if (!bounded_length(id, capacity, &length) || length == 0) return 0;
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)id[i];
        if (!(isalnum(c) || c == '_' || c == '-' || c == '.')) return 0;
    }
    return 1;
}

static const onion_ui_node_desc_v1 *find_node(const onion_ui_document *document,
                                               const char *id) {
    if (!document || !id) return NULL;
    for (size_t i = 0; i < document->node_count; ++i) {
        if (strcmp(document->nodes[i].id, id) == 0) return &document->nodes[i];
    }
    return NULL;
}

static int parent_accepts(uint32_t parent_kind, uint32_t child_kind) {
    if (child_kind == ONION_UI_NODE_LIST_ITEM) return parent_kind == ONION_UI_NODE_LIST;
    if (child_kind == ONION_UI_NODE_PAGE) return 0;
    return parent_kind == ONION_UI_NODE_PAGE || parent_kind == ONION_UI_NODE_GROUP;
}

static int kind_valid(uint32_t kind) {
    return kind >= ONION_UI_NODE_PAGE && kind <= ONION_UI_NODE_INPUT;
}

static onion_status validate_node_strings(const onion_ui_node_desc_v1 *node) {
    if (!valid_id(node->id, sizeof(node->id)) ||
        !bounded_length(node->parent_id, sizeof(node->parent_id), NULL) ||
        !bounded_length(node->title, sizeof(node->title), NULL) ||
        !bounded_length(node->description, sizeof(node->description), NULL) ||
        !bounded_length(node->target_id, sizeof(node->target_id), NULL) ||
        !bounded_length(node->binding_key, sizeof(node->binding_key), NULL) ||
        !bounded_length(node->value, sizeof(node->value), NULL)) {
        return ONION_E_INVALID_ARGUMENT;
    }
    if (node->title[0] == '\0') return ONION_E_INVALID_ARGUMENT;
    if (node->parent_id[0] != '\0' &&
        !valid_id(node->parent_id, sizeof(node->parent_id))) {
        return ONION_E_INVALID_ARGUMENT;
    }
    if (node->target_id[0] != '\0' &&
        !valid_id(node->target_id, sizeof(node->target_id))) {
        return ONION_E_INVALID_ARGUMENT;
    }
    if (node->binding_key[0] != '\0' &&
        !valid_id(node->binding_key, sizeof(node->binding_key))) {
        return ONION_E_INVALID_ARGUMENT;
    }
    return ONION_OK;
}

static onion_status validate_node_shape(const onion_ui_node_desc_v1 *node) {
    if (!kind_valid(node->kind) || node->value_type > ONION_UI_VALUE_STRING ||
        node->binding > ONION_UI_BINDING_EVENT) {
        return ONION_E_INVALID_ARGUMENT;
    }
    if (node->kind == ONION_UI_NODE_PAGE) {
        if (node->parent_id[0] != '\0') return ONION_E_INVALID_ARGUMENT;
    } else if (node->parent_id[0] == '\0') {
        return ONION_E_INVALID_ARGUMENT;
    }
    if (node->kind == ONION_UI_NODE_MENU) {
        if (node->target_id[0] == '\0' || node->binding != ONION_UI_BINDING_NONE)
            return ONION_E_INVALID_ARGUMENT;
    } else if (node->target_id[0] != '\0') {
        return ONION_E_INVALID_ARGUMENT;
    }
    if (node->kind == ONION_UI_NODE_ACTION) {
        if (node->binding != ONION_UI_BINDING_EVENT || node->binding_key[0] == '\0' ||
            node->value_type != ONION_UI_VALUE_NONE) {
            return ONION_E_INVALID_ARGUMENT;
        }
    } else if (node->binding != ONION_UI_BINDING_NONE &&
               node->binding_key[0] == '\0') {
        return ONION_E_INVALID_ARGUMENT;
    }
    if (node->kind == ONION_UI_NODE_TOGGLE &&
        node->value_type != ONION_UI_VALUE_BOOL) return ONION_E_INVALID_ARGUMENT;
    if ((node->kind == ONION_UI_NODE_LIST || node->kind == ONION_UI_NODE_INPUT) &&
        node->value_type != ONION_UI_VALUE_INT &&
        node->value_type != ONION_UI_VALUE_STRING) return ONION_E_INVALID_ARGUMENT;
    if (node->kind == ONION_UI_NODE_LIST_ITEM &&
        node->value_type != ONION_UI_VALUE_INT &&
        node->value_type != ONION_UI_VALUE_STRING) return ONION_E_INVALID_ARGUMENT;
    if ((node->kind == ONION_UI_NODE_PAGE || node->kind == ONION_UI_NODE_MENU ||
         node->kind == ONION_UI_NODE_GROUP || node->kind == ONION_UI_NODE_LABEL) &&
        (node->value_type != ONION_UI_VALUE_NONE ||
         node->binding != ONION_UI_BINDING_NONE)) return ONION_E_INVALID_ARGUMENT;
    if (node->max_length != 0 && node->min_length > node->max_length)
        return ONION_E_INVALID_ARGUMENT;
    return ONION_OK;
}

static int value_valid(const onion_ui_node_desc_v1 *node) {
    size_t length = 0;
    if (!bounded_length(node->value, sizeof(node->value), &length)) return 0;
    if (node->value_type == ONION_UI_VALUE_BOOL) {
        return strcmp(node->value, "0") == 0 || strcmp(node->value, "1") == 0 ||
               strcmp(node->value, "true") == 0 || strcmp(node->value, "false") == 0;
    }
    if (node->value_type == ONION_UI_VALUE_INT) {
        char *end = NULL;
        errno = 0;
        long long value = strtoll(node->value, &end, 10);
        if (errno != 0 || end == node->value || *end != '\0') return 0;
        return node->min_value > node->max_value ||
               (value >= node->min_value && value <= node->max_value);
    }
    return node->value_type == ONION_UI_VALUE_STRING &&
           length >= node->min_length &&
           (node->max_length == 0 || length <= node->max_length);
}

onion_status onion_ui_document_create(const onion_ui_document_desc_v1 *desc,
                                      onion_ui_document **out_document) {
    if (!desc || !out_document || desc->struct_size < sizeof(*desc) ||
        desc->abi_version != ONION_UI_ABI_VERSION ||
        !valid_id(desc->plugin_id, sizeof(desc->plugin_id)) ||
        !valid_id(desc->contribution_id, sizeof(desc->contribution_id)) ||
        !valid_id(desc->root_page_id, sizeof(desc->root_page_id)) ||
        !bounded_length(desc->title, sizeof(desc->title), NULL) ||
        !bounded_length(desc->description, sizeof(desc->description), NULL) ||
        desc->title[0] == '\0') {
        return ONION_E_INVALID_ARGUMENT;
    }
    onion_ui_document *document = (onion_ui_document *)calloc(1, sizeof(*document));
    if (!document) return ONION_E_NO_MEMORY;
    document->desc = *desc;
    *out_document = document;
    return ONION_OK;
}

void onion_ui_document_destroy(onion_ui_document *document) {
    if (!document) return;
    free(document->nodes);
    free(document);
}

onion_status onion_ui_document_add_node(onion_ui_document *document,
                                        const onion_ui_node_desc_v1 *node) {
    if (!document || !node || node->struct_size < sizeof(*node) ||
        node->abi_version != ONION_UI_ABI_VERSION ||
        document->node_count >= ONION_UI_MAX_NODES) {
        return ONION_E_INVALID_ARGUMENT;
    }
    onion_status result = validate_node_strings(node);
    if (result != ONION_OK) return result;
    result = validate_node_shape(node);
    if (result != ONION_OK) return result;
    if ((node->kind == ONION_UI_NODE_TOGGLE ||
         node->kind == ONION_UI_NODE_LIST ||
         node->kind == ONION_UI_NODE_LIST_ITEM ||
         node->kind == ONION_UI_NODE_INPUT) && !value_valid(node)) {
        return ONION_E_INVALID_ARGUMENT;
    }
    if (find_node(document, node->id)) return ONION_E_ALREADY_INITIALIZED;
    if (node->kind != ONION_UI_NODE_PAGE) {
        const onion_ui_node_desc_v1 *parent = find_node(document, node->parent_id);
        if (!parent) return ONION_E_NOT_FOUND;
        if (!parent_accepts(parent->kind, node->kind)) return ONION_E_INVALID_ARGUMENT;
    }
    if (document->node_count == document->node_capacity) {
        size_t capacity = document->node_capacity ? document->node_capacity * 2 : 16;
        onion_ui_node_desc_v1 *nodes = (onion_ui_node_desc_v1 *)realloc(
            document->nodes, capacity * sizeof(*nodes));
        if (!nodes) return ONION_E_NO_MEMORY;
        document->nodes = nodes;
        document->node_capacity = capacity;
    }
    document->nodes[document->node_count++] = *node;
    return ONION_OK;
}

onion_status onion_ui_document_validate(const onion_ui_document *document) {
    if (!document || document->node_count == 0) return ONION_E_INVALID_ARGUMENT;
    const onion_ui_node_desc_v1 *root = find_node(document, document->desc.root_page_id);
    if (!root || root->kind != ONION_UI_NODE_PAGE) return ONION_E_NOT_FOUND;
    for (size_t i = 0; i < document->node_count; ++i) {
        const onion_ui_node_desc_v1 *node = &document->nodes[i];
        if (node->kind == ONION_UI_NODE_MENU) {
            const onion_ui_node_desc_v1 *target = find_node(document, node->target_id);
            if (!target || target->kind != ONION_UI_NODE_PAGE) return ONION_E_NOT_FOUND;
        }
        if (node->kind == ONION_UI_NODE_LIST) {
            int has_item = 0;
            int has_selected_item = 0;
            for (size_t j = 0; j < document->node_count; ++j) {
                if (document->nodes[j].kind == ONION_UI_NODE_LIST_ITEM &&
                    strcmp(document->nodes[j].parent_id, node->id) == 0) {
                    if (document->nodes[j].value_type != node->value_type)
                        return ONION_E_INVALID_ARGUMENT;
                    has_item = 1;
                    if (strcmp(document->nodes[j].value, node->value) == 0)
                        has_selected_item = 1;
                }
            }
            if (!has_item || !has_selected_item) return ONION_E_INVALID_ARGUMENT;
        }
        size_t depth = 0;
        const onion_ui_node_desc_v1 *cursor = node;
        while (cursor->parent_id[0] != '\0') {
            cursor = find_node(document, cursor->parent_id);
            if (!cursor || ++depth > ONION_UI_MAX_DEPTH) return ONION_E_INVALID_ARGUMENT;
        }
    }
    return ONION_OK;
}

size_t onion_ui_document_node_count(const onion_ui_document *document) {
    return document ? document->node_count : 0;
}

static size_t string_size(const char *text, size_t capacity) {
    size_t length = 0;
    return bounded_length(text, capacity, &length) ? length : SIZE_MAX;
}

onion_status onion_ui_document_encoded_size(const onion_ui_document *document,
                                            size_t *out_size) {
    if (!out_size) return ONION_E_INVALID_ARGUMENT;
    onion_status result = onion_ui_document_validate(document);
    if (result != ONION_OK) return result;
    size_t size = ONION_UI_DOCUMENT_HEADER_SIZE;
    const size_t metadata_sizes[] = {
        string_size(document->desc.plugin_id, sizeof(document->desc.plugin_id)),
        string_size(document->desc.contribution_id, sizeof(document->desc.contribution_id)),
        string_size(document->desc.title, sizeof(document->desc.title)),
        string_size(document->desc.description, sizeof(document->desc.description)),
        string_size(document->desc.root_page_id, sizeof(document->desc.root_page_id)),
    };
    for (size_t i = 0; i < sizeof(metadata_sizes) / sizeof(metadata_sizes[0]); ++i)
        size += metadata_sizes[i];
    for (size_t i = 0; i < document->node_count; ++i) {
        const onion_ui_node_desc_v1 *node = &document->nodes[i];
        size += ONION_UI_NODE_HEADER_SIZE;
        size += strlen(node->id) + strlen(node->parent_id) + strlen(node->title) +
                strlen(node->description) + strlen(node->target_id) +
                strlen(node->binding_key) + strlen(node->value);
    }
    if (size > ONION_UI_DOCUMENT_MAX_ENCODED_SIZE || size > UINT32_MAX)
        return ONION_E_INVALID_ARGUMENT;
    *out_size = size;
    return ONION_OK;
}

static void put_u16(uint8_t **cursor, uint16_t value) {
    (*cursor)[0] = (uint8_t)value;
    (*cursor)[1] = (uint8_t)(value >> 8);
    *cursor += 2;
}

static void put_u32(uint8_t **cursor, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) (*cursor)[i] = (uint8_t)(value >> (i * 8));
    *cursor += 4;
}

static void put_u64(uint8_t **cursor, uint64_t value) {
    for (unsigned i = 0; i < 8; ++i) (*cursor)[i] = (uint8_t)(value >> (i * 8));
    *cursor += 8;
}

static void put_text(uint8_t **cursor, const char *text) {
    size_t length = strlen(text);
    if (length) memcpy(*cursor, text, length);
    *cursor += length;
}

onion_status onion_ui_document_encode(const onion_ui_document *document,
                                      void *buffer, size_t buffer_size,
                                      size_t *out_size) {
    size_t encoded_size = 0;
    onion_status result = onion_ui_document_encoded_size(document, &encoded_size);
    if (result != ONION_OK) return result;
    if (out_size) *out_size = encoded_size;
    if (!buffer || buffer_size < encoded_size) return ONION_E_INVALID_ARGUMENT;

    uint8_t *cursor = (uint8_t *)buffer;
    put_u32(&cursor, ONION_UI_DOCUMENT_MAGIC);
    put_u16(&cursor, ONION_UI_DOCUMENT_WIRE_VERSION);
    put_u16(&cursor, ONION_UI_DOCUMENT_HEADER_SIZE);
    put_u32(&cursor, (uint32_t)encoded_size);
    put_u32(&cursor, (uint32_t)document->node_count);
    put_u32(&cursor, document->desc.flags);
    put_u32(&cursor, (uint32_t)document->desc.priority);
    put_u16(&cursor, (uint16_t)strlen(document->desc.plugin_id));
    put_u16(&cursor, (uint16_t)strlen(document->desc.contribution_id));
    put_u16(&cursor, (uint16_t)strlen(document->desc.title));
    put_u16(&cursor, (uint16_t)strlen(document->desc.description));
    put_u16(&cursor, (uint16_t)strlen(document->desc.root_page_id));
    put_u16(&cursor, 0);
    put_text(&cursor, document->desc.plugin_id);
    put_text(&cursor, document->desc.contribution_id);
    put_text(&cursor, document->desc.title);
    put_text(&cursor, document->desc.description);
    put_text(&cursor, document->desc.root_page_id);

    for (size_t i = 0; i < document->node_count; ++i) {
        const onion_ui_node_desc_v1 *node = &document->nodes[i];
        const char *texts[] = {node->id, node->parent_id, node->title,
                               node->description, node->target_id,
                               node->binding_key, node->value};
        size_t record_size = ONION_UI_NODE_HEADER_SIZE;
        for (size_t j = 0; j < sizeof(texts) / sizeof(texts[0]); ++j)
            record_size += strlen(texts[j]);
        put_u32(&cursor, (uint32_t)record_size);
        put_u16(&cursor, (uint16_t)node->kind);
        put_u16(&cursor, 0);
        put_u32(&cursor, node->flags);
        put_u32(&cursor, node->value_type);
        put_u32(&cursor, node->binding);
        put_u64(&cursor, (uint64_t)node->min_value);
        put_u64(&cursor, (uint64_t)node->max_value);
        put_u32(&cursor, node->min_length);
        put_u32(&cursor, node->max_length);
        for (size_t j = 0; j < sizeof(texts) / sizeof(texts[0]); ++j)
            put_u16(&cursor, (uint16_t)strlen(texts[j]));
        put_u16(&cursor, 0);
        for (size_t j = 0; j < sizeof(texts) / sizeof(texts[0]); ++j)
            put_text(&cursor, texts[j]);
    }
    return (size_t)(cursor - (uint8_t *)buffer) == encoded_size
               ? ONION_OK : ONION_E_PROTOCOL;
}

onion_status onion_ui_get_services(const onion_host_services_v1 *host,
                                   onion_ui_services_v1 *out_services) {
    if (!out_services) return ONION_E_INVALID_ARGUMENT;
    memset(out_services, 0, sizeof(*out_services));
    return onion_service_query_interface(host, ONION_UI_SERVICE_NAME,
                                         ONION_UI_ABI_VERSION, out_services,
                                         sizeof(*out_services));
}

onion_status onion_ui_register(const onion_host_services_v1 *host,
                               const onion_ui_document *document,
                               onion_ui_handle *out_handle) {
    if (!document || !out_handle) return ONION_E_INVALID_ARGUMENT;
    onion_ui_services_v1 ui;
    onion_status result = onion_ui_get_services(host, &ui);
    if (result != ONION_OK) return result;
    if (!ui.register_document) return ONION_E_NOT_SUPPORTED;
    size_t size = 0;
    result = onion_ui_document_encoded_size(document, &size);
    if (result != ONION_OK) return result;
    void *encoded = malloc(size);
    if (!encoded) return ONION_E_NO_MEMORY;
    result = onion_ui_document_encode(document, encoded, size, NULL);
    if (result == ONION_OK)
        result = ui.register_document(ui.context, encoded, size, out_handle);
    free(encoded);
    return result;
}

onion_status onion_ui_unregister(const onion_host_services_v1 *host,
                                 onion_ui_handle handle) {
    onion_ui_services_v1 ui;
    onion_status result = onion_ui_get_services(host, &ui);
    if (result != ONION_OK) return result;
    return ui.unregister_document
               ? ui.unregister_document(ui.context, handle)
               : ONION_E_NOT_SUPPORTED;
}

onion_status onion_ui_set_value(const onion_host_services_v1 *host,
                                onion_ui_handle handle, const char *node_id,
                                onion_ui_value_type value_type,
                                const char *value) {
    if (!node_id || !value || !valid_id(node_id, ONION_UI_NODE_ID_MAX) ||
        value_type == ONION_UI_VALUE_NONE || value_type > ONION_UI_VALUE_STRING ||
        strlen(value) >= ONION_UI_VALUE_MAX) return ONION_E_INVALID_ARGUMENT;
    onion_ui_services_v1 ui;
    onion_status result = onion_ui_get_services(host, &ui);
    if (result != ONION_OK) return result;
    return ui.set_value ? ui.set_value(ui.context, handle, node_id, value_type, value)
                        : ONION_E_NOT_SUPPORTED;
}
