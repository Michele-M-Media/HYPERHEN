/* HyperHEN contributors. SPDX-License-Identifier: GPL-3.0-or-later */

#include "hypershell.hpp"

#include "dashboard_navigation.hpp"
#include "debug_settings_route_runtime.hpp"
#include "external_symbols.hpp"
#include "hooked_funcs.hpp"
#include "ipc.hpp"

#include <arpa/inet.h>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

extern MonoImage *pui_img;
extern MonoObject *Game;

MonoObject *CreateLabel(const char *name, float x, float y, const char *text,
                        MonoObject *font, int horzAlign, int vertAlign,
                        float r, float g, float b, float a);
MonoObject *CreateUIFont(int size, int style, int weight);
MonoObject *CreateUIColor(float r, float g, float b, float a);
MonoObject *New_Object(MonoClass *klass);
void Widget_Append_Child(MonoObject *widget, MonoObject *child);

#if defined(HYPERHEN_EXPERIMENTAL_HYPERSHELL)
extern "C" int hyperhen_liveshell_native_request(void);
extern "C" void hyperhen_liveshell_native_frame(void *application);
#endif

namespace {

constexpr const char *kRootName = "id_hyperhen_hypershell_root";
constexpr const char *kEnginePath =
    "/data/HyperHEN/internal/hypershell-engine.elf";
constexpr const char *kEngineIdentity = "hypershell-engine";
constexpr unsigned short kEnginePort = 9050;

enum class Page : int { Home = 0, Library, Appearance, Behavior, Games };

struct UiState {
  Page page = Page::Home;
  int selected = 0;
  bool engine = false;
  int dim = 25;
  int volume = 100;
  int blur = 0;
  int looks[4] = {0, 0, 0, 0};
  bool hide_menus = true;
  bool parallax = false;
  bool shuffle = false;
  bool main_video = true;
  bool game_videos = true;
  bool game_pause = true;
  bool store_only = false;
  bool confirm_delete = false;
  bool confirm_shutdown = false;
};

std::atomic<bool> g_open_requested{false};
std::atomic<bool> g_visible{false};
std::atomic<bool> g_dirty{false};
std::atomic<bool> g_close_requested{false};
std::atomic<bool> g_toolbox_requested{false};
std::atomic<bool> g_wait_for_release{false};
#if defined(HYPERHEN_EXPERIMENTAL_HYPERSHELL)
std::atomic<bool> g_renderer_armed{false};
#endif
UiState g_state;

pthread_mutex_t g_action_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t g_action_cond = PTHREAD_COND_INITIALIZER;
pthread_t g_action_thread{};
bool g_action_thread_started = false;
constexpr size_t kActionQueueCapacity = 64;
char g_pending_paths[kActionQueueCapacity][192] = {};
size_t g_action_head = 0;
size_t g_action_tail = 0;
size_t g_action_count = 0;

MonoObject *root_widget() {
  return Get_Property<MonoObject *>(pui_img, "Sce.PlayStation.PUI.UI2", "Scene",
                                    Game, "RootWidget");
}

MonoObject *find_named(MonoObject *root, const char *name) {
  if (!root || !name)
    return nullptr;
  MonoClass *widget =
      mono_class_from_name(pui_img, "Sce.PlayStation.PUI.UI2", "Widget");
  return Invoke<MonoObject *>(pui_img, widget, root, "FindWidgetByName",
                              mono_string_new(Root_Domain, name));
}

void remove_menu() {
  MonoObject *root = root_widget();
  MonoObject *panel = find_named(root, kRootName);
  if (!panel)
    return;
  MonoClass *widget =
      mono_class_from_name(pui_img, "Sce.PlayStation.PUI.UI2", "Widget");
  Invoke<void>(pui_img, widget, panel, "RemoveFromParent");
}

MonoObject *make_panel(const char *name, float x, float y, float width,
                       float height, float r, float g, float b, float a) {
  MonoClass *klass =
      mono_class_from_name(pui_img, "Sce.PlayStation.PUI.UI2", "Panel");
  if (!klass)
    return nullptr;
  MonoObject *panel = New_Object(klass);
  if (!panel)
    return nullptr;
  mono_runtime_object_init(panel);
  Set_Property(klass, panel, "Name", mono_string_new(Root_Domain, name));
  Set_Property(klass, panel, "X", x);
  Set_Property(klass, panel, "Y", y);
  Set_Property(klass, panel, "Width", width);
  Set_Property(klass, panel, "Height", height);
  if (MonoObject *color = CreateUIColor(r, g, b, a))
    Set_Property_Invoke(klass, panel, "BackgroundColor", color);
  Set_Property(klass, panel, "BackgroundVisibility", true);
  Set_Property(klass, panel, "BackgroundOpacity", 1.0f);
  Set_Property(klass, panel, "BackgroundStyle", 1);
  return panel;
}

bool append_label(MonoObject *parent, const char *name, float x, float y,
                  const char *text, MonoObject *font, float r, float g,
                  float b, float a, float width) {
  MonoObject *label =
      CreateLabel(name, x, y, text, font, 0, 0, r, g, b, a);
  if (!label)
    return false;
  MonoClass *klass =
      mono_class_from_name(pui_img, "Sce.PlayStation.PUI.UI2", "Label");
  Set_Property(klass, label, "Width", width);
  Widget_Append_Child(parent, label);
  return true;
}

const char *on_off(bool on) { return on ? "ON" : "OFF"; }

int item_count() {
  switch (g_state.page) {
  case Page::Home:
    return 7;
  case Page::Library:
    return 6;
  case Page::Appearance:
    return 9;
  case Page::Behavior:
    return 8;
  case Page::Games:
    return 4;
  }
  return 1;
}

const char *page_title() {
  switch (g_state.page) {
  case Page::Home:
    return "LIVE DASHBOARD  /  DYNAMIC HOME";
  case Page::Library:
    return "WALLPAPER LIBRARY  /  UPLOAD :9050";
  case Page::Appearance:
    return "APPEARANCE  /  ALL VISUAL CONTROLS";
  case Page::Behavior:
    return "PLAYBACK  /  DASHBOARD RULES";
  case Page::Games:
    return "GAME PROFILES  /  FOCUSED TILE";
  }
  return "HYPERSHELL";
}

void item_text(int item, char *out, size_t size) {
  if (g_state.page == Page::Home) {
    static const char *const labels[] = {
        "Dynamic wallpaper engine", "Wallpaper library", "Appearance",
        "Playback and dashboard rules", "Per-game profiles",
        "Open HyperHEN Tools", "Close HyperShell"};
    if (item == 0)
      std::snprintf(out, size, "%-31s %s", labels[item],
                    g_state.engine ? "ACTIVE" : "STOPPED");
    else
      std::snprintf(out, size, "%s", labels[item]);
    return;
  }
  if (g_state.page == Page::Library) {
    static const char *const labels[] = {
        "Previous wallpaper", "Next wallpaper", "Shuffle on start",
        "Delete current wallpaper", "Remote upload / complete library",
        "Back"};
    if (item == 2)
      std::snprintf(out, size, "%-31s %s", labels[item],
                    on_off(g_state.shuffle));
    else if (item == 3 && g_state.confirm_delete)
      std::snprintf(out, size, "CONFIRM: press X again to delete");
    else if (item == 4)
      std::snprintf(out, size, "Remote upload / library     http://PS5-IP:%u",
                    kEnginePort);
    else
      std::snprintf(out, size, "%s", labels[item]);
    return;
  }
  if (g_state.page == Page::Appearance) {
    if (item == 0)
      std::snprintf(out, size, "Dashboard dim                %d%%", g_state.dim);
    else if (item == 1)
      std::snprintf(out, size, "Wallpaper sound              %d%%", g_state.volume);
    else if (item == 2)
      std::snprintf(out, size, "Background blur              %s",
                    g_state.blur == 0 ? "OFF" : g_state.blur == 1 ? "SOFT" : "DEEP");
    else if (item >= 3 && item <= 6) {
      static const char *const names[] = {"Bottom fade", "Top fade", "Vignette", "Haze"};
      static const char *const levels[] = {"OFF", "SOFT", "DEEP"};
      std::snprintf(out, size, "%-31s %s", names[item - 3],
                    levels[g_state.looks[item - 3]]);
    } else if (item == 7)
      std::snprintf(out, size, "Parallax motion              %s",
                    on_off(g_state.parallax));
    else
      std::snprintf(out, size, "Back");
    return;
  }
  if (g_state.page == Page::Behavior) {
    if (item == 0)
      std::snprintf(out, size, "Main wallpaper on Home       %s", on_off(g_state.main_video));
    else if (item == 1)
      std::snprintf(out, size, "Hide in system menus         %s", on_off(g_state.hide_menus));
    else if (item == 2)
      std::snprintf(out, size, "PS Store only                %s", on_off(g_state.store_only));
    else if (item == 3)
      std::snprintf(out, size, "Pause while a game runs      %s", on_off(g_state.game_pause));
    else if (item == 4)
      std::snprintf(out, size, "Reset every HyperShell option");
    else if (item == 5)
      std::snprintf(out, size, "%s", g_state.confirm_shutdown
                                      ? "CONFIRM: press X again to stop service"
                                      : "Stop HyperShell service");
    else if (item == 6)
      std::snprintf(out, size, "Start / refresh service");
    else
      std::snprintf(out, size, "Back");
    return;
  }
  if (item == 0)
    std::snprintf(out, size, "Per-game wallpapers          %s", on_off(g_state.game_videos));
  else if (item == 1)
    std::snprintf(out, size, "Assign current video to focused game");
  else if (item == 2)
    std::snprintf(out, size, "Use main wallpaper for focused game");
  else
    std::snprintf(out, size, "Back");
  }

bool draw_menu() {
  MonoObject *root = root_widget();
  float screen_w = 0.0f;
  float screen_h = 0.0f;
  if (!root || !resolve_root_dimensions(root, &screen_w, &screen_h))
    return false;

  remove_menu();
  MonoObject *scrim = make_panel(kRootName, 0.0f, 0.0f, screen_w, screen_h,
                                 0.005f, 0.008f, 0.02f, 0.62f);
  if (!scrim)
    return false;
  Widget_Append_Child(root, scrim);

  const float panel_w = screen_w * 0.56f;
  const float panel_h = screen_h * 0.82f;
  const float panel_x = (screen_w - panel_w) * 0.5f;
  const float panel_y = (screen_h - panel_h) * 0.5f;
  MonoObject *panel = make_panel("id_hypershell_glass", panel_x, panel_y,
                                 panel_w, panel_h, 0.015f, 0.025f, 0.075f,
                                 0.88f);
  if (!panel)
    return false;
  Widget_Append_Child(scrim, panel);

  MonoObject *accent = make_panel("id_hypershell_accent", 0.0f, 0.0f, 8.0f,
                                  panel_h, 0.1f, 0.75f, 1.0f, 1.0f);
  if (accent)
    Widget_Append_Child(panel, accent);

  MonoObject *title_font = CreateUIFont(34, 1, 900);
  MonoObject *body_font = CreateUIFont(21, 1, 700);
  MonoObject *hint_font = CreateUIFont(16, 0, 500);
  if (!title_font || !body_font || !hint_font)
    return false;

  if (!append_label(panel, "id_hypershell_title", 52.0f, 30.0f, "HYPERSHELL",
                    title_font, 0.25f, 0.86f, 1.0f, 1.0f, panel_w - 90.0f) ||
      !append_label(panel, "id_hypershell_subtitle", 54.0f, 76.0f,
                    page_title(), hint_font, 0.65f, 0.72f,
                    0.88f, 1.0f, panel_w - 100.0f))
    return false;

  const float start_y = 122.0f;
  const int count = item_count();
  const float row_h = (panel_h - 190.0f) / static_cast<float>(count);
  for (int i = 0; i < count; ++i) {
    char text[160];
    char shown[176];
    char name[64];
    item_text(i, text, sizeof(text));
    std::snprintf(shown, sizeof(shown), "%s  %s",
                  i == g_state.selected ? ">" : " ", text);
    std::snprintf(name, sizeof(name), "id_hypershell_item_%d", i);
    const bool selected = i == g_state.selected;
    if (!append_label(panel, name, 54.0f, start_y + row_h * i, shown, body_font,
                      selected ? 0.25f : 0.82f, selected ? 0.90f : 0.84f,
                      selected ? 1.0f : 0.92f, 1.0f, panel_w - 104.0f))
      return false;
  }

  return append_label(panel, "id_hypershell_help", 54.0f, panel_h - 50.0f,
                      "D-PAD  Navigate / Adjust     X  Select     O  Close",
                      hint_font, 0.58f, 0.66f, 0.82f, 1.0f,
                      panel_w - 100.0f);
}

bool http_get(const char *path) {
  const int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0)
    return false;
  timeval timeout{2, 0};
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(kEnginePort);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
    close(fd);
    return false;
  }
  char request[384];
  const int length = std::snprintf(
      request, sizeof(request),
      "GET %s HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n", path);
  const bool sent = length > 0 && length < static_cast<int>(sizeof(request)) &&
                    send(fd, request, static_cast<size_t>(length), 0) == length;
  if (sent) {
    char response[96];
    (void)recv(fd, response, sizeof(response), 0);
  }
  close(fd);
  return sent;
}

