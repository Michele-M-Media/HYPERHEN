/* Injected into SceShellUI via elfldr_exec (see cr_hotkey_hook.c). Hooks
 * GamePad.GetData (L2+R3 hotkey) and OptionMenu/BootHelper (tile menu entry). */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <time.h>

#include <ps5/kernel.h>
#include <ps5/klog.h>

#include "cr_detour.h"
#include "cr_plog.h"
#include "cr_hotkey_signal.h"

#define SYS_DL_GET_LIST   0x217
#define SYS_DL_GET_INFO_2 0x2cd

#define MODULE_INFO_NAME_LENGTH 128
#define MODULE_INFO_SANDBOXED_PATH_LENGTH 1024
#define MODULE_INFO_MAX_SECTIONS 4
#define FINGERPRINT_LENGTH 20
#define MODULE_HANDLE_MAX 4096

typedef struct {
  uint64_t vaddr;
  uint32_t size;
  uint32_t prot;
} module_section_t;

/* matches SYS_dl_get_info_2's output layout; only filename/handle are used */
typedef struct {
  char filename[MODULE_INFO_NAME_LENGTH];
  uint64_t handle;
  uint8_t unknown0[32];
  uint64_t init;
  uint64_t fini;
  uint64_t eh_frame_hdr;
  uint64_t eh_frame_hdr_sz;
  uint64_t eh_frame;
  uint64_t eh_frame_sz;
  module_section_t sections[MODULE_INFO_MAX_SECTIONS];
  uint8_t unknown7[1176];
  uint8_t fingerprint[FINGERPRINT_LENGTH];
  uint32_t unknown8;
  char libname[MODULE_INFO_NAME_LENGTH];
  uint32_t unknown9;
  char sandboxed_path[MODULE_INFO_SANDBOXED_PATH_LENGTH];
  uint64_t sdk_version;
} module_info_t;

static int
find_module_handle(const char *module_name, int verbose) {
  pid_t pid = getpid();
  size_t num_handles = 0;

  syscall(SYS_DL_GET_LIST, pid, NULL, 0, &num_handles);
  if (!num_handles) {
    if (verbose) {
      cr_plog("hotkey_hook: no modules found in SceShellUI\n");
    }
    return 0;
  }

  /* Static buffer instead of malloc/free - a heap-decommit assertion in SceShellUI's
   * own allocator (seen on 4.03) fired around that pair. Old shells have 250+ modules. */
  static uintptr_t handles[MODULE_HANDLE_MAX];
  if (num_handles > MODULE_HANDLE_MAX) {
    cr_plog("hotkey_hook: %zu modules, more than the %d the list can hold\n", num_handles, MODULE_HANDLE_MAX);
    return 0;
  }
  syscall(SYS_DL_GET_LIST, pid, handles, num_handles, &num_handles);

  int found = 0;
  module_info_t info;
  for (size_t i = 0; i < num_handles && !found; i++) {
    memset(&info, 0, sizeof(info));
    syscall(SYS_DL_GET_INFO_2, pid, 1, handles[i], &info);
    if (!strcmp(info.filename, module_name)) {
      found = (int)info.handle;
    }
  }

  if (!found && verbose) {
    cr_plog("hotkey_hook: %s not found among %zu module(s); names containing \"mono\":\n", module_name, num_handles);
    int failures = 0;
    for (size_t i = 0; i < num_handles; i++) {
      memset(&info, 0, sizeof(info));
      int rc = (int)syscall(SYS_DL_GET_INFO_2, pid, 1, handles[i], &info);
      if (rc != 0 && ++failures > 5) {
        continue;
      }
      if (rc != 0 || strcasestr(info.filename, "mono")) {
        cr_plog("hotkey_hook:   handle[%zu]=%lu rc=%d name=%.60s lib=%.60s\n", i, (unsigned long)handles[i], rc,
                info.filename, info.libname);
      }
    }
  }

  return found;
}

/* opaque; only pointers to these exist here, real layout is in libmonosgen */
typedef struct MonoDomain  MonoDomain;
typedef struct MonoAssembly MonoAssembly;
typedef struct MonoImage   MonoImage;
typedef struct MonoClass   MonoClass;
typedef struct MonoMethod  MonoMethod;
typedef struct MonoThread  MonoThread;
typedef struct MonoObject  MonoObject;
typedef struct MonoString  MonoString;

