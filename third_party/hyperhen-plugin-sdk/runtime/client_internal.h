#pragma once

#include <stddef.h>
#include <stdint.h>

#include "onion/client.h"

onion_status onion_client_request(onion_client *client, uint16_t command,
                                  const void *payload, size_t payload_size,
                                  void *out_data, size_t out_capacity,
                                  size_t *out_size);

