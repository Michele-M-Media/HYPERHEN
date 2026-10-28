#ifndef SM_PKG_WS_H
#define SM_PKG_WS_H
#include <stddef.h>
#include <stdatomic.h>
#include "sm_pkg_stream.h"
int sm_pkg_ws_accept_key(const char *key, char out[32]);
int sm_pkg_ws_decode(unsigned char *data, size_t size, unsigned char *opcode,
                     int *fin, size_t *offset, size_t *length);
void sm_pkg_ws_serve(int fd, const char *key, const void *initial, size_t size,
                     sm_pkg_stream_t *stream, const atomic_bool *running);
#endif
