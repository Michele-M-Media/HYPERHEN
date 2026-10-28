#include "notify.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifdef __SCE__
typedef struct notify_request {
  char unused[45];
  char message[3075];
} notify_request_t;

int sceKernelSendNotificationRequest(int device, notify_request_t *request,
                                     size_t size, int unused);
#endif

void
notify_user(const char *fmt, ...) {
#ifdef __SCE__
  notify_request_t req;
  va_list args;

  memset(&req, 0, sizeof(req));
  va_start(args, fmt);
  vsnprintf(req.message, sizeof(req.message), fmt, args);
  va_end(args);

  sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
#else
  (void)fmt;
#endif
}
