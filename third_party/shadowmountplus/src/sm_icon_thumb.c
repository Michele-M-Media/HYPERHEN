#include "sm_platform.h"

#include <png.h>
#include <turbojpeg.h>

#include "sm_icon_thumb.h"
#include "sm_paths.h"
#include "sm_runtime.h"

#define SM_ICON_SOURCE_MAX_SIZE (4u * 1024u * 1024u)
#define SM_ICON_SOURCE_MAX_DIMENSION 2048u
#define SM_ICON_TEMP_MAX_AGE_SECONDS 300
#define SM_ICON_JPEG_QUALITY 90

static unsigned int g_icon_temp_serial;

static bool ensure_icon_cache_directory(void) {
  if (mkdir(LOG_DIR, 0777) != 0 && errno != EEXIST)
    return false;
  return mkdir(ICON_CACHE_DIR, 0777) == 0 || errno == EEXIST;
}

uint64_t sm_icon_source_fingerprint(const char *source_path,
                                    const struct stat *source_st) {
  uint64_t hash = UINT64_C(14695981039346656037);
  for (const unsigned char *p = (const unsigned char *)source_path; *p; ++p)
    hash = (hash ^ *p) * UINT64_C(1099511628211);
  const uint64_t fields[] = {
      (uint64_t)source_st->st_dev, (uint64_t)source_st->st_ino,
      (uint64_t)source_st->st_mtim.tv_sec,
      (uint64_t)source_st->st_mtim.tv_nsec, (uint64_t)source_st->st_size,
  };
  for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i) {
    for (unsigned int shift = 0; shift < 64; shift += 8)
      hash = (hash ^ ((fields[i] >> shift) & 255u)) * UINT64_C(1099511628211);
  }
  return hash;
}

// Average each covered source area with premultiplied alpha. This also avoids
// sampling aliases when a detailed icon is reduced by several times.
static bool resize_rgba(const unsigned char *source, uint32_t source_width,
                         uint32_t source_height, unsigned char *target,
                         uint32_t width, uint32_t height) {
  for (uint32_t y = 0; y < height; ++y) {
    if (should_stop_requested() || runtime_sleep_mode_active())
      return false;
    uint64_t top = ((uint64_t)y * source_height << 16u) / height;
    uint64_t bottom = ((uint64_t)(y + 1u) * source_height << 16u) / height;
    for (uint32_t x = 0; x < width; ++x) {
      uint64_t left = ((uint64_t)x * source_width << 16u) / width;
      uint64_t right = ((uint64_t)(x + 1u) * source_width << 16u) / width;
      uint64_t total = 0, alpha = 0, colors[3] = {0};
      for (uint32_t sy = (uint32_t)(top >> 16u);
           sy < (uint32_t)((bottom + 65535u) >> 16u); ++sy) {
        uint64_t y0 = (uint64_t)sy << 16u, y1 = y0 + 65536u;
        uint64_t wy = (bottom < y1 ? bottom : y1) - (top > y0 ? top : y0);
        for (uint32_t sx = (uint32_t)(left >> 16u);
             sx < (uint32_t)((right + 65535u) >> 16u); ++sx) {
          uint64_t x0 = (uint64_t)sx << 16u, x1 = x0 + 65536u;
          uint64_t wx = (right < x1 ? right : x1) - (left > x0 ? left : x0);
          uint64_t weight = wx * wy;
          const unsigned char *pixel = source + ((size_t)sy * source_width + sx) * 4u;
          total += weight;
          alpha += weight * pixel[3];
          for (size_t c = 0; c < 3u; ++c)
            colors[c] += weight * pixel[3] * pixel[c];
        }
      }
      unsigned char *pixel = target + ((size_t)y * width + x) * 4u;
      for (size_t c = 0; c < 3u; ++c)
        pixel[c] = alpha ? (unsigned char)((colors[c] + alpha / 2u) / alpha) : 0;
      pixel[3] = (unsigned char)((alpha + total / 2u) / total);
    }
  }
  return true;
}

