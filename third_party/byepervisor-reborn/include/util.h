#ifndef CPU_H
#define CPU_H

#include <stdint.h>

typedef struct notify_request {
  char useless1[45];
  char message[3075];
} notify_request_t;

int sceKernelGetCurrentCpu();
int sceKernelSendNotificationRequest(int, notify_request_t*, size_t, int);
int sceKernelSleep(int secs);
int sceKernelUsleep(int usecs);
int sceKernelDebugOutText(int channel, const char *text);
int sceKernelGetMainSocId();

int kread(uint64_t kaddr, void *dst, size_t len);
uint32_t kread32(uint64_t kaddr);
uint64_t kread64(uint64_t kaddr);
int kwrite(uint64_t kaddr, const void *src, size_t len);
void kwrite32(uint64_t kaddr, uint32_t val);
void kwrite64(uint64_t kaddr, uint64_t val);

int pin_to_core(int num);
void pin_to_first_available_core();
int get_cpu_core();

void notifyf(const char *fmt, ...);
void klog(const char *fmt, ...);
void kHexDump(uint64_t kaddr, uint64_t len);

#endif // CPU_H