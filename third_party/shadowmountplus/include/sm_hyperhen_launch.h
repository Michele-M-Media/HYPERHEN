#ifndef SM_HYPERHEN_LAUNCH_H
#define SM_HYPERHEN_LAUNCH_H

#include <stdbool.h>

/* Written by the HyperHEN bootstrapper only after its selected engine has
 * completed its existing readiness protocol, immediately before this ELF:
 *   v1 <firmware-8-hex-digits> <core|legacy|bypervisor> <lite-0-or-1> <launcher-pid>\n
 * Example: v1 05100000 bypervisor 0 123\n
 * The launcher must remain alive while waiting for automount readiness.
 * Missing or mismatched proof never enables a guessed legacy fallback. */
#define SM_HYPERHEN_ROUTE_FILE "/system_tmp/hyperhen/ready/automount_route"

bool sm_hyperhen_launch_route_ready(void);

#endif