static void cleanup_old_icon_files(const char *title_id, uint64_t fingerprint) {
  DIR *directory = opendir(ICON_CACHE_DIR);
  if (!directory)
    return;
  char prefix[MAX_TITLE_ID + 2u], version[MAX_TITLE_ID + 16u], current[32];
  (void)snprintf(prefix, sizeof(prefix), "%s-", title_id);
  (void)snprintf(version, sizeof(version), "%s-v%u-", title_id, SM_ICON_CACHE_VERSION);
  (void)snprintf(current, sizeof(current), "-%016llx.",
                 (unsigned long long)fingerprint);
  time_t now = time(NULL);
  struct dirent *entry;
  while ((entry = readdir(directory)) != NULL) {
    if (should_stop_requested() || runtime_sleep_mode_active())
      break;
    if (strncmp(entry->d_name, prefix, strlen(prefix)) != 0)
      continue;
    bool temporary = strstr(entry->d_name, ".tmp.") != NULL;
    if (!temporary && strncmp(entry->d_name, version, strlen(version)) == 0 &&
        strstr(entry->d_name, current))
      continue;
    const char *extension = strrchr(entry->d_name, '.');
    if (!temporary && (!extension ||
        (strcmp(extension, ".png") != 0 && strcmp(extension, ".jpg") != 0)))
      continue;
    char path[MAX_PATH];
    int written = snprintf(path, sizeof(path), "%s/%s", ICON_CACHE_DIR,
                            entry->d_name);
    if (written < 0 || (size_t)written >= sizeof(path))
      continue;
    struct stat st;
    if (lstat(path, &st) != 0 || !S_ISREG(st.st_mode) || now < st.st_mtime ||
        now - st.st_mtime < SM_ICON_TEMP_MAX_AGE_SECONDS)
      continue;
    (void)unlink(path);
  }
  (void)closedir(directory);
}

static bool encode_jpeg(const char *path, const unsigned char *pixels,
                         uint32_t width, uint32_t height) {
  tjhandle encoder = tjInitCompress();
  if (!encoder)
    return false;
  unsigned char *jpeg = NULL;
  unsigned long size = 0;
  bool encoded = tjCompress2(encoder, pixels, (int)width, 0, (int)height,
                             TJPF_RGBA, &jpeg, &size, TJSAMP_444,
                             SM_ICON_JPEG_QUALITY, TJFLAG_ACCURATEDCT) == 0;
  if (encoded) {
    FILE *file = fopen(path, "wb");
    encoded = file && fwrite(jpeg, 1, size, file) == size;
    if (file && fclose(file) != 0)
      encoded = false;
  }
  tjFree(jpeg);
  (void)tjDestroy(encoder);
  return encoded;
}

static bool copy_icon_file(const char *source_path, const char *target_path) {
  FILE *source = fopen(source_path, "rb");
  if (!source)
    return false;
  FILE *target = fopen(target_path, "wb");
  if (!target) {
    (void)fclose(source);
    return false;
  }
  unsigned char buffer[32768];
  size_t length;
  bool copied = true;
  while ((length = fread(buffer, 1, sizeof(buffer), source)) > 0) {
    if (should_stop_requested() || runtime_sleep_mode_active() ||
        fwrite(buffer, 1, length, target) != length) {
      copied = false;
      break;
    }
  }
  if (ferror(source))
    copied = false;
  if (fclose(source) != 0)
    copied = false;
  if (fclose(target) != 0)
    copied = false;
  return copied;
}

static bool encode_png(const char *path, const unsigned char *pixels,
                        uint32_t width, uint32_t height) {
  png_image output;
  memset(&output, 0, sizeof(output));
  output.version = PNG_IMAGE_VERSION;
  output.width = width;
  output.height = height;
  output.format = PNG_FORMAT_RGBA;
  bool encoded = png_image_write_to_file(&output, path, 0, pixels, 0, NULL) != 0;
  png_image_free(&output);
  return encoded;
}

