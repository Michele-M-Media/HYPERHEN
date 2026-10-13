/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * PS5 registry entity-id arithmetic (host-testable, no SDK).
 *
 * Account slot @a is 1..16. Entity = (slot-1)*65536 + base (@d).
 * Out-of-range slots return the fallback id @e.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

static inline int onion_reg_entity_number(int a, int d, int e) {
  const int b = 16;
  const int c = 65536;
  if (a < 1 || a > b)
    return e;
  return (a - 1) * c + d;
}

#ifdef __cplusplus
}
#endif
