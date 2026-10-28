#ifndef CR_API_CHEAT_PROFILES_H
#define CR_API_CHEAT_PROFILES_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int cr_api_cheat_profiles_handle(int fd, const char *method, const char *path,
                                  const char *query, const char *body, size_t body_len);

#ifdef __cplusplus
}
#endif

#endif /* CR_API_CHEAT_PROFILES_H */
