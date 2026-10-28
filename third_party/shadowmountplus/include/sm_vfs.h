/* SMB filesystem adapter derived from owendswang/ps5-web-file-manager
 * (src/vfs.c), GPL-3.0-or-later. See licenses/SMB-NOTICES.txt.
 * ShadowMountPlus integration and named-connection support. */
#pragma once

#include <dirent.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <unistd.h>

typedef struct sm_vfs_dir sm_vfs_dir_t;
int sm_vfs_stat(const char *path, struct stat *st);
int sm_vfs_lstat(const char *path, struct stat *st);
int sm_vfs_fstat(int fd, struct stat *st);
int sm_vfs_statvfs(const char *path, struct statvfs *st);
sm_vfs_dir_t *sm_vfs_opendir(const char *path);
struct dirent *sm_vfs_readdir(sm_vfs_dir_t *dir);
/* Sticky enumeration error; EOF leaves this at zero. */
int sm_vfs_dir_error(sm_vfs_dir_t *dir);
int sm_vfs_closedir(sm_vfs_dir_t *dir);
int sm_vfs_open(const char *path, int flags, ...);
int sm_vfs_close(int fd);
ssize_t sm_vfs_read(int fd, void *data, size_t size);
ssize_t sm_vfs_write(int fd, const void *data, size_t size);
ssize_t sm_vfs_pread(int fd, void *data, size_t size, off_t offset);
off_t sm_vfs_lseek(int fd, off_t offset, int whence);
int sm_vfs_fsync(int fd);
/* Queue one SMB flush; fsync/close must consume its result before releasing fd. */
int sm_vfs_start_fsync(int fd);
/* Check SMB parent write and target delete access without changing data. */
int sm_vfs_delete_access(const char *path, bool directory);
/* Check SMB directory creation rights without creating a probe file. */
int sm_vfs_mkdir_access(const char *path);
int sm_vfs_mkdir(const char *path, mode_t mode);
int sm_vfs_rmdir(const char *path);
int sm_vfs_unlink(const char *path);
int sm_vfs_rename(const char *src, const char *dst);
int sm_vfs_access(const char *path, int mode);
int sm_vfs_chmod(const char *path, mode_t mode);
int sm_vfs_fchmod(int fd, mode_t mode);
int sm_vfs_remote_fd(int fd);

char *sm_vfs_realpath(const char *path, char *out);
int sm_vfs_fdatasync(int fd);
int sm_vfs_fadvise(int fd, off_t offset, off_t length, int advice);

/* Keep existing filesystem code on one interface, including native paths. */
#if defined(SM_VFS_ENABLED) && !defined(SM_VFS_IMPLEMENTATION)
#define DIR sm_vfs_dir_t
#define realpath(...) sm_vfs_realpath(__VA_ARGS__)
#define fdatasync(...) sm_vfs_fdatasync(__VA_ARGS__)
#define posix_fadvise(...) sm_vfs_fadvise(__VA_ARGS__)
#define stat(...) sm_vfs_stat(__VA_ARGS__)
#define lstat(...) sm_vfs_lstat(__VA_ARGS__)
#define fstat(...) sm_vfs_fstat(__VA_ARGS__)
#define statvfs(...) sm_vfs_statvfs(__VA_ARGS__)
#define opendir(...) sm_vfs_opendir(__VA_ARGS__)
#define readdir(...) sm_vfs_readdir(__VA_ARGS__)
#define closedir(...) sm_vfs_closedir(__VA_ARGS__)
#define open(...) sm_vfs_open(__VA_ARGS__)
#define close(...) sm_vfs_close(__VA_ARGS__)
#define read(...) sm_vfs_read(__VA_ARGS__)
#define write(...) sm_vfs_write(__VA_ARGS__)
#define pread(...) sm_vfs_pread(__VA_ARGS__)
#define lseek(...) sm_vfs_lseek(__VA_ARGS__)
#define fsync(...) sm_vfs_fsync(__VA_ARGS__)
#define mkdir(...) sm_vfs_mkdir(__VA_ARGS__)
#define rmdir(...) sm_vfs_rmdir(__VA_ARGS__)
#define unlink(...) sm_vfs_unlink(__VA_ARGS__)
#define rename(...) sm_vfs_rename(__VA_ARGS__)
#define access(...) sm_vfs_access(__VA_ARGS__)
#define chmod(...) sm_vfs_chmod(__VA_ARGS__)
#define fchmod(...) sm_vfs_fchmod(__VA_ARGS__)
#endif
