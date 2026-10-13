#ifndef PPR_A53_TRANSPORT_H
#define PPR_A53_TRANSPORT_H

#include <stdint.h>

#include "ppr_patch.h"

struct a53_transport_options {
    int persistent;
    int batch;
    int mixed_io;
};

int a53_transport_initialize(const struct a53_transport_options *options);
int a53_transport_enable_time_acceleration(void);
int a53_transport_get_version(char *out, uint32_t out_size);
uint32_t a53_transport_parse_release(const char *version);
int a53_transport_verify_and_enable_fast(
    const struct a53_transport_options *requested);
void a53_transport_make_ppr(struct ppr_patch_transport *out, int fast_mode);
void a53_transport_shutdown(void);
void a53_transport_set_power_guard(int (*guard)(void));

#endif
