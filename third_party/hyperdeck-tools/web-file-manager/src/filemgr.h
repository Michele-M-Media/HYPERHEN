#pragma once

#include <microhttpd.h>

enum MHD_Result filemgr_api_request(struct MHD_Connection *conn,
                                    const char *url, const char *method,
                                    const char *body, size_t body_size);
enum MHD_Result filemgr_fs_request(struct MHD_Connection *conn);
enum MHD_Result filemgr_pkg_source_request(struct MHD_Connection *conn, const char *url, const char *method);
void filemgr_recover_extract_tasks(void);

int filemgr_upload_begin(struct MHD_Connection *conn, void **upload_ctx);
int filemgr_upload_data(void *upload_ctx, const char *data, size_t size);
enum MHD_Result filemgr_upload_finish(struct MHD_Connection *conn,
                                      void *upload_ctx);
void filemgr_upload_free(void *upload_ctx);
