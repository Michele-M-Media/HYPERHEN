/* Host-side tests for the console web UI HTTP request parser / response head.
 * The parser is fed growing prefixes of a request by the server loop, so
 * MORE vs TOO_BIG must be right on partial input. */
#include <stdio.h>
#include <string.h>

#include "../src/webui_http.c"

static int failures = 0;

#define CHECK(expr)                                                     \
    do {                                                                \
        if (!(expr)) {                                                  \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr);     \
            failures++;                                                 \
        }                                                               \
    } while (0)

static int parse_str(const char *s, webui_req_t *r) {
    return webui_http_parse(s, strlen(s), r);
}

static void parses_get_with_headers(void) {
    const char *req =
        "GET /api/fs/list?a=1 HTTP/1.1\r\n"
        "Host: 127.0.0.1:9180\r\n"
        "Origin: http://127.0.0.1:9180\r\n"
        "X-PS5U-Token: abcdef0123\r\n"
        "\r\n";
    webui_req_t r;
    CHECK(parse_str(req, &r) == WEBUI_PARSE_OK);
    CHECK(strcmp(r.method, "GET") == 0);
    CHECK(strcmp(r.path, "/api/fs/list") == 0);
    CHECK(strcmp(r.query, "a=1") == 0);
    CHECK(strcmp(r.host, "127.0.0.1:9180") == 0);
    CHECK(strcmp(r.origin, "http://127.0.0.1:9180") == 0);
    CHECK(strcmp(r.token, "abcdef0123") == 0);
    CHECK(r.content_length == 0);
    CHECK(r.header_len == strlen(req));
}

static void post_body_not_consumed(void) {
    const char *req = "POST /api/x HTTP/1.1\r\nContent-Length: 5\r\n\r\nhello";
    webui_req_t r;
    CHECK(parse_str(req, &r) == WEBUI_PARSE_OK);
    CHECK(r.content_length == 5);
    CHECK(r.header_len == strlen(req) - 5);
    CHECK(r.query[0] == '\0');
}

static void incomplete_headers_need_more(void) {
    webui_req_t r;
    CHECK(parse_str("GET / HTTP/1.1\r\nHost: x\r\n", &r) == WEBUI_PARSE_MORE);
    CHECK(webui_http_parse("", 0, &r) == WEBUI_PARSE_MORE);
    CHECK(parse_str("GET /a", &r) == WEBUI_PARSE_MORE);
}

static void oversized_headers_too_big(void) {
    char big[8193];
    webui_req_t r;
    memset(big, 'a', sizeof big);
    memcpy(big, "GET / HTTP/1.1\r\nX: ", 19);
    CHECK(webui_http_parse(big, sizeof big, &r) == WEBUI_PARSE_TOO_BIG);
    /* 8192 bytes without a terminator may still complete: MORE. */
    CHECK(webui_http_parse(big, 8192, &r) == WEBUI_PARSE_MORE);
}

static void long_path_too_big(void) {
    static char req[1100 + 64];
    webui_req_t r;
    size_t n = 0;
    memcpy(req, "GET /", 5); n = 5;
    memset(req + n, 'p', 1025); n += 1025;          /* path = 1026 chars */
    memcpy(req + n, " HTTP/1.1\r\n\r\n", 13); n += 13;
    CHECK(webui_http_parse(req, n, &r) == WEBUI_PARSE_TOO_BIG);
    /* Exactly 1024 is fine. */
    n = 5;
    memcpy(req, "GET /", 5);
    memset(req + n, 'p', 1023); n += 1023;
    memcpy(req + n, " HTTP/1.1\r\n\r\n", 13); n += 13;
    CHECK(webui_http_parse(req, n, &r) == WEBUI_PARSE_OK);
    CHECK(strlen(r.path) == 1024);
}

static void body_over_64k_too_big(void) {
    webui_req_t r;
    CHECK(parse_str("POST /a HTTP/1.1\r\nContent-Length: 65537\r\n\r\n", &r)
          == WEBUI_PARSE_TOO_BIG);
    CHECK(parse_str("POST /a HTTP/1.1\r\nContent-Length: 65536\r\n\r\n", &r)
          == WEBUI_PARSE_OK);
    CHECK(r.content_length == 65536);
    CHECK(parse_str("POST /a HTTP/1.1\r\nContent-Length: 99999999999999\r\n\r\n",
                    &r) == WEBUI_PARSE_TOO_BIG);
}

/* The streamed routes' ceiling: a large Content-Length parses (the server decides by path), the
 * Range header is kept, a second Range is refused like any duplicate that matters. */
