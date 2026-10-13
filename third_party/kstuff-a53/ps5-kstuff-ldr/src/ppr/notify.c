/* PPR diagnostics stay on stdout. Graphical notifications remain the
 * responsibility of kstuff; this module never opens /dev/notification0. */
#include "notify.h"

#include <stdarg.h>
#include <stdio.h>

void ppr_notify_flush(void) {
    /* No buffered graphical notifications. */
}

int ppr_printf(const char *format, ...) {
    va_list args;
    va_start(args, format);
    int rc = vprintf(format, args);
    va_end(args);
    fflush(stdout);
    return rc;
}

int ppr_puts(const char *text) {
    int rc = puts(text);
    fflush(stdout);
    return rc;
}

void ppr_perror(const char *prefix) {
    perror(prefix);
    fflush(stderr);
}
