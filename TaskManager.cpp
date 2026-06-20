// TaskManager.cpp
#include "TaskManager.h"
#include "FileSystemManager.h"
#include "FirstWebpage.h"
#include "Logging.h"
#include "NetworkManager.h"
#include "PumpManager.h"
#include "RTCManager.h"
#include "SecondWebpage.h"
#include "ThirdWebpage.h"
#include "TemperatureControl.h"
#include "TemperatureLogging.h"
#include "TimeSync.h"
#include "WebServerManager.h"
#include "Max31865-PT1000.h"
#include "AlarmManager.h"
#include <LittleFS.h>
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_task_wdt.h>
#include "DS18B20.h"
#include "SerialPrint.h"
#include "Config.h"
#include "esp_timer.h"
#include <freertos/queue.h>
#include "AlarmHistory.h"
#include "MemoryStats.h"
#include "RawTar.h"
#include "DiagLog.h"



QueueHandle_t logQueue = nullptr;


// Define flag variables
bool flagZeroLengthTime = false;
bool flagZeroLengthPumpState = false;
bool flagZeroLengthTemperatures = false;

// Temperature broadcast task telemetry (read by WebServerManager stats broadcaster)
volatile uint32_t g_tempBcastCalled  = 0;  // task chose to call broadcastTemperatures()
volatile uint32_t g_tempBcastSkipped = 0;  // task skipped because no writable WS clients


// Declare the flag as extern
extern volatile bool needToUpdatePumpRuntimes;

// Raw TAR producer task handle is defined in RawTar.cpp (NULL when idle)
extern TaskHandle_t thRawTarProducer;


// Task handles for synchronization (add new handles as needed)
TaskHandle_t thSetupRTC = NULL;
TaskHandle_t thSetupNetwork = NULL;
TaskHandle_t thInitNTP = NULL;
TaskHandle_t thinitSystemConfigDefaults = NULL;
TaskHandle_t thInitFileSystem = NULL;
TaskHandle_t thloadSystemConfigFromFS = NULL;
TaskHandle_t thinitTimeConfigDefaults = NULL;
TaskHandle_t thTemperatureLogging = NULL;
TaskHandle_t thInitPumps = NULL;
TaskHandle_t thUpdateTemperatures = NULL;
TaskHandle_t thPumpControl = NULL;
TaskHandle_t thsetupPumpBroadcasting = NULL;
TaskHandle_t thStartServer = NULL;
TaskHandle_t thSetupFirstPage = NULL;
TaskHandle_t thSetupSecondPage = NULL;
TaskHandle_t thSetupThirdPage = NULL;
TaskHandle_t threfreshCurrentTime = NULL;
TaskHandle_t thSetupLogDataRoute = NULL;
TaskHandle_t thcheckTimeAndAct = NULL;
TaskHandle_t thcheckAndSyncTime = NULL;
TaskHandle_t thSerialPrint = NULL;
TaskHandle_t thbroadcastTemperatures = NULL;
TaskHandle_t thmonitorStacks = NULL;
TaskHandle_t thlogZeroLengthMessages = NULL;
TaskHandle_t thUpdatePumpRuntimes = NULL;
TaskHandle_t thPrintCpuStats = NULL;
TaskHandle_t thFileSystemCleanup = NULL;
TaskHandle_t thTaskLogger = NULL;
TaskHandle_t thWebSocketTransmitter = NULL;
TaskHandle_t thEndofBootup = NULL;


