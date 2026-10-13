#pragma once

#include <stdint.h>

/* Stable encoded UI document format. Multi-byte integers are little-endian. */
#define ONION_UI_DOCUMENT_MAGIC 0x4449554Fu /* 'OUID' */
#define ONION_UI_DOCUMENT_WIRE_VERSION 1u
#define ONION_UI_DOCUMENT_HEADER_SIZE 36u
#define ONION_UI_NODE_HEADER_SIZE 60u
#define ONION_UI_DOCUMENT_MAX_ENCODED_SIZE (256u * 1024u)

