#include "a53_transport.h"
#include "notify.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/event.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <ps5/kernel.h>
#include <ps5/klog.h>

#define printf ppr_printf
#define puts ppr_puts
#define perror ppr_perror

#define DECI5S_MAGIC        0x73354450U
#define DECI5S_SRC_KERNEL   0x80FF0180U
#define DECI5S_DST_MP4      0x80FF0201U
#define DECI5S_PROTO_SDBGP  0x20000201U
#define DECI5S_DCMP         0x18U
#define SDBGP_READ_MEMORY       0x01040021U
#define SDBGP_WRITE_MEMORY      0x01040031U
#define SDBGP_GET_CONF          0x01010010U
#define SDBGP_RES_READ_MEMORY   0x02040021U
#define SDBGP_RES_GET_CONF      0x02010010U
#define SDBGP_MAX_WRITE_CHUNK   0x10000U
#define SDBGP_BATCH_READ_MAX    16U
#define SDBGP_MIXED_WRITE_MAX   8U
#define SDBGP_MIXED_ITEM_MAX    0x100U
#define SDBGP_BATCH_SCAN_CAP    0x4000U

#define SYSCORE_AUTH_ID      0x4800000000000007ULL
#define MEM_TYPE_PA_TO_EL3   0x020000U
#define MP4_COREDUMP_CMD     0x20303000U
#define IOCTL_START          0x800c410bU
#define IOCTL_FINISH         0x8008410fU
#define IOCTL_ALTER_STATE    0x40184115U

struct deci5s_hdr {
    uint32_t magic, self_size, packet_size;
    uint32_t src, dst, protocol_id, attr, user_data;
    uint64_t timestamp;
};

struct deci5s_cmd_hdr {
    struct deci5s_hdr header;
    uint32_t dcmp, code;
    uint32_t sequence_no, packet_no;
    uint32_t attr;
    uint8_t num_commands, reserved[3];
};

struct deci5s_mem_arg {
    uint32_t self_size;
    uint32_t access_size : 8, mem_type : 24;
    uint64_t addr, size;
};

struct sdbgp_command {
    uint32_t self_size;
    uint32_t total_size;
    uint32_t type;
    uint32_t flags;
    uint32_t command_no;
    uint32_t packet_no;
};

struct sdbgp_read_command {
    struct sdbgp_command common;
    uint32_t reserved;
    uint32_t n_args;
};

struct sdbgp_write_command {
    struct sdbgp_command common;
    uint32_t pid;
    uint32_t tid;
    uint32_t n_args;
    uint32_t padding;
};

struct sdbgp_read_result {
    uint32_t self_size;
    uint32_t access_and_type;
    uint64_t addr;
    uint64_t requested;
    uint64_t transferred;
    uint32_t status;
    uint32_t padding;
};

struct sdbgp_packet_result {
    uint64_t buffer;
    uint32_t buffer_size;
    uint32_t sequence_no;
    uint32_t response_offset;
};

struct sdbgp_response_view {
    size_t packet_offset;
    size_t commands_offset;
    size_t packet_end;
    uint32_t command_count;
};

_Static_assert(sizeof(struct deci5s_hdr) == 0x28, "DECI5S header ABI");
_Static_assert(sizeof(struct deci5s_cmd_hdr) == 0x40, "SDBGP outer ABI");
_Static_assert(sizeof(struct sdbgp_command) == 0x18, "SDBGP command ABI");
_Static_assert(sizeof(struct sdbgp_read_command) == 0x20,
               "SDBGP read ABI");
_Static_assert(sizeof(struct sdbgp_write_command) == 0x28,
               "SDBGP write ABI");
_Static_assert(sizeof(struct sdbgp_read_result) == 0x28,
               "SDBGP result ABI");

static int parse_read_response(const uint8_t *raw, size_t raw_size,
                               size_t response_offset,
                               uint32_t sequence_no,
                               uint32_t response_command_count,
                               uint32_t command_number,
                               uint64_t expected_address,
                               uint32_t expected_size, void *destination);

extern uint64_t sceKernelReadTsc(void);

static int g_initialized;
static uint64_t g_mp4sc;
static uint64_t g_zcn_bar2;
static uint64_t g_state_addr;
static uint64_t g_flags_addr;
static uint64_t g_buffer_slot;
static uint64_t g_iommu_slot;
static uint64_t g_size_slot;
static int g_persistent;
static int g_batch;
static int g_mixed_io;
static int g_transport_fd = -1;
static int g_transport_kq = -1;
static int g_atexit_registered;
static uint64_t g_transactions;
static uint64_t g_transport_opens;
static uint64_t g_elapsed_ticks;
static int (*g_power_guard)(void);

static struct clock_override_state {
    uint64_t hz_addr;
    uint32_t original_hz;
    uint32_t accelerated_hz;
    int enabled;
    int active;
    int restored;
    int phase_seen;
    int atexit_registered;
    uint64_t phase_ticks;
    uint64_t override_count;
} g_clock_override;

static uint32_t kread32(uint64_t address) {
    uint32_t value = 0;
    (void)kernel_copyout(address, &value, sizeof(value));
    return value;
}

static uint64_t kread64(uint64_t address) {
    uint64_t value = 0;
    (void)kernel_copyout(address, &value, sizeof(value));
    return value;
}

static int kread32_checked(uint64_t address, uint32_t *value) {
    if (!value)
        return -1;
    *value = 0;
    return kernel_copyout(address, value, sizeof(*value));
}

static int kread64_checked(uint64_t address, uint64_t *value) {
    if (!value)
        return -1;
    *value = 0;
    return kernel_copyout(address, value, sizeof(*value));
}

static int kwrite32(uint64_t address, uint32_t value) {
    return kernel_copyin(&value, address, sizeof(value));
}

static int restore_clock_override(const char *reason) {
    if (!g_clock_override.active)
        return 0;

    for (int attempt = 0; attempt < 3; attempt++) {
        if (kernel_copyin(&g_clock_override.original_hz,
                          g_clock_override.hz_addr,
                          sizeof(g_clock_override.original_hz)) == 0 &&
            kread32(g_clock_override.hz_addr) ==
                g_clock_override.original_hz) {
            g_clock_override.active = 0;
            g_clock_override.restored = 1;
            return 0;
        }
    }
    printf("[!] CRITICAL: failed to restore global hz at 0x%016lx (%s)\n",
           (unsigned long)g_clock_override.hz_addr, reason);
    klog_printf("[PPR] CRITICAL: global hz restore failed (%s)\n", reason);
    return -1;
}

static void restore_clock_override_atexit(void) {
    (void)restore_clock_override("atexit");
}

/* The phase5 worker computes its timeout as hz * tick_sbt.  KCODE is XOM,
 * so find the standard clock globals from their unique value relationship
 * in readable KDATA.  Refuse to write unless the complete scan yields one
 * exact tick/tick_sbt/hz layout. */
#define KERNEL_DATA_SCAN_CHUNK 0x10000U
#define KERNEL_DATA_SCAN_CAP   0x04000000ULL
#define KERNEL_CLOCK_MAX_PAIRS 64U
#define KERNEL_CLOCK_SCAN_PREFIX 16U
#define KERNEL_CLOCK_TICK_BACK   8U
#define KERNEL_CLOCK_EARLY_HZ_BACK 0x0cU
#define KERNEL_CLOCK_LATE_HZ_OFF 0x70ULL

struct kernel_clock_pair {
    uint64_t tick_sbt_addr;
    uint64_t hz_addr;
    uint64_t tick_sbt;
    uint32_t hz;
};