static MonoDomain *  (*p_mono_get_root_domain)(void);
static MonoThread *  (*p_mono_thread_attach)(MonoDomain *);
static MonoAssembly *(*p_mono_domain_assembly_open)(MonoDomain *, const char *);
static MonoImage *   (*p_mono_assembly_get_image)(MonoAssembly *);
static MonoClass *   (*p_mono_class_from_name)(MonoImage *, const char *, const char *);
static MonoMethod *  (*p_mono_class_get_method_from_name)(MonoClass *, const char *, int);
static uint64_t      (*p_mono_compile_method)(MonoMethod *);

/* 40 * 200ms = 8s per method; a fresh shell can take that long to load its UI. */
#define CR_MENU_RESOLVE_RETRIES 40
#define CR_MENU_RESOLVE_RETRY_MS 200

/* Methods are looked up once; the retries only cover a shell that is still loading an assembly. */
#define CR_METHOD_RETRIES 5
#define CR_METHOD_RETRY_MS 1000

/* Raised only while working; dropped across every sleep so the shell never idles under it. */
#define CR_PAYLOAD_AUTHID 0x4800000000010003ull
static uint64_t g_old_authid;

static void
retry_sleep(int ms) {
  kernel_set_ucred_authid(getpid(), g_old_authid);
  usleep(ms * 1000);
  kernel_set_ucred_authid(getpid(), CR_PAYLOAD_AUTHID);
}

static int
resolve_mono_symbols(void) {
  /* Older firmwares can still be loading Mono when the payload starts, so wait for it. */
  int handle = 0;
  for (int i = 0; i < CR_MENU_RESOLVE_RETRIES && !handle; i++) {
    handle = find_module_handle("libmonosgen-2.0.sprx", i == CR_MENU_RESOLVE_RETRIES - 1);
    if (!handle) {
      retry_sleep(CR_MENU_RESOLVE_RETRY_MS);
    }
  }
  if (!handle) {
    cr_plog("hotkey_hook: Mono isn't loaded in SceShellUI yet\n");
    return -1;
  }

  *(void **)&p_mono_get_root_domain =
      (void *)kernel_dynlib_dlsym(-1, handle, "mono_get_root_domain");
  *(void **)&p_mono_thread_attach =
      (void *)kernel_dynlib_dlsym(-1, handle, "mono_thread_attach");
  *(void **)&p_mono_domain_assembly_open =
      (void *)kernel_dynlib_dlsym(-1, handle, "mono_domain_assembly_open");
  *(void **)&p_mono_assembly_get_image =
      (void *)kernel_dynlib_dlsym(-1, handle, "mono_assembly_get_image");
  *(void **)&p_mono_class_from_name =
      (void *)kernel_dynlib_dlsym(-1, handle, "mono_class_from_name");
  *(void **)&p_mono_class_get_method_from_name =
      (void *)kernel_dynlib_dlsym(-1, handle, "mono_class_get_method_from_name");
  *(void **)&p_mono_compile_method =
      (void *)kernel_dynlib_dlsym(-1, handle, "mono_compile_method");

  cr_plog("hotkey_hook: mono handle=%d compile=%p\n", handle, (void *)p_mono_compile_method);

  if (!p_mono_get_root_domain || !p_mono_thread_attach || !p_mono_domain_assembly_open ||
      !p_mono_assembly_get_image || !p_mono_class_from_name ||
      !p_mono_class_get_method_from_name || !p_mono_compile_method) {
    cr_plog("hotkey_hook: couldn't resolve Mono symbols\n");
    return -1;
  }
  return 0;
}

/* mono_aot_get_method crashes inside libkernel_sys on this firmware, so only the compile call is used. */
static uint64_t
method_code_address(MonoDomain *domain, MonoMethod *method) {
  (void)domain;
  uint64_t addr = p_mono_compile_method(method);
  cr_plog("hotkey_hook: method %p compiled at 0x%llx\n", (void *)method, (unsigned long long)addr);
  return addr;
}

