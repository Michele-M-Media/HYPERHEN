/* PS5 (FreeBSD 11 based): kern.arandom is the kernel CSPRNG, the source FreeBSD 11's
 * own arc4random uses. getentropy() arrived in FreeBSD 12. */
#include "ava1_platform.h"

#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <time.h>
#include <unistd.h>

/* Hidden behind __POSIX_VISIBLE in the SDK headers, as in runtime.c. */
extern int posix_fallocate(int fd, off_t offset, off_t len);

#ifndef KERN_ARND
#define KERN_ARND 37
#endif

int ava1_platform_random(uint8_t *buf, size_t n) {
    int mib[2] = { CTL_KERN, KERN_ARND };
    while (n > 0) {
        size_t want = n > 256 ? 256 : n, got = want;
        if (sysctl(mib, 2, buf, &got, NULL, 0) != 0 || got == 0) return -1;
        buf += got;
        n -= got;
    }
    return 0;
}

void ava1_platform_sleep_ms(unsigned ms) { usleep((useconds_t)ms * 1000u); }

int ava1_platform_preallocate(int fd, uint64_t len) {
    int rc = posix_fallocate(fd, 0, (off_t)len);
    if (rc == 0 || rc == ENOSPC) return rc;
    return ftruncate(fd, (off_t)len) == 0 ? 0 : errno; /* exFAT may refuse fallocate */
}

void ava1_platform_set_mtime(int fd, const char *path, uint64_t mtime) {
    struct timespec ts[2];
    (void)fd;
    ts[0].tv_sec = (time_t)mtime;
    ts[0].tv_nsec = 0;
    ts[1] = ts[0];
    (void)utimensat(AT_FDCWD, path, ts, 0);
}
