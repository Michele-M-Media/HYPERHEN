#ifndef CR_API_GAMES_H
#define CR_API_GAMES_H

#include <stddef.h>

int cr_api_games_handle(int fd, const char *method, const char *path,
                         const char *query, const char *body, size_t body_len);

#endif /* CR_API_GAMES_H */
