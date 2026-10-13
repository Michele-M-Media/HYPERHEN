#ifndef HYPERHEN_BYPERVISOR_5XX_INTEGRATION_H
#define HYPERHEN_BYPERVISOR_5XX_INTEGRATION_H

#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#define HYPERHEN_HV5XX_ROOT "/system_tmp/hyperhen"
#define HYPERHEN_HV5XX_READY_DIR HYPERHEN_HV5XX_ROOT "/ready"
#define HYPERHEN_HV5XX_READY HYPERHEN_HV5XX_READY_DIR "/hypervisor_5xx"
#define HYPERHEN_HV5XX_FAILED HYPERHEN_HV5XX_READY_DIR "/hypervisor_5xx_failed"

static inline void hyperhen_hv5xx_ensure_root(void) {
    (void)mkdir(HYPERHEN_HV5XX_ROOT, 0777);
    (void)mkdir(HYPERHEN_HV5XX_READY_DIR, 0777);
}

static inline void hyperhen_hv5xx_mark(const char *path, const char *state) {
    FILE *fp;

    hyperhen_hv5xx_ensure_root();
    fp = fopen(path, "w");
    if (fp != NULL) {
        (void)fputs(state, fp);
        (void)fputc('\n', fp);
        (void)fclose(fp);
    }
}

static inline void hyperhen_hv5xx_begin(void) {
    hyperhen_hv5xx_ensure_root();
    (void)unlink(HYPERHEN_HV5XX_READY);
    (void)unlink(HYPERHEN_HV5XX_FAILED);
}

static inline void hyperhen_hv5xx_mark_ready(void) {
    hyperhen_hv5xx_mark(HYPERHEN_HV5XX_READY, "ready");
}

static inline void hyperhen_hv5xx_mark_failed(void) {
    hyperhen_hv5xx_mark(HYPERHEN_HV5XX_FAILED, "failed");
}

#endif