void *action_worker(void *) {
  for (;;) {
    char path[sizeof(g_pending_paths[0])] = {};
    pthread_mutex_lock(&g_action_lock);
    while (g_action_count == 0)
      pthread_cond_wait(&g_action_cond, &g_action_lock);
    std::snprintf(path, sizeof(path), "%s", g_pending_paths[g_action_head]);
    g_pending_paths[g_action_head][0] = '\0';
    g_action_head = (g_action_head + 1) % kActionQueueCapacity;
    --g_action_count;
    pthread_mutex_unlock(&g_action_lock);

    if (!std::strcmp(path, "@start")) {
#if defined(HYPERHEN_EXPERIMENTAL_HYPERSHELL)
      const int native_rc = hyperhen_liveshell_native_request();
      if (native_rc == 0)
        g_renderer_armed.store(true, std::memory_order_release);
      IPC_Ret launch = IPC_Ret::NO_ERROR;
      if (!http_get("/api/on")) {
        launch = IPC_Client::getInstance(true).LaunchPayload(kEnginePath,
                                                            kEngineIdentity);
        if (launch == IPC_Ret::NO_ERROR) {
          for (int waited = 0; waited < 30000; waited += 200) {
            if (http_get("/api/on"))
              break;
            usleep(200000);
          }
        }
      }
      LOG_DEBUG("HyperShell start: native=%d daemon=%d", native_rc,
                static_cast<int>(launch));
#else
      LOG_WARN("HyperShell renderer omitted from this build");
#endif
      continue;
    }
    if (!http_get(path))
      LOG_WARN("HyperShell service unavailable for %s", path);
  }
  return nullptr;
}

