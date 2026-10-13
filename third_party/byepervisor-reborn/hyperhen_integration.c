#include "hyperhen_integration.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define HYPERHEN_STATE_ROOT "/system_tmp/hyperhen"
#define HYPERHEN_READY_ROOT HYPERHEN_STATE_ROOT "/ready"
#define HYPERHEN_HV_ARMED HYPERHEN_READY_ROOT "/hypervisor_armed"
#define HYPERHEN_HV_READY HYPERHEN_READY_ROOT "/hypervisor"
#define HYPERHEN_HV_FAILED HYPERHEN_READY_ROOT "/hypervisor_failed"

int sceKernelSetProcessName(const char *name);

static void ensure_state_root(void) {
  (void)mkdir(HYPERHEN_STATE_ROOT, 0777);
  (void)mkdir(HYPERHEN_READY_ROOT, 0777);
}

static void write_marker(const char *path, const char *value) {
  int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (fd < 0)
    return;
  size_t length = strlen(value);
  (void)write(fd, value, length);
  (void)close(fd);
}

void hyperhen_hv_begin(void) {
  (void)sceKernelSetProcessName("hyperhen_hv.elf");
  ensure_state_root();
}

void hyperhen_hv_mark_armed(void) {
  char pid[32];
  int length = snprintf(pid, sizeof(pid), "%ld", (long)getpid());
  if (length > 0 && (size_t)length < sizeof(pid))
    write_marker(HYPERHEN_HV_ARMED, pid);
}

void hyperhen_hv_mark_ready(void) {
  ensure_state_root();
  write_marker(HYPERHEN_HV_READY, "1");
  (void)unlink(HYPERHEN_HV_ARMED);
  (void)unlink(HYPERHEN_HV_FAILED);
}

void hyperhen_hv_mark_failed(void) {
  ensure_state_root();
  write_marker(HYPERHEN_HV_FAILED, "1");
  (void)unlink(HYPERHEN_HV_ARMED);
}
