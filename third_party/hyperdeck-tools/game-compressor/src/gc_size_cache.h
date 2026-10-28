/*
 * Game Compressor - background folder-size estimate cache.
 */

#pragma once

#include <stdint.h>
#include <time.h>

typedef enum gc_size_status {
  GC_SIZE_STATUS_UNKNOWN = 0,
  GC_SIZE_STATUS_CACHED,
  GC_SIZE_STATUS_QUEUED,
  GC_SIZE_STATUS_SCANNING,
  GC_SIZE_STATUS_REFRESHING,
  GC_SIZE_STATUS_DONE,
  GC_SIZE_STATUS_FAILED,
} gc_size_status_t;

const char *gc_size_status_name(gc_size_status_t status);
int gc_size_cache_lookup(const char *path, uint64_t *size_out,
                         time_t *measured_at_out,
                         gc_size_status_t *status_out);
void gc_size_cache_store(const char *path, uint64_t size);
void gc_size_cache_forget(const char *path);
void gc_size_cache_queue_measure(const char *path);
void gc_size_cache_queue_recheck(const char *path);
gc_size_status_t gc_size_cache_priority(const char *path);