void queue_action(const char *path) {
  if (!path || !path[0])
    return;
  pthread_mutex_lock(&g_action_lock);
  if (g_action_count == kActionQueueCapacity) {
    g_pending_paths[g_action_head][0] = '\0';
    g_action_head = (g_action_head + 1) % kActionQueueCapacity;
    --g_action_count;
    LOG_WARN("HyperShell action queue full; oldest request replaced");
  }
  std::snprintf(g_pending_paths[g_action_tail],
                sizeof(g_pending_paths[g_action_tail]), "%s", path);
  g_action_tail = (g_action_tail + 1) % kActionQueueCapacity;
  ++g_action_count;
  if (!g_action_thread_started) {
    if (pthread_create(&g_action_thread, nullptr, action_worker, nullptr) == 0) {
      pthread_detach(g_action_thread);
      g_action_thread_started = true;
    }
  }
  pthread_cond_signal(&g_action_cond);
  pthread_mutex_unlock(&g_action_lock);
}

void queue_int(const char *prefix, int value) {
  char path[160];
  std::snprintf(path, sizeof(path), "%s%d", prefix, value);
  queue_action(path);
}

void queue_bool(const char *key, bool value) {
  char path[160];
  std::snprintf(path, sizeof(path), "/api/set?k=%s&v=%d", key,
                value ? 1 : 0);
  queue_action(path);
}

