#include "ava1_events.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "ava1_job.h"

static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static char g_path[256];
static uint32_t g_limit;

void ava1_events_set_path(const char *path) {
    pthread_mutex_lock(&g_mu);
    if (path && strlen(path) + 5 <= sizeof g_path) snprintf(g_path, sizeof g_path, "%s", path);
    else g_path[0] = '\0';
    pthread_mutex_unlock(&g_mu);
}

void ava1_events_set_limit(uint32_t bytes) {
    pthread_mutex_lock(&g_mu);
    g_limit = bytes;
    pthread_mutex_unlock(&g_mu);
}

void ava1_log_event(const char *line) {
    char buf[AVA1_EVENTS_LINE_MAX + 40], old[sizeof g_path + 8];
    struct stat st;
    struct tm tm;
    time_t now = time(NULL);
    size_t n, ll;
    uint32_t limit;
    int fd;
    if (!line) return;
    pthread_mutex_lock(&g_mu);
    if (!g_path[0]) goto out;
    gmtime_r(&now, &tm);
    n = strftime(buf, 32, "%Y-%m-%dT%H:%M:%SZ ", &tm);
    ll = strlen(line);
    if (ll > AVA1_EVENTS_LINE_MAX) ll = AVA1_EVENTS_LINE_MAX;
    memcpy(buf + n, line, ll);
    n += ll;
    buf[n++] = '\n';
    limit = g_limit ? g_limit : AVA1_EVENTS_LIMIT;
    if (stat(g_path, &st) == 0 && (uint64_t)st.st_size + n > limit) {
        /* Same directory, so same device: a plain rename is safe here. */
        snprintf(old, sizeof old, "%s.old", g_path);
        (void)rename(g_path, old);
    }
    fd = open(g_path, O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0644);
    if (fd >= 0) {
        ssize_t w = write(fd, buf, n);
        (void)w; /* best effort */
        close(fd);
    }
out:
    pthread_mutex_unlock(&g_mu);
}

void ava1_log_job_event(const char *what, const struct ava1_job *j, uint16_t status) {
    char line[AVA1_EVENTS_LINE_MAX];
    char msg[96] = "";
    if (!j) return;
    /* Only a finished job has a settled message; copying it racily is harmless text. */
    if (j->message[0]) {
        char clean[81];
        size_t i;
        for (i = 0; i < sizeof clean - 1 && j->message[i]; i++) {
            unsigned char c = (unsigned char)j->message[i];
            clean[i] = (c < 0x20 || c >= 0x7f || c == '"') ? '?' : (char)c; /* one line, never a forged one */
        }
        clean[i] = '\0';
        snprintf(msg, sizeof msg, " msg=\"%s\"", clean);
    }
    snprintf(line, sizeof line, "%s job=%02x%02x%02x%02x kind=%u status=%u files=%u/%u bytes=%llu/%llu lanes=%u%s", what,
             j->id[0], j->id[1], j->id[2], j->id[3], (unsigned)j->kind, (unsigned)status, (unsigned)j->files_done,
             (unsigned)j->m.files, (unsigned long long)j->bytes_durable, (unsigned long long)j->m.bytes,
             (unsigned)j->lanes, msg);
    ava1_log_event(line);
}
