/*
 * File Explorer - embedded asset registry.
 */

#pragma once

#include <stddef.h>

#include "websrv.h"


int asset_request(const http_request_t *req, const char *url);

void asset_register(const char *path, void *data, size_t size,
                    const char *mime);