static int locate_kernel_clock_pair(struct kernel_clock_pair *out) {
    uint64_t start = (uint64_t)KERNEL_ADDRESS_DATA_BASE;
    uint64_t end_anchor = (uint64_t)KERNEL_ADDRESS_ROOTVNODE;
    uint64_t span = end_anchor > start ? end_anchor - start + 0x100000ULL : 0;
    if (!out)
        return -1;
    if (span == 0 || span > KERNEL_DATA_SCAN_CAP)
        span = KERNEL_DATA_SCAN_CAP;

    printf("[*] dynamic clock scan: data=0x%016lx span=0x%lx\n",
           (unsigned long)start, (unsigned long)span);

    const size_t suffix = KERNEL_CLOCK_LATE_HZ_OFF + sizeof(uint32_t);
    uint8_t *raw = malloc(KERNEL_DATA_SCAN_CHUNK +
                          KERNEL_CLOCK_SCAN_PREFIX + suffix);
    if (!raw)
        return -1;

    size_t equation_count = 0;
    size_t pair_count = 0;
    struct kernel_clock_pair candidate = {0};
    for (uint64_t offset = 0; offset < span;
         offset += KERNEL_DATA_SCAN_CHUNK) {
        size_t core = KERNEL_DATA_SCAN_CHUNK;
        if (span - offset < core)
            core = (size_t)(span - offset);
        size_t prefix = offset >= KERNEL_CLOCK_SCAN_PREFIX ?
                        KERNEL_CLOCK_SCAN_PREFIX : 0;
        size_t tail = (size_t)(span - offset - core);
        if (tail > suffix)
            tail = suffix;
        size_t length = prefix + core + tail;
        uint64_t read_address = start + offset - prefix;
        if (kernel_copyout(read_address, raw, length) != 0) {
            printf("[!] kernel data read failed at +0x%lx\n",
                   (unsigned long)offset);
            free(raw);
            return -1;
        }

        for (size_t position = 0;
             position + sizeof(uint64_t) <= core; position += 8) {
            size_t index = prefix + position;
            if (index < KERNEL_CLOCK_SCAN_PREFIX ||
                index + suffix > length)
                continue;

            uint64_t tick_sbt = 0;
            uint32_t tick_usec = 0;
            uint32_t early_hz = 0;
            uint32_t late_hz = 0;
            memcpy(&tick_sbt, raw + index, sizeof(tick_sbt));
            memcpy(&tick_usec, raw + index - KERNEL_CLOCK_TICK_BACK,
                   sizeof(tick_usec));
            memcpy(&early_hz,
                   raw + index - KERNEL_CLOCK_EARLY_HZ_BACK,
                   sizeof(early_hz));
            memcpy(&late_hz, raw + index + KERNEL_CLOCK_LATE_HZ_OFF,
                   sizeof(late_hz));

            const uint32_t hz_values[2] = {early_hz, late_hz};
            const int64_t hz_offsets[2] = {
                -(int64_t)KERNEL_CLOCK_EARLY_HZ_BACK,
                (int64_t)KERNEL_CLOCK_LATE_HZ_OFF,
            };
            const char *layout_names[2] = {"early", "late"};
            for (size_t layout = 0; layout < 2; layout++) {
                uint32_t hz = hz_values[layout];
                if (hz < 10 || hz > 10000 ||
                    tick_sbt != 0x100000000ULL / hz)
                    continue;
                equation_count++;
                if (tick_usec != 1000000U / hz)
                    continue;

                uint64_t tick_sbt_address = start + offset + position;
                uint64_t hz_address =
                    (uint64_t)((int64_t)tick_sbt_address +
                               hz_offsets[layout]);
                printf("[*] %s clock signature: tick=%u "
                       "tick_sbt@0x%016lx hz@0x%016lx hz=%u\n",
                       layout_names[layout], tick_usec,
                       (unsigned long)tick_sbt_address,
                       (unsigned long)hz_address, hz);
                candidate.tick_sbt_addr = tick_sbt_address;
                candidate.hz_addr = hz_address;
                candidate.tick_sbt = tick_sbt;
                candidate.hz = hz;
                if (++pair_count > KERNEL_CLOCK_MAX_PAIRS) {
                    puts("[!] too many clock pairs; refusing ambiguity");
                    free(raw);
                    return -1;
                }
            }
        }
    }
    free(raw);

    printf("[*] tick_sbt/hz equation candidates: %lu\n",
           (unsigned long)equation_count);
    if (pair_count != 1) {
        printf("[!] dynamic clock locator is ambiguous: %lu pairs\n",
               (unsigned long)pair_count);
        return -1;
    }
    *out = candidate;
    return 0;
}

int a53_transport_enable_time_acceleration(void) {
    struct kernel_clock_pair pair;
    if (!g_initialized || locate_kernel_clock_pair(&pair) != 0)
        return -1;
    if (pair.hz < 100 || kread32(pair.hz_addr) != pair.hz ||
        kread64(pair.tick_sbt_addr) != pair.tick_sbt) {
        puts("[!] clock-pair validation changed; refusing time acceleration");
        return -1;
    }

    memset(&g_clock_override, 0, sizeof(g_clock_override));
    g_clock_override.hz_addr = pair.hz_addr;
    g_clock_override.original_hz = pair.hz;
    g_clock_override.accelerated_hz = pair.hz / 10;
    if (atexit(restore_clock_override_atexit) != 0) {
        puts("[!] could not register global-hz restore handler");
        return -1;
    }
    g_clock_override.atexit_registered = 1;
    g_clock_override.enabled = 1;
    printf("[+] phase5 time acceleration armed dynamically: hz %u -> %u "
           "during each kick only\n",
           pair.hz, g_clock_override.accelerated_hz);
    return 0;
}

static uint64_t swap_auth(uint64_t auth_id) {
    pid_t pid = getpid();
    uint64_t original = kernel_get_ucred_authid(pid);
    kernel_set_ucred_authid(pid, auth_id);
    return original;
}

static uint32_t access_size(uint64_t address, uint32_t length) {
    uint32_t combined = (uint32_t)address | length;
    if (combined & 1U) return 1;
    if (combined & 2U) return 3;
    if (combined & 4U) return 4;
    if (combined & 8U) return 5;
    return 6;
}

static uint32_t round_up_8(uint32_t value) {
    return (value + 7U) & ~7U;
}

static int find_mp4_device(void) {
    uint64_t bus = KERNEL_ADDRESS_BUS_DATA_DEVICES;
    if (!bus) {
        puts("[!] kernel bus_data_devices address is unavailable");
        return -1;
    }
    uint64_t device = kread64(bus);
    if (!device) {
        puts("[!] bus_data_devices is empty");
        return -1;
    }

    while (device) {
        uint64_t nameunit = kread64(device + 0x58);
        if (nameunit && kread32(nameunit) == 0x3034706dU) {
            g_mp4sc = kread64(device + 0x88);
            uint64_t bar2_resource = kread64(g_mp4sc + 0x18);
            g_zcn_bar2 = kread64(bar2_resource + 0x10);
            printf("[+] mp4sc=0x%lx zcn_bar2=0x%lx\n",
                   (unsigned long)g_mp4sc,
                   (unsigned long)g_zcn_bar2);
            return 0;
        }
        device = kread64(device + 0x18);
    }
    puts("[!] MP4 device was not found");
    return -1;
}

