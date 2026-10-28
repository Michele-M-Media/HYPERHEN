/* Token, Host and Origin checks for the console web UI server. Stops other
 * web pages (cross-site requests, DNS rebinding) from driving the API. */
#ifndef PS5UPLOAD_WEBUI_AUTH_H
#define PS5UPLOAD_WEBUI_AUTH_H

#include <stddef.h>
#include <stdint.h>
#include "webui_http.h"

typedef struct {
    const char *token;              /* 64 lowercase hex chars */
    uint16_t port;
    int lan;                        /* allow <own_addr>:<port> hosts */
    const char *const *own_addrs;
    size_t n_own;
} webui_auth_cfg_t;

enum webui_deny {
    WEBUI_ALLOW = 0,
    WEBUI_DENY_TOKEN,
    WEBUI_DENY_HOST,
    WEBUI_DENY_ORIGIN
};

/* 32 random bytes as 64 lowercase hex chars plus NUL. */
void webui_token_new(char out[65]);

/* Host/Origin are always checked; the token only when is_api. */
enum webui_deny webui_check(const webui_req_t *r, const webui_auth_cfg_t *c,
                            int is_api);

#endif
