/* Copyright (C) 2025 John Törnblom
 * SMB2/3 connection setup follows ps5-payload-dev/websrv
 * src/smb.c (GPL-3.0-or-later). Named connection storage is owned here.
 * See licenses/SMB-NOTICES.txt. */
#include "sm_platform.h"
#include "sm_smb.h"
#include "sm_log.h"
#include "sm_paths.h"
#include "sm_runtime.h"
#include <ctype.h>
#include <json-c/json.h>
#include <pthread.h>
#include <smb2/smb2.h>
#include <smb2/libsmb2.h>
#include <strings.h>
#include <sys/socket.h>

#ifndef SM_SMB_CONFIG_FILE
#define SM_SMB_CONFIG_FILE LOG_DIR "/smb.json"
#endif
#define SMB_CONFIG_MAX (128u * 1024u)
static sm_smb_connection_t connections[SM_SMB_CONNECTIONS_MAX];
static size_t connection_count;
static pthread_once_t config_once = PTHREAD_ONCE_INIT;
static pthread_mutex_t config_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t client_mutex = PTHREAD_MUTEX_INITIALIZER;

void sm_smb_lock(void) {
  pthread_mutex_lock(&client_mutex);
}
void sm_smb_unlock(void) {
  pthread_mutex_unlock(&client_mutex);
}
int sm_smb_result(int result) {
  if (result < 0) {
    errno = -result;
    return -1;
  }
  return result;
}

static bool safe_components(const char *path) {
  for (const unsigned char *p = (const unsigned char *)path; *p; ++p)
    if (*p < 32 || *p == 127 || *p == '\\') return false;
  for (const char *p = path; *p;) {
    const char *end = strchr(p, '/');
    size_t size = end ? (size_t)(end - p) : strlen(p);
    if (!size || (size == 1 && *p == '.') || (size == 2 && !strncmp(p, "..", 2))) return false;
    if (!end) break;
    p = end + 1;
  }
  return true;
}

static int base_address(const sm_smb_connection_t *c, sm_smb_address_t *a) {
  memset(a, 0, sizeof(*a));
  if (!sm_smb_path(c->path) || !safe_components(c->path + 6)) return EINVAL;
  const char *server = c->path + 6, *share = strchr(server, '/');
  if (!share || share == server || !share[1]) return EINVAL;
  size_t length = (size_t)(share - server);
  if (length >= sizeof(a->server)) return ENAMETOOLONG;
  memcpy(a->server, server, length);
  for (char *p = a->server; *p; ++p) {
    if (isspace((unsigned char)*p) || *p == '@' || *p == '?' || *p == '#') return EINVAL;
    *p = (char)tolower((unsigned char)*p);
  }
  ++share;
  const char *relative = strchr(share, '/');
  length = relative ? (size_t)(relative - share) : strlen(share);
  if (!length || length >= sizeof(a->share)) return EINVAL;
  memcpy(a->share, share, length);
  if (relative && strlcpy(a->relative, relative + 1, sizeof(a->relative)) >= sizeof(a->relative))
    return ENAMETOOLONG;
  size_t size = strlen(a->relative);
  if (size && a->relative[size - 1] == '/') a->relative[size - 1] = 0;
  const char *user = c->username;
  const char *domain = strchr(user, '\\');
  if (domain) {
    length = (size_t)(domain - user);
    if (!length || length >= sizeof(a->domain) || !domain[1]) return EINVAL;
    memcpy(a->domain, user, length);
    user = domain + 1;
  }
  strlcpy(a->user, *user ? user : "Guest", sizeof(a->user));
  strlcpy(a->password, c->password, sizeof(a->password));
  return 0;
}

int sm_smb_validate(const sm_smb_connection_t *c) {
  if (!c || !memchr(c->name, 0, sizeof(c->name)) || !memchr(c->path, 0, sizeof(c->path)) ||
      !memchr(c->username, 0, sizeof(c->username)) || !memchr(c->password, 0, sizeof(c->password)))
    return EINVAL;
  if (!c->name[0] || !strcmp(c->name, ".") || !strcmp(c->name, "..") ||
      isspace((unsigned char)c->name[0]) || isspace((unsigned char)c->name[strlen(c->name) - 1]))
    return EINVAL;
  for (const unsigned char *p = (const unsigned char *)c->name; *p; ++p)
    if (*p < 32 || *p == 127 || strchr("/\\:@?#%", *p)) return EINVAL;
  sm_smb_address_t address;
  return base_address(c, &address);
}

