/* Adapted from BestPig/BackPork (GPL-3.0). */
#include <stdint.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <time.h>
#include <sys/event.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <ps5/klog.h>
extern void notify(const char *fmt, ...);

#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysctl.h>
#include <sys/user.h>


#define IOVEC_ENTRY(x) {x ? (char *)x : 0, x ? strlen(x) + 1 : 0}
#define IOVEC_SIZE(x) (sizeof(x) / sizeof(struct iovec))

#define BP_LOG(...) klog_printf("[BP] " __VA_ARGS__)
#define HYPERHEN_AUTOMOUNT_SENTINEL "/data/.kstuff_noautomount"

static int hyperhen_automount_owns_mounts(void) {
    return access(HYPERHEN_AUTOMOUNT_SENTINEL, F_OK) == 0;
}

typedef struct app_info {
    uint32_t app_id;
    uint64_t unknown1;
    char title_id[14];
    char unknown2[0x3c];
} app_info_t;

extern int sceKernelGetAppInfo(pid_t pid, app_info_t *info);

// from john-tornblom
static pid_t find_pid(const char *name) {
    int mib[4] = {1, 14, 8, 0};
    pid_t mypid = getpid();
    pid_t pid = -1;
    size_t buf_size;
    uint8_t *buf;

    if (sysctl(mib, 4, 0, &buf_size, 0, 0)) {
        return -1;
    }

    if (!(buf = malloc(buf_size))) {
        return -1;
    }

    if (sysctl(mib, 4, buf, &buf_size, 0, 0)) {
        free(buf);
        return -1;
    }

    for (uint8_t *ptr = buf; ptr < (buf + buf_size);) {
        int ki_structsize = *(int *)ptr;
        pid_t ki_pid = *(pid_t *)&ptr[72];
        char *ki_tdname = (char *)&ptr[447];

        ptr += ki_structsize;
        if (!strcmp(name, ki_tdname) && ki_pid != mypid) {
            pid = ki_pid;
        }
    }

    free(buf);

    return pid;
}

static int mount2(const char *src, const char *dst, const char *type) {
    struct iovec iov[] = {
        IOVEC_ENTRY("fstype"),
        IOVEC_ENTRY(type),
        IOVEC_ENTRY("from"),
        IOVEC_ENTRY(src),
        IOVEC_ENTRY("fspath"),
        IOVEC_ENTRY(dst),
    };

    return nmount(iov, IOVEC_SIZE(iov), 0);
}


static char *mount_fakelibs(const char *sandbox_id, const char *cwd, pid_t pid, char *random_folder) {
    char fake_path[PATH_MAX + 1];
    snprintf(fake_path, sizeof(fake_path), "%s/fakelib", cwd);

    struct stat st;
    if (stat(fake_path, &st) != 0) {
        BP_LOG("fakelib stat failed pid=%d errno=%d path=%s\n", pid, errno, fake_path);
        printf("[WARNING] stat on %s failed (errno: %d, %s)\n", fake_path, errno, strerror(errno));
        return NULL;
    }

    char *fake_mount_path = (char *)malloc(PATH_MAX + 1);
    if (!fake_mount_path) {
        return NULL;
    }

    snprintf(fake_mount_path, PATH_MAX + 1, "/mnt/sandbox/%s/%s/common/lib", sandbox_id, random_folder);

    int res = mount2(fake_path, fake_mount_path, "unionfs");
    if (res != 0) {
        BP_LOG("unionfs mount failed pid=%d errno=%d dst=%s\n", pid, errno, fake_mount_path);
        printf("[WARNING] mount_unionfs failed: %d (errno: %d, %s)\n", res, errno, strerror(errno));
        /* No mount was created; do not force-unmount this path. */
        free(fake_mount_path);
        return NULL;
    }

    printf("[INFO] Mounted fakelibs from %s to %s\n", fake_path, fake_mount_path);
    BP_LOG("mounted pid=%d src=%s dst=%s\n", pid, fake_path, fake_mount_path);
    return fake_mount_path;
}

static int find_highest_sandbox_number(const char* title_id) {
    char base_path[PATH_MAX];
    int highest = -1;

    for (int i = 0; i < 1000; i++) {
        snprintf(base_path, sizeof(base_path), "/mnt/sandbox/%s_%03d", title_id, i);

        struct stat st;
        if (stat(base_path, &st) == 0 && S_ISDIR(st.st_mode)) {
            highest = i;
        } else {
            break;
        }
    }

    return highest;
}

static char* find_random_folder(const char* title_id, int sandbox_num) {
    char base_path[PATH_MAX];
    snprintf(base_path, sizeof(base_path), "/mnt/sandbox/%s_%03d", title_id, sandbox_num);

    DIR* dir = opendir(base_path);
    if (!dir) {
        return NULL;
    }

    struct dirent* entry;
    while ((entry = readdir(dir))) {
        if (entry->d_name[0] == '.') {
            continue;
        }

        char full_path[PATH_MAX];
        snprintf(full_path, sizeof(full_path), "%s/%s/common/lib", base_path, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode)) {
            char *folder = strdup(entry->d_name);
            closedir(dir);
            return folder;
        }
    }

    closedir(dir);
    return NULL;
}

