#include <signal.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arpa/inet.h>
#include <microhttpd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include "asset.h"
#include "filemgr.h"
#include "websrv.h"
#include "transfer.h"
#ifdef __SCE__
#include "app_installer.h"
#endif

#define REQUEST_BODY_MAX (4 * 1024 * 1024)
#define HTTP_CONNECTION_MEMORY_LIMIT (8 * 1024 * 1024)
#define HTTP_CONNECTION_MEMORY_INCREMENT (2 * 1024 * 1024)

static volatile sig_atomic_t g_stop_requested;
static int g_listen_fd = -1;

static void
websrv_tune_connection_socket(int fd) {
  const int sndbuf = TRANSFER_SOCKET_BUFFER_SIZE;
  const int nodelay = 1;

  /* OrbisOS HTTP sockets need an explicit send buffer to fill a GbE link. */
  transfer_socket_option(fd, SOL_SOCKET, SO_SNDBUF, sndbuf, "HTTP", "SO_SNDBUF");
  transfer_socket_option(fd, IPPROTO_TCP, TCP_NODELAY, nodelay, "HTTP", "TCP_NODELAY");
}

typedef struct request_context {
  char *body;
  size_t size;
  int too_large;
  int upload_stream;
  void *upload_ctx;
} request_context_t;

static enum MHD_Result
websrv_body_too_large(struct MHD_Connection *conn) {
  static const char json[] =
    "{\"ok\":false,\"error\":\"request body is too large\","
    "\"error_code\":\"request_body_too_large\",\"error_arg\":\"\"}";
  struct MHD_Response *resp =
    MHD_create_response_from_buffer(sizeof(json) - 1, (void *)json,
                                    MHD_RESPMEM_PERSISTENT);
  enum MHD_Result ret;

  if(!resp) {
    return MHD_NO;
  }
  MHD_add_response_header(resp, MHD_HTTP_HEADER_CONTENT_TYPE,
                          "application/json");
  ret = websrv_queue_response(conn, MHD_HTTP_CONTENT_TOO_LARGE, resp);
  MHD_destroy_response(resp);
  return ret;
}

enum MHD_Result
websrv_queue_response(struct MHD_Connection *conn, unsigned int status,
                      struct MHD_Response *resp) {
  MHD_add_response_header(resp, MHD_HTTP_HEADER_ACCESS_CONTROL_ALLOW_ORIGIN, "*");
  MHD_add_response_header(resp, MHD_HTTP_HEADER_CACHE_CONTROL, "no-store");
  return MHD_queue_response(conn, status, resp);
}

void
websrv_stop(void) {
  g_stop_requested = 1;
  if(g_listen_fd >= 0) {
    shutdown(g_listen_fd, SHUT_RDWR);
  }
}

int
websrv_stop_requested(void) {
  return g_stop_requested;
}

static enum MHD_Result
websrv_on_request(void *cls, struct MHD_Connection *conn, const char *url,
                  const char *method, const char *version,
                  const char *upload_data, size_t *upload_data_size,
                  void **con_cls) {
  request_context_t *ctx = *con_cls;
  (void)cls;
  (void)version;

  if(strcmp(method, MHD_HTTP_METHOD_GET) &&
     strcmp(method, MHD_HTTP_METHOD_POST) &&
     strcmp(method, MHD_HTTP_METHOD_HEAD)) {
    return MHD_NO;
  }

  if(!ctx) {
    if(!(ctx = calloc(1, sizeof(*ctx)))) {
      return MHD_NO;
    }
    ctx->upload_stream = !strcmp(url, "/api/upload-file") &&
                         !strcmp(method, MHD_HTTP_METHOD_POST);
    *con_cls = ctx;
    return MHD_YES;
  }

  if(*upload_data_size) {
    size_t chunk_size = *upload_data_size;

    if(ctx->upload_stream) {
      if(!ctx->upload_ctx && filemgr_upload_begin(conn, &ctx->upload_ctx)) {
        ctx->too_large = 1;
      }
      if(ctx->upload_ctx && filemgr_upload_data(ctx->upload_ctx, upload_data,
                                                chunk_size)) {
        ctx->too_large = 1;
      }
      *upload_data_size = 0;
      return MHD_YES;
    }

    if(chunk_size > REQUEST_BODY_MAX - ctx->size) {
      ctx->too_large = 1;
    } else if(!ctx->too_large) {
      char *body = realloc(ctx->body, ctx->size + chunk_size + 1);
      if(!body) {
        return MHD_NO;
      }
      ctx->body = body;
      memcpy(ctx->body + ctx->size, upload_data, chunk_size);
      ctx->size += chunk_size;
      ctx->body[ctx->size] = 0;
    }
    *upload_data_size = 0;
    return MHD_YES;
  }

  if(ctx->too_large) {
    return ctx->upload_stream ?
      filemgr_upload_finish(conn, ctx->upload_ctx) :
      websrv_body_too_large(conn);
  }

  if(ctx->upload_stream) {
    if(!ctx->upload_ctx && filemgr_upload_begin(conn, &ctx->upload_ctx)) {
      return filemgr_upload_finish(conn, ctx->upload_ctx);
    }
    return filemgr_upload_finish(conn, ctx->upload_ctx);
  }

  if(!strncmp(url, "/api/", 5)) {
    return filemgr_api_request(conn, url, method, ctx->body, ctx->size);
  }
  if(!strncmp(url, "/pkg-source/", strlen("/pkg-source/"))) {
    return filemgr_pkg_source_request(conn, url, method);
  }
  if(!strcmp(url, "/fs")) {
    return filemgr_fs_request(conn);
  }
  if(!strcmp(url, "/") || !url[0]) {
    return asset_request(conn, "/index.html");
  }
  if(!strcmp(url, "/favicon.ico")) {
    return asset_request(conn, "/icon0.png");
  }
  return asset_request(conn, url);
}