/* dll/namespace/class/method -> MonoMethod*, used by every hook below. */
static MonoMethod *
resolve_mono_method(MonoDomain *domain, const char *dll, const char *ns,
                     const char *cls, const char *method, int argc) {
  MonoAssembly *assembly = p_mono_domain_assembly_open(domain, dll);
  if (!assembly) {
    return NULL;
  }
  MonoImage *image = p_mono_assembly_get_image(assembly);
  if (!image) {
    return NULL;
  }
  MonoClass *klass = p_mono_class_from_name(image, ns, cls);
  if (!klass) {
    return NULL;
  }
  return p_mono_class_get_method_from_name(klass, method, argc);
}

/* A fresh SceShellUI respawn can still be mid-boot when we inject, and a
 * forced restart competes with the shell's own boot work - retry. */

static MonoMethod *
resolve_mono_method_retry(MonoDomain *domain, const char *dll, const char *ns,
                           const char *cls, const char *method, int argc) {
  for (int i = 0; i < CR_METHOD_RETRIES; i++) {
    MonoMethod *m = resolve_mono_method(domain, dll, ns, cls, method, argc);
    if (m) {
      cr_plog("hotkey_hook: resolved %s.%s after %d try(s)\n", cls, method, i + 1);
      return m;
    }
    retry_sleep(CR_METHOD_RETRY_MS);
  }
  cr_plog("hotkey_hook: %s.%s not found after %d tries\n", cls, method, CR_METHOD_RETRIES);
  return NULL;
}

/* Sce.PlayStation.Core.Input.GamePad's GamePadData value type — field order
 * and sizes matter here, it's returned by value via the SysV hidden-pointer ABI. */
typedef struct {
  uint8_t skip;
  uint32_t buttons;
  uint32_t buttons_prev;
  uint32_t buttons_down;
  uint32_t buttons_up;
  float analog_left_x;
  float analog_left_y;
  float analog_right_x;
  float analog_right_y;
} cr_gamepad_data_t;

#define CR_BUTTON_L2 4096u
#define CR_BUTTON_R3 32768u

/* Defaults to L2+R3 / 800 ms until the daemon config broadcast arrives. */
static uint32_t g_hotkey_combo = CR_BUTTON_L2 | CR_BUTTON_R3;
static uint32_t g_hotkey_hold_ms = 800;

static int g_hotkey_config_fd = -1;

/* Set by the daemon's first config packet, which it only sends once every hook is armed. */
static int g_hooks_ready;

