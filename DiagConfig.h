// DiagConfig.h
#pragma once
#include <stdint.h>

// -------------- Serial Diagnostic Categories (bitmask) --------------
// Use uint32_t so you can have up to 32 categories.
// Keep these stable so logs remain consistent over time.

enum : uint32_t {
  DBG_NONE      = 0u,

  DBG_MEM       = 1u << 0,  // heap/psram/stack snapshots
  DBG_FS        = 1u << 1,  // filesystem cleanup, file ops
  DBG_PUMPLOG   = 1u << 2,  // pump runtime log writing/aggregation
  DBG_TEMPLOG   = 1u << 3,  // temperature logging cache/flush
  DBG_ALARMLOG  = 1u << 4,  // alarm history / ndjson
  DBG_1WIRE     = 1u << 5,  // DS18B20 / OneWire discovery/read
  DBG_RTD       = 1u << 6,  // MAX31865 / PT1000 path
  DBG_NET       = 1u << 7,  // W5500/WiFi/NTP/DNS/etc.
  DBG_PUMP      = 1u << 8,  // pump control decisions, mode changes
  DBG_RTC       = 1u << 9,  // DS3231 / time validity / drift
  DBG_TARGZ     = 1u << 10, // tar.gz streaming + ringbuf
  DBG_TIMESYNC  = 1u << 11, // NTP/time sync events
  DBG_WEB       = 1u << 12, // WebSocket, HTTP handlers
  DBG_TASK      = 1u << 13, // task create/stack/wdt/traces
  DBG_SENSOR    = 1u << 14, // generic sensor pipeline
  DBG_CONFIG    = 1u << 15, // config load/save parsing
  DBG_PERF      = 1u << 16, // timing, long loop warnings
  DBG_PUMP_RUN_TIME_UI   = 1u << 17, // reading/parsing runtime logs for UI/API (SecondWebpage, /api/pump-runtimes)

  // Convenience masks
  DBG_ALL       = 0xFFFFFFFFu
};

// Recommended "dev default" and "field default" masks.
// Pick what you want as your system default in Config defaults.
//(DBG_MEM | DBG_FS | DBG_PUMPLOG | DBG_TEMPLOG | DBG_ALARMLOG | DBG_1WIRE | DBG_RTD | DBG_NET | DBG_PUMP | DBG_RTC | DBG_TARGZ | DBG_TIMESYNC | DBG_WEB | DBG_TASK | DBG_SENSOR | DBG_CONFIG | DBG_PERF | DBG_PUMP_RUN_TIME_UI);
static constexpr uint32_t DBG_DEFAULT_DEV_MASK =
  (DBG_MEM | DBG_FS | DBG_PUMPLOG | DBG_TEMPLOG | DBG_ALARMLOG | DBG_1WIRE | DBG_NET | DBG_PUMP | DBG_RTC | DBG_TARGZ | DBG_TIMESYNC | DBG_WEB | DBG_TASK | DBG_SENSOR | DBG_CONFIG | DBG_PERF | DBG_PUMP_RUN_TIME_UI);

static constexpr uint32_t DBG_DEFAULT_FIELD_MASK =
  (DBG_NET | DBG_RTC | DBG_TIMESYNC); // keep field builds quiet by default
