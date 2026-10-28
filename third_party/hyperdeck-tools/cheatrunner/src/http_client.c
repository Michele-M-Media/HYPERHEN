#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>

#include "http_client.h"
#include "cr_dns.h"

typedef struct http_ctx {
  int libnet_mem_id;
  int libssl_ctx_id;
  int libhttp_ctx_id;
  int tmpl_id;
  int conn_id;
  int req_id;
  http_progress_fn_t pfn;
  void              *pud;
} http_ctx_t;

int sceNetInit(void);
int sceNetPoolCreate(const char *name, int size, int flags);
int sceNetPoolDestroy(int memid);

int sceSslInit(size_t pool_size);
int sceSslTerm(int ctx_id);

int sceHttpInit(int memid, int ssl_ctx_id, size_t pool_size);
int sceHttpTerm(int ctx_id);
int sceHttpCreateTemplate(int libhttp_ctx_id, const char *agent, int http_ver, int auto_proxy);
int sceHttpsSetSslCallback(int tmpl_id, void *cb, void *user_arg);
int sceHttpSetResponseHeaderMaxSize(int tmpl_id, size_t size);
int sceHttpDeleteTemplate(int tmpl_id);
int sceHttpCreateConnectionWithURL(int tmpl_id, const char *url, int keep_alive);
int sceHttpDeleteConnection(int conn_id);
int sceHttpCreateRequestWithURL(int conn_id, int method, const char *url, uint64_t content_length);
int sceHttpSendRequest(int req_id, const void *post_data, size_t size);
int sceHttpSetConnectTimeOut(int id, unsigned int usec);
int sceHttpSetResolveTimeOut(int id, unsigned int usec);
int sceHttpSetSendTimeOut(int id, unsigned int usec);
int sceHttpSetRecvTimeOut(int id, unsigned int usec);
int sceHttpGetResponseContentLength(int req_id, int *known, uint64_t *content_length);
int sceHttpGetStatusCode(int req_id, int *status_code);
int sceHttpReadData(int req_id, void *data, size_t size);
int sceHttpDeleteRequest(int req_id);
int sceHttpAddRequestHeader(int req_id, const char *name, const char *value, unsigned int mode);
int sceHttpSetAutoRedirect(int id, int enable);
int sceHttpGetAllResponseHeaders(int req_id, char **header, size_t *header_size);

static int
http_ssl_cb(void) {
  return 0;
}

static long long
mono_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int
http_should_retry(int rc, int status_code) {
  if (rc != 0) {
    return 1;
  }
  return status_code == 429 || status_code == 500 || status_code == 502 || status_code == 503 || status_code == 504;
}

/* Libraries and template: set up once, then reusable for many requests. */
static int
http_session_init(http_ctx_t *ctx, const char *agent) {
  int err = 0;

  ctx->libnet_mem_id = -1;
  ctx->libssl_ctx_id = -1;
  ctx->libhttp_ctx_id = -1;
  ctx->tmpl_id = -1;
  ctx->conn_id = -1;
  ctx->req_id = -1;
  ctx->pfn = NULL;
  ctx->pud = NULL;

  if ((err = sceNetInit()) < 0) {
    return err;
  }
  if ((ctx->libnet_mem_id = sceNetPoolCreate("cheatrunner_http", 512 * 1024, 0)) < 0) {
    return ctx->libnet_mem_id;
  }
  if ((ctx->libssl_ctx_id = sceSslInit(384 * 1024)) < 0) {
    return ctx->libssl_ctx_id;
  }
  if ((ctx->libhttp_ctx_id = sceHttpInit(ctx->libnet_mem_id, ctx->libssl_ctx_id, 256 * 1024)) < 0) {
    return ctx->libhttp_ctx_id;
  }
  if ((ctx->tmpl_id = sceHttpCreateTemplate(ctx->libhttp_ctx_id, agent, 2, 1)) < 0) {
    return ctx->tmpl_id;
  }
  if ((err = sceHttpSetResponseHeaderMaxSize(ctx->tmpl_id, 0x2000)) < 0) {
    return err;
  }
  if ((err = sceHttpsSetSslCallback(ctx->tmpl_id, http_ssl_cb, 0)) < 0) {
    return err;
  }
  return 0;
}