static int initialize_mailbox(void) {
    const char *path = "/dev/mp4/dump";
    uint64_t original_auth = swap_auth(SYSCORE_AUTH_ID);
    int result = -1;
    int fd = -1;
    int kq = -1;
    int started = 0;
    uint8_t mp4_snapshot[0x1000];

    if (revoke(path) < 0)
        printf("[!] revoke %s: %s (errno=%d)\n",
               path, strerror(errno), errno);

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        printf("[!] open %s: %s (errno=%d)\n",
               path, strerror(errno), errno);
        goto out;
    }
    kq = kqueue();
    if (kq < 0) {
        perror("[!] kqueue");
        goto out;
    }

    struct kevent event;
    EV_SET(&event, fd, EVFILT_READ, EV_ADD, 0, 0, NULL);
    if (kevent(kq, &event, 1, NULL, 0, NULL) < 0) {
        perror("[!] kevent register");
        goto out;
    }

    uint32_t context[6] = {12, 0, 0, 0, 0, 0};
    if (ioctl(fd, IOCTL_START, context) < 0) {
        perror("[!] ioctl START");
        goto out;
    }
    started = 1;

    struct timespec timeout = {10, 0};
    int events = kevent(kq, NULL, 0, &event, 1, &timeout);
    if (events <= 0 || (event.flags & EV_ERROR) != 0) {
        if (events == 0)
            puts("[!] timeout waiting for the initial coredump event");
        else if (events < 0)
            perror("[!] initial coredump event");
        else
            printf("[!] initial coredump event error: %ld\n",
                   (long)event.data);
        goto out;
    }
    if (ioctl(fd, IOCTL_ALTER_STATE, context) < 0) {
        perror("[!] ioctl ALTER_STATE");
        goto out;
    }
    uint32_t reported_state = context[4] & 0xffffU;
    uint32_t reported_phases = reported_state & ~0x10U;
    int reported_state_valid = reported_phases == 3U ||
                               reported_phases == 7U ||
                               reported_phases == 0xfU;

    /* One coherent pre-FINISH snapshot replaces the overlapping scalar
     * kernel_copyout calls used to locate state/flags. */
    if (kernel_copyout(g_mp4sc, mp4_snapshot, sizeof(mp4_snapshot)) != 0) {
        puts("[!] failed to snapshot the MP4 mailbox state");
        goto out;
    }

    uint64_t state_slot = 0;
    uint64_t flags_slot = 0;
    uint64_t buffer_slot = 0;
    for (uint32_t offset = 4; offset < 0x1000; offset += 4) {
        uint32_t flags = 0;
        memcpy(&flags, mp4_snapshot + offset, sizeof(flags));
        if ((flags & 0xffffU) != 0x212U)
            continue;

        /* Early kernels keep state immediately before flags; newer kernels
         * inserted one dword between them.  At this handshake boundary the
         * completed phase bits form a prefix (3, 7, or 15).  Prefer the
         * adjacent early layout so an accumulated coredump count at -8
         * cannot be mistaken for state on 1.x-2.x. */
        const uint32_t state_distances[2] = {4, 8};
        for (size_t layout = 0; layout < 2; layout++) {
            uint32_t distance = state_distances[layout];
            if (offset < distance)
                continue;
            uint32_t state = 0;
            memcpy(&state, mp4_snapshot + offset - distance,
                   sizeof(state));
            uint32_t phases = (state & 0xffffU) & ~0x10U;
            if ((reported_state_valid &&
                 (state & 0xffffU) != reported_state) ||
                (!reported_state_valid && phases != 3U &&
                 phases != 7U && phases != 0xfU))
                continue;
            state_slot = g_mp4sc + offset - distance;
            flags_slot = g_mp4sc + offset;
            printf("[*] mailbox layout: %s state/flags spacing (%u bytes)\n",
                   distance == 4 ? "early" : "late", distance);
            break;
        }
        if (state_slot)
            break;
    }

    uint32_t finish[2] = {8, 0};
    if (ioctl(fd, IOCTL_FINISH, finish) < 0) {
        perror("[!] ioctl FINISH");
        goto out;
    }
    started = 0;

    if (!state_slot || !flags_slot) {
        puts("[!] coredump mailbox state/flags were not found");
        goto out;
    }
    /* FINISH publishes the buffer/IOMMU/size slots.  Preserve the original
     * ordering with a second coherent snapshot instead of reusing the
     * pre-FINISH state image. */
    if (kernel_copyout(g_mp4sc, mp4_snapshot, sizeof(mp4_snapshot)) != 0) {
        puts("[!] failed to snapshot the finalized MP4 mailbox state");
        goto out;
    }
    uint64_t first = (flags_slot - g_mp4sc) / 8;
    for (uint64_t index = first; index < 0x1000 / 8; index++) {
        uint64_t slot = g_mp4sc + index * 8;
        uint64_t value = 0;
        memcpy(&value, mp4_snapshot + index * 8, sizeof(value));
        if ((value >> 40) == 0xffffffULL &&
            (value & 0xfffffULL) == 0) {
            buffer_slot = slot;
            break;
        }
    }
    if (!buffer_slot || buffer_slot < g_mp4sc + 16) {
        puts("[!] coredump mailbox buffer was not found");
        goto out;
    }

    uint64_t buffer = 0;
    if (kread64_checked(buffer_slot, &buffer) != 0) {
        puts("[!] failed to recheck the coredump buffer pointer");
        goto out;
    }
    if ((buffer >> 40) != 0xffffffULL || (buffer & 0xfffffULL) != 0) {
        printf("[!] invalid coredump buffer pointer: 0x%lx\n",
               (unsigned long)buffer);
        goto out;
    }

    g_state_addr = state_slot;
    g_flags_addr = flags_slot;
    g_buffer_slot = buffer_slot;
    g_iommu_slot = buffer_slot - 8;
    g_size_slot = buffer_slot - 16;
    g_initialized = 1;
    printf("[+] state=0x%lx flags=0x%lx buf=0x%lx\n",
           (unsigned long)g_state_addr,
           (unsigned long)g_flags_addr,
           (unsigned long)g_buffer_slot);
    result = 0;

out:
    if (started && fd >= 0) {
        uint32_t finish_failed[2] = {8, 0};
        (void)ioctl(fd, IOCTL_FINISH, finish_failed);
    }
    if (kq >= 0)
        close(kq);
    if (fd >= 0)
        close(fd);
    swap_auth(original_auth);
    return result;
}

static int open_transport_pair(int *fd_out, int *kq_out) {
    const char *path = "/dev/mp4/dump";
    *fd_out = -1;
    *kq_out = -1;

    if (revoke(path) < 0)
        printf("[!] revoke %s: %s (errno=%d)\n",
               path, strerror(errno), errno);
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        printf("[!] open %s: %s (errno=%d)\n",
               path, strerror(errno), errno);
        return -1;
    }
    int kq = kqueue();
    if (kq < 0) {
        perror("[!] kqueue");
        close(fd);
        return -1;
    }
    struct kevent event;
    EV_SET(&event, fd, EVFILT_READ, EV_ADD, 0, 0, NULL);
    if (kevent(kq, &event, 1, NULL, 0, NULL) < 0) {
        perror("[!] kevent register");
        close(kq);
        close(fd);
        return -1;
    }
    *fd_out = fd;
    *kq_out = kq;
    ++g_transport_opens;
    return 0;
}

