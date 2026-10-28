#ifndef CR_HTTP_H
#define CR_HTTP_H

extern char g_listen_ip[64];
extern int g_http_listen_fd;

void *http_server_thread(void *arg);
void *http_net_watchdog_thread(void *arg);

int       cr_http_active_clients(void);
int       cr_http_max_concurrent(void);
long long cr_http_too_many_requests_count(void);

#endif /* CR_HTTP_H */
