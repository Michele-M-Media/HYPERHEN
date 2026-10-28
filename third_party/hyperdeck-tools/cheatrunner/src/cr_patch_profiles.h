#ifndef CR_PATCH_PROFILES_H
#define CR_PATCH_PROFILES_H

#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Loads saved profiles from disk; call once at boot. */
void patch_profiles_load(void);

/* Replaces the saved autoload profile for title_id. count==0 clears it. */
int  patch_profiles_save(const char *title_id, const char *const *entry_ids, int count);

/* Writes the saved profile as a JSON array of entryId strings, e.g. ["A1B2C3D4"]. */
int  patch_profiles_get_json(const char *title_id, char *out, size_t out_sz);

void patch_profiles_clear(const char *title_id);
int  patch_profiles_has(const char *title_id);

/* No-op if no profile saved. Blocks until done — call from a background thread. */
void patch_profiles_autoload_for_title(const char *title_id, pid_t pid);

#ifdef __cplusplus
}
#endif

#endif /* CR_PATCH_PROFILES_H */
