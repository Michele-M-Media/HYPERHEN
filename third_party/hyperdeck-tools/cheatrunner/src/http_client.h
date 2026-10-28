#pragma once

#include <stddef.h>
#include <stdint.h>

int http_get_url_ex(const char *agent, const char *url, size_t max_bytes,
                    int *status_out, uint8_t **data_out, size_t *len_out);
int http_get_url_ex_timeout(const char *agent, const char *url, size_t max_bytes, int timeout_ms,
                            int *status_out, uint8_t **data_out, size_t *len_out);

/* Optional download-progress callback: called after each read chunk.
 * received = bytes read so far; total = Content-Length (0 if unknown). */
typedef void (*http_progress_fn_t)(size_t received, size_t total, void *ud);
int http_get_url_ex_timeout_progress(const char *agent, const char *url, size_t max_bytes,
                                     int timeout_ms, int *status_out, uint8_t **data_out,
                                     size_t *len_out, http_progress_fn_t pfn, void *pud);

/* A reusable HTTP/TLS stack: initialised once, then one request per call. */
typedef struct http_session http_session_t;
http_session_t *http_session_open(const char *agent);
void http_session_close(http_session_t *se);
int http_session_get(http_session_t *se, const char *url, size_t max_bytes, int timeout_ms,
                     int *status_out, uint8_t **data_out, size_t *len_out);
int http_session_download_file(http_session_t *se, const char *url, const char *path, int timeout_ms,
                               int *status_out, size_t *bytes_out, http_progress_fn_t pfn, void *pud);
void http_session_last_timing(http_session_t *se, long long *read_ms, long long *write_ms, long long *total_ms);