void a53_transport_shutdown(void) {
    (void)restore_clock_override("transport shutdown");
    if (g_transport_kq < 0 && g_transport_fd < 0)
        return;
    uint64_t original_auth = swap_auth(SYSCORE_AUTH_ID);
    if (g_transport_kq >= 0) {
        close(g_transport_kq);
        g_transport_kq = -1;
    }
    if (g_transport_fd >= 0) {
        close(g_transport_fd);
        g_transport_fd = -1;
    }
    swap_auth(original_auth);
}

static int open_persistent_transport(void) {
    if (g_transport_fd >= 0 && g_transport_kq >= 0)
        return 0;
    if (open_transport_pair(&g_transport_fd, &g_transport_kq) != 0)
        return -1;
    if (!g_atexit_registered) {
        if (atexit(a53_transport_shutdown) != 0) {
            a53_transport_shutdown();
            return -1;
        }
        g_atexit_registered = 1;
    }
    return 0;
}

static int send_packet(struct deci5s_hdr *packet, uint32_t packet_length,
                       const void *data, uint32_t data_length,
                       struct sdbgp_packet_result *response) {
    if (g_power_guard && !g_power_guard()) {
        klog_printf("[PPR] resume: power state changed; transport refused\n");
        return -1;
    }
    uint64_t started = sceKernelReadTsc();
    uint64_t original_auth = swap_auth(SYSCORE_AUTH_ID);
    int result = -1;
    int fd = -1;
    int kq = -1;
    int owns_pair = 0;
    uint64_t buffer = 0;
    uint64_t iommu = 0;
    uint64_t raw_buffer_size = 0;
    uint32_t request = 0;
    int clock_error = 0;
    ++g_transactions;
    if (response)
        memset(response, 0, sizeof(*response));

    if (!packet || packet_length < data_length ||
        packet_length - data_length < sizeof(struct deci5s_cmd_hdr) ||
        (data_length && !data))
        goto out;

    if (g_persistent) {
        if (open_persistent_transport() != 0)
            goto out;
        fd = g_transport_fd;
        kq = g_transport_kq;
    } else {
        if (open_transport_pair(&fd, &kq) != 0)
            goto out;
        owns_pair = 1;
    }

    if (kwrite32(g_state_addr, ~0x10U) != 0 ||
        kwrite32(g_flags_addr, 0x210U) != 0) {
        puts("[!] failed to prepare the coredump mailbox state");
        goto out;
    }
    /* Validate the complete aperture before copying any caller-controlled
     * packet bytes into it. */
    if (kread64_checked(g_buffer_slot, &buffer) != 0 || !buffer ||
        (buffer >> 40) != 0xffffffULL || (buffer & 0xfffffULL) != 0 ||
        packet_length > UINT64_MAX - buffer ||
        kread64_checked(g_iommu_slot, &iommu) != 0 ||
        kread64_checked(g_size_slot, &raw_buffer_size) != 0 ||
        raw_buffer_size > UINT32_MAX || raw_buffer_size < packet_length) {
        puts("[!] invalid coredump mailbox buffer/IOMMU/size state");
        goto out;
    }

    if (kread32_checked(g_mp4sc + 0x160, &request) != 0) {
        puts("[!] failed to read the coredump request sequence");
        goto out;
    }
    request++;

    /* Canonical A53 copies the outer SDBGP sequence_no into the response.
     * Bind every parser to the current kernel request generation so bytes
     * left after an earlier, longer response cannot satisfy readback. */
    packet->timestamp = sceKernelReadTsc();
    ((struct deci5s_cmd_hdr *)packet)->sequence_no = request;

    if (kernel_copyin(packet, buffer, packet_length - data_length) != 0) {
        puts("[!] failed to copy the DECI5S packet into the mailbox");
        goto out;
    }
    if (data_length &&
        kernel_copyin(data, buffer + packet_length - data_length,
                      data_length) != 0) {
        puts("[!] failed to copy DECI5S inline data into the mailbox");
        goto out;
    }
    uint32_t buffer_size = (uint32_t)raw_buffer_size;
    if (kwrite32(g_zcn_bar2 + 0xf7000, (uint32_t)(iommu >> 32)) != 0 ||
        kwrite32(g_zcn_bar2 + 0xf8000, (uint32_t)iommu) != 0 ||
        kwrite32(g_zcn_bar2 + 0xf9000, buffer_size) != 0) {
        puts("[!] failed to program the coredump mailbox aperture");
        goto out;
    }

    if (kwrite32(g_mp4sc + 0x160, request) != 0 ||
        kwrite32(g_mp4sc + 0x164, MP4_COREDUMP_CMD) != 0) {
        puts("[!] failed to publish the coredump mailbox request");
        goto out;
    }

    if (g_clock_override.enabled) {
        if (kread32(g_clock_override.hz_addr) !=
            g_clock_override.original_hz) {
            puts("[!] global hz changed before accelerated kick; refusing write");
            goto out;
        }
        /* Mark active first so every subsequent error path restores hz. */
        g_clock_override.active = 1;
        g_clock_override.restored = 0;
        g_clock_override.phase_seen = 0;
        g_clock_override.phase_ticks = 0;
        if (kernel_copyin(&g_clock_override.accelerated_hz,
                          g_clock_override.hz_addr,
                          sizeof(g_clock_override.accelerated_hz)) != 0 ||
            kread32(g_clock_override.hz_addr) !=
                g_clock_override.accelerated_hz) {
            puts("[!] global hz override write/readback failed");
            (void)restore_clock_override("write failure");
            goto out;
        }
        g_clock_override.override_count++;
    }

    uint64_t kick_started = sceKernelReadTsc();
    if (kwrite32(g_zcn_bar2 + 0xf6000, MP4_COREDUMP_CMD) != 0) {
        puts("[!] failed to trigger the coredump mailbox request");
        goto out;
    }

    struct kevent event;
    int events = 0;
    if (g_clock_override.active) {
        struct timespec no_wait = {0, 0};
        for (uint32_t iteration = 0; iteration < 15000; iteration++) {
            if (g_clock_override.active &&
                (kread32(g_state_addr) & 0x10U) != 0) {
                g_clock_override.phase_seen = 1;
                g_clock_override.phase_ticks =
                    sceKernelReadTsc() - kick_started;
                if (restore_clock_override("phase5 transition") != 0) {
                    clock_error = 1;
                    break;
                }
            }
            events = kevent(kq, NULL, 0, &event, 1, &no_wait);
            if (events != 0)
                break;
            usleep(1000);
        }
    } else {
        struct timespec timeout = {15, 0};
        events = kevent(kq, NULL, 0, &event, 1, &timeout);
    }
    if (clock_error) {
        result = -4;
    } else if (events <= 0) {
        if (events == 0)
            puts("[!] DECI5S response timeout");
        else
            perror("[!] DECI5S response");
        result = -2;
    } else if ((event.flags & EV_ERROR) != 0) {
        printf("[!] DECI5S kqueue event error: %ld\n", (long)event.data);
        result = -2;
    } else {
        result = 0;
    }
    if (result == 0 && g_clock_override.enabled &&
        !g_clock_override.phase_seen) {
        puts("[!] accelerated kick completed without observing phase5; refusing unverified timing");
        result = -4;
    }

out:
    /* Every successfully opened dump descriptor gets exactly one FINISH,
     * including pre-trigger copy/MMIO failures.  Otherwise an optimization
     * failure could leave the mailbox session itself wedged. */
    if (fd >= 0) {
        uint32_t finish[2] = {8, 0};
        if (ioctl(fd, IOCTL_FINISH, finish) < 0) {
            perror("[!] ioctl FINISH");
            if (result == 0)
                result = -3;
        }
    }
    if (g_clock_override.active &&
        restore_clock_override("send cleanup") != 0)
        result = -4;
    if (owns_pair) {
        if (kq >= 0)
            close(kq);
        if (fd >= 0)
            close(fd);
    } else if (result != 0 && g_persistent) {
        a53_transport_shutdown();
    }
    swap_auth(original_auth);
    g_elapsed_ticks += sceKernelReadTsc() - started;
    if (result == 0 && response) {
        response->buffer = buffer;
        response->buffer_size = (uint32_t)raw_buffer_size;
        response->sequence_no = request;
        /* The MP4 dump transport appends the response directly after the
         * complete request packet.  Keeping this boundary avoids treating
         * an older response left elsewhere in the aperture as current. */
        response->response_offset = packet_length;
    }
    return result;
}

