#ifndef CR_STORE_LOOKUP_H
#define CR_STORE_LOOKUP_H

#include <stddef.h>

/* Fetches game name from the PS Store product page for content_id; returns 1 on success, 0 on failure. */
int cr_store_lookup(const char *content_id, char *name_out, size_t name_size, int timeout_ms);

#endif /* CR_STORE_LOOKUP_H */
