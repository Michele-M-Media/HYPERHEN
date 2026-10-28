#pragma once

#include <dirent.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <unistd.h>

typedef struct wfm_dir wfm_dir_t;
int wfm_stat(const char *path, struct stat *st);
int wfm_lstat(const char *path, struct stat *st);
int wfm_fstat(int fd, struct stat *st);
int wfm_statvfs(const char *path, struct statvfs *st);
wfm_dir_t *wfm_opendir(const char *path);
struct dirent *wfm_readdir(wfm_dir_t *dir);
/* Sticky enumeration error; EOF leaves this at zero. */
int wfm_dir_error(wfm_dir_t *dir);
int wfm_closedir(wfm_dir_t *dir);
int wfm_open(const char *path, int flags, ...);
int wfm_close(int fd);
ssize_t wfm_read(int fd, void *data, size_t size);
ssize_t wfm_write(int fd, const void *data, size_t size);
ssize_t wfm_pread(int fd, void *data, size_t size, off_t offset);
off_t wfm_lseek(int fd, off_t offset, int whence);
int wfm_fsync(int fd);
int wfm_mkdir(const char *path, mode_t mode);
int wfm_rmdir(const char *path);
int wfm_unlink(const char *path);
int wfm_rename(const char *src, const char *dst);
int wfm_access(const char *path, int mode);
int wfm_chmod(const char *path, mode_t mode);
int wfm_fchmod(int fd, mode_t mode);
int wfm_fd_remote(int fd);
/* Expected file/range size; no-op for native descriptors. */
void wfm_set_transfer_size(int fd, uint64_t size);

/* Keep existing filesystem code on one interface, including native paths. */
#ifndef WFM_VFS_IMPLEMENTATION
#define DIR wfm_dir_t
#define stat(...) wfm_stat(__VA_ARGS__)
#define lstat(...) wfm_lstat(__VA_ARGS__)
#define fstat(...) wfm_fstat(__VA_ARGS__)
#define statvfs(...) wfm_statvfs(__VA_ARGS__)
#define opendir(...) wfm_opendir(__VA_ARGS__)
#define readdir(...) wfm_readdir(__VA_ARGS__)
#define closedir(...) wfm_closedir(__VA_ARGS__)
#define open(...) wfm_open(__VA_ARGS__)
#define close(...) wfm_close(__VA_ARGS__)
#define read(...) wfm_read(__VA_ARGS__)
#define write(...) wfm_write(__VA_ARGS__)
#define pread(...) wfm_pread(__VA_ARGS__)
#define lseek(...) wfm_lseek(__VA_ARGS__)
#define fsync(...) wfm_fsync(__VA_ARGS__)
#define mkdir(...) wfm_mkdir(__VA_ARGS__)
#define rmdir(...) wfm_rmdir(__VA_ARGS__)
#define unlink(...) wfm_unlink(__VA_ARGS__)
#define rename(...) wfm_rename(__VA_ARGS__)
#define access(...) wfm_access(__VA_ARGS__)
#define chmod(...) wfm_chmod(__VA_ARGS__)
#define fchmod(...) wfm_fchmod(__VA_ARGS__)
#endif
