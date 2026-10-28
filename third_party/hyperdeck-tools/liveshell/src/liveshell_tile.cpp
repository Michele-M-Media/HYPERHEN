/*
 * liveshell_tile.cpp - the LiveShell tile on the home screen. GPL-3.0.
 *
 * No PKG: a title folder /user/app/LVSH00001/sce_sys/{param.json, icon0.png} whose param.json is a
 * web-app entry (applicationCategoryType 65536) with a deep link to our window (kWebUrl). The
 * console's installer registers the folder (sceAppInstUtilAppInstallTitleDir, looked up by NID as
 * there is no stub for it). If the payload's own rights are refused (newer firmware), the call is
 * repeated as ShellCore and the rights are put back.
 */
#include "liveshell.hpp"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include <ps5/kernel.h>

extern "C" {
extern const uint8_t liveshell_icon_png[];
extern const uint8_t liveshell_icon_png_end[];
}

__asm__(".section .rodata\n"
        ".global liveshell_icon_png\n"
        ".global liveshell_icon_png_end\n"
        ".align  16\n"
        "liveshell_icon_png:\n"
        ".incbin \"" LIVESHELL_ICON_PNG "\"\n"
        "liveshell_icon_png_end:\n"
        ".previous\n");

#ifndef HYPERHEN_NO_MEDIA_TILE
namespace liveshell {
namespace {

constexpr const char* kTitleId   = "LVSH00001";
constexpr const char* kAppRoot   = "/user/app/";
constexpr const char* kAppDir    = "/user/app/LVSH00001";
constexpr const char* kSysDir    = "/user/app/LVSH00001/sce_sys";
constexpr const char* kTileMarker = "/data/LiveShell/tile_added";
constexpr uint64_t    kShellCore = 0x4801000000000013ULL;

using TitleDirFn = int (*)(const char* title_id, const char* root, void* opt);
using VoidFn     = int (*)();

// No SDK stub for libSceAppInstUtil: everything is looked up in the loaded module.
VoidFn g_inst_init = nullptr;
VoidFn g_inst_term = nullptr;

std::string manifest() {
	std::string names;
	for (const char* lang : {"en-US", "en-GB", "fr-FR", "de-DE", "es-ES", "it-IT", "pt-BR", "ru-RU", "ja-JP",
	                         "ko-KR", "zh-Hans", "ar-AE"}) {
		names += std::string(",\n    \"") + lang + "\": { \"titleName\": \"LiveShell Engine\" }";
	}
	return std::string("{\n  \"titleId\": \"") + kTitleId + "\",\n" +
	       "  \"applicationCategoryType\": 65536,\n" +
	       "  \"deeplinkUri\": \"" + kWebUrl + "\",\n" + "  \"localizedParameters\": {\n" +
	       "    \"defaultLanguage\": \"en-US\"" + names + "\n  }\n}\n";
}

// Writes the file when its content differs. 1 = written, 0 = unchanged, -1 = error.
int put_file(const std::string& path, const void* data, size_t size) {
	struct stat st {};
	if (stat(path.c_str(), &st) == 0 && static_cast<size_t>(st.st_size) == size) {
		std::string old(size, '\0');
		const int   fd = open(path.c_str(), O_RDONLY);
		const bool  same = fd >= 0 && read(fd, &old[0], size) == static_cast<ssize_t>(size) && memcmp(old.data(), data, size) == 0;
		if (fd >= 0) {
			close(fd);
		}
		if (same) {
			return 0;
		}
	}
	const std::string tmp = path + ".tmp";
	const int         fd  = open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (fd < 0) {
		log("tile: cannot write %s (errno %d)", tmp.c_str(), errno);
		return -1;
	}
	const bool ok = write(fd, data, size) == static_cast<ssize_t>(size);
	close(fd);
	if (!ok || rename(tmp.c_str(), path.c_str()) != 0) {
		log("tile: cannot store %s (errno %d)", path.c_str(), errno);
		unlink(tmp.c_str());
		return -1;
	}
	return 1;
}

using LoadStartFn = int (*)(const char* path, size_t args, const void* argp, uint32_t flags, void* opt, int* res);

// The payload is not linked against libSceAppInstUtil, so load it through sceKernelLoadStartModule.
bool load_appinstutil(uint32_t& handle) {
	if (kernel_dynlib_handle(-1, "libSceAppInstUtil.sprx", &handle) == 0 && handle != 0) {
		return true;
	}
	uint32_t kh = 0;
	for (const char* name : {"libkernel_web.sprx", "libkernel.sprx", "libkernel_sys.sprx"}) {
		if (kernel_dynlib_handle(-1, name, &kh) == 0 && kh != 0) {
			break;
		}
	}
	auto load = kh != 0 ? reinterpret_cast<LoadStartFn>(kernel_dynlib_resolve(-1, kh, "wzvqT4UqKX8")) : nullptr;
	if (load == nullptr) {
		log("tile: sceKernelLoadStartModule not found (libkernel handle %#x)", kh);
		return false;
	}
	for (const char* root : {"/system/common/lib/", "/system/priv/lib/", "/system_ex/common_ex/lib/"}) {
		const std::string path = std::string(root) + "libSceAppInstUtil.sprx";
		int               res  = 0;
		const int         id   = load(path.c_str(), 0, nullptr, 0, nullptr, &res);
		log("tile: load %s -> %#x (res %d)", path.c_str(), static_cast<unsigned>(id), res);
		if (id >= 0 && kernel_dynlib_handle(-1, "libSceAppInstUtil.sprx", &handle) == 0 && handle != 0) {
			return true;
		}
	}
	return false;
}

using InstallAllFn = int (*)(void* opt);
using AppExistsFn  = int (*)(const char* title_id, int* exists);
TitleDirFn   g_title_dir   = nullptr;
InstallAllFn g_install_all = nullptr;
AppExistsFn  g_app_exists  = nullptr;

// Per-title install first; where it is missing (fw 13.60) or fails, AppInstallAll registers every
// title folder under /user/app. Success = rc 0 or AppExists says yes (on 13.60 no
// /user/appmeta/<id> folder shows up, so that is not a usable check).
int register_once() {
	int rc = g_inst_init();
	if (rc != 0) {
		log("tile: AppInstUtil init rc %#x (authid %#lx)", static_cast<unsigned>(rc),
		    static_cast<unsigned long>(kernel_get_ucred_authid(-1)));
		return rc;
	}
	rc = -1;
	if (g_title_dir != nullptr) {
		rc = g_title_dir(kTitleId, kAppRoot, nullptr);
		log("tile: AppInstallTitleDir(%s) rc %#x", kTitleId, static_cast<unsigned>(rc));
	}
	if (rc != 0 && g_install_all != nullptr) {
		rc = g_install_all(nullptr);
		log("tile: AppInstallAll rc %#x (authid %#lx)", static_cast<unsigned>(rc),
		    static_cast<unsigned long>(kernel_get_ucred_authid(-1)));
	}
	int exists = -1;
	if (g_app_exists != nullptr) {
		int e = 0;
		exists = g_app_exists(kTitleId, &e) == 0 ? (e != 0 ? 1 : 0) : -1;
		log("tile: AppExists(%s) = %d", kTitleId, exists);
	}
	g_inst_term();
	return (rc == 0 || exists == 1) ? 0 : (rc != 0 ? rc : -1);
}

template <typename T>
T appinst_fn(uint32_t handle, const char* nid, const char* name) {
	void* fn = reinterpret_cast<void*>(kernel_dynlib_resolve(-1, handle, nid));
	if (fn == nullptr) {
		fn = reinterpret_cast<void*>(kernel_dynlib_dlsym(-1, handle, name));
	}
	return reinterpret_cast<T>(fn);
}

} // namespace

int install_tile() {
	mkdir(kAppDir, 0777);
	mkdir(kSysDir, 0777);
	const std::string json = manifest();
	const int         a    = put_file(std::string(kSysDir) + "/param.json", json.data(), json.size());
	const int         b    = put_file(std::string(kSysDir) + "/icon0.png", liveshell_icon_png,
	                                  static_cast<size_t>(liveshell_icon_png_end - liveshell_icon_png));
	if (a < 0 || b < 0) {
		return -1;
	}
	// Registered on every start (a tile removed from the home screen comes back). "Added"
	// notification only when the tile was NOT installed before this start (AppExists); the old
	// marker file stayed after the tile was deleted, so the notice never came back (test 33).
	bool first = access(kTileMarker, F_OK) != 0; // fallback if AppExists is unavailable

	uint32_t handle = 0;
	if (!load_appinstutil(handle)) {
		log("tile: libSceAppInstUtil could not be loaded");
		return -1;
	}
	g_title_dir   = appinst_fn<TitleDirFn>(handle, "Wudg3Xe3heE", "sceAppInstUtilAppInstallTitleDir");
	g_install_all = appinst_fn<InstallAllFn>(handle, "+scQA5stvjs", "sceAppInstUtilAppInstallAll");
	g_app_exists  = appinst_fn<AppExistsFn>(handle, "kUT4RpxclMQ", "sceAppInstUtilAppExists");
	g_inst_init = reinterpret_cast<VoidFn>(kernel_dynlib_dlsym(-1, handle, "sceAppInstUtilInitialize"));
	g_inst_term = reinterpret_cast<VoidFn>(kernel_dynlib_dlsym(-1, handle, "sceAppInstUtilTerminate"));
	log("tile: titledir %p installall %p init %p term %p", reinterpret_cast<void*>(g_title_dir),
	    reinterpret_cast<void*>(g_install_all), reinterpret_cast<void*>(g_inst_init), reinterpret_cast<void*>(g_inst_term));
	if ((g_title_dir == nullptr && g_install_all == nullptr) || g_inst_init == nullptr || g_inst_term == nullptr) {
		return -1;
	}
	if (g_app_exists != nullptr && g_inst_init() == 0) {
		int e = 0;
		if (g_app_exists(kTitleId, &e) == 0) {
			first = e == 0;
		}
		g_inst_term();
		log("tile: installed before this start: %s", first ? "no" : "yes");
	}

	int rc = register_once();
	if (rc != 0) {
		// Own rights refused: once more as ShellCore, then restore.
		const uint64_t old_authid = kernel_get_ucred_authid(-1);
		uint8_t        old_caps[16] {};
		const bool     have_caps = kernel_get_ucred_caps(-1, old_caps) == 0;
		uint8_t        all_caps[16];
		memset(all_caps, 0xff, sizeof(all_caps));
		kernel_set_ucred_authid(-1, kShellCore);
		kernel_set_ucred_caps(-1, all_caps);
		rc = register_once();
		kernel_set_ucred_authid(-1, old_authid);
		if (have_caps) {
			kernel_set_ucred_caps(-1, old_caps);
		}
	}
	log("tile: %s (rc %#x)", rc == 0 ? "on the home screen" : "NOT registered", static_cast<unsigned>(rc));
	if (rc != 0) {
		return -1;
	}
	if (first) {
		const int fd = open(kTileMarker, O_WRONLY | O_CREAT | O_TRUNC, 0666);
		if (fd >= 0) {
			close(fd);
		}
	}
	return first ? 1 : 0;
}

} // namespace liveshell
#endif
