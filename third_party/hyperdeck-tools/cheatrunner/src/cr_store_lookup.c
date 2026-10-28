#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "third_party/cJSON.h"
#include "cr_log.h"
#include "cr_store_lookup.h"
#include "http_client.h"

#define STORE_BASE    "https://store.playstation.com/en-us/product/"
#define STORE_MAX_BYTES (384 * 1024)

static const char *
mem_find(const char *hay, size_t hay_len, const char *needle) {
  size_t nl = strlen(needle);
  if (nl == 0 || hay_len < nl) return NULL;
  for (size_t i = 0; i + nl <= hay_len; i++) {
    if (hay[i] == needle[0] && !memcmp(hay + i, needle, nl))
      return hay + i;
  }
  return NULL;
}

/* Extract the game name from a PlayStation Store product HTML page.
 * Looks for the JSON-LD script block and reads the "name" field. */
static char *
extract_name_from_store_html(const char *html, size_t html_len) {
  static const char OPEN[]  = "<script id=\"mfe-jsonld-tags\" type=\"application/ld+json\">";
  static const char CLOSE[] = "</script>";

  const char *p = mem_find(html, html_len, OPEN);
  if (!p) return NULL;
  p += sizeof(OPEN) - 1;

  size_t remaining = html_len - (size_t)(p - html);
  const char *q = mem_find(p, remaining, CLOSE);
  if (!q) return NULL;

  size_t jlen = (size_t)(q - p);
  char *jbuf = malloc(jlen + 1);
  if (!jbuf) return NULL;
  memcpy(jbuf, p, jlen);
  jbuf[jlen] = '\0';

  cJSON *obj = cJSON_Parse(jbuf);
  free(jbuf);
  if (!obj) return NULL;

  char *result = NULL;
  cJSON *name = cJSON_GetObjectItem(obj, "name");
  if (cJSON_IsString(name) && name->valuestring && name->valuestring[0])
    result = strdup(name->valuestring);
  cJSON_Delete(obj);
  return result;
}

int
cr_store_lookup(const char *content_id, char *name_out, size_t name_size, int timeout_ms) {
  if (!content_id || !content_id[0] || !name_out || name_size == 0)
    return 0;

  char url[300];
  if (snprintf(url, sizeof(url), STORE_BASE "%s", content_id) >= (int)sizeof(url))
    return 0;

  int status = 0;
  uint8_t *html = NULL;
  size_t html_len = 0;
  int rc = http_get_url_ex_timeout("CheatRunner/1.0", url, STORE_MAX_BYTES,
                                    timeout_ms, &status, &html, &html_len);
  if (rc != 0 || status != 200 || !html || html_len == 0) {
    free(html);
    cr_log("debug", "store.lookup", "fetch failed rc=%d status=%d url=%s", rc, status, url);
    return 0;
  }

  char *name = extract_name_from_store_html((const char *)html, html_len);
  free(html);
  if (!name) {
    cr_log("debug", "store.lookup", "parse failed (no jsonld name) cid=%s", content_id);
    return 0;
  }

  snprintf(name_out, name_size, "%s", name);
  free(name);
  return 1;
}
