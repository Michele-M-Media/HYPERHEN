#ifndef SM_SMB_H
#define SM_SMB_H
#include "sm_limits.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define SM_SMB_CONNECTIONS_MAX 16u
typedef struct {
  char name[64], path[MAX_PATH], username[256], password[512];
  bool active;
} sm_smb_connection_t;
typedef struct {
  char path[MAX_PATH], server[256], share[256], relative[MAX_PATH];
  char user[256], domain[256], password[512];
} sm_smb_address_t;

struct smb2_context;
static inline bool sm_smb_path(const char *path) {
  return path && !strncmp(path, "smb://", 6);
}
size_t sm_smb_snapshot(sm_smb_connection_t out[SM_SMB_CONNECTIONS_MAX]);
int sm_smb_get(const char *name, sm_smb_connection_t *out);
int sm_smb_validate(const sm_smb_connection_t *connection);
int sm_smb_save(const char *previous_name, const sm_smb_connection_t *connection);
int sm_smb_remove(const char *name);
int sm_smb_check(const sm_smb_connection_t *connection, uint16_t *dialect);
int sm_smb_normalize(const char *path, char out[MAX_PATH]);
bool sm_smb_path_within(const char *path, const char *root);
int sm_smb_parse_address(const char *path, sm_smb_address_t *address);
// libsmb2 has process-wide context bookkeeping. All library calls share this lock.
void sm_smb_lock(void);
void sm_smb_unlock(void);
struct smb2_context *sm_smb_connect_path(const char *path, sm_smb_address_t *address);
int sm_smb_result(int result);
#endif