/* Adapted from BestPig/BackPork. Keep all work in the loader ELF process. */
#define MAX_BP_GAMES 64
static unsigned bp_resume_generation;

void backpork_request_resume_rearm(void) {
    unsigned generation = __atomic_add_fetch(&bp_resume_generation, 1,
                                              __ATOMIC_SEQ_CST);
    BP_LOG("resume: rearm requested generation=%u\n", generation);
}

typedef struct {
    pid_t pid;
    char *mount_path;
} bp_game_t;

static char *try_mount_game(pid_t pid, const char *title,
                            const bp_game_t *games) {
    int number = find_highest_sandbox_number(title);
    if (number < 0) return NULL;
    char sandbox[14], app0[PATH_MAX];
    snprintf(sandbox, sizeof(sandbox), "%s_%03d", title, number);
    snprintf(app0, sizeof(app0), "/mnt/sandbox/%s/app0", sandbox);
    char fakelib[PATH_MAX];
    struct stat st;
    snprintf(fakelib, sizeof(fakelib), "%s/fakelib", app0);
    if (stat(fakelib, &st) != 0 || !S_ISDIR(st.st_mode))
        return NULL;
    char *folder = find_random_folder(title, number);
    if (!folder) return NULL;
    char target[PATH_MAX];
    snprintf(target, sizeof(target), "/mnt/sandbox/%s/%s/common/lib",
             sandbox, folder);
    for (int i = 0; i < MAX_BP_GAMES; ++i) {
        if (games[i].mount_path && !strcmp(games[i].mount_path, target)) {
            BP_LOG("mount already active pid=%d dst=%s\n", pid, target);
            free(folder);
            return NULL;
        }
    }
    char *mounted = mount_fakelibs(sandbox, app0, pid, folder);
    free(folder);
    return mounted;
}

static bp_game_t *game_slot(bp_game_t *games, pid_t pid, int create) {
    bp_game_t *free_slot = NULL;
    for (int i = 0; i < MAX_BP_GAMES; ++i) {
        if (games[i].pid == pid) return &games[i];
        if (!games[i].pid && !free_slot) free_slot = &games[i];
    }
    if (create && free_slot) {
        free_slot->pid = pid;
        return free_slot;
    }
    return NULL;
}

static void release_game(bp_game_t *game) {
    if (game->mount_path) {
        int rc = unmount(game->mount_path, 0);
        BP_LOG("unmount pid=%d rc=%d errno=%d dst=%s\n", game->pid,
               rc, rc ? errno : 0, game->mount_path);
        free(game->mount_path);
    }
    game->mount_path = NULL;
    game->pid = 0;
}

