#pragma once

/* Writes to the system log and forwards the line to the daemon, which keeps it in its own log. */
void cr_plog(const char *fmt, ...);
