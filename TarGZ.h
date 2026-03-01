#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include "Config.h"

#include "freertos/FreeRTOS.h"  // StackType_t

// tgz producer stack depth (WORDS like the rest of your tasks)
#ifndef TGZ_PRODUCER_TASK_STACK_WORDS
  #define TGZ_PRODUCER_TASK_STACK_WORDS \
    ((TGZ_PRODUCER_TASK_STACK_BYTES + sizeof(StackType_t) - 1) / sizeof(StackType_t))
#endif

// ===== FS safety helpers (header-only; used by ThirdWebpage + TarGZ) =====
static inline bool isSafePath(const String &rel) {
  // Expect a LittleFS absolute path like "/Pump_Logs" or "/Alarm_Logs/file.txt"
  if (rel.length() == 0) return false;
  if (!rel.startsWith("/")) return false;
  if (rel.indexOf("..") >= 0) return false;
  return true;
}

static inline String baseNameOf(const String &path) {
  int slash = path.lastIndexOf('/');
  if (slash < 0) return path;
  if (slash == (int)path.length() - 1) {
    // trailing slash — strip and try again
    String tmp = path;
    while (tmp.endsWith("/") && tmp.length() > 1) tmp.remove(tmp.length() - 1);
    slash = tmp.lastIndexOf('/');
    return (slash < 0) ? tmp : tmp.substring(slash + 1);
  }
  return path.substring(slash + 1);
}

// Exposed for TaskManager monitorStacks() "last-run" print
extern TaskHandle_t thTgzProducer;
extern volatile uint32_t tgzLastStackWords;
extern volatile uint32_t tgzLastHwmWords;

namespace TarGZ {
  // Call once at startup (registerRoutes calls it automatically).
  // Prints a warning if PSRAM ringbuffer is requested but PSRAM is unavailable.
  void begin();

  // Registers /fs/download_compressed route.
  void registerRoutes(AsyncWebServer &server);
}