void enter_page(Page page) {
  g_state.page = page;
  g_state.selected = 0;
  g_state.confirm_delete = false;
  g_state.confirm_shutdown = false;
  g_dirty.store(true, std::memory_order_release);
}

void reset_local_state() {
  g_state.engine = true;
  g_state.dim = 25;
  g_state.volume = 100;
  g_state.blur = 0;
  for (int &look : g_state.looks)
    look = 0;
  g_state.hide_menus = true;
  g_state.parallax = false;
  g_state.shuffle = false;
  g_state.main_video = true;
  g_state.game_videos = true;
  g_state.game_pause = true;
  g_state.store_only = false;
  g_state.confirm_delete = false;
  g_state.confirm_shutdown = false;
}

void adjust_selected(int direction) {
  if (g_state.page != Page::Appearance)
    return;

  if (g_state.selected == 0) {
    g_state.dim += direction * 5;
    if (g_state.dim < 0)
      g_state.dim = 80;
    if (g_state.dim > 80)
      g_state.dim = 0;
    queue_int("/api/dim?v=", g_state.dim);
  } else if (g_state.selected == 1) {
    g_state.volume += direction * 10;
    if (g_state.volume < 0)
      g_state.volume = 100;
    if (g_state.volume > 100)
      g_state.volume = 0;
    queue_int("/api/volume?v=", g_state.volume);
  } else if (g_state.selected == 2) {
    g_state.blur = (g_state.blur + direction + 3) % 3;
    queue_int("/api/blur?v=", g_state.blur);
  } else if (g_state.selected >= 3 && g_state.selected <= 6) {
    const int look = g_state.selected - 3;
    static const char *const prefixes[] = {
        "/api/look?k=bottom&v=", "/api/look?k=top&v=",
        "/api/look?k=vignette&v=", "/api/look?k=haze&v="};
    g_state.looks[look] = (g_state.looks[look] + direction + 3) % 3;
    queue_int(prefixes[look], g_state.looks[look]);
  } else if (g_state.selected == 7) {
    g_state.parallax = !g_state.parallax;
    queue_bool("parallax", g_state.parallax);
  } else {
    return;
  }
  g_dirty.store(true, std::memory_order_release);
}

