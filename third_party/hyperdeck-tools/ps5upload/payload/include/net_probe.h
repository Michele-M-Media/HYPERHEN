/* The reach-back probe behind net.reach / NET_REACH (P3 Task 9), split out of runtime.c so the
 * host tests (ava1-ctest) compile and run the real code against a loopback listener.
 *
 * Request: {"host":"a.b.c.d","port":"19113","timeout_ms":"3000"} (numbers as strings).
 * Opens a TCP connection from this machine and closes it at once; nothing is sent. */
#ifndef PS5UPLOAD_NET_PROBE_H
#define PS5UPLOAD_NET_PROBE_H

#include <stddef.h>

/* Runs the probe for a request body and writes the JSON answer into `resp`:
 *   {"ok":true,"ms":N}
 *   {"ok":false,"timed_out":B,"errno":E,"err":"...","ms":N}      (a measurement, not a failure)
 *   {"ok":false,"err":"bad_request"} / {"ok":false,"err":"bad_address"}   (the request was wrong)
 * Returns the answer's length (always fits `resp` when cap >= 320). `timeout_ms` is clamped to
 * 100..15000, default 3000. Timing uses CLOCK_MONOTONIC. */
size_t net_probe_reach(const char *body, size_t body_len, char *resp, size_t cap);

#endif