static void initialize_outer(struct deci5s_cmd_hdr *header,
                             uint32_t total_size, uint8_t count) {
    memset(header, 0, sizeof(*header));
    header->header.magic = DECI5S_MAGIC;
    header->header.self_size = sizeof(struct deci5s_hdr);
    header->header.packet_size = total_size;
    header->header.src = DECI5S_SRC_KERNEL;
    header->header.dst = DECI5S_DST_MP4;
    header->header.protocol_id = DECI5S_PROTO_SDBGP;
    header->dcmp = DECI5S_DCMP;
    /* SceDeci5sSdbgpHeader::total_size starts at the SDBGP header, not at
     * the outer DECI5S header.  The legacy 0x50 constant is correct only for
     * the 0x78-byte scalar READ_MEMORY request; GET_CONF is 0x60 bytes and
     * therefore requires 0x38 here. */
    header->code = total_size - sizeof(struct deci5s_hdr);
    header->num_commands = count;
}

static int read_memory(uint64_t address, void *destination, uint32_t size) {
    struct {
        struct deci5s_cmd_hdr header;
        struct { uint32_t self, total, type, pad[4], args; } command;
        struct deci5s_mem_arg argument;
    } packet;
    if (!g_initialized || !destination || size == 0 || size > INT_MAX)
        return -1;

    memset(&packet, 0, sizeof(packet));
    initialize_outer(&packet.header, sizeof(packet), 1);
    packet.command.self = sizeof(packet.command);
    packet.command.total = sizeof(packet.command) + sizeof(packet.argument);
    packet.command.type = SDBGP_READ_MEMORY;
    packet.command.args = 1;
    packet.argument.self_size = sizeof(packet.argument);
    packet.argument.access_size = access_size(address, size);
    packet.argument.mem_type = MEM_TYPE_PA_TO_EL3;
    packet.argument.addr = address;
    packet.argument.size = size;

    struct sdbgp_packet_result response;
    if (send_packet(&packet.header.header, sizeof(packet), NULL, 0,
                    &response) != 0)
        return -1;

    size_t scan_size = response.buffer_size;
    if (scan_size > SDBGP_BATCH_SCAN_CAP)
        scan_size = SDBGP_BATCH_SCAN_CAP;
    uint8_t *raw = calloc(1, scan_size);
    if (!raw)
        return -1;
    int parsed = kernel_copyout(response.buffer, raw, scan_size) == 0 &&
        parse_read_response(raw, scan_size, response.response_offset,
                            response.sequence_no, 1, 0,
                            address, size, destination) == 0;
    free(raw);
    if (!parsed) {
        printf("[!] A53 read 0x%lx response validation failed\n",
               (unsigned long)address);
        return -1;
    }
    return (int)size;
}

static int write_memory(uint64_t address, const void *source, uint32_t size) {
    struct scalar_write_prefix {
        struct deci5s_cmd_hdr header;
        struct { uint32_t self, total, type, pad[5], args, padding; } command;
        struct deci5s_mem_arg argument;
    };
    if (!g_initialized || !source || size == 0 ||
        size > SDBGP_MAX_WRITE_CHUNK)
        return -1;

    uint32_t inline_size = round_up_8(size);
    uint32_t command_size = sizeof(((struct scalar_write_prefix *)0)->command) +
                            sizeof(struct deci5s_mem_arg) + inline_size;
    uint32_t total = sizeof(struct deci5s_cmd_hdr) + command_size;
    if ((uint64_t)total > kread64(g_size_slot)) {
        puts("[!] SDBGP write packet exceeds the mailbox buffer");
        return -1;
    }

    uint8_t *raw = calloc(1, total);
    if (!raw)
        return -1;
    struct scalar_write_prefix *packet =
        (struct scalar_write_prefix *)raw;
    initialize_outer(&packet->header, total, 1);
    packet->command.self = sizeof(packet->command);
    packet->command.total = command_size;
    packet->command.type = SDBGP_WRITE_MEMORY;
    packet->command.args = 1;
    packet->argument.self_size = sizeof(packet->argument);
    packet->argument.access_size = access_size(address, size);
    packet->argument.mem_type = MEM_TYPE_PA_TO_EL3;
    packet->argument.addr = address;
    packet->argument.size = size;
    memcpy(raw + sizeof(*packet), source, size);

    int result = send_packet(&packet->header.header, total, NULL, 0, NULL);
    free(raw);
    return result;
}

static int locate_sdbgp_response(const uint8_t *raw, size_t raw_size,
                                 size_t response_offset,
                                 uint32_t sequence_no,
                                 uint32_t response_command_count,
                                 struct sdbgp_response_view *view) {
    if (!raw || !view || response_command_count == 0 ||
        response_command_count > SDBGP_BATCH_READ_MAX ||
        response_offset > raw_size ||
        sizeof(struct deci5s_cmd_hdr) > raw_size - response_offset)
        return -1;

    /* /dev/mp4/dump retains the request at the start of the aperture and
     * appends its response at the request-size boundary.  Validate that one
     * current boundary instead of scanning stale tail bytes for lookalikes. */
    struct deci5s_cmd_hdr outer;
    uint32_t command_count = 0;
    memcpy(&outer, raw + response_offset, sizeof(outer));
    memcpy(&command_count, &outer.num_commands, sizeof(command_count));

    if (outer.header.magic != DECI5S_MAGIC ||
        outer.header.self_size != sizeof(struct deci5s_hdr) ||
        outer.header.src != DECI5S_DST_MP4 ||
        outer.header.dst != DECI5S_SRC_KERNEL ||
        outer.header.protocol_id != DECI5S_PROTO_SDBGP ||
        outer.header.packet_size < sizeof(struct deci5s_cmd_hdr) ||
        outer.header.packet_size > raw_size - response_offset ||
        outer.dcmp != DECI5S_DCMP ||
        outer.code != outer.header.packet_size - sizeof(struct deci5s_hdr) ||
        outer.sequence_no != sequence_no || outer.packet_no != 0 ||
        outer.attr != 1 || command_count != response_command_count)
        return -1;

    size_t packet_end = response_offset + outer.header.packet_size;
    size_t command_offset = response_offset + sizeof(outer);
    uint32_t seen_commands = 0;
    for (uint32_t i = 0; i < command_count; i++) {
        struct sdbgp_command command;
        if (command_offset + sizeof(command) > packet_end)
            return -1;
        memcpy(&command, raw + command_offset, sizeof(command));
        if (command.self_size < sizeof(command) ||
            command.total_size < command.self_size ||
            command.total_size > packet_end - command_offset ||
            command.command_no >= command_count ||
            (seen_commands & (1U << command.command_no)) != 0)
            return -1;
        seen_commands |= 1U << command.command_no;
        command_offset += command.total_size;
    }
    if (command_offset != packet_end ||
        seen_commands != ((1U << command_count) - 1U))
        return -1;

    view->packet_offset = response_offset;
    view->commands_offset = response_offset + sizeof(outer);
    view->packet_end = packet_end;
    view->command_count = command_count;
    return 0;
}