void toggle_selected() {
  if (g_state.page == Page::Home) {
    if (g_state.selected == 0) {
      g_state.engine = !g_state.engine;
      queue_action(g_state.engine ? "@start" : "/api/off");
    } else if (g_state.selected == 1) {
      enter_page(Page::Library);
    } else if (g_state.selected == 2) {
      enter_page(Page::Appearance);
    } else if (g_state.selected == 3) {
      enter_page(Page::Behavior);
    } else if (g_state.selected == 4) {
      enter_page(Page::Games);
    } else if (g_state.selected == 5) {
      g_toolbox_requested.store(true, std::memory_order_release);
      g_close_requested.store(true, std::memory_order_release);
    } else {
      g_close_requested.store(true, std::memory_order_release);
    }
  } else if (g_state.page == Page::Library) {
    if (g_state.selected == 0) {
      queue_action("/api/step?d=-1");
    } else if (g_state.selected == 1) {
      queue_action("/api/step?d=1");
    } else if (g_state.selected == 2) {
      g_state.shuffle = !g_state.shuffle;
      queue_bool("shuffle", g_state.shuffle);
    } else if (g_state.selected == 3) {
      if (g_state.confirm_delete) {
        queue_action("/api/delete-current");
        g_state.confirm_delete = false;
      } else {
        g_state.confirm_delete = true;
      }
    } else if (g_state.selected == 4) {
      g_state.engine = true;
      queue_action("@start");
    } else {
      enter_page(Page::Home);
    }
  } else if (g_state.page == Page::Appearance) {
    if (g_state.selected == 8)
      enter_page(Page::Home);
    else
      adjust_selected(1);
  } else if (g_state.page == Page::Behavior) {
    if (g_state.selected == 0) {
      g_state.main_video = !g_state.main_video;
      queue_bool("mainvideo", g_state.main_video);
    } else if (g_state.selected == 1) {
      g_state.hide_menus = !g_state.hide_menus;
      queue_bool("menuhide", g_state.hide_menus);
    } else if (g_state.selected == 2) {
      g_state.store_only = !g_state.store_only;
      queue_bool("storeonly", g_state.store_only);
    } else if (g_state.selected == 3) {
      g_state.game_pause = !g_state.game_pause;
      queue_bool("gamepause", g_state.game_pause);
    } else if (g_state.selected == 4) {
      reset_local_state();
      queue_action("/api/reset");
    } else if (g_state.selected == 5) {
      if (g_state.confirm_shutdown) {
        queue_action("/api/shutdown");
        g_state.engine = false;
        g_state.confirm_shutdown = false;
      } else {
        g_state.confirm_shutdown = true;
      }
    } else if (g_state.selected == 6) {
      g_state.engine = true;
      queue_action("@start");
    } else {
      enter_page(Page::Home);
    }
  } else {
    if (g_state.selected == 0) {
      g_state.game_videos = !g_state.game_videos;
      queue_bool("gamevideos", g_state.game_videos);
    } else if (g_state.selected == 1) {
      queue_action("/api/focusbg?mode=current");
    } else if (g_state.selected == 2) {
      queue_action("/api/focusbg?mode=main");
    } else {
      enter_page(Page::Home);
    }
  }
  g_dirty.store(true, std::memory_order_release);
}

} // namespace

