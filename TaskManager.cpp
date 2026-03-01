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
#include "TarGZ.h"
#include "DiagLog.h"



QueueHandle_t logQueue = nullptr;

// Define the mutex handles
SemaphoreHandle_t pumpStateMutex = NULL;
SemaphoreHandle_t temperatureMutex = NULL;
//SemaphoreHandle_t fileSystemMutex = NULL;

// Define flag variables
bool flagZeroLengthTime = false;
bool flagZeroLengthPumpState = false;
bool flagZeroLengthTemperatures = false;

// Temperature broadcast task telemetry (read by WebServerManager stats broadcaster)
volatile uint32_t g_tempBcastCalled  = 0;  // task chose to call broadcastTemperatures()
volatile uint32_t g_tempBcastSkipped = 0;  // task skipped because no writable WS clients


// Declare the flag as extern
extern volatile bool needToUpdatePumpRuntimes;

// TGZ producer task handle is defined in TarGZ.cpp (NULL when idle)
extern TaskHandle_t thTgzProducer;


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
TaskHandle_t thSystemStatsBroadcaster = NULL;
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
    {"SystemStatsBroadcaster",   thSystemStatsBroadcaster,       4096},
    {"SetupFirstPage",           thSetupFirstPage,               2048},
    {"SetupSecondPage",          thSetupSecondPage,              4096},
    {"SetupThirdPage",           thSetupThirdPage,               4096},
    {"SetupLogDataRoute",        thSetupLogDataRoute,            2048},
    {"refreshCurrentTime",       threfreshCurrentTime,           8192},
    {"checkTimeAndAct",          thcheckTimeAndAct,              4096},
    {"checkAndSyncTime",         thcheckAndSyncTime,             4096},
    {"SerialPrint",              thSerialPrint,                  2048},
    {"broadcastTemperatures",    thbroadcastTemperatures,        4096},
    {"logZeroLengthMessages",    thlogZeroLengthMessages,        2048},
    {"UpdatePumpRuntimes",       thUpdatePumpRuntimes,           8192},
    {"TaskTemperatureLogging",   thTemperatureLogging,           4096},
    {"FileSystemCleanup",        thFileSystemCleanup,            4096},
    {"tgzProducer",              thTgzProducer, (uint32_t)TGZ_PRODUCER_TASK_STACK_WORDS},
    {"EndofBootup",              thEndofBootup,                  4096}
  };

  // Monitor Stacks output
  const int numTasks = (int)(sizeof(tasks) / sizeof(tasks[0]));

  for (int i = 0; i < numTasks; i++) {
    TaskHandle_t h = tasks[i].taskHandle;

    if (h == NULL) {

      // prints the last-run stack usage using values you captured right before the producer task deleted itself:
      if (strcmp(tasks[i].taskName, "tgzProducer") == 0 &&
          tgzLastStackWords > 0 && tgzLastHwmWords > 0) {

        uint32_t stackWords = tgzLastStackWords;
        uint32_t hwmWords   = tgzLastHwmWords;

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
  setupRTC();
  xTaskNotifyGive(thInitFileSystem);
  vTaskDelete(NULL);
}


void TaskInitFileSystem(void *pvParameters) {
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  initializeFileSystem();
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
  setupLogDataRoute();
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
  esp_task_wdt_add(NULL);
  for (;;) {
    updateTemperatures();
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}


void TaskPumpControl(void *pvParameters) {
  const TickType_t bootDelayTicks = pdMS_TO_TICKS(15000);
  vTaskDelay(bootDelayTicks);

  esp_task_wdt_add(NULL);
  TickType_t xLastWakeTime = xTaskGetTickCount();
  for (;;) {
    esp_task_wdt_reset();
    vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(1000));
    PumpControl();
    esp_task_wdt_reset();
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


void TaskcheckAndSyncTime(void *pvParameters) {
  esp_task_wdt_add(NULL);
  TickType_t xLastWakeTime = xTaskGetTickCount();
  for (;;) {
    vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(1000));
    esp_task_wdt_reset();
    checkAndSyncTime();
    esp_task_wdt_reset();
  }
}


void TaskbroadcastTemperatures(void *pvParameters) {
  esp_task_wdt_add(NULL);
  TickType_t xLastWakeTime = xTaskGetTickCount();
  for (;;) {
    vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(5000));
    esp_task_wdt_reset();

    // Only broadcast if at least one client is connected AND can accept data now
    ws.cleanupClients();
    bool anyWritableClient = false;
    for (auto& client : ws.getClients()) {
      if (client.status() != WS_CONNECTED) continue;
      if (client.queueIsFull()) continue;
      if (!client.canSend()) continue;
      anyWritableClient = true;
      break;
    }
    if (anyWritableClient) {
      g_tempBcastCalled++;
      broadcastTemperatures();
    } else {
      g_tempBcastSkipped++;
    }

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
  LogEvent ev;
  for (;;) {
    if (xQueueReceive(logQueue, &ev, portMAX_DELAY)) {

      // 1) Make sure the FS is mounted
      if (!g_fileSystemReady) {
        LOG_ERR("[TaskLogger] FS not ready; dropping log event\n");
        continue;
      }

      // 2) Try to lock the filesystem mutex with retries
      if (!takeFileSystemMutexWithRetry("[TaskLogger]",
                                        pdMS_TO_TICKS(2000),
                                        3)) {
        LOG_ERR("[TaskLogger] Failed to lock FS mutex; dropping log event\n");
        continue;
      }

      // 3) Create /Pump_Logs if missing
      if (!LittleFS.exists("/Pump_Logs")) {
        LittleFS.mkdir("/Pump_Logs");
      }

      // 4) Do the actual append
      String fn = "/Pump_Logs/pump" + String(ev.pumpIndex + 1) + "_Log.txt";
      File f = LittleFS.open(fn, FILE_APPEND);
      if (!f) {
        LOG_ERR("[TaskLogger] Failed to open '%s' for append\n", fn.c_str());
      } else {
        f.printf("%s %04d-%02d-%02d %02d:%02d:%02d\n",
                 ev.isStart ? "START" : "STOP",
                 ev.ts.year(),  ev.ts.month(), ev.ts.day(),
                 ev.ts.hour(),  ev.ts.minute(), ev.ts.second());
        f.close();
      }

      // 5) Always release the mutex
      xSemaphoreGive(fileSystemMutex);
    }
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
  enableTemperatureLogging(); // ensures temperature logging is last at boot
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









// ========================= TASKS (FULL SECTION) =========================
// This task section matches the boot-chain order used by startAllTasks().
// Boot chain (notify gated):
//   SetupRTC -> InitFileSystem -> InitSystemConfigDefaults -> InitTimeConfigDefaults
//   -> LoadSystemConfigFromFS(+diag) -> SetupNetwork -> InitNTP -> InitPumps -> StartServer
//   -> SetupFirstPage -> SetupSecondPage -> SetupThirdPage -> SetupLogDataRoute -> refreshCurrentTime
//
// WDT bugfix included in TaskSetupNetwork and TaskInitNTP:
//   - Do NOT esp_task_wdt_add() before ulTaskNotifyTake() blocking call.
// =======================================================================

void startAllTasks() {

  AlarmManager_begin();

  logQueue = xQueueCreate(20, sizeof(LogEvent));

  // ---------------- Background tasks (run regardless of boot chain) ----------------
  
  // PINNED TO CORE 1: Heavy File System writes
  xTaskCreatePinnedToCore(TaskLogger, "TaskLogger", 4096, NULL, 1, &thTaskLogger, 1);
  
  // PINNED TO CORE 1: DS18B20 OneWire protocol (Disables Interrupts!)
  xTaskCreatePinnedToCore(TaskUpdateTemperatures, "UpdateTemperature", 4096, NULL, 4, &thUpdateTemperatures, 1);
  
  xTaskCreate(TaskPumpControl, "PumpControl", 4096, NULL, 4, &thPumpControl);
  xTaskCreate(TasksetupPumpBroadcasting, "setupPumpBroadcasting", 4096, NULL, 2, &thsetupPumpBroadcasting);
  xTaskCreate(TaskcheckTimeAndAct, "checkTimeAndAct", 4096, NULL, 2, &thcheckTimeAndAct);
  xTaskCreate(TaskcheckAndSyncTime, "checkAndSyncTime", 4096, NULL, 2, &thcheckAndSyncTime);
  xTaskCreate(TaskbroadcastTemperatures, "broadcastTemperatures", 4096, NULL, 3, &thbroadcastTemperatures);
  xTaskCreate(TasklogZeroLengthMessages, "logZeroLengthMessages", 2048, NULL, 1, &thlogZeroLengthMessages);
  xTaskCreate(TaskUpdatePumpRuntimes, "UpdatePumpRuntimes", 8192, NULL, 1, &thUpdatePumpRuntimes);
  
  // PINNED TO CORE 1: Heavy File System writes
  xTaskCreatePinnedToCore(TaskTemperatureLogging, "TaskTemperatureLogging", 4096, NULL, 1, &thTemperatureLogging, 1);

  // ---------------- Boot chain tasks (all gated by notifications) ----------------
  xTaskCreate(TaskSetupRTC, "SetupRTC", 4096, NULL, 1, &thSetupRTC);
  
  // PINNED TO CORE 1: File System mounting
  xTaskCreatePinnedToCore(TaskInitFileSystem, "InitFileSystem", 4096, NULL, 1, &thInitFileSystem, 1);
  
  xTaskCreate(TaskInitSystemConfigDefaults, "initSystemConfigDefaults", 2048, NULL, 1, &thinitSystemConfigDefaults);
  xTaskCreate(TaskInitTimeConfigDefaults, "initTimeConfigDefaults", 4096, NULL, 1, &thinitTimeConfigDefaults);

  // Load configs BEFORE bringing up the network
  xTaskCreate(TaskLoadSystemConfigFromFS, "loadSystemConfigFromFS", 4096, NULL, 1, &thloadSystemConfigFromFS);

  // ---------------- NETWORK PROTOCOL TASKS ----------------
  // PINNED TO CORE 0: Network drivers, W5500 interrupts, Server
  xTaskCreatePinnedToCore(TaskSetupNetwork, "SetupNetwork", 4096, NULL, 5, &thSetupNetwork, 0);
  xTaskCreatePinnedToCore(TaskInitNTP, "InitNTP", 4096, NULL, 1, &thInitNTP, 0);
  xTaskCreatePinnedToCore(TaskStartServer, "StartServer", 4096, NULL, 1, &thStartServer, 0);

  xTaskCreate(TaskInitPumps, "InitPumps", 2048, NULL, 1, &thInitPumps);

  xTaskCreate(TaskSetupFirstPage, "SetupFirstPage", 2048, NULL, 1, &thSetupFirstPage);
  xTaskCreate(TaskSetupSecondPage, "SetupSecondPage", 4096, NULL, 1, &thSetupSecondPage);
  xTaskCreate(TaskSetupThirdPage, "SetupThirdPage", 4096, NULL, 1, &thSetupThirdPage);
  xTaskCreate(TaskSetupLogDataRoute, "SetupLogDataRoute", 2048, NULL, 1, &thSetupLogDataRoute);

  // Gate refreshCurrentTime behind the route setup (so time display starts after routes are ready)
  xTaskCreate(TaskrefreshCurrentTime, "refreshCurrentTime", 8192, NULL, 2, &threfreshCurrentTime);

  xTaskCreate(TaskWebSocketTransmitter, "WSTransmitter", 4096, NULL, 1, &thSystemStatsBroadcaster);

  //xTaskCreate(TaskmonitorStacks, "monitorStacks", 4096, NULL, 1, &thmonitorStacks); // Displays memory usage
  //xTaskCreate(TaskPrintCpuStats, "CPUSTATS", 2048, nullptr, tskIDLE_PRIORITY+1, &thPrintCpuStats); // CPU usage
  //xTaskCreate(TaskFileSystemCleanup, "FileSystemCleanup", 4096, NULL, 1, &thFileSystemCleanup);

  xTaskCreate(TaskEndofBootup, "EndofBootup", 4096, NULL, 1, &thEndofBootup);

  AlarmHistory_begin();
}