// Function to monitor stack usage of tasks (accurate + low-stack)
void monitorStacks() {

  struct TaskInfo {
    const char*   taskName;
    TaskHandle_t  taskHandle;
    uint32_t      stackDepthWords;  // MUST match what you pass to xTaskCreate/xTaskCreatePinnedToCore
  };

  // IMPORTANT:
  // - uxTaskGetStackHighWaterMark() returns "words"
  // - xTaskCreate() stack parameter is also "words" (Arduino-ESP32 follows FreeRTOS here)
  // So we track everything in WORDS, then convert to BYTES for printing.

  const uint32_t W = sizeof(StackType_t);

  TaskInfo tasks[] = {
    {"TaskLogger",               thTaskLogger,                   4096},
    {"SetupRTC",                 thSetupRTC,                     2048},
    {"SetupNetwork",             thSetupNetwork,                 8192},
    {"initSystemConfigDefaults", thinitSystemConfigDefaults,     2048},
    {"InitFileSystem",           thInitFileSystem,               4096},
    {"initTimeConfigDefaults",   thinitTimeConfigDefaults,       4096},
    {"loadSystemConfigFromFS",   thloadSystemConfigFromFS,       8192},
    {"InitNTP",                  thInitNTP,                      4096},
    {"InitPumps",                thInitPumps,                    2048},
    {"UpdateTemperatures",       thUpdateTemperatures,           4096},
    {"PumpControl",              thPumpControl,                  4096},
    {"setupPumpBroadcasting",    thsetupPumpBroadcasting,        4096},
    {"StartServer",              thStartServer,                  4096},
    {"SystemStatsBroadcaster",   thWebSocketTransmitter,         4096},
    {"SetupFirstPage",           thSetupFirstPage,               2048},
    {"SetupSecondPage",          thSetupSecondPage,              4096},
    {"SetupThirdPage",           thSetupThirdPage,               4096},
    {"SetupLogDataRoute",        thSetupLogDataRoute,            2048},
    {"refreshCurrentTime",       threfreshCurrentTime,           8192},
    {"checkTimeAndAct",          thcheckTimeAndAct,              4096},
    {"checkAndSyncTime",         thcheckAndSyncTime,             4096},
    {"SerialPrint",              thSerialPrint,                  2048},
    {"logZeroLengthMessages",    thlogZeroLengthMessages,        2048},
    {"UpdatePumpRuntimes",       thUpdatePumpRuntimes,           8192},
    {"TaskTemperatureLogging",   thTemperatureLogging,           4096},
    {"FileSystemCleanup",        thFileSystemCleanup,            4096},
    {"rawTarProducer",           thRawTarProducer, (uint32_t)RAW_TAR_PRODUCER_TASK_STACK_WORDS},
    {"EndofBootup",              thEndofBootup,                  4096}
  };

  // Monitor Stacks output
  const int numTasks = (int)(sizeof(tasks) / sizeof(tasks[0]));

  for (int i = 0; i < numTasks; i++) {
    TaskHandle_t h = tasks[i].taskHandle;

    if (h == NULL) {

      // prints the last-run stack usage using values you captured right before the producer task deleted itself:
      if (strcmp(tasks[i].taskName, "rawTarProducer") == 0 &&
          rawTarLastStackWords > 0 && rawTarLastHwmWords > 0) {

        uint32_t stackWords = rawTarLastStackWords;
        uint32_t hwmWords   = rawTarLastHwmWords;

        uint32_t usedWords = (stackWords > hwmWords) ? (stackWords - hwmWords) : 0;

        uint32_t stackBytes = stackWords * W;
        uint32_t usedBytes  = usedWords  * W;
        uint32_t hwmBytes   = hwmWords   * W;

        float pct = (stackWords > 0) ? (100.0f * (float)usedWords / (float)stackWords) : 0.0f;

            LOG_CAT(DBG_TASK,
            "Task %-22s: not running (last run: used %u / %u bytes (%.1f%%), free(min) %u bytes)\n",
            tasks[i].taskName, usedBytes, stackBytes, pct, hwmBytes);
  } else {
    LOG_CAT(DBG_TASK, "Task %-22s: not running / no handle\n", tasks[i].taskName);
  }


      continue;
    }

    UBaseType_t hwmWords = uxTaskGetStackHighWaterMark(h);
    uint32_t stackWords  = tasks[i].stackDepthWords;

    uint32_t usedWords = 0;
    if (stackWords > (uint32_t)hwmWords) usedWords = stackWords - (uint32_t)hwmWords;

    uint32_t stackBytes = stackWords * W;
    uint32_t usedBytes  = usedWords  * W;
    uint32_t hwmBytes   = (uint32_t)hwmWords * W;

    float pct = (stackWords > 0) ? (100.0f * (float)usedWords / (float)stackWords) : 0.0f;

        LOG_CAT(DBG_TASK, "Task %-22s: used %u / %u bytes (%.1f%%), free(min) %u bytes\n",
            tasks[i].taskName, usedBytes, stackBytes, pct, hwmBytes);
  }

  LOG_CAT(DBG_TASK, "\n");

}





