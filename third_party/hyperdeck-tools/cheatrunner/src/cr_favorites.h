#ifndef CR_FAVORITES_H
#define CR_FAVORITES_H

/* Favorites and recents for game tiles, stored in /data/cheatrunner/favorites.json so every
 * device sees the same lists. */

void favorites_load(void);

/* HTTP handlers (routed from cr_api_games_handle). */
void handle_api_favorites_get(int fd);
void handle_api_favorites_set(int fd, const char *query);
void handle_api_favorites_recent(int fd, const char *query);

#endif /* CR_FAVORITES_H */
