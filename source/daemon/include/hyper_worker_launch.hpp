#pragma once

#include <cstddef>
#include <cstdint>
#include <sys/types.h>

namespace onion::daemon::hyper {
// Hyper-only staging; the shared payload loader and its protocol are unchanged.
pid_t launch_worker(const uint8_t *elf, size_t size, const char *args, uint64_t token);
}