/* One connection and request on an already-initialised session. */
static int
http_request_open(http_ctx_t *ctx, const char *url, int timeout_ms) {
  /* DNS bypass: resolve via 8.8.8.8 (the PS5 DNS may be 127.0.0.1), put the IP in the URL and set the
   * Host header. ssl_cb returning 0 suppresses the SNI/cert mismatch. */
  char resolved_url[4096];
  char orig_host[256];
  orig_host[0] = '\0';
  {
    const char *scheme_end = strstr(url, "://");
    if (scheme_end) {
      scheme_end += 3;
      const char *host_end = scheme_end;
      while (*host_end && *host_end != '/' && *host_end != ':' && *host_end != '?')
        host_end++;
      size_t host_len = (size_t)(host_end - scheme_end);
      if (host_len > 0 && host_len < sizeof(orig_host)) {
        memcpy(orig_host, scheme_end, host_len);
        orig_host[host_len] = '\0';
        /* Only resolve if it looks like a hostname (not already an IP) */
        int already_ip = 1;
        for (size_t _i = 0; _i < host_len; _i++) {
          char c = orig_host[_i];
          if (c != '.' && (c < '0' || c > '9')) { already_ip = 0; break; }
        }
        if (!already_ip) {
          char ip[64] = {0};
          if (cr_dns_resolve(orig_host, ip, sizeof(ip)) == 0 && ip[0]) {
            size_t scheme_len = (size_t)(scheme_end - url);
            snprintf(resolved_url, sizeof(resolved_url), "%.*s%s%s",
                     (int)scheme_len, url, ip, host_end);
            url = resolved_url;
          } else {
            orig_host[0] = '\0'; /* resolution failed — use original URL */
          }
        } else {
          orig_host[0] = '\0';
        }
      }
    }
  }
  if ((ctx->conn_id = sceHttpCreateConnectionWithURL(ctx->tmpl_id, url, 0)) < 0) {
    return ctx->conn_id;
  }
  if ((ctx->req_id = sceHttpCreateRequestWithURL(ctx->conn_id, 0, url, 0)) < 0) {
    return ctx->req_id;
  }
  /* Restore original Host header so HTTP/1.1 routing and server-side SNI work */
  if (orig_host[0]) {
    (void)sceHttpAddRequestHeader(ctx->req_id, "Host", orig_host, 0);
  }
  if (timeout_ms > 0) {
    unsigned int t_us = (unsigned int)timeout_ms * 1000u;
    /* Set timeouts on the template, connection and request: some sceHttp versions only honour one of them. */
    (void)sceHttpSetConnectTimeOut(ctx->tmpl_id, t_us);
    (void)sceHttpSetResolveTimeOut(ctx->tmpl_id, t_us);
    (void)sceHttpSetSendTimeOut(ctx->tmpl_id, t_us);
    (void)sceHttpSetRecvTimeOut(ctx->tmpl_id, t_us);
    (void)sceHttpSetConnectTimeOut(ctx->conn_id, t_us);
    (void)sceHttpSetResolveTimeOut(ctx->conn_id, t_us);
    (void)sceHttpSetSendTimeOut(ctx->conn_id, t_us);
    (void)sceHttpSetRecvTimeOut(ctx->conn_id, t_us);
    (void)sceHttpSetConnectTimeOut(ctx->req_id, t_us);
    (void)sceHttpSetResolveTimeOut(ctx->req_id, t_us);
    (void)sceHttpSetSendTimeOut(ctx->req_id, t_us);
    (void)sceHttpSetRecvTimeOut(ctx->req_id, t_us);
  }

  return 0;
}

