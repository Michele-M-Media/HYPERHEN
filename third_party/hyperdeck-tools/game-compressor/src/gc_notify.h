#ifndef GC_NOTIFY_H
#define GC_NOTIFY_H

void gc_notify_operation_done(const char *action, const char *game,
                              const char *status, const char *detail);
void gc_notify_message(const char *title, const char *detail);

#endif