static void stream_cap_and_range(void) {
    webui_req_t r;
    const char *big = "POST /api/upload/data HTTP/1.1\r\nContent-Length: 1073741824\r\n\r\n";
    CHECK(webui_http_parse_cap(big, strlen(big), &r, 1ull << 42) == WEBUI_PARSE_OK);
    CHECK(r.content_length == 1073741824ull);
    CHECK(webui_http_parse(big, strlen(big), &r) == WEBUI_PARSE_TOO_BIG);
    CHECK(parse_str("GET /dl/x HTTP/1.1\r\nRange: bytes=10-\r\n\r\n", &r) == WEBUI_PARSE_OK);
    CHECK(strcmp(r.range, "bytes=10-") == 0);
    CHECK(parse_str("GET /dl/x HTTP/1.1\r\nRange: bytes=1-2\r\nrange: bytes=3-4\r\n\r\n", &r) == WEBUI_PARSE_BAD);
    {
        char req[400];
        char longv[201];
        memset(longv, '1', 200);
        longv[200] = '\0';
        snprintf(req, sizeof req, "GET /dl/x HTTP/1.1\r\nRange: bytes=%s-\r\n\r\n", longv);
        CHECK(parse_str(req, &r) == WEBUI_PARSE_OK); /* too long: ignored, served whole */
        CHECK(r.range[0] == '\0');
    }
}

static void chunked_is_bad(void) {
    webui_req_t r;
    CHECK(parse_str("POST /a HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n", &r)
          == WEBUI_PARSE_BAD);
    CHECK(parse_str("POST /a HTTP/1.1\r\ntransfer-encoding: chunked\r\n\r\n", &r)
          == WEBUI_PARSE_BAD);
}

static void header_names_case_insensitive(void) {
    webui_req_t r;
    CHECK(parse_str("GET / HTTP/1.1\r\nHOST: h:9180\r\norigin: o\r\n"
                    "x-ps5u-token: tok\r\ncontent-length: 7\r\n\r\n", &r)
          == WEBUI_PARSE_OK);
    CHECK(strcmp(r.host, "h:9180") == 0);
    CHECK(strcmp(r.origin, "o") == 0);
    CHECK(strcmp(r.token, "tok") == 0);
    CHECK(r.content_length == 7);
}

static void malformed_is_bad(void) {
    webui_req_t r;
    CHECK(parse_str("GET /\r\n\r\n", &r) == WEBUI_PARSE_BAD);
    CHECK(parse_str("GET x HTTP/1.1\r\n\r\n", &r) == WEBUI_PARSE_BAD);
    CHECK(parse_str("get / HTTP/1.1\r\n\r\n", &r) == WEBUI_PARSE_BAD);
    CHECK(parse_str("GET / HTTP/2\r\n\r\n", &r) == WEBUI_PARSE_BAD);
    CHECK(parse_str("GET / HTTP/1.1\r\nNoColon\r\n\r\n", &r) == WEBUI_PARSE_BAD);
    CHECK(parse_str("POST / HTTP/1.1\r\nContent-Length: 1x\r\n\r\n", &r)
          == WEBUI_PARSE_BAD);
    CHECK(parse_str("POST / HTTP/1.1\r\nContent-Length: 1\r\n"
                    "Content-Length: 2\r\n\r\n", &r) == WEBUI_PARSE_BAD);
    CHECK(parse_str("GET / HTTP/1.1\r\nX-PS5U-Token: "
                    "00000000000000000000000000000000"
                    "000000000000000000000000000000000\r\n\r\n", &r)
          == WEBUI_PARSE_BAD);
}

