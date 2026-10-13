#include <onion/plugin.h>

#include <stdio.h>
#include <unistd.h>

ONION_PLUGIN_DEFINE("ONIO00002", "1.00", "Onion daemon sample",
                    ONION_PLUGIN_CAP_IPC, ONION_PLUGIN_FLAG_LONG_RUNNING |
                        ONION_PLUGIN_FLAG_STOP_SUPPORTED);

int main(void) {
    puts("HyperHEN daemon sample started");
    for (;;) {
        pause();
    }
}