static int
http_init(http_ctx_t *ctx, const char *agent, const char *url, int timeout_ms) {
  int err = http_session_init(ctx, agent);
  if (err < 0) {
    return err;
  }
  return http_request_open(ctx, url, timeout_ms);
}

static int
http_request(http_ctx_t *ctx, uint8_t **data, size_t *len, int *status_out, size_t max_bytes) {
  int err = 0;
  int status = -1;

  *data = NULL;
  *len = 0;
  if (status_out) {
    *status_out = -1;
  }

  if ((err = sceHttpSendRequest(ctx->req_id, 0, 0)) < 0) {
    return err;
  }
  if ((err = sceHttpGetStatusCode(ctx->req_id, &status)) < 0) {
    return err;
  }
  if (status_out) {
    *status_out = status;
  }

  int cl_known = 1;
  uint64_t probed = 0;
  if (sceHttpGetResponseContentLength(ctx->req_id, &cl_known, &probed) < 0) {
    cl_known = 1;
    probed = 0;
  }
  if (max_bytes > 0 && cl_known == 0 && probed > (uint64_t)max_bytes) {
    return -2;
  }

  /* cl_known==0 means Content-Length is present; probed holds its value. */
  size_t progress_total = (cl_known == 0 && probed > 0) ? (size_t)probed : 0;

  size_t cap = (cl_known == 0 && probed > 0) ? (size_t)probed : 16384;
  if (max_bytes > 0 && cap > max_bytes) {
    cap = max_bytes;
    if (cap == 0) {
      cap = 1;
    }
  }
  size_t off = 0;
  uint8_t *buf = malloc(cap);
  if (!buf) {
    return -1;
  }

  for (;;) {
    if ((cap - off) < 4096) {
      size_t new_cap = cap * 2;
      if (max_bytes > 0 && new_cap > max_bytes) {
        new_cap = max_bytes;
      }
      if (new_cap <= cap) {
        free(buf);
        return -2;
      }
      uint8_t *nbuf = realloc(buf, new_cap);
      if (!nbuf) {
        free(buf);
        return -1;
      }
      buf = nbuf;
      cap = new_cap;
    }

    int n = sceHttpReadData(ctx->req_id, buf + off, cap - off);
    if (n < 0) {
      free(buf);
      return n;
    }
    if (n == 0) {
      break;
    }
    if (max_bytes > 0 && off + (size_t)n > max_bytes) {
      free(buf);
      return -2;
    }
    off += (size_t)n;
    if (ctx->pfn) ctx->pfn(off, progress_total, ctx->pud);
  }

  *data = buf;
  *len = off;
  return 0;
}

static void
http_request_close(http_ctx_t *ctx) {
  if (ctx->req_id >= 0) {
    sceHttpDeleteRequest(ctx->req_id);
    ctx->req_id = -1;
  }
  if (ctx->conn_id >= 0) {
    sceHttpDeleteConnection(ctx->conn_id);
    ctx->conn_id = -1;
  }
}

static void
http_fini(http_ctx_t *ctx) {
  http_request_close(ctx);
  if (ctx->tmpl_id >= 0) {
    sceHttpDeleteTemplate(ctx->tmpl_id);
  }
  if (ctx->libhttp_ctx_id >= 0) {
    sceHttpTerm(ctx->libhttp_ctx_id);
  }
  if (ctx->libssl_ctx_id >= 0) {
    sceSslTerm(ctx->libssl_ctx_id);
  }
  if (ctx->libnet_mem_id >= 0) {
    sceNetPoolDestroy(ctx->libnet_mem_id);
  }
}

int
http_get_url_ex(const char *agent, const char *url, size_t max_bytes,
                int *status_out, uint8_t **data_out, size_t *len_out) {
  return http_get_url_ex_timeout(agent, url, max_bytes, 0, status_out, data_out, len_out);
}