static void head_has_connection_close(void) {
    char out[512];
    int n = webui_http_head(out, sizeof out, 200, "application/json", 42, NULL);
    CHECK(n > 0 && (size_t)n == strlen(out));
    CHECK(strncmp(out, "HTTP/1.1 200 OK\r\n", 17) == 0);
    CHECK(strstr(out, "Connection: close\r\n") != NULL);
    CHECK(strstr(out, "Cache-Control: no-store\r\n") != NULL);
    CHECK(strstr(out, "X-Content-Type-Options: nosniff\r\n") != NULL);
    CHECK(strstr(out, "Content-Type: application/json\r\n") != NULL);
    CHECK(strstr(out, "Content-Length: 42\r\n") != NULL);
    CHECK(strstr(out, "Access-Control") == NULL);
    n = (int)strlen(out);
    CHECK(n >= 4 && strcmp(out + n - 4, "\r\n\r\n") == 0);

    n = webui_http_head(out, sizeof out, 404, "text/plain", 0,
                        "X-Foo: bar\r\n");
    CHECK(n > 0 && strstr(out, "404 Not Found") != NULL);
    CHECK(strstr(out, "X-Foo: bar\r\n") != NULL);

    CHECK(strstr(out, "Cache-Control: no-store\r\n") != NULL);

    /* An own Cache-Control line replaces no-store (first line or a later one). */
    n = webui_http_head(out, sizeof out, 200, "application/json", 9,
                        "Content-Encoding: gzip\r\nCache-Control: public, max-age=31536000, immutable\r\n");
    CHECK(n > 0 && strstr(out, "no-store") == NULL);
    CHECK(strstr(out, "Cache-Control: public, max-age=31536000, immutable\r\n") != NULL);
    CHECK(strstr(out, "Content-Encoding: gzip\r\n") != NULL);
    n = webui_http_head(out, sizeof out, 200, "application/json", 9, "Cache-Control: max-age=5\r\n");
    CHECK(n > 0 && strstr(out, "no-store") == NULL && strstr(out, "Cache-Control: max-age=5\r\n") != NULL);
    /* "Cache-Control" inside another header's value is not a Cache-Control line. */
    n = webui_http_head(out, sizeof out, 200, "text/plain", 0, "X-Note: Cache-Control: nope\r\n");
    CHECK(n > 0 && strstr(out, "Cache-Control: no-store\r\n") != NULL);
    /* Any letter case counts as the own line (HTTP header names are case-insensitive). */
    n = webui_http_head(out, sizeof out, 200, "text/plain", 0, "cache-control: no-cache\r\n");
    CHECK(n > 0 && strstr(out, "no-store") == NULL && strstr(out, "cache-control: no-cache\r\n") != NULL);
    n = webui_http_head(out, sizeof out, 200, "text/plain", 0, "X-A: 1\r\nCACHE-CONTROL: no-cache\r\n");
    CHECK(n > 0 && strstr(out, "no-store") == NULL);
    /* A longer name that starts the same is another header. */
    n = webui_http_head(out, sizeof out, 200, "text/plain", 0, "Cache-Controlled: x\r\n");
    CHECK(n > 0 && strstr(out, "Cache-Control: no-store\r\n") != NULL);

    CHECK(webui_http_head(out, 20, 200, "text/plain", 1, NULL) == -1);
    CHECK(webui_http_head(out, sizeof out, 200, NULL, 1, NULL) > 0);
}

static void every_prefix_of_long_request(void) {
    static char req[2300];
    webui_req_t r;
    size_t n = 0, k, term;
    memcpy(req, "GET /", 5); n = 5;
    memset(req + n, 'p', 99); n += 99;               /* path 100 */
    req[n++] = '?';
    memset(req + n, 'q', 1000); n += 1000;           /* query 1000 */
    memcpy(req + n, " HTTP/1.1\r\nHost: h\r\n\r\n", 22); n += 22;
    term = n;
    for (k = 0; k < term; k++)
        CHECK(webui_http_parse(req, k, &r) == WEBUI_PARSE_MORE);
    CHECK(webui_http_parse(req, n, &r) == WEBUI_PARSE_OK);
    CHECK(strlen(r.query) == 1000);

    /* Largest legal line (path 1024, query 1024) also never flips. */
    n = 0;
    memcpy(req, "DELETE /", 8); n = 8;
    memset(req + n, 'p', 1023); n += 1023;
    req[n++] = '?';
    memset(req + n, 'q', 1024); n += 1024;
    memcpy(req + n, " HTTP/1.1\r\n\r\n", 13); n += 13;
    for (k = 0; k < n; k++)
        CHECK(webui_http_parse(req, k, &r) == WEBUI_PARSE_MORE);
    CHECK(webui_http_parse(req, n, &r) == WEBUI_PARSE_OK);
}

static void duplicate_identity_headers_bad(void) {
    webui_req_t r;
    CHECK(parse_str("GET / HTTP/1.1\r\nHost: a\r\nhost: b\r\n\r\n", &r)
          == WEBUI_PARSE_BAD);
    CHECK(parse_str("GET / HTTP/1.1\r\nOrigin: a\r\nORIGIN: a\r\n\r\n", &r)
          == WEBUI_PARSE_BAD);
    CHECK(parse_str("GET / HTTP/1.1\r\nX-PS5U-Token: a\r\nx-ps5u-token: b\r\n\r\n",
                    &r) == WEBUI_PARSE_BAD);
}

int main(void) {
    every_prefix_of_long_request();
    duplicate_identity_headers_bad();
    parses_get_with_headers();
    post_body_not_consumed();
    incomplete_headers_need_more();
    oversized_headers_too_big();
    long_path_too_big();
    body_over_64k_too_big();
    chunked_is_bad();
    stream_cap_and_range();
    header_names_case_insensitive();
    malformed_is_bad();
    head_has_connection_close();
    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("webui_http_selftest: all passed\n");
    return 0;
}
