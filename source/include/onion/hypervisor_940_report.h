/* Copyright (C) 2026 HyperHEN contributors */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* The pinned 9.40 payload emits this only after a fully successful resume. */
static inline bool onion_hypervisor_940_report_ready(const void *data,
                                                      size_t size) {
  static const char success[] = ">>> HV DEFEATED";
  const unsigned char *bytes = (const unsigned char *)data;
  const size_t token_size = sizeof(success) - 1;

  if (!bytes || size < token_size)
    return false;
  for (size_t offset = 0; offset <= size - token_size; ++offset) {
    if (memcmp(bytes + offset, success, token_size) == 0)
      return true;
  }
  return false;
}