int
http_get_url_ex_timeout_progress(const char *agent, const char *url, size_t max_bytes, int timeout_ms,
                                  int *status_out, uint8_t **data_out, size_t *len_out,
                                  http_progress_fn_t pfn, void *pud) {
  http_ctx_t ctx;
  uint8_t *body = NULL;
  size_t body_len = 0;
  int status = -1;
  int rc = 0;
  int init_rc = 0;
  int attempts = 2;
  int effective_timeout_ms = timeout_ms > 0 ? timeout_ms : 8000;

  if (!url || !data_out || !len_out) {
    return -1;
  }
  *data_out = NULL;
  *len_out = 0;
  if (status_out) {
    *status_out = -1;
  }

  if (!agent) {
    agent = "CheatRunner/0.1";
  }

  for (int attempt = 0; attempt < attempts; attempt++) {
    body = NULL;
    body_len = 0;
    status = -1;
    rc = -1;

    init_rc = http_init(&ctx, agent, url, effective_timeout_ms);
    if (init_rc < 0) {
      /* Init failure is structural (pool alloc, SSL init, DNS) — retrying won't help. */
      http_fini(&ctx);
      rc = init_rc;
      break;
    }
    ctx.pfn = pfn;
    ctx.pud = pud;
    rc = http_request(&ctx, &body, &body_len, &status, max_bytes);
    http_fini(&ctx);

    if (!http_should_retry(rc, status) || attempt == attempts - 1) {
      break;
    }
    free(body);
    body = NULL;
    body_len = 0;
    usleep((useconds_t)((attempt + 1) * 250000));
  }

  if (status_out) {
    *status_out = status;
  }
  if (rc != 0) {
    free(body);
    return rc;
  }
  *data_out = body;
  *len_out = body_len;
  return 0;
}

int
http_get_url_ex_timeout(const char *agent, const char *url, size_t max_bytes, int timeout_ms,
                        int *status_out, uint8_t **data_out, size_t *len_out) {
  return http_get_url_ex_timeout_progress(agent, url, max_bytes, timeout_ms,
                                          status_out, data_out, len_out, NULL, NULL);
}

struct http_session {
  http_ctx_t ctx;
  long long last_read_ms, last_write_ms, last_total_ms;
};

http_session_t *
http_session_open(const char *agent) {
  http_session_t *se = calloc(1, sizeof(*se));
  if (!se) {
    return NULL;
  }
  if (http_session_init(&se->ctx, agent ? agent : "CheatRunner/0.1") < 0) {
    http_fini(&se->ctx);
    free(se);
    return NULL;
  }
  return se;
}

void
http_session_close(http_session_t *se) {
  if (!se) {
    return;
  }
  http_fini(&se->ctx);
  free(se);
}

int
http_session_get(http_session_t *se, const char *url, size_t max_bytes, int timeout_ms,
                 int *status_out, uint8_t **data_out, size_t *len_out) {
  if (!se || !url || !data_out || !len_out) {
    return -1;
  }
  *data_out = NULL;
  *len_out = 0;
  int status = -1, rc = -1;
  uint8_t *body = NULL;
  size_t body_len = 0;
  int effective_timeout_ms = timeout_ms > 0 ? timeout_ms : 8000;

  for (int attempt = 0; attempt < 2; attempt++) {
    body = NULL;
    body_len = 0;
    status = -1;
    rc = http_request_open(&se->ctx, url, effective_timeout_ms);
    if (rc == 0) {
      se->ctx.pfn = NULL;
      se->ctx.pud = NULL;
      rc = http_request(&se->ctx, &body, &body_len, &status, max_bytes);
    }
    http_request_close(&se->ctx);
    if (!http_should_retry(rc, status) || attempt == 1) {
      break;
    }
    free(body);
    body = NULL;
    usleep((useconds_t)((attempt + 1) * 250000));
  }
  if (status_out) {
    *status_out = status;
  }
  if (rc != 0) {
    free(body);
    return rc;
  }
  *data_out = body;
  *len_out = body_len;
  return 0;
}

