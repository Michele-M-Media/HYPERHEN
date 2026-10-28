#ifndef CR_PATHS_H
#define CR_PATHS_H

#include <stddef.h>
#include <stdint.h>

#define CHEATRUNNER_DATA_DIR          "/data/cheatrunner"
#define CHEATRUNNER_CHEATS_DIR        "/data/cheatrunner/cheats"
#define CHEATRUNNER_CHEATS_JSON_DIR   "/data/cheatrunner/cheats/json"
#define CHEATRUNNER_CHEATS_SHN_DIR    "/data/cheatrunner/cheats/shn"
#define CHEATRUNNER_CHEATS_MC4_DIR    "/data/cheatrunner/cheats/mc4"
#define ETAHEN_CHEATS_DIR             "/data/etaHEN/cheats"
#define ELF_ARSENAL_CHEATS_DIR        "/data/elf-arsenal/cheats"
#define CHEATRUNNER_PATCHES_DIR       "/data/cheatrunner/patches"
#define CHEATRUNNER_PATCHES_XML_DIR   "/data/cheatrunner/patches/xml"
#define CHEATRUNNER_PATCHES_PS5_DIR   "/data/cheatrunner/patches/xml_prospero"
#define CHEATRUNNER_PATCHES_XML_CR_DIR "/data/cheatrunner/patches/xml/cheatrunner"
#define CHEATRUNNER_PATCHES_PS5_CR_DIR "/data/cheatrunner/patches/xml_prospero/cheatrunner"
#define ELF_ARSENAL_PATCHES_XML_DIR   "/data/elf-arsenal/patches/xml"
#define CHEATRUNNER_CACHE_DIR         "/data/cheatrunner/cache"
#define CHEATRUNNER_CACHE_ICON_DIR    "/data/cheatrunner/cache/icons"
#define CHEATRUNNER_CACHE_PIC0_DIR    "/data/cheatrunner/cache/pic0"
#define CHEATRUNNER_CACHE_REPO_DIR    "/data/cheatrunner/cache/repos"
#define CHEATRUNNER_CACHE_APPDB_DIR   "/data/cheatrunner/cache/appdb"
#define CHEATRUNNER_CACHE_SOURCES_DIR "/data/cheatrunner/cache/sources"
#define CHEATRUNNER_CACHE_TITLE_NAMES_DIR "/data/cheatrunner/cache/title-names"
#define CHEATRUNNER_CONFIG_PATH       "/data/cheatrunner/config.ini"
#define CHEATRUNNER_SOURCES_PATH      "/data/cheatrunner/sources.json"
#define CHEATRUNNER_LOG_PATH          "/data/cheatrunner/logs.txt"
#define CHEATRUNNER_NOTIFICATIONS_PATH "/data/cheatrunner/notifications.json"
#define CHEATRUNNER_ACTIVITY_PATH       "/data/cheatrunner/activity.json"
#define CHEATRUNNER_CRASH_SUSPECTS_PATH "/data/cheatrunner/crash_suspects.json"
#define CHEATRUNNER_FAVORITES_PATH      "/data/cheatrunner/favorites.json"
#define CHEATRUNNER_CHEAT_PROFILES_PATH "/data/cheatrunner/cheat_profiles.json"
#define CHEATRUNNER_PATCH_PROFILES_PATH "/data/cheatrunner/patch_profiles.json"
#define CHEATRUNNER_CRASH_LOG_PATH      "/data/cheatrunner/crash.log"

/* General utilities */
void     str_trim(char *s);
uint64_t now_ms(void);

/* File I/O */
int write_file_atomic(const char *path, const uint8_t *data, size_t len);
int write_file_fast(const char *path, const uint8_t *data, size_t len);
int read_file_bytes(const char *path, uint8_t **out, size_t *out_len);
int read_file_text(const char *path, char **out);

/* Directory helpers */
void ensure_data_dirs(void);
int  ensure_dir_recursive(const char *path);

/* Path/filename validation */
int         is_safe_filename(const char *name);
int         is_safe_repo_rel_path(const char *path);
int         ends_with_case(const char *s, const char *suffix);
const char *path_basename_ptr(const char *path);

#endif /* CR_PATHS_H */
