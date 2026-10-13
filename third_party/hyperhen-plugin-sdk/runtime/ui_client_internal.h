#pragma once

#include "onion/client.h"
#include "onion/ui.h"

onion_status onion_ui_client_make_services(onion_client *client,
                                           onion_ui_services_v1 *out_services);