static bool create_thumbnail(const char *source_path, const char *base_path,
                              const struct stat *source_st, uint32_t edge,
                              char out[MAX_PATH]) {
  uint64_t fingerprint = sm_icon_source_fingerprint(source_path, source_st);
  png_image image;
  memset(&image, 0, sizeof(image));
  image.version = PNG_IMAGE_VERSION;
  if (!png_image_begin_read_from_file(&image, source_path)) {
    png_image_free(&image);
    return false;
  }
  if (image.width == 0 || image.height == 0 ||
      image.width > SM_ICON_SOURCE_MAX_DIMENSION ||
      image.height > SM_ICON_SOURCE_MAX_DIMENSION) {
    png_image_free(&image);
    return false;
  }
  image.format = PNG_FORMAT_RGBA;
  size_t source_size = PNG_IMAGE_SIZE(image);
  unsigned char *source = malloc(source_size);
  if (!source || !png_image_finish_read(&image, NULL, source, 0, NULL)) {
    free(source);
    png_image_free(&image);
    return false;
  }
  uint32_t source_width = image.width, source_height = image.height;
  png_image_free(&image);
  // Keep the legacy thumb endpoint PNG-compatible.
  bool opaque = edge != SM_ICON_THUMB_SIZE;
  for (size_t i = 3; opaque && i < source_size; i += 4u) {
    if (source[i] != 255u) { opaque = false; break; }
  }
  uint32_t longest = source_width > source_height ? source_width : source_height;
  uint32_t scaled = longest < edge ? longest : edge;
  uint32_t width = (source_width * scaled + longest / 2u) / longest;
  uint32_t height = (source_height * scaled + longest / 2u) / longest;
  if (width == 0) width = 1;
  if (height == 0) height = 1;
  unsigned char *target = source;
  if (width != source_width || height != source_height) {
    target = malloc((size_t)width * height * 4u);
    if (!target || !resize_rgba(source, source_width, source_height, target, width, height)) {
      free(source);
      free(target);
      return false;
    }
    free(source);
  }
  int written = snprintf(out, MAX_PATH, "%s.%s", base_path, opaque ? "jpg" : "png");
  if (written < 0 || written >= MAX_PATH) {
    free(target);
    return false;
  }
  unsigned int serial = __sync_add_and_fetch(&g_icon_temp_serial, 1u);
  char temp_path[MAX_PATH];
  written = snprintf(temp_path, sizeof(temp_path), "%s.tmp.%ld.%u", out,
                      (long)getpid(), serial);
  if (written < 0 || (size_t)written >= sizeof(temp_path)) {
    free(target);
    return false;
  }
  bool encoded;
  if (opaque) {
    encoded = encode_jpeg(temp_path, target, width, height);
    struct stat jpeg_st;
    // Pixel art can already be a very small palette PNG. Avoid making its
    // cache heavier just to use JPEG, and keep original PNG bytes at full size.
    if (encoded && stat(temp_path, &jpeg_st) == 0 &&
        jpeg_st.st_size >= source_st->st_size) {
      char png_path[MAX_PATH];
      written = snprintf(png_path, sizeof(png_path), "%s.png", temp_path);
      if (written >= 0 && (size_t)written < sizeof(png_path)) {
        bool png_encoded = width == source_width && height == source_height
                               ? copy_icon_file(source_path, png_path)
                               : encode_png(png_path, target, width, height);
        struct stat png_st;
        if (png_encoded && stat(png_path, &png_st) == 0 &&
            png_st.st_size < jpeg_st.st_size && rename(png_path, temp_path) == 0) {
          memcpy(out + strlen(out) - 3u, "png", 4u);
        }
        (void)unlink(png_path);
      }
    }
  } else {
    encoded = width == source_width && height == source_height
                  ? copy_icon_file(source_path, temp_path)
                  : encode_png(temp_path, target, width, height);
  }
  free(target);
  struct stat current;
  if (!encoded || should_stop_requested() || runtime_sleep_mode_active() ||
      stat(source_path, &current) != 0 ||
      sm_icon_source_fingerprint(source_path, &current) != fingerprint) {
    (void)unlink(temp_path);
    return false;
  }
  int fd = open(temp_path, O_RDONLY);
  bool synced = fd >= 0 && fsync(fd) == 0;
  if (fd >= 0)
    (void)close(fd);
  if (!synced || rename(temp_path, out) != 0) {
    (void)unlink(temp_path);
    return false;
  }
  return true;
}

bool sm_icon_thumbnail_path(const char *title_id, const char *source_path,
                            const struct stat *source_st, uint32_t edge,
                            char out[MAX_PATH]) {
  if (!title_id || strlen(title_id) != 9u ||
      strspn(title_id, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789") != 9u ||
      !source_path || !source_st || !out || !S_ISREG(source_st->st_mode) ||
      source_st->st_size <= 0 ||
      (uint64_t)source_st->st_size > SM_ICON_SOURCE_MAX_SIZE ||
      (edge != SM_ICON_THUMB_SIZE && edge != SM_ICON_CARD_SIZE && edge != SM_ICON_COVER_SIZE) ||
      should_stop_requested() || runtime_sleep_mode_active() ||
      !ensure_icon_cache_directory())
    return false;
  uint64_t fingerprint = sm_icon_source_fingerprint(source_path, source_st);
  char base[MAX_PATH];
  int written = snprintf(base, sizeof(base), "%s/%s-v%u-%u-%016llx", ICON_CACHE_DIR,
                          title_id, SM_ICON_CACHE_VERSION, edge,
                          (unsigned long long)fingerprint);
  if (written < 0 || (size_t)written >= sizeof(base))
    return false;
  const char *extensions[] = {"jpg", "png"};
  for (size_t i = 0; i < sizeof(extensions) / sizeof(extensions[0]); ++i) {
    written = snprintf(out, MAX_PATH, "%s.%s", base, extensions[i]);
    if (written < 0 || written >= MAX_PATH)
      return false;
    struct stat st;
    if (lstat(out, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0)
      return true;
  }
  if (!create_thumbnail(source_path, base, source_st, edge, out))
    return false;
  cleanup_old_icon_files(title_id, fingerprint);
  return true;
}