void logZeroLengthMessages() {
  if (flagZeroLengthTime) {
LOG_CAT(DBG_WEB, "[WS] Time WebSocket attempted to send a zero-length message.\n");
flagZeroLengthTime = false;
}
  if (flagZeroLengthPumpState) {
LOG_CAT(DBG_WEB, "[WS] Pump State WebSocket attempted to send a zero-length message.\n");
flagZeroLengthPumpState = false;
}
  if (flagZeroLengthTemperatures) {
LOG_CAT(DBG_WEB, "[WS] Temperatures WebSocket attempted to send a zero-length message.\n");
flagZeroLengthTemperatures = false;
}}




// ---------------- Boot chain tasks ----------------

void TaskSetupRTC(void *pvParameters) {
  Serial.println("[BOOT] TaskSetupRTC START");

  setupRTC();

  // Give the RTC a short chance to promote its stored time into CurrentTime
  // before the rest of the boot chain proceeds.
  if (!g_timeValid && g_rtcOk) {
    uint32_t startMs = millis();
    while (!g_timeValid && (millis() - startMs < 2000UL)) {
      if (syncCurrentTimeFromRTCIfValid()) {
        break;
      }
      vTaskDelay(pdMS_TO_TICKS(100));
    }
  }

  Serial.println("[BOOT] TaskSetupRTC DONE");
  xTaskNotifyGive(thInitFileSystem);
  vTaskDelete(NULL);
}


void TaskInitFileSystem(void *pvParameters) {
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  Serial.println("[BOOT] TaskInitFileSystem START");

  initializeFileSystem();

  Serial.println("[BOOT] TaskInitFileSystem DONE");
  AlarmHistory_onFileSystemReady();
  closeAllOpenPumpLogs();
  if (thFileSystemCleanup != NULL) {
    xTaskNotifyGive(thFileSystemCleanup);
  }
  xTaskNotifyGive(thinitSystemConfigDefaults);
  vTaskDelete(NULL);
}


void TaskInitSystemConfigDefaults(void *pvParameters) {
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  initSystemConfigDefaults();
  xTaskNotifyGive(thinitTimeConfigDefaults);
  vTaskDelete(NULL);
}


void TaskInitTimeConfigDefaults(void *pvParameters) {
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  initTimeConfigDefaults();
  if (!loadTimeConfigFromFS()) {
    saveTimeConfigToFS();
  }
  xTaskNotifyGive(thloadSystemConfigFromFS);
  vTaskDelete(NULL);
}


void TaskLoadSystemConfigFromFS(void *pvParameters) {
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  const int maxAttempts = 3;
  bool ok = false;

  for (int attempt = 1; attempt <= maxAttempts; ++attempt) {
    if (loadSystemConfigFromFS()) {
      ok = true;
      break;
    }
    LOG_CAT(DBG_CONFIG,
            "[Config] loadSystemConfigFromFS() attempt %d/%d failed, retrying...\n",
            attempt, maxAttempts);
    vTaskDelay(pdMS_TO_TICKS(1000));
  }

  if (!ok) {
    LOG_CAT(DBG_CONFIG, "[Config] Giving up loading %s, using defaults.\n", SYSTEM_CONFIG_PATH);
  }

  // Load diagnostic serial config if user has created it (do NOT auto-create)
  if (loadDiagSerialConfigFromFS()) {
    LOG_CAT(DBG_CONFIG, "[Config] Loaded diag serial config from %s\n", DIAG_SERIAL_CONFIG_PATH);
  } else {
    LOG_CAT(DBG_CONFIG, "[Config] No diag serial config at %s; using Config.h defaults.\n", DIAG_SERIAL_CONFIG_PATH);
  }

  // DS18B20 assignment config is created with defaults if missing so the
  // webpage/API has a real file to edit later.
  if (loadDs18B20ConfigFromFS()) {
    LOG_CAT(DBG_CONFIG, "[DS18B20Config] Loaded DS18B20 assignment config from %s\n", DS18B20_CONFIG_PATH);
  } else {
    LOG_CAT(DBG_CONFIG, "[DS18B20Config] No valid config at %s; saving defaults.\n", DS18B20_CONFIG_PATH);
    saveDs18B20ConfigToFS();
  }

  initDS18B20Sensors();

  xTaskNotifyGive(thSetupNetwork);
  vTaskDelete(NULL);
}


