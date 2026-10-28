#pragma once

#include <microhttpd.h>

typedef struct pkg_details {
  char content_id[37];
  char title[512];
  int platform; /* 4 = PS4, 5 = PS5; not an input to pkg_info.platform. */
  int has_icon;
} pkg_details_t;

int pkg_read_details(const char *path, pkg_details_t *details);
enum MHD_Result pkg_icon_response(struct MHD_Connection *conn, const char *path);
/* Borrows an already-open installation source. */
enum MHD_Result pkg_icon_response_fd(struct MHD_Connection *conn, int fd);
enum MHD_Result api_pkg_info(struct MHD_Connection *conn);
enum MHD_Result api_pkg_icon(struct MHD_Connection *conn);