static void
init_hotkey_config_socket(void) {
  g_hotkey_config_fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (g_hotkey_config_fd < 0) {
    return;
  }

  int opt = 1;
  setsockopt(g_hotkey_config_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
  setsockopt(g_hotkey_config_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(CR_HOTKEY_CONFIG_SIGNAL_PORT);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  if (bind(g_hotkey_config_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
    cr_plog("hotkey_hook: couldn't bind the config signal port\n");
    close(g_hotkey_config_fd);
    g_hotkey_config_fd = -1;
  }
}

/* Non-blocking - called once per GetData call, drains to the newest packet. */
static void
poll_hotkey_config(void) {
  if (g_hotkey_config_fd < 0) {
    return;
  }
  cr_hotkey_config_packet_t pkt;
  while (recv(g_hotkey_config_fd, &pkt, sizeof(pkt), MSG_DONTWAIT) == (ssize_t)sizeof(pkt)) {
    if (!__atomic_exchange_n(&g_hooks_ready, 1, __ATOMIC_ACQ_REL)) {
      cr_plog("hotkey_hook: first config packet, hooks are live\n");
    }
    if (pkt.combo != 0) {
      g_hotkey_combo = pkt.combo;
    }
    if (pkt.hold_ms >= 100 && pkt.hold_ms <= 5000) {
      g_hotkey_hold_ms = pkt.hold_ms;
    }
  }
}

/* Until then the hooks only forward to the original functions. */
static int
hooks_ready(void) {
  if (__atomic_load_n(&g_hooks_ready, __ATOMIC_ACQUIRE) || g_hotkey_config_fd < 0) {
    return 1;
  }
  poll_hotkey_config();
  return __atomic_load_n(&g_hooks_ready, __ATOMIC_ACQUIRE);
}

static uint64_t
now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

/* Hook sites collected here; the ones not armed in place are left to the daemon - see cr_hotkey_signal.h. */
static cr_hook_request_t g_hook_req;

static void
send_hook_request(void) {
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) {
    return;
  }

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(CR_HOTKEY_SIGNAL_PORT);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  g_hook_req.magic = CR_HOOK_REQUEST_MAGIC;
  ssize_t n = sendto(fd, &g_hook_req, sizeof(g_hook_req), 0, (struct sockaddr *)&addr, sizeof(addr));
  cr_plog("hotkey_hook: hook request with %u site(s) sent, sendto=%zd\n", g_hook_req.count, n);
  close(fd);
}

static void
send_signal(const char *msg) {
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) {
    return;
  }

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(CR_HOTKEY_SIGNAL_PORT);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  ssize_t n = sendto(fd, msg, strlen(msg), 0, (struct sockaddr *)&addr, sizeof(addr));
  cr_plog("hotkey_hook: signal \"%s\" sent, sendto=%zd\n", msg, n);
  close(fd);
}

static cr_gamepad_data_t (*orig_get_data)(int);

/* Always forwards to the real GetData; SceShellUI's own input handling depends on its result. */
static cr_gamepad_data_t
hook_get_data(int device_index) {
  cr_gamepad_data_t result;
  memset(&result, 0, sizeof(result));
  if (orig_get_data) {
    result = orig_get_data(device_index);
  }

  static int logged;
  if (!logged) {
    logged = 1;
    cr_plog("hotkey_hook: GetData hook called for the first time\n");
  }
  if (!hooks_ready()) {
    return result;
  }
  poll_hotkey_config();

  static uint64_t held_since_ms = 0;
  static int fired = 0;

  int combo_held = (result.buttons & g_hotkey_combo) == g_hotkey_combo;
  uint64_t now = now_ms();

  if (combo_held) {
    if (held_since_ms == 0) {
      held_since_ms = now;
    } else if (!fired && now - held_since_ms >= g_hotkey_hold_ms) {
      fired = 1;
      send_signal("hotkey");
    }
  } else {
    held_since_ms = 0;
    fired = 0;
  }

  return result;
}

/* Game-tile OptionsMenu entry: steals an existing row's slot in
 * OptionMenu.createJson and catches the click in BootHelper.Boot. */

static MonoDomain *g_menu_domain;

/* Check for Update goes through BootHelper.Boot. "Information" looked safer, but its click goes
 * through UrlConfigResolver and our Boot hook never sees it. */
#define CR_MENU_STEAL_ID "MENU_ID_CHECK_PATCH"

/* 1 = log every row createJson sees instead of substituting one. */
#define CR_MENU_DISCOVERY_LOG 0

/* No colon on purpose - "scheme:path" strings get treated as a real URI and
 * rejected by native code (UrlConfigResolver) before Boot() ever sees them. */
#define CR_TRAINER_URI_PREFIX "cheatrunner?trainer="
#define CR_TRAINER_URI_PREFIX_LEN 20

static MonoString *(*p_mono_string_new)(MonoDomain *, const char *);
static char *      (*p_mono_string_to_utf8)(MonoString *);
static void        (*p_mono_free)(void *);

static int
resolve_menu_mono_symbols(int mono_handle) {
  *(void **)&p_mono_string_new =
      (void *)kernel_dynlib_dlsym(-1, mono_handle, "mono_string_new");
  *(void **)&p_mono_string_to_utf8 =
      (void *)kernel_dynlib_dlsym(-1, mono_handle, "mono_string_to_utf8");
  *(void **)&p_mono_free =
      (void *)kernel_dynlib_dlsym(-1, mono_handle, "mono_free");

  return (p_mono_string_new && p_mono_string_to_utf8 && p_mono_free) ? 0 : -1;
}

static void
mono_string_to_cstr(MonoString *s, char *out, size_t out_sz) {
  out[0] = 0;
  if (!s) {
    return;
  }
  char *utf8 = p_mono_string_to_utf8(s);
  if (!utf8) {
    return;
  }
  snprintf(out, out_sz, "%s", utf8);
  p_mono_free(utf8);
}

/* "titleid=" match is case-insensitive - real URIs seen in the field use
 * lowercase "titleid=". */
#if !CR_MENU_DISCOVERY_LOG
static int
extract_titleid_from_uri(const char *uri, char *out, size_t out_sz) {
  size_t len = strlen(uri);
  const char *p = NULL;
  for (size_t i = 0; i + 8 <= len; i++) {
    if (!strncasecmp(uri + i, "titleid=", 8)) {
      p = uri + i + 8;
      break;
    }
  }
  if (!p) {
    return 0;
  }

  size_t i = 0;
  while (p[i] && p[i] != '&' && i + 1 < out_sz) {
    out[i] = p[i];
    i++;
  }
  out[i] = 0;
  return i > 0;
}
#endif /* !CR_MENU_DISCOVERY_LOG */

typedef void (*create_json_fn)(MonoObject *, MonoObject *, MonoString *, MonoString *,
                                MonoString *, MonoString *, MonoString *, MonoObject *, bool);
static create_json_fn orig_create_json;

static void
menu_create_json_hook(MonoObject *inst, MonoObject *array, MonoString *id,
                       MonoString *label, MonoString *action_url, MonoString *action_id,
                       MonoString *message_id, MonoObject *sub_menu, bool enable) {
  static int logged;
  if (!logged) {
    logged = 1;
    cr_plog("menu_hook: createJson hook called for the first time\n");
  }
  if (!hooks_ready()) {
    orig_create_json(inst, array, id, label, action_url, action_id, message_id, sub_menu, enable);
    return;
  }

  char id_buf[64];
  mono_string_to_cstr(id, id_buf, sizeof(id_buf));

#if CR_MENU_DISCOVERY_LOG
  char label_buf[128], url_buf[256];
  mono_string_to_cstr(label, label_buf, sizeof(label_buf));
  mono_string_to_cstr(action_url, url_buf, sizeof(url_buf));
  cr_plog("menu_hook: id=%s label=%s url=%s enable=%d\n",
              id_buf, label_buf, url_buf, (int)enable);
#else
  if (!strcmp(id_buf, CR_MENU_STEAL_ID)) {
    char url_buf[256], tid[16];
    mono_string_to_cstr(action_url, url_buf, sizeof(url_buf));
    if (extract_titleid_from_uri(url_buf, tid, sizeof(tid))) {
      char cheat_url[64];
      snprintf(cheat_url, sizeof(cheat_url), "%s%s", CR_TRAINER_URI_PREFIX, tid);
      orig_create_json(inst, array, p_mono_string_new(g_menu_domain, "MENU_ID_CHEATRUNNER"),
                        p_mono_string_new(g_menu_domain, "\xe2\x98\x85 CheatRunner"),
                        p_mono_string_new(g_menu_domain, cheat_url),
                        action_id, NULL, sub_menu, true);
      return;
    }
  }
#endif

  orig_create_json(inst, array, id, label, action_url, action_id, message_id, sub_menu, enable);
}

typedef bool (*boot3_fn)(MonoString *, int, MonoString *);
typedef bool (*boot2_fn)(MonoString *, int);
static boot3_fn orig_boot_3;
static boot2_fn orig_boot_2;

static bool
menu_boot_hook_3(MonoString *uri, int opt, MonoString *tid_for_boot) {
  if (!hooks_ready()) {
    return orig_boot_3(uri, opt, tid_for_boot);
  }
  char uri_buf[256];
  mono_string_to_cstr(uri, uri_buf, sizeof(uri_buf));

  if (!strncmp(uri_buf, CR_TRAINER_URI_PREFIX, CR_TRAINER_URI_PREFIX_LEN)) {
    char signal[32];
    snprintf(signal, sizeof(signal), "%s%s", CR_MENU_SIGNAL_PREFIX, uri_buf + CR_TRAINER_URI_PREFIX_LEN);
    cr_plog("menu_hook: Boot(3) caught %s\n", uri_buf);
    send_signal(signal);
    return orig_boot_3(p_mono_string_new(g_menu_domain, "pshomeui:navigateToHome?bootCondition=psButton"),
                        opt, tid_for_boot);
  }
  return orig_boot_3(uri, opt, tid_for_boot);
}

static bool
menu_boot_hook_2(MonoString *uri, int opt) {
  if (!hooks_ready()) {
    return orig_boot_2(uri, opt);
  }
  char uri_buf[256];
  mono_string_to_cstr(uri, uri_buf, sizeof(uri_buf));

  if (!strncmp(uri_buf, CR_TRAINER_URI_PREFIX, CR_TRAINER_URI_PREFIX_LEN)) {
    char signal[32];
    snprintf(signal, sizeof(signal), "%s%s", CR_MENU_SIGNAL_PREFIX, uri_buf + CR_TRAINER_URI_PREFIX_LEN);
    cr_plog("menu_hook: Boot(2) caught %s\n", uri_buf);
    send_signal(signal);
    return orig_boot_2(p_mono_string_new(g_menu_domain, "pshomeui:navigateToHome?bootCondition=psButton"), opt);
  }
  return orig_boot_2(uri, opt);
}

/* Adds a prepared site to the batch the daemon will arm. */
static void *
prepare_hook(uint64_t addr, void *dest, uint8_t kind) {
  if (!addr || g_hook_req.count >= CR_HOOK_MAX_SITES) {
    return NULL;
  }
  void *tramp = cr_detour_prepare(addr, dest, &g_hook_req.sites[g_hook_req.count]);
  cr_plog("hotkey_hook: prepare kind=%u target=0x%llx -> %s\n", (unsigned)kind, (unsigned long long)addr,
              !tramp ? "failed" : tramp == CR_DETOUR_ALREADY_HOOKED ? "already hooked" : "ok");
  if (tramp && tramp != CR_DETOUR_ALREADY_HOOKED) {
    g_hook_req.sites[g_hook_req.count].kind = kind;
    g_hook_req.count++;
  }
  return tramp;
}

/* Best-effort - a firmware where this fails still gets a working L2+R3
 * hotkey, since main() calls this after GetData is already prepared. */
static void
install_menu_hooks(MonoDomain *domain) {
  g_menu_domain = domain;

  int mono_handle = find_module_handle("libmonosgen-2.0.sprx", 1);
  if (!mono_handle || resolve_menu_mono_symbols(mono_handle)) {
    cr_plog("hotkey_hook: couldn't resolve mono_string_* for the menu hooks\n");
    return;
  }

  MonoMethod *cj = resolve_mono_method_retry(domain,
      "/system_ex/common_ex/lib/Sce.Vsh.ShellUI.ReactNativeShellApp.dll",
      "ReactNative.Modules.ShellUI.HomeUI", "OptionMenu", "createJson", 8);
  if (cj) {
    cr_plog("hotkey_hook: OptionMenu.createJson is method %p\n", (void *)cj);
    uint64_t addr = method_code_address(domain, cj);
    void *tramp = prepare_hook(addr, (void *)menu_create_json_hook, CR_HOOK_KIND_MENU);
    if (tramp && tramp != CR_DETOUR_ALREADY_HOOKED) {
      orig_create_json = (create_json_fn)tramp;
      cr_plog("hotkey_hook: OptionsMenu hook ready\n");
    } else {
      cr_plog("hotkey_hook: couldn't hook OptionMenu.createJson\n");
    }
  } else {
    cr_plog("hotkey_hook: OptionMenu.createJson not found\n");
  }

  /* Poll both overloads together so a 2-arg-only firmware doesn't burn the 3-arg wait first. */
  static const char *boot_dll = "/system_ex/common_ex/lib/Sce.Vsh.ShellUI.AppSystem.dll";
  MonoMethod *boot = NULL;
  int boot_argc = 0;
  for (int i = 0; i < CR_METHOD_RETRIES && !boot; i++) {
    boot = resolve_mono_method(domain, boot_dll, "Sce.Vsh.ShellUI.AppSystem", "BootHelper", "Boot", 3);
    if (boot) {
      boot_argc = 3;
    } else if ((boot = resolve_mono_method(domain, boot_dll, "Sce.Vsh.ShellUI.AppSystem", "BootHelper", "Boot", 2))) {
      boot_argc = 2;
    } else {
      retry_sleep(CR_METHOD_RETRY_MS);
    }
  }
  if (!boot) {
    cr_plog("hotkey_hook: BootHelper.Boot not found\n");
    return;
  }
  cr_plog("hotkey_hook: BootHelper.Boot(%d) is method %p\n", boot_argc, (void *)boot);

  uint64_t addr = method_code_address(domain, boot);
  void *tramp = prepare_hook(addr, boot_argc == 3 ? (void *)menu_boot_hook_3 : (void *)menu_boot_hook_2, CR_HOOK_KIND_BOOT);
  if (tramp && tramp != CR_DETOUR_ALREADY_HOOKED) {
    if (boot_argc == 3) {
      orig_boot_3 = (boot3_fn)tramp;
    } else {
      orig_boot_2 = (boot2_fn)tramp;
    }
    cr_plog("hotkey_hook: Boot(%d) hook ready\n", boot_argc);
  } else {
    cr_plog("hotkey_hook: couldn't hook BootHelper.Boot\n");
  }
}

/* elfldr_exec restores SceShellUI's own ucred before resuming, so main()
 * starts unprivileged again - raise it just long enough for the Mono calls. */
int
main(void) {
  cr_plog("hotkey_hook: setting up the hotkey\n");

  init_hotkey_config_socket();

  pid_t self = getpid();
  uint64_t old_authid = g_old_authid = kernel_get_ucred_authid(self);
  kernel_set_ucred_authid(self, CR_PAYLOAD_AUTHID);
  cr_plog("hotkey_hook: running in pid %d, authid 0x%llx raised, firmware 0x%x\n", (int)self,
          (unsigned long long)old_authid, (unsigned)(kernel_get_fw_version() >> 16));

  int rc = 1;
  int signalled = 0;

  if (resolve_mono_symbols()) {
    goto out;
  }

  MonoDomain *domain = p_mono_get_root_domain();
  if (!domain) {
    cr_plog("hotkey_hook: couldn't get Mono's root domain\n");
    goto out;
  }
  cr_plog("hotkey_hook: root domain %p\n", (void *)domain);

  /* pthread_create gives us a thread Mono has never seen; every other Mono
   * call here needs this first or mono_thread_info_current() asserts. */
  p_mono_thread_attach(domain);
  cr_plog("hotkey_hook: thread attached to Mono\n");

  /* Retries here too - a shell that respawned seconds ago can still be
   * loading Core.dll, and a single-shot open would fail on a working setup. */
  MonoMethod *method = resolve_mono_method_retry(
      domain, "/system_ex/common_ex/lib/Sce.PlayStation.Core.dll",
      "Sce.PlayStation.Core.Input", "GamePad", "GetData", 1);

  /* GetData drives the button combo; the menu hooks work without it. */
  if (method) {
    cr_plog("hotkey_hook: GamePad.GetData is method %p\n", (void *)method);
    uint64_t addr = method_code_address(domain, method);
    void *trampoline = prepare_hook(addr, (void *)hook_get_data, CR_HOOK_KIND_GETDATA);
    if (trampoline == CR_DETOUR_ALREADY_HOOKED) {
      /* A previous session's hook is still resident; tell the daemon so it can use it. */
      cr_plog("hotkey_hook: target already hooked by a previous session\n");
      send_signal("old");
      signalled = 1;
      goto out;
    }
    if (trampoline) {
      orig_get_data = (cr_gamepad_data_t(*)(int))trampoline;
      cr_plog("hotkey_hook: hotkey prepared\n");
    }
  } else {
    cr_plog("hotkey_hook: GamePad.GetData not found, the button combo won't work\n");
  }

  /* Menu hooks join the same batch - one suspend arms them all. */
  cr_plog("hotkey_hook: installing the menu hooks\n");
  install_menu_hooks(domain);
  if (g_hook_req.count > 0) {
    /* Every original pointer is set by now, so a live hook can already forward. */
    for (uint32_t i = 0; i < g_hook_req.count; i++) {
      (void)cr_detour_commit(&g_hook_req.sites[i]);
    }
    cr_plog("hotkey_hook: handing %u hook site(s) to the daemon\n", g_hook_req.count);
    send_hook_request();
    signalled = 1;
    rc = 0;
  }

out:
  cr_plog("hotkey_hook: payload main done rc=%d signalled=%d\n", rc, signalled);
  if (rc && !signalled) {
    /* Say so rather than making the daemon sit out its confirmation timeout. */
    send_signal("err");
  }
  if (rc && g_hotkey_config_fd >= 0) {
    /* This thread is done, but the socket would stay bound inside SceShellUI
     * and steal SO_REUSEPORT config packets from whichever hook does take. */
    close(g_hotkey_config_fd);
    g_hotkey_config_fd = -1;
  }

  /* The installed hook only sends a UDP packet and needs no privilege; do not leave the shell authid raised. */
  kernel_set_ucred_authid(self, old_authid);

  /* Stay alive like the hooks' own thread would: a Mono-attached thread must not exit. */
  while (rc == 0) {
    sleep(3600);
  }
  return rc;
}
