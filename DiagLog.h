// DiagLog.h
#pragma once
#include <Arduino.h>
#include "Config.h"
#include "DiagConfig.h"

// Compile-time master gate:
// - If ENABLE_SERIAL_DIAGNOSTICS == 0, nothing prints (field build safe).
// Runtime gate:
// - g_config.diagSerialEnable + g_config.diagSerialMask controls categories.

inline bool diagEnabled(uint32_t catBit) {
#if ENABLE_SERIAL_DIAGNOSTICS
  if (!g_config.diagSerialEnable) return false;
  return (g_config.diagSerialMask & catBit) != 0;
#else
  (void)catBit;
  return false;
#endif
}

#if ENABLE_SERIAL_DIAGNOSTICS

  // Category-gated info/debug prints
  #define LOG_CAT(catBit, fmt, ...) \
    do { if (diagEnabled((catBit))) { Serial.printf((fmt), ##__VA_ARGS__); } } while (0)

  // Errors: compiled-in AND runtime enabled (mask ignored so errors can still show)
  #define LOG_ERR(fmt, ...) \
    do { if (g_config.diagSerialEnable) { Serial.printf((fmt), ##__VA_ARGS__); } } while (0)

#else

  // Field build: fully silent, and arguments are NOT evaluated
  // Swallow everything so "unused" warnings don't happen and side-effects don't run.
  #define LOG_CAT(catBit, fmt, ...) \
    do { (void)sizeof(catBit); (void)sizeof(fmt); } while (0)

  #define LOG_ERR(fmt, ...) \
    do { (void)sizeof(fmt); } while (0)

#endif
