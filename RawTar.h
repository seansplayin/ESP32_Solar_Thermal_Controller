// RawTar.h
#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

// ===== FS safety helpers (used by ThirdWebpage + RawTar) =====
static inline bool isSafePath(const String &rel) {
  if (rel.length() == 0) return false;
  if (!rel.startsWith("/")) return false;
  if (rel.indexOf("..") >= 0) return false;
  return true;
}

static inline String baseNameOf(const String &path) {
  int slash = path.lastIndexOf('/');
  if (slash < 0) return path;
  if (slash == (int)path.length() - 1) {
    String tmp = path;
    while (tmp.endsWith("/") && tmp.length() > 1) tmp.remove(tmp.length() - 1);
    slash = tmp.lastIndexOf('/');
    return (slash < 0) ? tmp : tmp.substring(slash + 1);
  }
  return path.substring(slash + 1);
}

// Exposed for TaskManager monitorStacks() last-run print.  The raw TAR
// streaming route is chunk-callback based, so this normally stays NULL.
extern TaskHandle_t thRawTarProducer;
extern volatile uint32_t rawTarLastStackWords;
extern volatile uint32_t rawTarLastHwmWords;

namespace RawTar {
  void begin();
  void registerRoutes(AsyncWebServer &server);
}
