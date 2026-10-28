/* Media launcher adapted from websrv 0.34 src/ps5/sys.c.
 * Copyright (C) 2024 John Törnblom; HyperHEN adaptation (C) 2026.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "hyperdeck_backend.hpp"
#include <onion/fs.h>
#include <onion/log.h>
#include <ps5/kernel.h>
#include <cerrno>
#include <ctime>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <sys/stat.h>
#include <unistd.h>

extern "C" const unsigned char hyperdeck_param[],hyperdeck_icon[];
extern "C" const unsigned long hyperdeck_param_size,hyperdeck_icon_size;

namespace hyperdeck {
namespace {
bool directory(const char *path) { struct stat st{};if(!lstat(path,&st))return S_ISDIR(st.st_mode);return errno==ENOENT && !mkdir(path,0755); }
bool equal_file(const char *path,const unsigned char *data,size_t size) {
  uint8_t *old=nullptr;size_t old_size=0;
  if(!read_file_alloc(path,size,&old,&old_size))return false;
  const bool same=old_size==size && !std::memcmp(old,data,size);free(old);return same;
}
bool marker_recent(const char *path, time_t max_age_seconds) {
  struct stat marker{};
  if (lstat(path, &marker) != 0 || !S_ISREG(marker.st_mode)) return false;
  const time_t now = time(nullptr);
  return now != static_cast<time_t>(-1) &&
         (marker.st_mtime >= now || now - marker.st_mtime < max_age_seconds);
}
using InstallTitleDir = int (*)(const char *, const char *, void *);
using InstallAll = int (*)(void *);
using AppExists = int (*)(const char *, int *);

struct InstallerApi {
  InstallTitleDir install_title_dir = nullptr;
  InstallAll install_all = nullptr;
  AppExists app_exists = nullptr;
};

template <typename T>
T resolve_installer_export(void *module, uint32_t handle, const char *nid,
                           const char *name) {
  if (module) {
    if (auto symbol = reinterpret_cast<T>(dlsym(module, name))) return symbol;
  }
  if (handle != 0) {
    if (auto symbol = reinterpret_cast<T>(
            kernel_dynlib_resolve(-1, handle, nid))) return symbol;
    return reinterpret_cast<T>(kernel_dynlib_dlsym(-1, handle, name));
  }
  return nullptr;
}

InstallerApi resolve_installer(void *module) {
  uint32_t handle = 0;
  const int result = kernel_dynlib_handle(-1, "libSceAppInstUtil.sprx", &handle);
  if (result != 0) {
    LOG_WARN("HyperDeck Media module handle lookup failed: 0x%08x",
             static_cast<unsigned>(result));
    handle = 0;
  }

  InstallerApi api;
  api.install_title_dir = resolve_installer_export<InstallTitleDir>(
      module, handle, "Wudg3Xe3heE", "sceAppInstUtilAppInstallTitleDir");
  api.install_all = resolve_installer_export<InstallAll>(
      module, handle, "+scQA5stvjs", "sceAppInstUtilAppInstallAll");
  api.app_exists = resolve_installer_export<AppExists>(
      module, handle, "kUT4RpxclMQ", "sceAppInstUtilAppExists");

  LOG_INFO("HyperDeck Media APIs: TitleDir=%s InstallAll=%s AppExists=%s",
           api.install_title_dir ? "yes" : "no",
           api.install_all ? "yes" : "no", api.app_exists ? "yes" : "no");
  if (!api.install_title_dir && !api.install_all)
    LOG_ERROR("HyperDeck Media title registration exports are unavailable");
  return api;
}

int title_exists(const InstallerApi &api, const char *title) {
  if (!api.app_exists) return -1;
  int exists = 0;
  const int result = api.app_exists(title, &exists);
  LOG_INFO("HyperDeck Media AppExists returned 0x%08x exists=%d",
           static_cast<unsigned>(result), exists);
  if (result != 0) return -1;
  return exists != 0 ? 1 : 0;
}

int wait_title_exists(const InstallerApi &api, const char *title) {
  int state = title_exists(api, title);
  for (int attempt = 1; state == 0 && attempt < 20; ++attempt) {
    usleep(100000);
    state = title_exists(api, title);
  }
  return state;
}

bool accepted_install_result(int result) {
  return result == 0 || static_cast<uint32_t>(result) == 0x80990002U;
}
struct InstallerContext {
  uint64_t previous = 0;
  uint8_t previous_caps[16]{};
  bool acquired = false;
  bool caps_acquired = false;
  bool initialized = false;
  void *module = nullptr;
  int (*terminate)(void) = nullptr;
  ~InstallerContext() {
    if (initialized && terminate) terminate();
    if (module) dlclose(module);
    if (caps_acquired && kernel_set_ucred_caps(-1, previous_caps))
      LOG_ERROR("HyperDeck installer capability restore failed");
    if (acquired && kernel_set_ucred_authid(-1, previous))
      LOG_ERROR("HyperDeck installer context restore failed");
  }
  bool prepare() {
    previous = kernel_get_ucred_authid(-1);
    if (!previous || kernel_set_ucred_authid(-1, 0x4801000000000013ULL)) {
      LOG_ERROR("HyperDeck installer context unavailable");
      return false;
    }
    acquired = true;
    if (kernel_get_ucred_caps(-1, previous_caps) == 0) {
      uint8_t installer_caps[16];
      std::memset(installer_caps, 0xff, sizeof(installer_caps));
      if (kernel_set_ucred_caps(-1, installer_caps) == 0)
        caps_acquired = true;
      else
        LOG_WARN("HyperDeck installer capability elevation unavailable");
    } else {
      LOG_WARN("HyperDeck installer capabilities could not be saved");
    }
    // Load AppInstUtil at the optional post-pipeline stage. A new bootstrap
    // DT_NEEDED dependency could prevent the core itself from starting.
    module = dlopen("/system/common/lib/libSceAppInstUtil.sprx", RTLD_LAZY);
    auto initialize = module ? reinterpret_cast<int (*)(void)>(
        dlsym(module,"sceAppInstUtilInitialize")) : nullptr;
    terminate = module ? reinterpret_cast<int (*)(void)>(
        dlsym(module,"sceAppInstUtilTerminate")) : nullptr;
    uint32_t handle = 0;
    if ((!initialize || !terminate) &&
        kernel_dynlib_handle(-1,"libSceAppInstUtil.sprx",&handle)==0) {
      if (!initialize) initialize = reinterpret_cast<int (*)(void)>(
          kernel_dynlib_dlsym(-1,handle,"sceAppInstUtilInitialize"));
      if (!terminate) terminate = reinterpret_cast<int (*)(void)>(
          kernel_dynlib_dlsym(-1,handle,"sceAppInstUtilTerminate"));
    }
    if (!initialize || !terminate) {
      LOG_ERROR("HyperDeck AppInstUtil lifecycle exports unavailable");return false;
    }
    const int result = initialize();
    initialized = result == 0;
    if (!initialized)
      LOG_ERROR("HyperDeck installer initialization failed: 0x%08x", static_cast<unsigned>(result));
    return initialized;
  }
};
}
bool install_media_launcher() {
  constexpr const char *title="HYPR00001";
  constexpr const char *param="/user/app/HYPR00001/sce_sys/param.json";
  constexpr const char *icon="/user/app/HYPR00001/sce_sys/icon0.png";
  constexpr const char *top_icon="/user/app/HYPR00001/icon0.png";
  constexpr const char *metadata="/user/appmeta/HYPR00001";
  constexpr const char *metadata_param="/user/appmeta/HYPR00001/param.json";
  constexpr const char *metadata_icon="/user/appmeta/HYPR00001/icon0.png";
  constexpr const char *owner="/user/app/HYPR00001/.hyperdeck-owner";
  constexpr const char *registered="/user/app/HYPR00001/.hyperdeck-media-registered";
  constexpr const char *pending="/user/app/HYPR00001/.hyperdeck-media-pending";
  constexpr unsigned char ownership[]="HyperHEN Hyperdeck Media launcher v1\n";
  constexpr unsigned char registration[]="HyperHEN Hyperdeck Media registration v2\n";
  constexpr unsigned char pending_registration[]="HyperHEN Hyperdeck Media registration pending v2\n";
  struct stat existing{};
  const bool app_present=lstat("/user/app/HYPR00001",&existing)==0;
  const bool app_owned=equal_file(owner,ownership,sizeof(ownership)-1) ||
                       equal_file(param,hyperdeck_param,hyperdeck_param_size);
  if(app_present && (!S_ISDIR(existing.st_mode) || !app_owned)) {
    LOG_WARN("Hyperdeck title ID is occupied by an unrecognized app; refusing replacement");return false;
  }
  struct stat metadata_existing{},metadata_manifest{};
  if(lstat(metadata,&metadata_existing)==0) {
    const int manifest_result=lstat(metadata_param,&metadata_manifest);
    if(!S_ISDIR(metadata_existing.st_mode) ||
       (manifest_result==0 &&
        !equal_file(metadata_param,hyperdeck_param,hyperdeck_param_size)) ||
       (manifest_result!=0 && (errno!=ENOENT || !app_owned))) {
      LOG_WARN("Hyperdeck appmeta title ID is occupied by unrecognized metadata; refusing replacement");return false;
    }
  }
  const bool unchanged=equal_file(param,hyperdeck_param,hyperdeck_param_size) &&
                       equal_file(icon,hyperdeck_icon,hyperdeck_icon_size) &&
                       equal_file(top_icon,hyperdeck_icon,hyperdeck_icon_size) &&
                       equal_file(metadata_param,hyperdeck_param,hyperdeck_param_size) &&
                       equal_file(metadata_icon,hyperdeck_icon,hyperdeck_icon_size);
  InstallerContext context;
  if (!context.prepare()) return false;
  const InstallerApi api = resolve_installer(context.module);
  if (unchanged) {
    if (api.app_exists) {
      const int exists = title_exists(api, title);
      if (exists == 1) {
        if (!equal_file(registered,registration,sizeof(registration)-1) &&
            !write_file_atomic(registered,registration,sizeof(registration)-1)) {
          LOG_WARN("Hyperdeck Media launcher confirmation marker could not be saved");
        }
        (void)unlink(pending);
        LOG_INFO("Hyperdeck Media launcher already registered through AppExists");
        return true;
      }
    } else if (marker_recent(pending,90)) {
      LOG_INFO("Hyperdeck Media registration remains pending system confirmation");
      return false;
    }
  }
  if (!api.install_title_dir && !api.install_all && !api.app_exists) return false;
  // A stale marker cannot prove that the system still owns this title.
  (void)unlink(registered);
  (void)unlink(pending);
  LOG_INFO("HyperDeck Media staging HYPR00001 in installer context");
  if(!directory("/user/app/HYPR00001") || !directory("/user/app/HYPR00001/sce_sys") || !directory(metadata) ||
     !write_file_atomic(owner,ownership,sizeof(ownership)-1) ||
     !write_file_atomic(icon,hyperdeck_icon,hyperdeck_icon_size) ||
     !write_file_atomic(param,hyperdeck_param,hyperdeck_param_size) ||
     !write_file_atomic(top_icon,hyperdeck_icon,hyperdeck_icon_size) ||
     !write_file_atomic(metadata_icon,hyperdeck_icon,hyperdeck_icon_size) ||
     !write_file_atomic(metadata_param,hyperdeck_param,hyperdeck_param_size)) {
    LOG_WARN("Hyperdeck Media launcher staging failed: %s",std::strerror(errno));return false;
  }
  int result = -1;
  bool accepted = false;
  int exists = -1;
  if (api.install_title_dir) {
    LOG_INFO("HyperDeck Media registering HYPR00001 through AppInstallTitleDir");
    result = api.install_title_dir(title,"/user/app/",nullptr);
    accepted = accepted_install_result(result);
    LOG_INFO("HyperDeck Media AppInstallTitleDir returned 0x%08x",
             static_cast<unsigned>(result));
    if (api.app_exists) exists = wait_title_exists(api, title);
  }
  if (exists != 1 && api.install_all) {
    LOG_INFO("HyperDeck Media registering HYPR00001 through AppInstallAll");
    result = api.install_all(nullptr);
    accepted = accepted_install_result(result);
    LOG_INFO("HyperDeck Media AppInstallAll returned 0x%08x",
             static_cast<unsigned>(result));
  }
  if (api.app_exists) exists = wait_title_exists(api, title);
  const bool confirmed = exists == 1;
  if(confirmed) {
    if(!write_file_atomic(registered,registration,sizeof(registration)-1)) {
      LOG_WARN("Hyperdeck Media registration succeeded but its confirmation marker could not be saved");
    }
    (void)unlink(pending);
    LOG_INFO("Hyperdeck Media registration confirmed (0x%08x)",static_cast<unsigned>(result));
    return true;
  }
  if(accepted) {
    if (!write_file_atomic(pending,pending_registration,sizeof(pending_registration)-1))
      LOG_WARN("Hyperdeck Media registration was accepted but its pending marker could not be saved");
    LOG_INFO("Hyperdeck Media registration accepted; awaiting system confirmation (0x%08x)",static_cast<unsigned>(result));
    return false;
  }
  (void)unlink(pending);
  (void)unlink(registered);
  LOG_WARN("Hyperdeck Media registration unavailable: 0x%08x",static_cast<unsigned>(result));
  return false;
}
}
