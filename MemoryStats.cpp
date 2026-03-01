#include "MemoryStats.h"
#include <esp_heap_caps.h>
#include "Config.h"
#include "DiagLog.h"

// [FIXED] Replaced snprintf with String concatenation to prevent Stack Overflow
// snprintf with %f requires huge stack space (dtoa), which crashes inside Ticker/Timer tasks.
static String formatUsage(size_t freeBytes, size_t totalBytes, size_t minFreeBytes) {
  // Calculate USED bytes
  size_t usedBytes = (totalBytes > freeBytes) ? (totalBytes - freeBytes) : 0;

  // Calculate USED percentage
  float pctUsed = (totalBytes > 0)
      ? (static_cast<float>(usedBytes) / static_cast<float>(totalBytes)) * 100.0f
      : 0.0f;

  // Format: "used / total bytes (xx.x% used, minFree N)"
  // We use String() constructors because they use HEAP memory, avoiding the
  // STACK overflow caused by snprintf's heavy floating-point handlers.
  String out = String((unsigned long)usedBytes) + " / " + 
               String((unsigned long)totalBytes) + " bytes (";
               
  out += String(pctUsed, 1) + "% used, minFree " + 
         String((unsigned long)minFreeBytes) + ")";

  return out;
}

String getHeapInternalString() {
  const uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;

  size_t freeHeap  = heap_caps_get_free_size(caps);
  size_t totalHeap = heap_caps_get_total_size(caps);
  size_t minFree   = heap_caps_get_minimum_free_size(caps);

  return formatUsage(freeHeap, totalHeap, minFree);
}

String getPsramString() {
  const uint32_t caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;

  size_t total = heap_caps_get_total_size(caps);
  if (total == 0) {
    return String("N/A");
  }

  size_t free  = heap_caps_get_free_size(caps);
  size_t minFree = heap_caps_get_minimum_free_size(caps);

  return formatUsage(free, total, minFree);
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