// WDT fix: do NOT add this task to WDT before blocking on notify.
void TaskSetupNetwork(void *pvParameters) {
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

  esp_task_wdt_add(NULL);
  esp_task_wdt_reset();

  setupNetwork();

  esp_task_wdt_reset();
  esp_task_wdt_delete(NULL);

  xTaskNotifyGive(thInitNTP);

  thSetupNetwork = NULL;
  vTaskDelete(NULL);
}


// WDT fix: do NOT add this task to WDT before blocking on notify.
void TaskInitNTP(void *pvParameters) {
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

  esp_task_wdt_add(NULL);
  esp_task_wdt_reset();

  initNTP();

  esp_task_wdt_reset();
  esp_task_wdt_delete(NULL);

  xTaskNotifyGive(thInitPumps);
  vTaskDelete(NULL);
}


void TaskInitPumps(void *pvParameters) {
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  initializePumps();
  xTaskNotifyGive(thStartServer);
  vTaskDelete(NULL);
}


void TaskStartServer(void *pvParameters) {
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  while (!isNetworkConnected()) {
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
  vTaskDelay(1000 / portTICK_PERIOD_MS);
  startServer();
  xTaskNotifyGive(thSetupFirstPage);
  vTaskDelete(NULL);
}


void TaskSetupFirstPage(void *pvParameters) {
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  setupFirstPageRoutes();
  xTaskNotifyGive(thSetupSecondPage);
  vTaskDelete(NULL);
}


void TaskSetupSecondPage(void *pvParameters) {
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  setupSecondPageRoutes();
  xTaskNotifyGive(thSetupThirdPage);
  vTaskDelete(NULL);
}


void TaskSetupThirdPage(void *pvParameters) {
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  setupThirdPageRoutes();
  xTaskNotifyGive(thSetupLogDataRoute);
  vTaskDelete(NULL);
}


void TaskSetupLogDataRoute(void *pvParameters) {
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

  // Legacy /get-log-data route removed.
  // Keep this task only to preserve the existing boot notification chain.
  xTaskNotifyGive(threfreshCurrentTime);
  vTaskDelete(NULL);
}


// Gated to start after SetupLogDataRoute notifies it (so it doesn't run during early boot).
void TaskrefreshCurrentTime(void *pv) {
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

  esp_task_wdt_add(NULL);
  for (;;) {
    refreshCurrentTime();
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}


// ---------------- Background tasks (unchanged behavior) ----------------

void TaskUpdateTemperatures(void *pvParameters) {
    (void)pvParameters;
    
    // 1. Register this task with the Watchdog Timer
    esp_task_wdt_add(NULL); 
    
    const TickType_t xFrequency = pdMS_TO_TICKS(5000); // 5 Seconds
    TickType_t xLastWakeTime = xTaskGetTickCount();

    for (;;) {
        // 2. CRITICAL: Pet the watchdog so the ESP32 doesn't reboot!
        esp_task_wdt_reset(); 

        if (!ds18B20SensorsReady()) {
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }

        // 3. Read the physical hardware (DS18B20s, PT1000)
        updateTemperatures(); 

        // 4. Run your 2-decimal change detection logic.
        broadcastTemperatures(); 

        // 5. Sleep exactly until the next interval
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}


void TaskPumpControl(void *pvParameters) {
  (void)pvParameters; // Good practice to prevent compiler warnings
  
  const TickType_t bootDelayTicks = pdMS_TO_TICKS(15000);
  vTaskDelay(bootDelayTicks);

  // 1. Register with Watchdog
  esp_task_wdt_add(NULL); 
  
  TickType_t xLastWakeTime = xTaskGetTickCount();
  
  for (;;) {
    // 2. Pet the Watchdog at the start of every loop
    esp_task_wdt_reset(); 
    
    PumpControl();
    
    // 3. Sleep for 1 second
    vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(1000));
  }
}


void TasksetupPumpBroadcasting(void *pvParameters) {
  esp_task_wdt_add(NULL);
  TickType_t xLastWake = xTaskGetTickCount();
  for (;;) {
    esp_task_wdt_reset();
    vTaskDelayUntil(&xLastWake, pdMS_TO_TICKS(5000));
    setupPumpBroadcasting();
    esp_task_wdt_reset();
  }
}


void TaskcheckTimeAndAct(void *pvParameters) {
  esp_task_wdt_add(NULL);

  TickType_t xLastWakeTime = xTaskGetTickCount();
  for (;;) {
    esp_task_wdt_reset();
    vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(1000));
    checkTimeAndAct();
    esp_task_wdt_reset();
  }
}


void TaskTemperatureLogging(void *pvParameters) {
  setupTemperatureLogging();
  esp_task_wdt_add(NULL);
  for (;;) {
    TaskTemperatureLogging_Run(pvParameters);
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}


void TaskmonitorStacks(void *pvParameters) {
  esp_task_wdt_add(NULL);
  TickType_t xLastWakeTime = xTaskGetTickCount();
  for (;;) {
    vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(60000));
    esp_task_wdt_reset();
    monitorStacks();
    esp_task_wdt_reset();
  }
}


void TasklogZeroLengthMessages(void *pvParameters) {
  esp_task_wdt_add(NULL);
  TickType_t xLastWakeTime = xTaskGetTickCount();
  for (;;) {
    vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(1000));
    esp_task_wdt_reset();
    logZeroLengthMessages();
    esp_task_wdt_reset();
  }
}


