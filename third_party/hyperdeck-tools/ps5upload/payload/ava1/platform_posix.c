#include "ava1_platform.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <sys/random.h>
#endif

int ava1_platform_random(uint8_t *buf, size_t n) {
    while (n > 0) {
        size_t k = n > 256 ? 256 : n;
        if (getentropy(buf, k) != 0) return -1;
        buf += k;
        n -= k;
    }
    return 0;
}

void ava1_platform_sleep_ms(unsigned ms) { usleep((useconds_t)ms * 1000u); }

int ava1_platform_preallocate(int fd, uint64_t len) {
#if defined(__linux__)
    int rc = posix_fallocate(fd, 0, (off_t)len);
    if (rc == 0 || rc == ENOSPC) return rc;
#endif
    return ftruncate(fd, (off_t)len) == 0 ? 0 : errno;
}

void ava1_platform_set_mtime(int fd, const char *path, uint64_t mtime) {
    struct timespec ts[2];
    (void)path;
    ts[0].tv_sec = (time_t)mtime;
    ts[0].tv_nsec = 0;
    ts[1] = ts[0];
    (void)futimens(fd, ts);
}
