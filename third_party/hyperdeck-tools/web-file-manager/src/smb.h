#pragma once

#include <limits.h>
#include <stdint.h>
#include <microhttpd.h>
#include <smb2/smb2.h>
#include <smb2/libsmb2.h>
#include "json_util.h"

typedef struct smb_address {
  char path[PATH_MAX];
  char server[256];
  char share[256];
  char relative[PATH_MAX];
  char user[256];
  char domain[256];
  char password[512];
} smb_address_t;

int smb_path(const char *path);
int smb_parse_address(const char *input, smb_address_t *address);
int smb_path_current(const char *current, const char *root);
/* Serialize libsmb2 calls: its context registry is global, including sync calls. */
void smb_lock(void);
void smb_unlock(void);
struct smb2_context *smb_connect_path(const char *path, smb_address_t *address);
int smb_result(int result);
void smb_append_spaces(strbuf_t *json, const char *current, int *first);
enum MHD_Result api_smb_shares(struct MHD_Connection *conn, const char *body, size_t size);
enum MHD_Result api_smb_add(struct MHD_Connection *conn, const char *body,
                            size_t size, int connecting);
enum MHD_Result api_smb_remove(struct MHD_Connection *conn, const char *body,
                               size_t size);