int backpork_main(void) {
    int lock_fd = open("/data/kstuff-backpork-monitor.lock",
                       O_RDWR | O_CREAT, 0600);
    if (lock_fd < 0 || flock(lock_fd, LOCK_EX | LOCK_NB) != 0) {
        BP_LOG("monitor already active or lock unavailable pid=%d errno=%d\n",
               getpid(), errno);
        if (lock_fd >= 0) close(lock_fd);
        return -1;
    }
    BP_LOG("native singleton monitor started pid=%d\n", getpid());
    unsigned last_armed_generation = 0;
    unsigned confirmed_generation = 0;
    int ownership_logged = 0;
    for (;;) {
        if (hyperhen_automount_owns_mounts()) {
            if (!ownership_logged) {
                BP_LOG("paused while HyperHEN Automount owns mount lifecycle\n");
                ownership_logged = 1;
            }
            sleep(1);
            continue;
        }
        if (ownership_logged) {
            BP_LOG("HyperHEN Automount released ownership; monitor resumed\n");
            ownership_logged = 0;
        }
        pid_t syscore = find_pid("SceSysCore.elf");
        if (syscore < 0) { sleep(1); continue; }
        int kq = kqueue();
        if (kq < 0) { sleep(1); continue; }
        struct kevent change;
        /* The original BackPork receives inherited child events on this kq. */
        EV_SET(&change, syscore, EVFILT_PROC,
               EV_ADD | EV_ENABLE | EV_CLEAR,
               NOTE_FORK | NOTE_EXEC | NOTE_TRACK | NOTE_EXIT, 0, NULL);
        if (kevent(kq, &change, 1, NULL, 0, NULL) < 0) {
            BP_LOG("syscore watch failed pid=%d errno=%d\n", syscore, errno);
            close(kq);
            sleep(1);
            continue;
        }
        bp_game_t games[MAX_BP_GAMES] = {0};
        unsigned armed_generation = __atomic_load_n(&bp_resume_generation,
                                                     __ATOMIC_SEQ_CST);
        BP_LOG("monitor active syscore=%d\n", syscore);
        if (armed_generation != last_armed_generation)
            BP_LOG("resume: BackPork monitor rearmed syscore=%d generation=%u\n",
                   syscore, armed_generation);
        last_armed_generation = armed_generation;
        int resume_rearm = 0;
        int rearm_deferred = 0;
        for (;;) {
            if (hyperhen_automount_owns_mounts()) {
                BP_LOG("HyperHEN Automount claimed lifecycle; releasing BackPork mounts\n");
                ownership_logged = 1;
                break;
            }
            if (__atomic_load_n(&bp_resume_generation,
                                __ATOMIC_SEQ_CST) != armed_generation) {
                int active_mount = 0;
                for (int i = 0; i < MAX_BP_GAMES; ++i)
                    if (games[i].mount_path) active_mount = 1;
                if (!active_mount) {
                    BP_LOG("resume: rebuilding SysCore watch\n");
                    resume_rearm = 1;
                    break;
                }
                if (!rearm_deferred) {
                    BP_LOG("resume: rearm deferred until game exits\n");
                    rearm_deferred = 1;
                }
            }
            struct kevent event;
            struct timespec wait = {0, 250000000};
            int n = kevent(kq, NULL, 0, &event, 1, &wait);
            if (n < 0) {
                BP_LOG("monitor event failed errno=%d\n", errno);
                break;
            }
            if (!n) continue;
            pid_t pid = (pid_t)event.ident;
            if (pid == syscore && (event.fflags & NOTE_EXIT)) {
                BP_LOG("syscore exited pid=%d\n", syscore);
                break;
            }
            if (event.fflags & NOTE_EXIT) {
                bp_game_t *game = game_slot(games, pid, 0);
                if (game) release_game(game);
                continue;
            }
            if (event.fflags & NOTE_CHILD) {
                bp_game_t *game = game_slot(games, pid, 1);
                if (!game) {
                    BP_LOG("game table full pid=%d\n", pid);
                } else {
                    app_info_t info;
                    char title[10] = {0};
                    int game_title_ready = 0;
                    /*
                     * NOTE_CHILD may arrive before AppInfo has a title ID.
                     * A single lookup can miss the only pre-EXEC mount window.
                     */
                    for (int attempt = 0; attempt < 20; ++attempt) {
                        memset(&info, 0, sizeof(info));
                        if (sceKernelGetAppInfo(pid, &info) == 0) {
                            memcpy(title, info.title_id, 9);
                            title[9] = 0;
                            if (!strncmp(title, "PPSA", 4) ||
                                !strncmp(title, "CUSA", 4)) {
                                game_title_ready = 1;
                                break;
                            }
                        }
                        struct timespec pause = {0, 1000000};
                        nanosleep(&pause, NULL);
                    }
                    if (!game_title_ready) {
                        BP_LOG("child title not ready pid=%d last=%s\n",
                               pid, title[0] ? title : "(empty)");
                    } else {
                        for (int attempt = 0; attempt < 50; ++attempt) {
                            game->mount_path = try_mount_game(pid, title, games);
                            if (game->mount_path) {
                                BP_LOG("mounted before exec pid=%d title=%s attempt=%d dst=%s\n",
                                       pid, title, attempt, game->mount_path);
                                if (armed_generation > confirmed_generation) {
                                    BP_LOG("resume: BackPork early mount confirmed generation=%u title=%s\n",
                                           armed_generation, title);
                                    notify("HyperCore BackPork active again!");
                                    confirmed_generation = armed_generation;
                                }
                                break;
                            }
                            struct timespec pause = {0, 1000000};
                            nanosleep(&pause, NULL);
                        }
                        if (!game->mount_path)
                            BP_LOG("before-exec mount missed pid=%d title=%s\n",
                                   pid, title);
                    }
                }
            }
            if (event.fflags & NOTE_EXEC) {
                bp_game_t *game = game_slot(games, pid, 0);
                if (game && !game->mount_path) {
                    app_info_t info = {0};
                    if (sceKernelGetAppInfo(pid, &info) == 0) {
                        char title[10] = {0};
                        memcpy(title, info.title_id, 9);
                        if (!strncmp(title, "PPSA", 4) ||
                            !strncmp(title, "CUSA", 4)) {
                            for (int attempt = 0; attempt < 20; ++attempt) {
                                game->mount_path = try_mount_game(pid, title, games);
                                if (game->mount_path) {
                                    BP_LOG("mounted inherited pid=%d attempt=%d dst=%s\n",
                                           pid, attempt, game->mount_path);
                                    break;
                                }
                                struct timespec pause = {0, 1000000};
                                nanosleep(&pause, NULL);
                            }
                            if (!game->mount_path)
                                BP_LOG("inherited mount missed pid=%d title=%s\n",
                                       pid, title);
                        }
                    } else {
                        BP_LOG("app info unavailable pid=%d errno=%d\n",
                               pid, errno);
                    }
                }
            }
        }
        for (int i = 0; i < MAX_BP_GAMES; ++i)
            if (games[i].pid) release_game(&games[i]);
        close(kq);
        if (!resume_rearm) sleep(1);
    }
    return 0;
}

void *backpork_thread_entry(void *unused) {
    (void)unused;
    backpork_main();
    return NULL;
}
