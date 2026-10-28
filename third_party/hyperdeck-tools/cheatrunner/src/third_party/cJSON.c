/* Wrapper that silences strict warnings from the upstream cJSON sources. */

#pragma clang diagnostic ignored "-Wunreachable-code-generic-assoc"
#pragma clang diagnostic ignored "-Wmissing-prototypes"
#pragma clang diagnostic ignored "-Wstrict-prototypes"
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wsign-compare"
#pragma clang diagnostic ignored "-Wint-conversion"

#include "cJSON_real.c"