static int find_response_command(const uint8_t *raw,
                                 const struct sdbgp_response_view *view,
                                 uint32_t command_number,
                                 uint32_t expected_type,
                                 size_t *command_offset_out,
                                 struct sdbgp_command *command_out) {
    if (!raw || !view || !command_offset_out || !command_out ||
        command_number >= view->command_count)
        return -1;

    size_t command_offset = view->commands_offset;
    for (uint32_t i = 0; i < view->command_count; i++) {
        struct sdbgp_command command;
        memcpy(&command, raw + command_offset, sizeof(command));
        if (command.command_no == command_number) {
            if (command.type != expected_type)
                return -1;
            *command_offset_out = command_offset;
            *command_out = command;
            return 0;
        }
        command_offset += command.total_size;
    }
    return -1;
}

static int parse_read_response(const uint8_t *raw, size_t raw_size,
                               size_t response_offset,
                               uint32_t sequence_no,
                               uint32_t response_command_count,
                               uint32_t command_number,
                               uint64_t expected_address,
                               uint32_t expected_size, void *destination) {
    struct sdbgp_response_view view;
    struct sdbgp_command command;
    size_t command_offset = 0;
    if (!destination || expected_size == 0 ||
        locate_sdbgp_response(raw, raw_size, response_offset, sequence_no,
                              response_command_count, &view) != 0 ||
        find_response_command(raw, &view, command_number,
                              SDBGP_RES_READ_MEMORY, &command_offset,
                              &command) != 0)
        return -1;

    size_t result_offset = command_offset + command.self_size;
    struct sdbgp_read_result result;
    if (result_offset + sizeof(result) >
        command_offset + command.total_size)
        return -1;
    memcpy(&result, raw + result_offset, sizeof(result));
    if (result.self_size != sizeof(result) || result.status != 0 ||
        result.addr != expected_address || result.requested != expected_size ||
        result.transferred != expected_size)
        return -1;

    size_t data_offset = result_offset + sizeof(result);
    if (expected_size > command_offset + command.total_size - data_offset)
        return -1;
    memcpy(destination, raw + data_offset, expected_size);
    return 0;
}

static int read_many(void *context, const uint64_t *addresses,
                     const uint32_t *sizes, void *const *destinations,
                     uint32_t count) {
    (void)context;
    struct read_block {
        struct sdbgp_read_command command;
        struct deci5s_mem_arg argument;
    };
    if (!g_batch || !addresses || !sizes || !destinations || count == 0 ||
        count > SDBGP_BATCH_READ_MAX)
        return -1;
    for (uint32_t i = 0; i < count; i++) {
        if (!destinations[i] || sizes[i] == 0 || sizes[i] > 0x100U)
            return -1;
    }

    size_t packet_size = sizeof(struct deci5s_cmd_hdr) +
                         count * sizeof(struct read_block);
    uint64_t raw_mailbox_size = 0;
    if (kread64_checked(g_size_slot, &raw_mailbox_size) != 0 ||
        raw_mailbox_size > UINT32_MAX || packet_size > raw_mailbox_size)
        return -1;
    uint32_t mailbox_size = (uint32_t)raw_mailbox_size;
    size_t scan_size = mailbox_size;
    if (scan_size > SDBGP_BATCH_SCAN_CAP)
        scan_size = SDBGP_BATCH_SCAN_CAP;
    uint8_t *packet = calloc(1, packet_size);
    if (!packet)
        return -1;
    uint8_t *raw = calloc(1, scan_size);
    if (!raw) {
        free(packet);
        return -1;
    }
    struct deci5s_cmd_hdr *header = (struct deci5s_cmd_hdr *)packet;
    initialize_outer(header, (uint32_t)packet_size, (uint8_t)count);
    struct read_block *blocks = (struct read_block *)(header + 1);
    for (uint32_t i = 0; i < count; i++) {
        blocks[i].command.common.self_size = sizeof(blocks[i].command);
        blocks[i].command.common.total_size = sizeof(blocks[i]);
        blocks[i].command.common.type = SDBGP_READ_MEMORY;
        blocks[i].command.common.command_no = i;
        blocks[i].command.n_args = 1;
        blocks[i].argument.self_size = sizeof(blocks[i].argument);
        blocks[i].argument.access_size = access_size(addresses[i], sizes[i]);
        blocks[i].argument.mem_type = MEM_TYPE_PA_TO_EL3;
        blocks[i].argument.addr = addresses[i];
        blocks[i].argument.size = sizes[i];
    }
    struct sdbgp_packet_result response;
    int result = send_packet(&header->header, (uint32_t)packet_size, NULL, 0,
                             &response);
    free(packet);
    if (result != 0) {
        free(raw);
        return -1;
    }
    if (scan_size > response.buffer_size)
        scan_size = response.buffer_size;
    if (kernel_copyout(response.buffer, raw, scan_size) != 0) {
        free(raw);
        return -1;
    }
    for (uint32_t i = 0; i < count; i++) {
        if (parse_read_response(raw, scan_size, response.response_offset,
                                response.sequence_no, count, i, addresses[i],
                                sizes[i], destinations[i]) != 0) {
            free(raw);
            return -1;
        }
    }
    free(raw);
    return 0;
}