static bool json_string(struct json_object *o, const char *key, char *out, size_t size) {
  struct json_object *v;
  if (!json_object_object_get_ex(o, key, &v) || !json_object_is_type(v, json_type_string))
    return false;
  const char *value = json_object_get_string(v);
  if (strlen(value) != (size_t)json_object_get_string_len(v) || strlen(value) >= size) return false;
  strlcpy(out, value, size);
  return true;
}

static void load_connections(void) {
  int fd = open(SM_SMB_CONFIG_FILE, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
  if (fd < 0) {
    if (errno != ENOENT) log_debug("  [SMB] settings unavailable: status=%d", errno);
    return;
  }
  struct stat st;
  bool valid =
      fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0 && st.st_size <= SMB_CONFIG_MAX;
  char *data = valid ? calloc(1, (size_t)st.st_size + 1) : NULL;
  size_t used = 0;
  while (data && used < (size_t)st.st_size) {
    ssize_t count = read(fd, data + used, (size_t)st.st_size - used);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) {
      valid = false;
      break;
    }
    used += (size_t)count;
  }
  close(fd);
  struct json_object *array = NULL;
  struct json_tokener *tokener = data && valid ? json_tokener_new() : NULL;
  if (tokener) {
    json_tokener_set_flags(tokener, JSON_TOKENER_STRICT | JSON_TOKENER_VALIDATE_UTF8);
    array = json_tokener_parse_ex(tokener, data, (int)used);
    size_t end = json_tokener_get_parse_end(tokener);
    while (end < used && isspace((unsigned char)data[end]))
      ++end;
    if (json_tokener_get_error(tokener) != json_tokener_success || end != used) {
      if (array) json_object_put(array);
      array = NULL;
    }
    json_tokener_free(tokener);
  }
  free(data);
  valid = array && json_object_is_type(array, json_type_array) &&
          json_object_array_length(array) <= SM_SMB_CONNECTIONS_MAX;
  for (size_t i = 0; valid && i < json_object_array_length(array); ++i) {
    struct json_object *item = json_object_array_get_idx(array, i), *active;
    sm_smb_connection_t c = {0};
    valid = json_object_is_type(item, json_type_object) &&
            json_string(item, "name", c.name, sizeof(c.name)) &&
            json_string(item, "path", c.path, sizeof(c.path)) &&
            json_string(item, "username", c.username, sizeof(c.username)) &&
            json_string(item, "password", c.password, sizeof(c.password)) &&
            json_object_object_get_ex(item, "active", &active) &&
            json_object_is_type(active, json_type_boolean);
    if (!valid) break;
    c.active = json_object_get_boolean(active);
    if (sm_smb_validate(&c)) {
      valid = false;
      break;
    }
    for (size_t j = 0; j < connection_count; ++j)
      if (!strcmp(connections[j].name, c.name)) valid = false;
    if (valid) connections[connection_count++] = c;
  }
  if (array) json_object_put(array);
  if (!valid) {
    memset(connections, 0, sizeof(connections));
    connection_count = 0;
    log_debug("  [SMB] invalid settings ignored: %s", SM_SMB_CONFIG_FILE);
  }
}

size_t sm_smb_snapshot(sm_smb_connection_t out[SM_SMB_CONNECTIONS_MAX]) {
  pthread_once(&config_once, load_connections);
  pthread_mutex_lock(&config_mutex);
  size_t count = connection_count;
  memcpy(out, connections, count * sizeof(*out));
  pthread_mutex_unlock(&config_mutex);
  return count;
}

int sm_smb_get(const char *name, sm_smb_connection_t *out) {
  if (!name || !out) return EINVAL;
  pthread_once(&config_once, load_connections);
  pthread_mutex_lock(&config_mutex);
  int status = ENOENT;
  for (size_t i = 0; i < connection_count; ++i)
    if (!strcmp(name, connections[i].name)) {
      *out = connections[i];
      status = 0;
      break;
    }
  pthread_mutex_unlock(&config_mutex);
  return status;
}

