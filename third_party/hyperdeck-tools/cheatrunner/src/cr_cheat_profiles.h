#ifndef CR_CHEAT_PROFILES_H
#define CR_CHEAT_PROFILES_H

#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Loads saved profiles from disk; call once at boot. */
void cheat_profiles_load(void);

/* Replaces the saved autoload profile for title_id. count==0 clears it. */
int  cheat_profiles_save(const char *title_id, const char *const *mod_names, int count);

/* Writes the saved profile as a JSON array of mod-name strings, e.g. ["A","B"]. */
int  cheat_profiles_get_json(const char *title_id, char *out, size_t out_sz);

void cheat_profiles_clear(const char *title_id);
int  cheat_profiles_has(const char *title_id);

/* No-op if no profile saved. Blocks until done — call from a background thread. */
void cheat_profiles_autoload_for_title(const char *title_id, pid_t pid);

#ifdef __cplusplus
}
#endif

#endif /* CR_CHEAT_PROFILES_H */