static int write_many_read_many(
        void *context, const uint64_t *addresses, const void *const *sources,
        const uint32_t *sizes, void *const *destinations, uint32_t count) {
    (void)context;
    struct read_block {
        struct sdbgp_read_command command;
        struct deci5s_mem_arg argument;
    };
    uint32_t write_sizes[SDBGP_MIXED_WRITE_MAX];

    if (!g_mixed_io || !addresses || !sources || !sizes || !destinations ||
        count == 0 || count > SDBGP_MIXED_WRITE_MAX)
        return -1;
    uint32_t packet_size = sizeof(struct deci5s_cmd_hdr) +
                           count * sizeof(struct read_block);
    for (uint32_t i = 0; i < count; i++) {
        if (!sources[i] || !destinations[i] || sizes[i] == 0 ||
            sizes[i] > SDBGP_MAX_WRITE_CHUNK ||
            sizes[i] > SDBGP_MIXED_ITEM_MAX)
            return -1;
        write_sizes[i] = sizeof(struct sdbgp_write_command) +
                         sizeof(struct deci5s_mem_arg) + round_up_8(sizes[i]);
        if (write_sizes[i] > UINT32_MAX - packet_size)
            return -1;
        packet_size += write_sizes[i];
    }
    uint64_t raw_mailbox_size = 0;
    if (kread64_checked(g_size_slot, &raw_mailbox_size) != 0 ||
        raw_mailbox_size > UINT32_MAX || packet_size > raw_mailbox_size)
        return -1;
    uint32_t mailbox_size = (uint32_t)raw_mailbox_size;
    size_t scan_size = mailbox_size;
    if (scan_size > SDBGP_BATCH_SCAN_CAP)
        scan_size = SDBGP_BATCH_SCAN_CAP;
    uint8_t *packet = calloc(1, packet_size);
    if (!packet)
        return -1;
    /* Allocate every fallible host buffer before the packet can mutate A53
     * memory.  After send_packet succeeds, only copyout/validation remains. */
    uint8_t *raw = calloc(1, scan_size);
    if (!raw) {
        free(packet);
        return -1;
    }
    struct deci5s_cmd_hdr *header = (struct deci5s_cmd_hdr *)packet;
    initialize_outer(header, packet_size, (uint8_t)(count * 2U));

    uint32_t offset = sizeof(*header);
    for (uint32_t i = 0; i < count; i++) {
        struct sdbgp_write_command *command =
            (struct sdbgp_write_command *)(packet + offset);
        struct deci5s_mem_arg *argument =
            (struct deci5s_mem_arg *)(packet + offset + sizeof(*command));
        command->common.self_size = sizeof(*command);
        command->common.total_size = write_sizes[i];
        command->common.type = SDBGP_WRITE_MEMORY;
        command->common.command_no = i;
        command->n_args = 1;
        argument->self_size = sizeof(*argument);
        argument->access_size = access_size(addresses[i], sizes[i]);
        argument->mem_type = MEM_TYPE_PA_TO_EL3;
        argument->addr = addresses[i];
        argument->size = sizes[i];
        memcpy(argument + 1, sources[i], sizes[i]);
        offset += write_sizes[i];
    }
    for (uint32_t i = 0; i < count; i++) {
        struct read_block *block = (struct read_block *)(packet + offset);
        block->command.common.self_size = sizeof(block->command);
        block->command.common.total_size = sizeof(*block);
        block->command.common.type = SDBGP_READ_MEMORY;
        block->command.common.command_no = i + count;
        block->command.n_args = 1;
        block->argument.self_size = sizeof(block->argument);
        block->argument.access_size = access_size(addresses[i], sizes[i]);
        block->argument.mem_type = MEM_TYPE_PA_TO_EL3;
        block->argument.addr = addresses[i];
        block->argument.size = sizes[i];
        offset += sizeof(*block);
    }

    struct sdbgp_packet_result response;
    int result = send_packet(&header->header, packet_size, NULL, 0, &response);
    free(packet);
    if (result != 0) {
        free(raw);
        return -1;
    }
    if (scan_size > response.buffer_size)
        scan_size = response.buffer_size;
    if (kernel_copyout(response.buffer, raw, scan_size) != 0) {
        free(raw);
        return -1;
    }
    for (uint32_t i = 0; i < count; i++) {
        if (parse_read_response(raw, scan_size, response.response_offset,
                                response.sequence_no, count * 2U, i + count,
                                addresses[i], sizes[i], destinations[i]) != 0) {
            free(raw);
            return -1;
        }
    }
    free(raw);
    return 0;
}

static int write_pair_read_pair(
        void *context, uint64_t address0, const void *source0, uint32_t size0,
        void *readback0, uint64_t address1, const void *source1,
        uint32_t size1, void *readback1) {
    const uint64_t addresses[2] = {address0, address1};
    const void *sources[2] = {source0, source1};
    const uint32_t sizes[2] = {size0, size1};
    void *destinations[2] = {readback0, readback1};
    return write_many_read_many(context, addresses, sources, sizes,
                                destinations, 2);
}

static int callback_read(void *context, uint64_t address, void *destination,
                         uint32_t size) {
    (void)context;
    return read_memory(address, destination, size);
}

static int callback_write(void *context, uint64_t address, const void *source,
                          uint32_t size) {
    (void)context;
    return write_memory(address, source, size);
}

static void callback_log(void *context, const char *message) {
    (void)context;
    puts(message);
}

static uint64_t callback_transactions(void *context) {
    (void)context;
    return g_transactions;
}

static uint64_t callback_opens(void *context) {
    (void)context;
    return g_transport_opens;
}

static uint64_t callback_ticks(void *context) {
    (void)context;
    return g_elapsed_ticks;
}

int a53_transport_initialize(const struct a53_transport_options *options) {
    if (!options || g_clock_override.active ||
        (g_power_guard && !g_power_guard()))
        return -1;
    /* A53 mailbox and clock addresses must be rediscovered after rest. */
    a53_transport_shutdown();
    g_initialized = 0;
    g_mp4sc = g_zcn_bar2 = 0;
    g_state_addr = g_flags_addr = g_buffer_slot = 0;
    g_iommu_slot = g_size_slot = 0;
    memset(&g_clock_override, 0, sizeof(g_clock_override));
    g_persistent = options->persistent;
    g_batch = options->batch;
    g_mixed_io = options->mixed_io;
    if (find_mp4_device() != 0)
        return -1;
    return initialize_mailbox();
}

void a53_transport_set_power_guard(int (*guard)(void)) {
    g_power_guard = guard;
}

int a53_transport_get_version(char *out, uint32_t out_size) {
    struct {
        struct deci5s_cmd_hdr header;
        struct { uint32_t self, total, type, pad[4], args; } command;
    } packet;
    if (!out || out_size < 2)
        return -1;
    memset(&packet, 0, sizeof(packet));
    initialize_outer(&packet.header, sizeof(packet), 1);
    packet.command.self = sizeof(packet.command);
    packet.command.total = sizeof(packet.command);
    packet.command.type = SDBGP_GET_CONF;
    struct sdbgp_packet_result response;
    if (send_packet(&packet.header.header, sizeof(packet), NULL, 0,
                    &response) != 0)
        return -1;

    memset(out, 0, out_size);
    size_t scan_size = response.buffer_size;
    if (scan_size > SDBGP_BATCH_SCAN_CAP)
        scan_size = SDBGP_BATCH_SCAN_CAP;
    uint8_t *raw = calloc(1, scan_size);
    if (!raw || kernel_copyout(response.buffer, raw, scan_size) != 0) {
        free(raw);
        puts("[!] GET_CONF response copyout failed");
        return -1;
    }

    struct sdbgp_response_view view;
    struct sdbgp_command command;
    size_t command_offset = 0;
    if (locate_sdbgp_response(raw, scan_size, response.response_offset,
                              response.sequence_no, 1, &view) != 0 ||
        find_response_command(raw, &view, 0, SDBGP_RES_GET_CONF,
                              &command_offset, &command) != 0) {
        free(raw);
        puts("[!] GET_CONF response envelope validation failed");
        return -1;
    }

    const char release_marker[] = "releases/";
    size_t text_begin = command_offset + command.self_size;
    size_t text_end = command_offset + command.total_size;
    size_t marker = text_begin;
    while (marker + sizeof(release_marker) - 1U <= text_end &&
           memcmp(raw + marker, release_marker,
                  sizeof(release_marker) - 1U) != 0)
        marker++;
    if (marker + sizeof(release_marker) - 1U > text_end) {
        free(raw);
        puts("[!] GET_CONF release string was not found in the current response");
        return -1;
    }

    size_t string_begin = marker;
    while (string_begin > text_begin && raw[string_begin - 1U] >= 0x20U &&
           raw[string_begin - 1U] <= 0x7eU)
        string_begin--;
    size_t string_end = marker + sizeof(release_marker) - 1U;
    while (string_end < text_end && raw[string_end] >= 0x20U &&
           raw[string_end] <= 0x7eU)
        string_end++;
    size_t string_size = string_end - string_begin;
    if (string_size >= out_size)
        string_size = out_size - 1U;
    memcpy(out, raw + string_begin, string_size);
    free(raw);
    return 0;
}

uint32_t a53_transport_parse_release(const char *version) {
    const char *release = strstr(version, "releases/");
    unsigned int major = 0;
    unsigned int minor = 0;
    if (!release || sscanf(release + 9, "%u.%u", &major, &minor) != 2 ||
        major > 99 || minor > 99)
        return 0;
    return ((major / 10) << 28) | ((major % 10) << 24) |
           ((minor / 10) << 20) | ((minor % 10) << 16);
}

