#ifndef CR_SHUTDOWN_H
#define CR_SHUTDOWN_H

extern volatile int g_shutdown_requested;

void cheatrunner_close_servers(void);
void cheatrunner_request_shutdown(int delay_ms);

#endif