static void
websrv_on_completed(void *cls, struct MHD_Connection *connection,
                    void **con_cls, enum MHD_RequestTerminationCode toe) {
  (void)cls;
  (void)connection;
  (void)toe;
  request_context_t *ctx = *con_cls;

  if(ctx) {
    if(ctx->upload_ctx) {
      filemgr_upload_free(ctx->upload_ctx);
    }
    free(ctx->body);
    free(ctx);
  }
  *con_cls = NULL;
}

int
websrv_listen(unsigned short port) {
  struct sockaddr_in server_addr;
  struct sockaddr_in client_addr;
  struct MHD_Daemon *httpd;
  socklen_t addr_len;
  int connfd;
  int srvfd;

  signal(SIGPIPE, SIG_IGN);

  if((srvfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    perror("socket");
    return -1;
  }

  if(setsockopt(srvfd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0) {
    perror("setsockopt");
    close(srvfd);
    return -1;
  }
  {
    const int rcvbuf = TRANSFER_SOCKET_BUFFER_SIZE;

    /* Set before listen so accepted PS5 sockets inherit the larger window. */
    transfer_socket_option(srvfd, SOL_SOCKET, SO_RCVBUF, rcvbuf, "HTTP", "SO_RCVBUF");
  }

  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  server_addr.sin_port = htons(port);

  if(bind(srvfd, (struct sockaddr *)&server_addr, sizeof(server_addr))) {
    perror("bind");
    close(srvfd);
    return -1;
  }
  if(listen(srvfd, 16)) {
    perror("listen");
    close(srvfd);
    return -1;
  }
#ifdef __SCE__
  app_install_if_needed(port);
#endif
  g_stop_requested = 0;
  g_listen_fd = srvfd;

  if(!(httpd = MHD_start_daemon(MHD_USE_THREAD_PER_CONNECTION | MHD_USE_ITC |
#if WFM_DEBUG
                                MHD_USE_DEBUG |
#endif
                                MHD_USE_NO_LISTEN_SOCKET |
                                MHD_USE_INTERNAL_POLLING_THREAD | MHD_USE_TURBO,
                                0, NULL, NULL, &websrv_on_request, NULL,
                                MHD_OPTION_CONNECTION_MEMORY_LIMIT,
                                (size_t)HTTP_CONNECTION_MEMORY_LIMIT,
                                MHD_OPTION_CONNECTION_MEMORY_INCREMENT,
                                (size_t)HTTP_CONNECTION_MEMORY_INCREMENT,
                                MHD_OPTION_NOTIFY_COMPLETED,
                                &websrv_on_completed, NULL, MHD_OPTION_END))) {
    perror("MHD_start_daemon");
    close(srvfd);
    return -1;
  }

  while(!g_stop_requested) {
    addr_len = sizeof(client_addr);
    if((connfd = accept(srvfd, (struct sockaddr *)&client_addr, &addr_len)) < 0) {
#if WFM_DEBUG
      if(!g_stop_requested) perror("accept");
#endif
      break;
    }
    websrv_tune_connection_socket(connfd);
    if(MHD_add_connection(httpd, connfd, (struct sockaddr *)&client_addr,
                          addr_len) != MHD_YES) {
#if WFM_DEBUG
      perror("MHD_add_connection");
#endif
      close(connfd);
      break;
    }
  }

  MHD_stop_daemon(httpd);
  g_listen_fd = -1;
  return close(srvfd);
}

unsigned short websrv_port(void) {
  struct sockaddr_in address;
  socklen_t size = sizeof(address);
  return g_listen_fd >= 0 && !getsockname(g_listen_fd, (struct sockaddr *)&address, &size) ?
    ntohs(address.sin_port) : 0;
}
