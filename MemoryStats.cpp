// MemoryStats.cpp
#include "MemoryStats.h"
#include <esp_heap_caps.h>
#include "Config.h"
#include "DiagLog.h"
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#include <stdio.h>
#include <string.h>

static constexpr size_t MEM_STATS_CACHE_LEN = 112;

static portMUX_TYPE g_memStatsMux = portMUX_INITIALIZER_UNLOCKED;

// Fixed buffers avoid cross-task mutation of global String objects.
static char g_cachedHeapStatsString[MEM_STATS_CACHE_LEN]  = "--";
static char g_cachedPsramStatsString[MEM_STATS_CACHE_LEN] = "N/A";

static void copyCString(char* dst, size_t dstLen, const char* src) {
  if (!dst || dstLen == 0) return;
  if (!src) src = "";

  strncpy(dst, src, dstLen - 1);
  dst[dstLen - 1] = '\0';
}

static void formatUsageToBuffer(char* out,
                                size_t outLen,
                                size_t freeBytes,
                                size_t totalBytes,
                                size_t minFreeBytes) {
  if (!out || outLen == 0) return;

  const size_t usedBytes = (totalBytes > freeBytes) ? (totalBytes - freeBytes) : 0;

  const uint32_t pctTenths = (totalBytes > 0)
      ? (uint32_t)(((uint64_t)usedBytes * 1000ULL + ((uint64_t)totalBytes / 2ULL)) / (uint64_t)totalBytes)
      : 0;

  snprintf(out,
           outLen,
           "%lu / %lu bytes (%lu.%01lu%% used, minFree %lu)",
           (unsigned long)usedBytes,
           (unsigned long)totalBytes,
           (unsigned long)(pctTenths / 10),
           (unsigned long)(pctTenths % 10),
           (unsigned long)minFreeBytes);

  out[outLen - 1] = '\0';
}

static void setCachedHeapStatsString(const char* value) {
  portENTER_CRITICAL(&g_memStatsMux);
  copyCString(g_cachedHeapStatsString, sizeof(g_cachedHeapStatsString), value);
  portEXIT_CRITICAL(&g_memStatsMux);
}

static void setCachedPsramStatsString(const char* value) {
  portENTER_CRITICAL(&g_memStatsMux);
  copyCString(g_cachedPsramStatsString, sizeof(g_cachedPsramStatsString), value);
  portEXIT_CRITICAL(&g_memStatsMux);
}

void updateHeapStatsCache() {
  const uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;

  const size_t freeHeap  = heap_caps_get_free_size(caps);
  const size_t totalHeap = heap_caps_get_total_size(caps);
  const size_t minFree   = heap_caps_get_minimum_free_size(caps);

  char out[MEM_STATS_CACHE_LEN];
  formatUsageToBuffer(out, sizeof(out), freeHeap, totalHeap, minFree);

  setCachedHeapStatsString(out);
}

void updatePsramStatsCache() {
  const uint32_t caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;

  const size_t total = heap_caps_get_total_size(caps);
  if (total == 0) {
    setCachedPsramStatsString("N/A");
    return;
  }

  const size_t free    = heap_caps_get_free_size(caps);
  const size_t minFree = heap_caps_get_minimum_free_size(caps);

  char out[MEM_STATS_CACHE_LEN];
  formatUsageToBuffer(out, sizeof(out), free, total, minFree);

  setCachedPsramStatsString(out);
}

String getCachedHeapInternalString() {
  char out[MEM_STATS_CACHE_LEN];

  portENTER_CRITICAL(&g_memStatsMux);
  copyCString(out, sizeof(out), g_cachedHeapStatsString);
  portEXIT_CRITICAL(&g_memStatsMux);

  return String(out);
}

String getCachedPsramString() {
  char out[MEM_STATS_CACHE_LEN];

  portENTER_CRITICAL(&g_memStatsMux);
  copyCString(out, sizeof(out), g_cachedPsramStatsString);
  portEXIT_CRITICAL(&g_memStatsMux);

  return String(out);
}

String getHeapInternalString() {
  const uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;

  const size_t freeHeap  = heap_caps_get_free_size(caps);
  const size_t totalHeap = heap_caps_get_total_size(caps);
  const size_t minFree   = heap_caps_get_minimum_free_size(caps);

  char out[MEM_STATS_CACHE_LEN];
  formatUsageToBuffer(out, sizeof(out), freeHeap, totalHeap, minFree);

  return String(out);
}

String getPsramString() {
  const uint32_t caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;

  const size_t total = heap_caps_get_total_size(caps);
  if (total == 0) {
    return String("N/A");
  }

  const size_t free    = heap_caps_get_free_size(caps);
  const size_t minFree = heap_caps_get_minimum_free_size(caps);

  char out[MEM_STATS_CACHE_LEN];
  formatUsageToBuffer(out, sizeof(out), free, total, minFree);

  return String(out);
}

void MemoryStats_printSnapshot(const char* tag) {
  if (!tag) tag = "Mem";

  const uint32_t iTotal = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
  const uint32_t iFree  = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  const uint32_t iMin   = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
  const uint32_t iLfb   = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);

  const uint32_t pTotal = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
  const uint32_t pFree  = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  const uint32_t pMin   = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);
  const uint32_t pLfb   = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);

#if ENABLE_SERIAL_DIAGNOSTICS
    LOG_CAT(DBG_MEM, "[%s] Internal Heap: free=%u total=%u minEver=%u lfb=%u\n",
          tag, (unsigned)iFree, (unsigned)iTotal, (unsigned)iMin, (unsigned)iLfb);

  if (pTotal > 0) {
    LOG_CAT(DBG_MEM, "[%s] PSRAM:        free=%u total=%u minEver=%u lfb=%u\n",
            tag, (unsigned)pFree, (unsigned)pTotal, (unsigned)pMin, (unsigned)pLfb);
  } else {
    LOG_CAT(DBG_MEM, "[%s] PSRAM:        not available\n", tag);
  }

#else
  (void)iTotal; (void)iFree; (void)iMin; (void)iLfb;
  (void)pTotal; (void)pFree; (void)pMin; (void)pLfb;
#endif
}