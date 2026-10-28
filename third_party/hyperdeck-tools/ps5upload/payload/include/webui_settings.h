/* The console web UI's settings file: {"lan":false,"tile":true}.
 *
 * lan:  0 serves the page on 127.0.0.1 only (the PS5's own browser); 1 also on the LAN.
 * tile: 1 keeps the "ps5upload" home-screen tile (webui_tile.c) registered; 0 removes it.
 *
 * Pure file I/O (host-tested in payload/tests/webui_settings_selftest.c). Callers serialize
 * saves to one path themselves (webui_api.c holds its settings lock across a save). */
#ifndef PS5UPLOAD_WEBUI_SETTINGS_H
#define PS5UPLOAD_WEBUI_SETTINGS_H

#define WEBUI_SETTINGS_PATH "/data/ps5upload/webui.json"

typedef struct {
    int lan;
    int tile;
} webui_settings_t;

/* lan = 0, tile = 1. */
void webui_settings_defaults(webui_settings_t *out);

/* Fills *out from the file. Returns 0 when the file was read; 1 when it is missing or garbled
 * (not one JSON object, or a key present with a value other than true/false): *out is then the
 * defaults, whole. A valid object that lacks a key keeps that key's default. Never fails. */
int webui_settings_load(const char *path, webui_settings_t *out);

/* Writes the file atomically: `<path>.tmp` in the same directory, fsync, then rename() over
 * `path` within that directory (never across mounts). A failed save leaves the previous file
 * as it was and no temp file behind. 0, or -1. */
int webui_settings_save(const char *path, const webui_settings_t *s);

#endif
