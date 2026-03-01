#include "AlarmManager.h"
#include "Config.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>
#include "TemperatureControl.h"
#include "FileSystemManager.h"
#include <esp_heap_caps.h>
#include "MemoryStats.h"
#include "DiagLog.h"


// ===== Hourly guard (RTC-based) =====
extern DateTime CurrentTime;   // provided by your RTC refresh task

static int  g_hc_lastY = -1;
static int  g_hc_lastM = -1;
static int  g_hc_lastD = -1;
static int  g_hc_lastH = -1;
static const uint8_t g_hc_windowSec = 2;

static inline bool hcTimeValid(const DateTime& t) {
  return (t.year() >= 2024 && t.year() <= 2099);
}


// Bring in your task handles from TaskManager.cpp
extern TaskHandle_t thSetupRTC;
extern TaskHandle_t thSetupNetwork;
extern TaskHandle_t thInitNTP;
extern TaskHandle_t thinitSystemConfigDefaults;
extern TaskHandle_t thInitFileSystem;
extern TaskHandle_t thloadSystemConfigFromFS;
extern TaskHandle_t thinitTimeConfigDefaults;
extern TaskHandle_t thTemperatureLogging;
extern TaskHandle_t thInitPumps;
extern TaskHandle_t thUpdateTemperatures;  
extern TaskHandle_t thPumpControl;
extern TaskHandle_t thsetupPumpBroadcasting;
extern TaskHandle_t thStartServer;
extern TaskHandle_t thSetupFirstPage;
extern TaskHandle_t thSetupSecondPage;
extern TaskHandle_t thSetupThirdPage;
extern TaskHandle_t threfreshCurrentTime;
extern TaskHandle_t thSetupLogDataRoute;
extern TaskHandle_t thcheckTimeAndAct;
extern TaskHandle_t thcheckAndSyncTime;
extern TaskHandle_t thSerialPrint; 
extern TaskHandle_t thbroadcastTemperatures;
extern TaskHandle_t thmonitorStacks; 
extern TaskHandle_t thlogZeroLengthMessages;
extern TaskHandle_t thUpdatePumpRuntimes;
extern TaskHandle_t thPrintCpuStats;
extern TaskHandle_t thFileSystemCleanup;
extern TaskHandle_t thTaskLogger;
extern TaskHandle_t thSystemStatsBroadcaster;
extern TaskHandle_t thEndofBootup;

// tgzProducer last-run stack stats (WORDS)




static float pctUsed(uint32_t used, uint32_t total) {
  if (total == 0) return 0;
  return 100.0f * ((float)used / (float)total);
}

