#include <onion/client.h>
#include <onion/ipc.h>
#include <onion/plugin.h>
#include <onion/status.h>
#include <onion/transport.h>

#include <stdio.h>
#include <unistd.h>

ONION_PLUGIN_DEFINE("ONIO00001", "1.00", "Onion hello", 
                    ONION_PLUGIN_CAP_IPC, ONION_PLUGIN_FLAG_LONG_RUNNING);

int main(void) {
    onion_transport transport = {0};
    onion_socket_transport socket_state = {0};
    onion_client client = ONION_CLIENT_INITIALIZER;
    onion_status status = onion_socket_transport_connect(
        &transport, &socket_state, ONION_PLUGIN_IPC_SOCKET_PATH);
    if (status == ONION_OK) status = onion_client_init(&client, &transport);
    if (status == ONION_OK)
        status = onion_client_open_session(&client, &onion_plugin_descriptor);
    if (status == ONION_OK) status = onion_client_ping(&client);
    if (status != ONION_OK) {
        printf("HyperHEN connection failed: %s\n", onion_status_string(status));
        onion_client_deinit(&client);
        onion_socket_transport_deinit(&transport);
        return 1;
    }

    puts("HyperHEN hello plugin connected");
    for (;;) {
        sleep(60);
    }
    return 0;
}