static int write_connections(const sm_smb_connection_t *items, size_t count) {
  struct json_object *array = json_object_new_array();
  if (!array) return ENOMEM;
  for (size_t i = 0; i < count; ++i) {
    struct json_object *o = json_object_new_object();
    if (!o) {
      json_object_put(array);
      return ENOMEM;
    }
    const char *keys[] = {"name", "path", "username", "password", "active"};
    const char *values[] = {items[i].name, items[i].path, items[i].username, items[i].password};
    for (size_t j = 0; j < 5; ++j) {
      struct json_object *value =
          j == 4 ? json_object_new_boolean(items[i].active) : json_object_new_string(values[j]);
      if (!value || json_object_object_add(o, keys[j], value)) {
        if (value) json_object_put(value);
        json_object_put(o);
        json_object_put(array);
        return ENOMEM;
      }
    }
    if (json_object_array_add(array, o)) {
      json_object_put(o);
      json_object_put(array);
      return ENOMEM;
    }
  }
  char temp[MAX_PATH];
  int length = snprintf(temp, sizeof(temp), "%s.tmp", SM_SMB_CONFIG_FILE);
  if (length < 0 || (size_t)length >= sizeof(temp)) {
    json_object_put(array);
    return ENAMETOOLONG;
  }
  char parent[MAX_PATH];
  strlcpy(parent, SM_SMB_CONFIG_FILE, sizeof(parent));
  char *slash = strrchr(parent, '/');
  if (slash) {
    *slash = 0;
    if (parent[0]) (void)mkdir(parent, 0700);
  }
  int fd = open(temp, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_NONBLOCK, 0600);
  int status = fd < 0 ? errno : 0;
  struct stat st;
  if (!status && fstat(fd, &st) != 0) status = errno;
  if (!status && !S_ISREG(st.st_mode)) status = EINVAL;
  const char *data = json_object_to_json_string_ext(array, JSON_C_TO_STRING_PRETTY);
  if (!data && !status) status = ENOMEM;
  size_t size = data ? strlen(data) : 0, used = 0;
  if (!status && fchmod(fd, 0600)) status = errno;
  while (!status && used < size) {
    ssize_t written = write(fd, data + used, size - used);
    if (written < 0 && errno == EINTR) continue;
    if (written <= 0)
      status = written < 0 ? errno : EIO;
    else
      used += (size_t)written;
  }
  if (!status && fsync(fd)) status = errno;
  if (fd >= 0 && close(fd) && !status) status = errno;
  if (!status && rename(temp, SM_SMB_CONFIG_FILE)) status = errno;
  if (status) (void)unlink(temp);
  json_object_put(array);
  return status;
}

int sm_smb_save(const char *previous, const sm_smb_connection_t *c) {
  int status = sm_smb_validate(c);
  if (status) return status;
  pthread_once(&config_once, load_connections);
  pthread_mutex_lock(&config_mutex);
  size_t index = connection_count;
  if (previous && *previous) {
    for (size_t i = 0; i < connection_count; ++i)
      if (!strcmp(previous, connections[i].name)) index = i;
    if (index == connection_count) status = ENOENT;
  }
  for (size_t i = 0; i < connection_count; ++i)
    if (i != index && !strcmp(c->name, connections[i].name)) status = EEXIST;
  if (index == connection_count && connection_count == SM_SMB_CONNECTIONS_MAX) status = ENOSPC;
  sm_smb_connection_t *next = calloc(SM_SMB_CONNECTIONS_MAX, sizeof(*next));
  if (!next) {
    pthread_mutex_unlock(&config_mutex);
    return ENOMEM;
  }
  memcpy(next, connections, sizeof(connections));
  size_t count = connection_count;
  if (!status) {
    next[index] = *c;
    if (index == count) ++count;
    status = write_connections(next, count);
    if (!status) {
      memcpy(connections, next, sizeof(connections));
      connection_count = count;
    }
  }
  memset(next, 0, sizeof(connections));
  free(next);
  pthread_mutex_unlock(&config_mutex);
  return status;
}

int sm_smb_remove(const char *name) {
  if (!name || !*name) return EINVAL;
  pthread_once(&config_once, load_connections);
  pthread_mutex_lock(&config_mutex);
  sm_smb_connection_t *next = calloc(SM_SMB_CONNECTIONS_MAX, sizeof(*next));
  if (!next) {
    pthread_mutex_unlock(&config_mutex);
    return ENOMEM;
  }
  size_t count = 0;
  for (size_t i = 0; i < connection_count; ++i)
    if (strcmp(name, connections[i].name)) next[count++] = connections[i];
  int status = count == connection_count ? ENOENT : write_connections(next, count);
  if (!status) {
    memset(connections, 0, sizeof(connections));
    memcpy(connections, next, count * sizeof(*next));
    connection_count = count;
  }
  memset(next, 0, sizeof(connections));
  free(next);
  pthread_mutex_unlock(&config_mutex);
  return status;
}