// Alarm rule (deterministic):
// - TRIP if internal heap free NOW <= 8 KB OR largest free block <= 4 KB
// - CLEAR when internal heap free NOW >= 12 KB AND largest free block >= 6 KB
// - Still reports min-ever (internal) + PSRAM free/total in the alarm text.
//
// NOTE: MALLOC_CAP_INTERNAL is truly internal RAM (what you care about most).
static void checkHeap90() {
  // Match MemoryStats' INTERNAL definition for trigger math
  const uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;

  const uint32_t totalNow = heap_caps_get_total_size(caps);
  const uint32_t freeNow  = heap_caps_get_free_size(caps);
  const uint32_t usedNow  = (totalNow > freeNow) ? (totalNow - freeNow) : 0;

  const float usedPctNow = (totalNow > 0)
    ? (100.0f * (float)usedNow / (float)totalNow)
    : 0.0f;

  // EXACT UI strings (same formatter, same fields, same wording)
  const String heapStr  = getHeapInternalString();
  const String psramStr = getPsramString();

  if (usedPctNow >= 90.0f) {
    AlarmManager_set(ALM_HEAP_HIGH, ALM_ALARM,
                    "Heap high: %s | PSRAM: %s",
                    heapStr.c_str(),
                    psramStr.c_str());
  } else if (usedPctNow <= 85.0f) {
    AlarmManager_clear(ALM_HEAP_HIGH,
                      "Heap OK: %s | PSRAM: %s",
                      heapStr.c_str(),
                      psramStr.c_str());
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
  // Option B: never dereference task handles.
  // We snapshot the current task list and look up tasks by NAME.

  struct TaskSpec { const char* name; uint32_t stackUnits; };

  // stackUnits MUST match what was passed to xTaskCreate/xTaskCreatePinnedToCore.
  // NOTE: On this project build, StackType_t is 1 byte (see TGZ debug print),
  // so these are effectively BYTES. If StackType_t changes, the math still works.
  static const TaskSpec specs[] = {
    {"TaskLogger",               4096},
    {"SetupRTC",                 4096},
    {"SetupNetwork",             4096},
    {"initSystemConfigDefaults", 2048},
    {"InitFileSystem",           4096},
    {"initTimeConfigDefaults",   4096},
    {"loadSystemConfigFromFS",   4096},
    {"InitNTP",                  4096},
    {"InitPumps",                2048},
    {"UpdateTemperature",        4096},
    {"PumpControl",              4096},
    {"setupPumpBroadcasting",    4096},
    {"StartServer",              4096},
    {"SystemStatsBroadcaster",   4096},
    {"SetupFirstPage",           2048},
    {"SetupSecondPage",          4096},
    {"SetupThirdPage",           4096},
    {"SetupLogDataRoute",        2048},
    {"refreshCurrentTime",       8192},
    {"checkTimeAndAct",          4096},
    {"checkAndSyncTime",         4096},
    {"SerialPrint",              2048},
    {"broadcastTemperatures",    4096},
    {"logZeroLengthMessages",    2048},
    {"UpdatePumpRuntimes",       8192},
    {"TaskTemperatureLogging",   4096},
    {"FileSystemCleanup",        4096},
    {"EndofBootup",              4096},
  };

  // Allocate the snapshot on the heap to keep stack usage low.
  const UBaseType_t want = uxTaskGetNumberOfTasks();
  if (want == 0) return;

  TaskStatus_t* st = (TaskStatus_t*)heap_caps_malloc((size_t)want * sizeof(TaskStatus_t),
                                                    MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!st) {
    AlarmManager_set(ALM_STACK_HIGH, ALM_WARN, "Stacks: OOM allocating snapshot (%u tasks)", (unsigned)want);
    return;
  }

  UBaseType_t got = uxTaskGetSystemState(st, want, nullptr);

  auto findHwmByName = [&](const char* name, configSTACK_DEPTH_TYPE* out) -> bool {
    if (!out) return false;
    for (UBaseType_t i = 0; i < got; i++) {
      const char* n = st[i].pcTaskName;
      if (n && strcmp(n, name) == 0) {
        *out = st[i].usStackHighWaterMark;
        return true;
      }
    }
    return false;
  };

  bool any = false;
  float worst = 0.0f;
  const char* worstName = nullptr;

  char offenders[180];
  offenders[0] = '\0';

  for (const auto &s : specs) {
    if (!s.name || s.stackUnits == 0) continue;

    configSTACK_DEPTH_TYPE hwm = 0;
    bool found = findHwmByName(s.name, &hwm);
    if (!found) {
      // Task not currently running (or snapshot did not include it). That's OK.
      // One-shot boot tasks will often be gone by the time the hourly check runs.
      continue;
    }

    uint32_t stackUnits = s.stackUnits;
    uint32_t usedUnits  = (stackUnits > (uint32_t)hwm) ? (stackUnits - (uint32_t)hwm) : 0;
    float pct = 100.0f * (float)usedUnits / (float)stackUnits;

    if (pct > worst) { worst = pct; worstName = s.name; }

    if (pct >= 90.0f) {
      any = true;
      char tmp[48];
      snprintf(tmp, sizeof(tmp), "%s=%.0f%% ", s.name, pct);
      strncat(offenders, tmp, sizeof(offenders) - strlen(offenders) - 1);
    }
  }

  heap_caps_free(st);

  if (any) {
    AlarmManager_set(ALM_STACK_HIGH, ALM_ALARM, "High stack: %s(worst %.1f%% %s)",
                     offenders, worst, worstName ? worstName : "?");
  } else if (worst <= 85.0f) {
    AlarmManager_clear(ALM_STACK_HIGH, "Stacks OK (worst %.1f%% %s)",
                       worst, worstName ? worstName : "?");
  }
}

void runHourlyHealthCheck() {
  // ---- once-per-hour + small window guard ----
  if (!hcTimeValid(CurrentTime)) return;
  if (CurrentTime.second() > g_hc_windowSec) return;

  const int y = CurrentTime.year();
  const int m = CurrentTime.month();
  const int d = CurrentTime.day();
  const int h = CurrentTime.hour();

  const bool alreadyRanThisHour =
    (g_hc_lastY == y && g_hc_lastM == m && g_hc_lastD == d && g_hc_lastH == h);

  if (alreadyRanThisHour) return;

  // Mark first so even if something inside stalls, we don't re-enter-spam
  g_hc_lastY = y; g_hc_lastM = m; g_hc_lastD = d; g_hc_lastH = h;

  // Optional diagnostics after guards
  LOG_CAT(DBG_TASK, "[HealthCheck] runHourlyHealthCheck() millis=%lu\n", (unsigned long)millis());

  // checkStacks90();  // commented out for testing, suspect this is causing Ethernet disconnects
  checkHeap90();
  checkFs90();
  //MemoryStats_printSnapshot("HC_0020");   // commented out for testing, suspect this is causing Ethernet disconnects

  if (storageT >= g_config.storageHeatingLimit) {
    AlarmManager_set(ALM_STORAGE_OVERTEMP, ALM_ALARM, "Storage overtemp: %.1f >= %.1f",
                     storageT, g_config.storageHeatingLimit);
  } else if (storageT <= (g_config.storageHeatingLimit - 5.0f)) {
    AlarmManager_clear(ALM_STORAGE_OVERTEMP, "Storage temp back in range: %.1f", storageT);
  }
}


