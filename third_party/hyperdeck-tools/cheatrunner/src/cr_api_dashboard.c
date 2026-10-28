#include <stdint.h>
#include <string.h>
#include "cr_api_internal.h"
#include "cr_api_dashboard.h"
#include "dashboard_html_gz.h"
#include "dashboard_css_gz.h"
#include "dashboard_js_gz.h"

extern const char g_dashboard_html[];
extern const char g_dashboard_css[];
extern const char g_dashboard_js[];

static void
send_asset(int fd, const char *content_type, const char *data,
           int accepts_gzip, const unsigned char *gz_data, unsigned long gz_len, int cacheable) {
  if (accepts_gzip) {
    http_send_response_gzip(fd, 200, content_type, gz_data, (size_t)gz_len, cacheable);
    return;
  }
  if (cacheable) {
    http_send_response_cached(fd, 200, content_type, (const uint8_t *)data, strlen(data));
  } else {
    http_send_response(fd, 200, content_type, (const uint8_t *)data, strlen(data));
  }
}

int
cr_api_dashboard_handle(int fd, const char *method, const char *path,
                         const char *query, const char *body, size_t body_len,
                         int accepts_gzip) {
  (void)method; (void)query; (void)body; (void)body_len;
  if (!strcmp(path, "/") || !strcmp(path, "/index.html") ||
      !strcmp(path, "/launcher.html")) {
    send_asset(fd, "text/html; charset=utf-8", g_dashboard_html, accepts_gzip,
               g_dashboard_html_gz, g_dashboard_html_gz_len, 0);
    return 1;
  }
  if (!strcmp(path, "/dashboard.css")) {
    send_asset(fd, "text/css; charset=utf-8", g_dashboard_css, accepts_gzip,
               g_dashboard_css_gz, g_dashboard_css_gz_len, 0);
    return 1;
  }
  if (!strcmp(path, "/dashboard.js")) {
    send_asset(fd, "application/javascript; charset=utf-8", g_dashboard_js, accepts_gzip,
               g_dashboard_js_gz, g_dashboard_js_gz_len, 0);
    return 1;
  }
  if (!strcmp(path, "/CheatRunner.png") || !strcmp(path, "/favicon.png") ||
      !strcmp(path, "/icon.png")        || !strcmp(path, "/apple-touch-icon.png")) {
    http_send_png_asset(fd);
    return 1;
  }
  /* A 404 makes browsers drop the offline cache an older build installed. */
  if (!strcmp(path, "/cache.appcache")) {
    http_send_response(fd, 404, "text/plain", (const uint8_t *)"gone", 4);
    return 1;
  }
  if (!strcmp(path, "/manifest.json")) {
    static const char manifest[] =
      "{\"name\":\"CheatRunner\",\"short_name\":\"CheatRunner\","
      "\"description\":\"PS5 Web Launcher & Cheat Trainer\","
      "\"theme_color\":\"#3B82F6\",\"background_color\":\"#070b16\","
      "\"display\":\"standalone\","
      "\"icons\":[{\"src\":\"/CheatRunner.png\",\"sizes\":\"192x192\",\"type\":\"image/png\"},"
      "{\"src\":\"/CheatRunner.png\",\"sizes\":\"512x512\",\"type\":\"image/png\","
      "\"purpose\":\"any maskable\"}]}";
    http_send_response(fd, 200, "application/manifest+json",
      (const uint8_t *)manifest, sizeof(manifest) - 1);
    return 1;
  }
  return 0;
}
