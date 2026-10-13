#ifndef PPR_NOTIFY_H
#define PPR_NOTIFY_H

#include <stddef.h>

int ppr_printf(const char *format, ...);
int ppr_puts(const char *text);
void ppr_perror(const char *prefix);
void ppr_notify_flush(void);

#endif