void TaskLogger(void* pv) {
  (void)pv;

  // Phase 1 pump-log buffering:
  // logPumpEvent() now captures START/STOP records in RAM immediately.
  // This task only services the legacy fallback queue and periodically flushes
  // the RAM buffer to /Pump_Logs.  A busy filesystem/archive download no longer
  // causes pump runtime events to be dropped.
  for (;;) {
    servicePumpLogBufferOnce(600000UL, 64); // 10-minute flush or 64 pending events
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}


void TaskUpdatePumpRuntimes(void *pvParameters) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    esp_task_wdt_add(NULL);
    esp_task_wdt_reset();
    if (needToUpdatePumpRuntimes) {
      needToUpdatePumpRuntimes = false;
      updateAllRuntimes();
    }
    esp_task_wdt_reset();
    xTaskNotifyGive(thEndofBootup);
    esp_task_wdt_delete(NULL);
  }
}


void TaskEndofBootup(void *pvParameters) {
  // Do NOT enable temperature logging here.
  // Logging now enables automatically the moment valid time is acquired.
  MemoryStats_printSnapshot("BootComplete");
  vTaskDelete(NULL);
}


void TaskPrintCpuStats(void*) {
  static char buf[1024];
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(5000));
    memset(buf, 0, sizeof(buf));
    vTaskGetRunTimeStats(buf);
    LOG_CAT(DBG_PERF, "------ CPU RUN-TIME STATS ------\n");
    LOG_CAT(DBG_PERF, "%s\n", buf);
  }
}


void TaskFileSystemCleanup(void *pvParameters) {
  const TickType_t bootDelayTicks = pdMS_TO_TICKS(65000);
  vTaskDelay(bootDelayTicks);
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (!g_fileSystemReady) {
      LOG_CAT(DBG_FS, "[FS.CleanupTask] Skipping — g_fileSystemReady == false\n");
      continue;
    }
    enforceTemperatureLogDiskLimit();
  }
}
// ======================= END TASKS (FULL SECTION) =======================









// ====================================================== TASKS (FULL SECTION) ===================================================