/* Copies the Location header of the current response into `out`. */
static int
http_response_location(http_ctx_t *ctx, char *out, size_t out_sz) {
  char *hdr = NULL;
  size_t hdr_sz = 0;
  if (sceHttpGetAllResponseHeaders(ctx->req_id, &hdr, &hdr_sz) < 0 || !hdr) {
    return -1;
  }
  for (size_t i = 0; i + 9 < hdr_sz; i++) {
    if ((i == 0 || hdr[i - 1] == '\n') && !strncasecmp(hdr + i, "Location:", 9)) {
      const char *v = hdr + i + 9;
      while (v < hdr + hdr_sz && *v == ' ') v++;
      size_t n = 0;
      while (v + n < hdr + hdr_sz && v[n] != '\r' && v[n] != '\n') n++;
      if (n == 0 || n >= out_sz) {
        return -1;
      }
      memcpy(out, v, n);
      out[n] = '\0';
      return 0;
    }
  }
  return -1;
}

/* Streams a response body straight to `path` (for archives too big to hold in RAM). */
int
http_session_download_file(http_session_t *se, const char *url, const char *path, int timeout_ms,
                           int *status_out, size_t *bytes_out, http_progress_fn_t pfn, void *pud) {
  if (!se || !url || !path) {
    return -1;
  }
  int status = -1, rc, fd = -1;
  size_t total = 0;
  long long read_ms = 0, write_ms = 0, begin_ms = mono_ms();
  const size_t chunk_sz = 128 * 1024;
  uint8_t *chunk = malloc(chunk_sz);
  if (!chunk) {
    return -1;
  }
  char cur[4096], next[4096];
  snprintf(cur, sizeof(cur), "%s", url);
  /* Release assets redirect to another host; follow by hand so each hop goes through the DNS bypass. */
  for (int hop = 0;; hop++) {
    rc = http_request_open(&se->ctx, cur, timeout_ms > 0 ? timeout_ms : 30000);
    if (rc == 0) {
      (void)sceHttpSetAutoRedirect(se->ctx.req_id, 0);
      if ((rc = sceHttpSendRequest(se->ctx.req_id, 0, 0)) >= 0) {
        rc = sceHttpGetStatusCode(se->ctx.req_id, &status);
      }
    }
    int moved = status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
    if (rc >= 0 && moved && hop < 5 && http_response_location(&se->ctx, next, sizeof(next)) == 0) {
      http_request_close(&se->ctx);
      snprintf(cur, sizeof(cur), "%s", next);
      continue;
    }
    break;
  }
  if (rc >= 0 && status == 200) {
    fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (fd < 0) {
      rc = -1;
    }
    while (rc >= 0) {
      long long t0 = mono_ms();
      int n = sceHttpReadData(se->ctx.req_id, chunk, chunk_sz);
      read_ms += mono_ms() - t0;
      if (n < 0) {
        rc = n;
        break;
      }
      if (n == 0) {
        rc = 0;
        break;
      }
      long long t1 = mono_ms();
      ssize_t wr = write(fd, chunk, (size_t)n);
      write_ms += mono_ms() - t1;
      if (wr != (ssize_t)n) {
        rc = -1;
        break;
      }
      total += (size_t)n;
      if (pfn) {
        pfn(total, 0, pud);
      }
    }
    if (fd >= 0) {
      close(fd);
    }
  }
  http_request_close(&se->ctx);
  free(chunk);
  if (rc != 0 || status != 200) {
    unlink(path);
  }
  se->last_read_ms = read_ms;
  se->last_write_ms = write_ms;
  se->last_total_ms = mono_ms() - begin_ms;
  if (status_out) {
    *status_out = status;
  }
  if (bytes_out) {
    *bytes_out = total;
  }
  return rc < 0 ? rc : 0;
}

/* Timing split of the last http_session_download_file call. */
void
http_session_last_timing(http_session_t *se, long long *read_ms, long long *write_ms, long long *total_ms) {
  *read_ms = se->last_read_ms;
  *write_ms = se->last_write_ms;
  *total_ms = se->last_total_ms;
}