int a53_transport_verify_and_enable_fast(
        const struct a53_transport_options *requested) {
    uint32_t baseline = 0;
    uint32_t batch_values[SDBGP_BATCH_READ_MAX] = {0};
    uint32_t persistent0 = 0;
    uint32_t persistent1 = 0;
    int need_batch;
    int combined_failed = 0;
    int batch_ok = 0;
    int persistent_ok = 0;

    if (!requested || !g_initialized)
        return -1;

    /* Probe optimized reads before exposing them to the mutating patch state
     * machine. This also guards the dynamically discovered 7.61/9.60 layout. */
    a53_transport_shutdown();
    g_persistent = 0;
    g_batch = 0;
    g_mixed_io = 0;

    if (!requested->persistent && !requested->batch && !requested->mixed_io)
        return 0;
    need_batch = requested->batch || requested->mixed_io;

    uint64_t addresses[SDBGP_BATCH_READ_MAX];
    uint32_t sizes[SDBGP_BATCH_READ_MAX];
    void *destinations[SDBGP_BATCH_READ_MAX];
    for (uint32_t i = 0; i < SDBGP_BATCH_READ_MAX; i++) {
        /* Repeat one immutable layout word: the purpose is to validate the
         * real 16-command envelope used by an 8-write/8-read mutation, not
         * to sample unrelated dynamic fields.  Passing 16 also covers the
         * 15-command action preflight. */
        addresses[i] = PPR_PATCH_LAYOUT_PA;
        sizes[i] = sizeof(batch_values[i]);
        destinations[i] = &batch_values[i];
    }

    if (requested->persistent) {
        /* The common fast payload needs persistent+batch together.  Use the
         * scalar baseline as request 1 and the batch comparison as request 2
         * on the same descriptor.  That verifies both features (including
         * their composition) with two transactions and one open. */
        g_persistent = 1;
        if (read_memory(PPR_PATCH_LAYOUT_PA, &baseline,
                        sizeof(baseline)) < 0) {
            puts("[!] fast transport probe: persistent scalar baseline read failed");
            goto done;
        }

        if (need_batch) {
            g_batch = 1;
            int batch_matches = read_many(
                NULL, addresses, sizes, destinations,
                SDBGP_BATCH_READ_MAX) == 0;
            for (uint32_t i = 0; batch_matches &&
                 i < SDBGP_BATCH_READ_MAX; i++)
                batch_matches = batch_values[i] == baseline;
            if (batch_matches) {
                persistent_ok = 1;
                batch_ok = 1;
            } else {
                /* A failed combined probe does not prove which feature
                 * failed.  Probe each independently only on this uncommon
                 * fallback path so a usable optimization is not discarded. */
                combined_failed = 1;
                puts("[!] fast transport probe: persistent batch mismatch; checking independent fallbacks");
                a53_transport_shutdown();
                g_persistent = 0;
                g_batch = 1;
                memset(batch_values, 0, sizeof(batch_values));
                batch_matches = read_many(
                    NULL, addresses, sizes, destinations,
                    SDBGP_BATCH_READ_MAX) == 0;
                for (uint32_t i = 0; batch_matches &&
                     i < SDBGP_BATCH_READ_MAX; i++)
                    batch_matches = batch_values[i] == baseline;
                if (batch_matches) {
                    batch_ok = 1;
                } else {
                    puts("[!] fast transport probe: batch read mismatch; disabling batch/mixed I/O");
                }

                g_batch = 0;
                g_persistent = 1;
                if (read_memory(PPR_PATCH_LAYOUT_PA, &persistent0,
                                sizeof(persistent0)) >= 0 &&
                    read_memory(PPR_PATCH_LAYOUT_PA, &persistent1,
                                sizeof(persistent1)) >= 0 &&
                    persistent0 == baseline && persistent1 == baseline) {
                    persistent_ok = 1;
                } else {
                    puts("[!] fast transport probe: persistent reuse mismatch; disabling persistent transport");
                }
            }
        } else {
            if (read_memory(PPR_PATCH_LAYOUT_PA, &persistent0,
                            sizeof(persistent0)) >= 0 &&
                persistent0 == baseline) {
                persistent_ok = 1;
            } else {
                puts("[!] fast transport probe: persistent reuse mismatch; disabling persistent transport");
            }
        }
    } else {
        if (read_memory(PPR_PATCH_LAYOUT_PA, &baseline,
                        sizeof(baseline)) < 0) {
            puts("[!] fast transport probe: scalar baseline read failed");
            goto done;
        }
        if (need_batch) {
            g_batch = 1;
            int batch_matches = read_many(
                NULL, addresses, sizes, destinations,
                SDBGP_BATCH_READ_MAX) == 0;
            for (uint32_t i = 0; batch_matches &&
                 i < SDBGP_BATCH_READ_MAX; i++)
                batch_matches = batch_values[i] == baseline;
            if (batch_matches) {
                batch_ok = 1;
            } else {
                puts("[!] fast transport probe: batch read mismatch; disabling batch/mixed I/O");
            }
        }
    }

done:
    if (combined_failed && persistent_ok && batch_ok) {
        /* Both features work independently, but their composition did not.
         * Prefer batching because it removes far more transactions; never
         * re-enable the failed persistent+batch combination. */
        puts("[!] fast transport probe: independent modes pass but composition failed; using batch without persistence");
        persistent_ok = 0;
    }
    /* Keep a successfully reused persistent descriptor for the patch itself;
     * this avoids another revoke/open/kqueue registration after the probe. */
    if (!persistent_ok)
        a53_transport_shutdown();
    g_batch = requested->batch && batch_ok;
    /* Do not issue a synthetic write merely to probe the mixed packet form:
     * that would violate the zero-write guarantee before patch-state
     * classification.  It uses the same validated multi-command envelope;
     * the local write/result ABI is compile-time asserted above, and the first
     * intended mutation includes its own exact readback. */
    g_mixed_io = requested->mixed_io && g_batch;
    g_persistent = requested->persistent && persistent_ok;

    /* The capability probe is not part of the patch operation statistics.
     * A verified persistent descriptor may already be open for request 3. */
    g_transactions = 0;
    g_transport_opens = 0;
    g_elapsed_ticks = 0;
    return ((requested->batch && !g_batch) ||
            (requested->mixed_io && !g_mixed_io) ||
            (requested->persistent && !persistent_ok)) ? 1 : 0;
}

void a53_transport_make_ppr(struct ppr_patch_transport *out, int fast_mode) {
    memset(out, 0, sizeof(*out));
    out->read = callback_read;
    out->write = callback_write;
    out->read_many = g_batch ? read_many : NULL;
    /* Keep production mutations within the packet shape proven by the
     * original fast path: two writes followed by their two exact readbacks.
     * The generic helper remains private so the pair adapter can share the
     * strict response parser, timeout handling and bounds checks without
     * exposing the newer 8-way mutation to the PPR state machine. */
    out->write_many_read_many = NULL;
    out->write_pair_read_pair = g_mixed_io ? write_pair_read_pair : NULL;
    out->log = callback_log;
    out->transaction_count = callback_transactions;
    out->transport_open_count = callback_opens;
    out->elapsed_ticks = callback_ticks;
    out->fast_mode = fast_mode;
    out->batch_enabled = g_batch;
    out->mixed_io_enabled = g_mixed_io;
}
