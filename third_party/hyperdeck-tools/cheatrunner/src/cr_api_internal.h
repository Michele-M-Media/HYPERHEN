#ifndef CR_API_INTERNAL_H
#define CR_API_INTERNAL_H

/* Handlers live in cr_api.c; declared here so the split routing modules can call them. */

#include <stddef.h>
#include <stdint.h>

/* ---- HTTP send helpers (defined in cr_api.c) ---- */
void http_send_json(int fd, int status, const char *body);
void http_send_oom(int fd);
void http_send_response(int fd, int status, const char *content_type,
                        const uint8_t *body, size_t body_len);
void http_send_response_cached(int fd, int status, const char *content_type,
                               const uint8_t *body, size_t body_len);
void http_send_response_gzip(int fd, int status, const char *content_type,
                             const uint8_t *body, size_t body_len, int cacheable);
int  query_value(const char *query, const char *key, char *out, size_t out_size);

/* ---- /api/logs ---- */
void handle_api_logs(int fd, const char *query);
void handle_api_logs_clear(int fd);
void handle_api_health(int fd);

/* ---- /api/games, /appdb, /launch ---- */
void handle_api_games(int fd, const char *query);
void handle_appdb_icon(int fd, const char *query);
void handle_appdb_lookup(int fd, const char *query);
void handle_api_titles_lookup(int fd, const char *query);
void handle_appdb_pic0(int fd, const char *query);
void handle_launch(int fd, const char *query);
void handle_api_running(int fd);
void handle_api_launch_status(int fd);
void handle_api_debug_process(int fd, const char *query);
void handle_api_status(int fd);
void handle_api_state(int fd);

/* ---- /api/cheats (local, non-remote) ---- */
void handle_api_cheats_index(int fd);
void handle_api_cheats_get(int fd, const char *query);
void handle_api_cheats_state(int fd, const char *query);
void handle_api_cheats_debug(int fd, const char *query);
void handle_api_cheats_toggle(int fd, const char *query);
void handle_api_cheats_apply_dryrun(int fd, const char *query);
void handle_api_cheats_engine(int fd);
void handle_api_cheats_engine_toggle(int fd, const char *query);
void handle_api_cheats_delete(int fd, const char *query);
void handle_api_cheats_find(int fd, const char *query);
void handle_api_cheats_raw(int fd, const char *query);
void handle_api_cheats_validate(int fd, const char *query);
void handle_api_cheats_address_debug(int fd, const char *query);
void handle_api_cheats_list(int fd);
void handle_api_cheats_download(int fd, const char *query);
void handle_api_cheats_scan(int fd, const char *query);
void handle_api_cheats_mc4_debug(int fd, const char *query);
void handle_api_cheats_clear_crash_flags(int fd, const char *query);
void handle_api_cheats_disable_all(int fd, const char *query);
void handle_api_cheats_select(int fd, const char *query);
void handle_api_cheats_select_auto(int fd, const char *query);
void handle_api_cheats_upload(int fd, const char *query, const char *body, size_t body_len);
void handle_api_cheats_download_all(int fd);
void handle_api_cheats_download_all_status(int fd);
void handle_api_cheats_repo_download(int fd, const char *query);
void handle_api_cheats_repo_download_status(int fd);

/* ---- /api/sources, /api/cheats/remote, /api/cheats/repo ---- */

/* ---- /api/dev, /api/cfg ---- */
void handle_api_config(int fd);
void handle_api_config_set(int fd, const char *query);
void handle_api_config_reset(int fd);
void handle_api_config_preset(int fd, const char *query);
void handle_api_notifications(int fd);
void handle_api_notifications_read(int fd, const char *query);
void handle_api_notifications_clear(int fd);
void handle_api_activity(int fd);
void handle_api_activity_title(int fd, const char *query);
void handle_api_activity_reset(int fd);
void handle_api_dev_privileges(int fd);
void handle_api_dev_diag(int fd);
void handle_api_dev_open_browser(int fd, const char *method, const char *query);
void handle_api_dev_memtest(int fd);
void handle_api_dev_shutdown(int fd, const char *method, const char *query,
                             const char *client_ip);
void handle_api_user_context(int fd);
void handle_api_diag_title(int fd, const char *query);

/* ---- dashboard static assets ---- */
void http_send_png_asset(int fd);

#endif /* CR_API_INTERNAL_H */
