#ifndef CR_API_PATCHES_H
#define CR_API_PATCHES_H

#include <stddef.h>

int cr_api_patches_handle(int fd, const char *method, const char *path,
                           const char *query, const char *body, size_t body_len);

/* Applies one patch entry by its stable entryId. force=1 bypasses the version
 * guard. err gets a short machine-readable reason on failure. */
int patch_apply_by_entry_id(const char *title_id, const char *entry_id, int force,
                            char *err, size_t err_size);

#endif /* CR_API_PATCHES_H */
