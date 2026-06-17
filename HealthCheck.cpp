#include "AlarmManager.h"
#include "Config.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>
#include "TemperatureControl.h"


// Bring in your task handles
extern TaskHandle_t thSetupNetwork;
extern TaskHandle_t thUpdateTemperatures;
extern TaskHandle_t thStartServer;
// ... add the ones you care about

static float pctUsed(uint32_t used, uint32_t total) {
  if (total == 0) return 0;
  return 100.0f * ((float)used / (float)total);
}

static void checkHeap90() {
  uint32_t total = ESP.getHeapSize();
  uint32_t minFree = ESP.getMinFreeHeap(); // best for “did we ever get tight”
  float usedPct = 100.0f - pctUsed(minFree, total);

  if (usedPct >= 90.0f) {
    AlarmManager_set(ALM_HEAP_HIGH, ALM_ALARM, "Heap tight: minFree=%u / %u (used≈%.1f%%)",
                    (unsigned)minFree, (unsigned)total, usedPct);
  } else if (usedPct <= 85.0f) { // hysteresis
    AlarmManager_clear(ALM_HEAP_HIGH, "Heap OK: minFree=%u / %u (used≈%.1f%%)",
                       (unsigned)minFree, (unsigned)total, usedPct);
  }
}

static void checkFs90() {
  size_t total = LittleFS.totalBytes();
  size_t used  = LittleFS.usedBytes();
  float usedPct = pctUsed((uint32_t)used, (uint32_t)total);

  if (usedPct >= 90.0f) {
    AlarmManager_set(ALM_FS_HIGH, ALM_ALARM, "LittleFS high: used=%u / %u (%.1f%%)",
                     (unsigned)used, (unsigned)total, usedPct);
  } else if (usedPct <= 85.0f) {
    AlarmManager_clear(ALM_FS_HIGH, "LittleFS OK: used=%u / %u (%.1f%%)",
                       (unsigned)used, (unsigned)total, usedPct);
  }
}

static void checkStacks90() {
  struct TaskInfo { const char* name; TaskHandle_t h; uint32_t stackWords; };

  // stackWords MUST match what was passed to xTaskCreate/xTaskCreatePinnedToCore
  const TaskInfo t[] = {
    {"SetupNetwork",       thSetupNetwork,       8192},
    {"UpdateTemperatures", thUpdateTemperatures, 4096},
    {"StartServer",        thStartServer,        4096},
    // ... add the rest you care about
  };

  bool any = false;
  float worst = 0.0f;
  const char* worstName = nullptr;

  char offenders[180];
  offenders[0] = '\0';

  for (auto &x : t) {
    if (!x.h || x.stackWords == 0) continue;
    UBaseType_t hwm = uxTaskGetStackHighWaterMark(x.h); // words
    uint32_t usedWords = (x.stackWords > (uint32_t)hwm) ? (x.stackWords - (uint32_t)hwm) : 0;
    float pct = 100.0f * (float)usedWords / (float)x.stackWords;

    if (pct > worst) { worst = pct; worstName = x.name; }
    if (pct >= 90.0f) {
      any = true;
      char tmp[48];
      snprintf(tmp, sizeof(tmp), "%s=%.0f%% ", x.name, pct);
      strncat(offenders, tmp, sizeof(offenders)-strlen(offenders)-1);
    }
  }

  if (any) {
    AlarmManager_set(ALM_STACK_HIGH, ALM_ALARM, "High stack: %s(worst %.1f%% %s)",
                     offenders, worst, worstName ? worstName : "?");
  } else if (worst <= 85.0f) {
    AlarmManager_clear(ALM_STACK_HIGH, "Stacks OK (worst %.1f%% %s)",
                       worst, worstName ? worstName : "?");
  }
}

void runHourlyHealthCheck() {
  checkStacks90();
  checkHeap90();
  checkFs90();
  if (storageT >= g_config.storageHeatingLimit) {
  AlarmManager_set(ALM_STORAGE_OVERTEMP, ALM_ALARM, "Storage overtemp: %.1f >= %.1f",
                   storageT, g_config.storageHeatingLimit);
} else if (storageT <= (g_config.storageHeatingLimit - 5.0f)) {
  AlarmManager_clear(ALM_STORAGE_OVERTEMP, "Storage temp back in range: %.1f", storageT);
}

}
