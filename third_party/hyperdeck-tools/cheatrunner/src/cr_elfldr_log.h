/* Adapter so elfldr.c logs through cr_log instead of its own stdout/klog macros. */
#pragma once

#include <errno.h>
#include <string.h>

#include "cr_log.h"
#include "pt.h"

#define LOG_PUTS(s) cr_log("warn", "elfldr", "%s", (s))
#define LOG_DEBUG(...) cr_log("debug", "elfldr", __VA_ARGS__)
#define LOG_PERROR(s) cr_log("warn", "elfldr", "%s: %s", (s), strerror(errno))
#define LOG_PT_PERROR(pid, s) pt_perror((pid), (s))