void hypershell_request_open(void) {
  g_wait_for_release.store(true, std::memory_order_release);
  g_open_requested.store(true, std::memory_order_release);
}

bool hypershell_handle_gamepad(GamePadData *data) {
  if (!data)
    return false;

  const auto buttons = static_cast<uint32_t>(data->Buttons);
  const auto previous = static_cast<uint32_t>(data->ButtonsPrev);
#if defined(HYPERHEN_EXPERIMENTAL_HYPERSHELL)
  constexpr uint32_t kShortcut = static_cast<uint32_t>(L1) |
                                 static_cast<uint32_t>(R1) |
                                 static_cast<uint32_t>(Triangle);
  if (dashboard_navigation::hypershell_shortcut_pressed(
          buttons, previous, kShortcut, true)) {
    LOG_DEBUG("HyperShell shortcut activated: buttons=%#x previous=%#x",
              buttons, previous);
    if (g_visible.load(std::memory_order_acquire))
      g_close_requested.store(true, std::memory_order_release);
    else
      hypershell_request_open();
    data->Buttons = None;
    data->ButtonsDown = None;
    data->ButtonsUp = None;
    return true;
  }
#endif
  if (!g_visible.load(std::memory_order_acquire) &&
      !g_open_requested.load(std::memory_order_acquire))
    return false;

  uint32_t pressed = static_cast<uint32_t>(data->ButtonsDown);
  if (!pressed)
    pressed = buttons & ~previous;

  if (g_wait_for_release.load(std::memory_order_acquire)) {
    if (buttons == 0)
      g_wait_for_release.store(false, std::memory_order_release);
    data->Buttons = None;
    data->ButtonsDown = None;
    data->ButtonsUp = None;
    return true;
  }

  if (pressed & (static_cast<uint32_t>(Circle) |
                 static_cast<uint32_t>(Back))) {
    if (g_state.page == Page::Home)
      g_close_requested.store(true, std::memory_order_release);
    else
      enter_page(Page::Home);
  } else if (pressed & static_cast<uint32_t>(Up)) {
    const int count = item_count();
    g_state.selected = (g_state.selected + count - 1) % count;
    g_state.confirm_delete = false;
    g_state.confirm_shutdown = false;
    g_dirty.store(true, std::memory_order_release);
  } else if (pressed & static_cast<uint32_t>(Down)) {
    const int count = item_count();
    g_state.selected = (g_state.selected + 1) % count;
    g_state.confirm_delete = false;
    g_state.confirm_shutdown = false;
    g_dirty.store(true, std::memory_order_release);
  } else if (pressed & static_cast<uint32_t>(Left)) {
    adjust_selected(-1);
  } else if (pressed & static_cast<uint32_t>(Right)) {
    adjust_selected(1);
  } else if (pressed & (static_cast<uint32_t>(Cross) |
                        static_cast<uint32_t>(Enter))) {
    toggle_selected();
  }

  data->Buttons = None;
  data->ButtonsDown = None;
  data->ButtonsUp = None;
  return true;
}

void hypershell_frame(MonoObject *application) {
#if defined(HYPERHEN_EXPERIMENTAL_HYPERSHELL)
  if (g_renderer_armed.load(std::memory_order_acquire))
    hyperhen_liveshell_native_frame(application);
#else
  (void)application;
#endif
  if (g_open_requested.exchange(false, std::memory_order_acq_rel)) {
    g_visible.store(true, std::memory_order_release);
    g_dirty.store(true, std::memory_order_release);
  }

  if (!g_visible.load(std::memory_order_acquire))
    return;

  if (g_close_requested.exchange(false, std::memory_order_acq_rel)) {
    remove_menu();
    g_visible.store(false, std::memory_order_release);
    if (g_toolbox_requested.exchange(false, std::memory_order_acq_rel))
      GoToURI(shellui_debug_settings_toolbox_uri());
    return;
  }

  if (g_dirty.exchange(false, std::memory_order_acq_rel) && !draw_menu()) {
    remove_menu();
    g_visible.store(false, std::memory_order_release);
    g_close_requested.store(false, std::memory_order_release);
    g_toolbox_requested.store(false, std::memory_order_release);
    g_wait_for_release.store(false, std::memory_order_release);
    LOG_WARN("HyperShell dashboard unavailable; normal input restored");
  }
}

