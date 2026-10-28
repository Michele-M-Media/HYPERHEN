#pragma once

#define PKG_INSTALL_UNSUPPORTED 0x7fffffff

int pkg_installer_initialize(void);
int pkg_installer_install(const char *path);

/* An acquired source keeps its file/context alive until release. */
typedef struct pkg_install_source pkg_install_source_t;
pkg_install_source_t *pkg_installer_source_acquire(const char *name);
int pkg_installer_source_fd(const pkg_install_source_t *source);
void pkg_installer_source_release(pkg_install_source_t *source);