void startAllTasks() {

  AlarmManager_begin();

  logQueue = xQueueCreate(30, sizeof(LogEvent));


  // =============================================================================================================================
  // ----------------------------------------- CORE 0: Background, Heavy I/O, Web, & Sensors -------------------------------------
  // Pinning OneWire here prevents it from blinding the Ethernet MAC interrupts on Core 1.
  // =============================================================================================================================
  //xTaskCreatePinnedToCore(TaskLogger, "TaskLogger", 4096, NULL, 1, &thTaskLogger, 0);
   xTaskCreate(TaskLogger, "TaskLogger", 4096, NULL, 1, &thTaskLogger);

  //xTaskCreatePinnedToCore(TaskUpdateTemperatures, "UpdateTemperature", 4096, NULL, 4, &thUpdateTemperatures, 0);
   xTaskCreate(TaskUpdateTemperatures, "UpdateTemperature", 4096, NULL, 4, &thUpdateTemperatures);

  //xTaskCreatePinnedToCore(TaskTemperatureLogging, "TaskTemperatureLogging", 4096, NULL, 1, &thTemperatureLogging, 0);
   xTaskCreate(TaskTemperatureLogging, "TaskTemperatureLogging", 4096, NULL, 1, &thTemperatureLogging);

  //xTaskCreatePinnedToCore(TaskUpdatePumpRuntimes, "UpdatePumpRuntimes", 8192, NULL, 1, &thUpdatePumpRuntimes, 0);
   xTaskCreate(TaskUpdatePumpRuntimes, "UpdatePumpRuntimes", 8192, NULL, 1, &thUpdatePumpRuntimes);

  //xTaskCreatePinnedToCore(TaskInitNTP, "InitNTP", 4096, NULL, 1, &thInitNTP, 0);
   xTaskCreate(TaskInitNTP, "InitNTP", 4096, NULL, 1, &thInitNTP);


  //xTaskCreatePinnedToCore(TaskWebSocketTransmitter, "WSTransmitter", 4096, NULL, 1, &thWebSocketTransmitter, 0);
   xTaskCreate(TaskWebSocketTransmitter, "WSTransmitter", 4096, NULL, 1, &thWebSocketTransmitter);



// =============================================================================================================================
// -------------------------------------------Boot sequence and routing (Run once and die)--------------------------------------
// =============================================================================================================================
  //xTaskCreatePinnedToCore(TaskSetupRTC, "SetupRTC", 4096, NULL, 1, &thSetupRTC, 0);
   xTaskCreate(TaskSetupRTC, "SetupRTC", 4096, NULL, 1, &thSetupRTC);

  //xTaskCreatePinnedToCore(TaskInitFileSystem, "InitFileSystem", 4096, NULL, 1, &thInitFileSystem, 0);
   xTaskCreate(TaskInitFileSystem, "InitFileSystem", 4096, NULL, 1, &thInitFileSystem);

  //xTaskCreatePinnedToCore(TaskInitSystemConfigDefaults, "initSystemConfigDefaults", 2048, NULL, 1, &thinitSystemConfigDefaults, 0);
   xTaskCreate(TaskInitSystemConfigDefaults, "initSystemConfigDefaults", 2048, NULL, 1, &thinitSystemConfigDefaults);

  //xTaskCreatePinnedToCore(TaskInitTimeConfigDefaults, "initTimeConfigDefaults", 4096, NULL, 1, &thinitTimeConfigDefaults, 0);
   xTaskCreate(TaskInitTimeConfigDefaults, "initTimeConfigDefaults", 4096, NULL, 1, &thinitTimeConfigDefaults);

  //xTaskCreatePinnedToCore(TaskLoadSystemConfigFromFS, "loadSystemConfigFromFS", 4096, NULL, 1, &thloadSystemConfigFromFS, 0);
   xTaskCreate(TaskLoadSystemConfigFromFS, "loadSystemConfigFromFS", 4096, NULL, 1, &thloadSystemConfigFromFS);

  //xTaskCreatePinnedToCore(TaskInitPumps, "InitPumps", 2048, NULL, 1, &thInitPumps, 0);
   xTaskCreate(TaskInitPumps, "InitPumps", 2048, NULL, 1, &thInitPumps);

  //xTaskCreatePinnedToCore(TaskSetupFirstPage, "SetupFirstPage", 2048, NULL, 1, &thSetupFirstPage, 0);
   xTaskCreate(TaskSetupFirstPage, "SetupFirstPage", 2048, NULL, 1, &thSetupFirstPage);

  //xTaskCreatePinnedToCore(TaskSetupSecondPage, "SetupSecondPage", 4096, NULL, 1, &thSetupSecondPage, 0);
   xTaskCreate(TaskSetupSecondPage, "SetupSecondPage", 4096, NULL, 1, &thSetupSecondPage);

  //xTaskCreatePinnedToCore(TaskSetupThirdPage, "SetupThirdPage", 4096, NULL, 1, &thSetupThirdPage, 0);
   xTaskCreate(TaskSetupThirdPage, "SetupThirdPage", 4096, NULL, 1, &thSetupThirdPage);

  //xTaskCreatePinnedToCore(TaskSetupLogDataRoute, "SetupLogDataRoute", 2048, NULL, 1, &thSetupLogDataRoute, 0);
   xTaskCreate(TaskSetupLogDataRoute, "SetupLogDataRoute", 2048, NULL, 1, &thSetupLogDataRoute);

  //xTaskCreatePinnedToCore(TaskrefreshCurrentTime, "refreshCurrentTime", 8192, NULL, 2, &threfreshCurrentTime, 0);
   xTaskCreate(TaskrefreshCurrentTime, "refreshCurrentTime", 8192, NULL, 2, &threfreshCurrentTime);

  //xTaskCreatePinnedToCore(TaskEndofBootup, "EndofBootup", 4096, NULL, 1, &thEndofBootup, 0);
   xTaskCreate(TaskEndofBootup, "EndofBootup", 4096, NULL, 1, &thEndofBootup);


// =============================================================================================================================
// ----------------------------------- CORE 1: Real-time Control & Native Networking -------------------------------------------
// ------------Core 1 is deliberately left mostly empty to service LwIP, async_tcp, and w5500_tsk flawlessly.-------------------
// =============================================================================================================================
  
  //xTaskCreatePinnedToCore(TaskSetupNetwork, "SetupNetwork", 8192, NULL, 5, &thSetupNetwork, 1);
   xTaskCreate(TaskSetupNetwork, "SetupNetwork", 8192, NULL, 5, &thSetupNetwork);

  //xTaskCreatePinnedToCore(TaskPumpControl, "PumpControl", 4096, NULL, 4, &thPumpControl, 1);
   xTaskCreate(TaskPumpControl, "PumpControl", 4096, NULL, 4, &thPumpControl);

  //xTaskCreatePinnedToCore(TasksetupPumpBroadcasting, "setupPumpBroadcasting", 4096, NULL, 2, &thsetupPumpBroadcasting, 1);
   xTaskCreate(TasksetupPumpBroadcasting, "setupPumpBroadcasting", 4096, NULL, 2, &thsetupPumpBroadcasting);

  //xTaskCreatePinnedToCore(TaskcheckTimeAndAct, "checkTimeAndAct", 4096, NULL, 2, &thcheckTimeAndAct, 1);
   xTaskCreate(TaskcheckTimeAndAct, "checkTimeAndAct", 4096, NULL, 2, &thcheckTimeAndAct);

  //xTaskCreatePinnedToCore(TasklogZeroLengthMessages, "logZeroLengthMessages", 2048, NULL, 1, &thlogZeroLengthMessages, 1);
   xTaskCreate(TasklogZeroLengthMessages, "logZeroLengthMessages", 2048, NULL, 1, &thlogZeroLengthMessages);

  //xTaskCreatePinnedToCore(TaskStartServer, "StartServer", 4096, NULL, 1, &thStartServer, 1);
   xTaskCreate(TaskStartServer, "StartServer", 4096, NULL, 1, &thStartServer);

  //xTaskCreate(TaskmonitorStacks, "monitorStacks", 4096, NULL, 1, &thmonitorStacks); // Displays memory usage
  //xTaskCreate(TaskPrintCpuStats, "CPUSTATS", 2048, nullptr, tskIDLE_PRIORITY+1, &thPrintCpuStats); // CPU usage

  AlarmHistory_begin();
}

  
  
  
