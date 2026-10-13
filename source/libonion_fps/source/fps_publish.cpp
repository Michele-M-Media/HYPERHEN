/* Copyright (C) 2026 OnionHEN / LightningMods */
#include <onion/fps_publish.hpp>

#include <onion/log.h>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace onion {
namespace fps {
namespace {

OnionFpsSample *g_map = nullptr;
bool g_logged_fail = false;

} // namespace

bool publish_open() {
  if (g_map && g_map != MAP_FAILED)
    return true;

  const int fd = open(ONION_SYSTEM_TMP_FPS_SAMPLE, O_RDWR | O_CREAT, 0644);
  if (fd < 0) {
    if (!g_logged_fail) {
      LOG_ERROR("fps: open %s failed: %s", ONION_SYSTEM_TMP_FPS_SAMPLE,
                std::strerror(errno));
      g_logged_fail = true;
    }
    return false;
  }
  if (ftruncate(fd, ONION_FPS_SAMPLE_BYTES) != 0) {
    if (!g_logged_fail) {
      LOG_ERROR("fps: ftruncate failed: %s", std::strerror(errno));
      g_logged_fail = true;
    }
    close(fd);
    return false;
  }
  void *p = mmap(nullptr, ONION_FPS_SAMPLE_BYTES, PROT_READ | PROT_WRITE,
                 MAP_SHARED, fd, 0);
  close(fd);
  if (p == MAP_FAILED) {
    if (!g_logged_fail) {
      LOG_ERROR("fps: mmap failed: %s", std::strerror(errno));
      g_logged_fail = true;
    }
    return false;
  }
  g_map = static_cast<OnionFpsSample *>(p);
  std::memset(g_map, 0, sizeof(*g_map));
  g_map->magic = ONION_FPS_MAGIC;
  g_map->pid = -1;
  return true;
}

void publish_close() {
  if (g_map && g_map != MAP_FAILED)
    munmap(g_map, ONION_FPS_SAMPLE_BYTES);
  g_map = nullptr;
}

void publish(const OnionFpsSample &sample) {
  if (!publish_open())
    return;
  const uint32_t seq = g_map->seq;
  __atomic_store_n(&g_map->seq, seq + 1u, __ATOMIC_RELEASE);
  OnionFpsSample tmp = sample;
  tmp.magic = ONION_FPS_MAGIC;
  tmp.seq = seq + 2u;
  std::memcpy(static_cast<void *>(g_map), &tmp, sizeof(tmp));
  __atomic_store_n(&g_map->seq, seq + 2u, __ATOMIC_RELEASE);
}

} // namespace fps
} // namespace onion
