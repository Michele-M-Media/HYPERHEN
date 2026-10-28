#ifndef PS5UPLOAD_AVA1_GLUE_H
#define PS5UPLOAD_AVA1_GLUE_H

#include <stddef.h>
#include <stdint.h>

/* Starts the AVA1 server on 9120. 0 on success; 0 on success, or an error when the server could not start. */
int ava1_payload_start(void);

/* The AVA1 RPC dispatch the server's workers use (data-plane methods when the data layer is up,
 * then the management methods), for an in-process caller: the console web UI. The caller sets its
 * identity first (ava1_server_set_rpc_peer). Returns an AVA1 status; the reply or the error's
 * cause is in out / *out_len. */
int ava1_payload_rpc(uint16_t method, const uint8_t *body, uint32_t body_len, uint8_t *out, size_t cap,
                     size_t *out_len);

#endif