int sm_smb_parse_address(const char *path, sm_smb_address_t *a) {
  if (!path || !sm_smb_path(path) || strlen(path) >= MAX_PATH) {
    errno = EINVAL;
    return -1;
  }
  const char *name = path + 6, *slash = strchr(name, '/');
  size_t size = slash ? (size_t)(slash - name) : strlen(name);
  if (!size || size >= 64 || (slash && !safe_components(slash + 1))) {
    errno = EINVAL;
    return -1;
  }
  char alias[64];
  memcpy(alias, name, size);
  alias[size] = 0;
  sm_smb_connection_t c;
  int status = sm_smb_get(alias, &c);
  if (!status && !c.active) status = ENODEV;
  if (!status) status = base_address(&c, a);
  memset(&c, 0, sizeof(c));
  if (status) {
    errno = status;
    return -1;
  }
  char relative[MAX_PATH];
  int length = snprintf(relative, sizeof(relative), "%s%s%s", a->relative,
                        a->relative[0] && slash && slash[1] ? "/" : "", slash ? slash + 1 : "");
  if (length < 0 || (size_t)length >= sizeof(relative)) {
    errno = ENAMETOOLONG;
    return -1;
  }
  size_t end = strlen(relative);
  if (end && relative[end - 1] == '/') relative[end - 1] = 0;
  strlcpy(a->relative, relative, sizeof(a->relative));
  length = snprintf(a->path, sizeof(a->path), "smb://%s%s%s", alias, slash && slash[1] ? "/" : "",
                    slash ? slash + 1 : "");
  if (length < 0 || (size_t)length >= sizeof(a->path)) {
    errno = ENAMETOOLONG;
    return -1;
  }
  end = strlen(a->path);
  if (end && a->path[end - 1] == '/') a->path[end - 1] = 0;
  return 0;
}

int sm_smb_normalize(const char *path, char out[MAX_PATH]) {
  sm_smb_address_t address;
  if (sm_smb_parse_address(path, &address)) return errno;
  strlcpy(out, address.path, MAX_PATH);
  return 0;
}

bool sm_smb_path_within(const char *path, const char *root) {
  sm_smb_address_t a, b;
  if (sm_smb_parse_address(path, &a) || sm_smb_parse_address(root, &b)) return false;
  size_t length = strlen(b.relative);
  return !strcasecmp(a.server, b.server) && !strcasecmp(a.share, b.share) &&
         !strncasecmp(a.relative, b.relative, length) &&
         (!length || !a.relative[length] || a.relative[length] == '/');
}

static struct smb2_context *connect_address(const sm_smb_address_t *a) {
  if (should_stop_requested() || runtime_sleep_mode_active()) {
    errno = ECANCELED;
    return NULL;
  }
  struct smb2_context *ctx = smb2_init_context();
  if (!ctx) {
    errno = ENOMEM;
    return NULL;
  }
  smb2_set_version(ctx, SMB2_VERSION_ANY);
  smb2_set_timeout(ctx, 5);
  smb2_set_user(ctx, a->user);
  smb2_set_password(ctx, a->password);
  if (a->domain[0]) smb2_set_domain(ctx, a->domain);
  smb2_set_security_mode(ctx, SMB2_NEGOTIATE_SIGNING_ENABLED);
  int status = smb2_connect_share(ctx, a->server, a->share, a->user);
  if (status < 0) {
    smb2_destroy_context(ctx);
    errno = -status;
    return NULL;
  }
  // Console defaults are about 32/64 KiB. Grow small buffers for sustained
  // transfers without reducing larger system defaults or making tuning fatal.
  int saved_errno = errno;
  int options[] = {SO_SNDBUF, SO_RCVBUF};
  for (size_t i = 0; i < sizeof(options) / sizeof(options[0]); ++i) {
    int current = 0, desired = 512 * 1024;
    socklen_t size = sizeof(current);
    int fd = smb2_get_fd(ctx);
    if (getsockopt(fd, SOL_SOCKET, options[i], &current, &size) == 0 && current < desired)
      (void)setsockopt(fd, SOL_SOCKET, options[i], &desired, sizeof(desired));
  }
  errno = saved_errno;
  return ctx;
}

struct smb2_context *sm_smb_connect_path(const char *path, sm_smb_address_t *a) {
  if (sm_smb_parse_address(path, a)) return NULL;
  return connect_address(a);
}

int sm_smb_check(const sm_smb_connection_t *c, uint16_t *dialect) {
  int status = sm_smb_validate(c);
  if (status) return status;
  sm_smb_address_t a;
  status = base_address(c, &a);
  if (status) return status;
  sm_smb_lock();
  struct smb2_context *ctx = connect_address(&a);
  struct smb2_stat_64 st;
  if (!ctx)
    status = errno;
  else {
    int result = smb2_stat(ctx, a.relative, &st);
    status = result < 0 ? -result : st.smb2_type != SMB2_TYPE_DIRECTORY ? ENOTDIR : 0;
    if (!status && dialect) *dialect = smb2_get_dialect(ctx);
    smb2_destroy_context(ctx);
  }
  sm_smb_unlock();
  return status;
}
