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



QueueHandle_t logQueue = nullptr;

// Define the mutex handles
SemaphoreHandle_t pumpStateMutex = NULL;
SemaphoreHandle_t temperatureMutex = NULL;
//SemaphoreHandle_t fileSystemMutex = NULL;

// Define flag variables
bool flagZeroLengthTime = false;
bool flagZeroLengthPumpState = false;
bool flagZeroLengthTemperatures = false;

// Declare the flag as extern
extern volatile bool needToUpdatePumpRuntimes;

// TGZ producer task handle is defined in ThirdWebpage.cpp (NULL when idle)
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
TaskHandle_t thTgzProducer = NULL;
TaskHandle_t thEndofBootup = NULL;

// tgzProducer last-run stack stats (WORDS)
volatile uint32_t tgzLastStackWords = 0;
volatile uint32_t tgzLastHwmWords   = 0;

// Function to monitor stack usage of tasks
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
    {"TaskTemperatureLogging",     thTemperatureLogging,         4096},
    {"FileSystemCleanup",        thFileSystemCleanup,            4096},
    {"tgzProducer", thTgzProducer, (uint32_t)TGZ_PRODUCER_TASK_STACK_BYTES},
    {"EndofBootup",              thEndofBootup,                  2048}
    
  };





  const int numTasks = (int)(sizeof(tasks) / sizeof(tasks[0]));

  for (int i = 0; i < numTasks; i++) {
    TaskHandle_t h = tasks[i].taskHandle;

    if (h == NULL) {

  // Special-case tgzProducer: print last-run stats even when the on-demand task isn't alive
  if (strcmp(tasks[i].taskName, "tgzProducer") == 0 &&
      tgzLastStackWords > 0 && tgzLastHwmWords > 0) {

    uint32_t stackWords = tgzLastStackWords;
    uint32_t hwmWords   = tgzLastHwmWords;

    uint32_t usedWords = (stackWords > hwmWords) ? (stackWords - hwmWords) : 0;

    uint32_t stackBytes = stackWords * W;
    uint32_t usedBytes  = usedWords  * W;
    uint32_t hwmBytes   = hwmWords   * W;

    float pct = (stackWords > 0) ? (100.0f * (float)usedWords / (float)stackWords) : 0.0f;

    Serial.printf("Task %-22s: not running (last run: used %u / %u bytes (%.1f%%), free(min) %u bytes)\n",
                  tasks[i].taskName, usedBytes, stackBytes, pct, hwmBytes);
  } else {
    Serial.printf("Task %-22s: not running / no handle\n", tasks[i].taskName);
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

    Serial.printf("Task %-22s: used %u / %u bytes (%.1f%%), free(min) %u bytes\n",
                  tasks[i].taskName, usedBytes, stackBytes, pct, hwmBytes);
  }

  Serial.println();
  Serial.println();
}




//used by targz for streaming directory downloads .tar
BaseType_t spawnTaskOptionalCore(
  TaskFunction_t fn,
  const char* name,
  uint32_t stackBytes,
  void* arg,
  UBaseType_t priority,
  TaskHandle_t* outHandle,
  int core
) {
  if (core >= 0) {
    return xTaskCreatePinnedToCore(fn, name, stackBytes, arg, priority, outHandle, core);
  }
  // Not pinned (no affinity)
  return xTaskCreate(fn, name, stackBytes, arg, priority, outHandle);
}






void logZeroLengthMessages() {
if (flagZeroLengthTime) {
Serial.println("[Error] Time WebSocket attempted to send a zero-length message.");
flagZeroLengthTime = false;
}
if (flagZeroLengthPumpState) {
Serial.println("[Error] Pump State WebSocket attempted to send a zero-length message.");
flagZeroLengthPumpState = false;
}
if (flagZeroLengthTemperatures) {
Serial.println("[Error] Temperatures WebSocket attempted to send a zero-length message.");
flagZeroLengthTemperatures = false;
}}




void TaskSetupRTC(void *pvParameters) {
setupRTC();
xTaskNotifyGive(thSetupNetwork); 
vTaskSuspend(NULL);
}


void TaskSetupNetwork(void *pvParameters) {
  esp_task_wdt_add(NULL);
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  esp_task_wdt_reset();
  setupNetwork();
  esp_task_wdt_reset();
  xTaskNotifyGive(thInitFileSystem);
  esp_task_wdt_delete(NULL);
  vTaskSuspend(NULL);
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
vTaskSuspend(NULL);
}




void TaskInitSystemConfigDefaults(void *pvParameters) {
ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
initSystemConfigDefaults();
 xTaskNotifyGive(thinitTimeConfigDefaults);
vTaskSuspend(NULL);
}




void TaskInitTimeConfigDefaults(void *pvParameters) {
ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
initTimeConfigDefaults();
if (!loadTimeConfigFromFS()) {
saveTimeConfigToFS();
}
xTaskNotifyGive(thInitNTP);
vTaskSuspend(NULL);
}




void TaskInitNTP(void *pvParameters) {
  esp_task_wdt_add(NULL);
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  esp_task_wdt_reset();
  esp_task_wdt_delete(NULL);
  initNTP();    
  xTaskNotifyGive(thloadSystemConfigFromFS);
  vTaskSuspend(NULL);
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
        Serial.printf("[Config] loadSystemConfigFromFS() attempt %d/%d failed, retrying...\n",
                      attempt, maxAttempts);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    if (!ok) {
        Serial.println("[Config] Giving up loading system_config.json, using defaults.");
    }

    xTaskNotifyGive(thInitPumps);
    vTaskSuspend(NULL);
}




void TaskInitPumps(void *pvParameters) {
ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
initializePumps();
xTaskNotifyGive(thStartServer);
vTaskSuspend(NULL);
}




void TaskUpdateTemperatures(void *pvParameters) {
    esp_task_wdt_add(NULL);
    for (;;) {
        updateTemperatures();
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(2000));
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
    vTaskDelayUntil(&xLastWake, pdMS_TO_TICKS(1000));
    setupPumpBroadcasting();
    esp_task_wdt_reset();
  }
}




void TaskStartServer(void *pvParameters) {
ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
while (!isNetworkConnected()) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    startServer();
    xTaskNotifyGive(thSetupFirstPage);
    vTaskSuspend(NULL);
}




void TaskSetupFirstPage(void *pvParameters) {
ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
setupFirstPageRoutes();
xTaskNotifyGive(thSetupSecondPage);
vTaskSuspend(NULL);
}




void TaskSetupSecondPage(void *pvParameters) {
ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
setupSecondPageRoutes();
xTaskNotifyGive(thSetupThirdPage);
vTaskSuspend(NULL);
}




void TaskSetupThirdPage(void *pvParameters) {
ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
setupThirdPageRoutes();
xTaskNotifyGive(thSetupLogDataRoute);
vTaskSuspend(NULL);
}




void TaskrefreshCurrentTime(void *pv) {
  esp_task_wdt_add(NULL);
  for (;;) {
    refreshCurrentTime();
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}




void TaskSetupLogDataRoute(void *pvParameters) {
ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
setupLogDataRoute();
xTaskNotifyGive(threfreshCurrentTime);
vTaskSuspend(NULL);
}




void TaskcheckTimeAndAct(void *pvParameters) {
  esp_task_wdt_init;
TickType_t xLastWakeTime = xTaskGetTickCount();
for (;;) {
vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(1000));
checkTimeAndAct(); 
}}




void TaskcheckAndSyncTime(void *pvParameters) {
  esp_task_wdt_init;
TickType_t xLastWakeTime = xTaskGetTickCount(); 
for (;;) {
vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(1000));
checkAndSyncTime();
}}




void TaskSerialPrint(void *pvParameters) {
  esp_task_wdt_init;
for (;;) {
SerialPrint();
vTaskDelay(pdMS_TO_TICKS(5000));
}}




void TaskbroadcastTemperatures(void *pvParameters) {
  for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_task_wdt_add(NULL);
        esp_task_wdt_reset();
        broadcastTemperatures();
        esp_task_wdt_reset();
        esp_task_wdt_delete(NULL);
  }
}




void TaskTemperatureLogging(void *pvParameters) {
    setupTemperatureLogging();
    esp_task_wdt_add(NULL);
    for (;;)
    {
        TaskTemperatureLogging_Run(pvParameters);
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}




void TaskmonitorStacks(void *pvParameters) {
    esp_task_wdt_init;
    for (;;) {
      monitorStacks();
      vTaskDelay(pdMS_TO_TICKS(5000));
    }
}




void TasklogZeroLengthMessages(void *pvParameters) {
    esp_task_wdt_init;
    for (;;) {
      logZeroLengthMessages();
      vTaskDelay(pdMS_TO_TICKS(10000));
    }
}



void TaskLogger(void* pv) {
  LogEvent ev;
    for (;;) {
      if (xQueueReceive(logQueue, &ev, portMAX_DELAY)) {
          // 1) Make sure the FS is mounted
          if (!g_fileSystemReady) {
              Serial.println("[TaskLogger] FS not ready; dropping log event");
              continue;
          }
          // 2) Try to lock the filesystem mutex with retries
          if (!takeFileSystemMutexWithRetry("[TaskLogger]",
                                            pdMS_TO_TICKS(2000),
                                            3)) {
              Serial.println("[TaskLogger] Failed to lock FS mutex; dropping log event");
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
              Serial.printf("[TaskLogger] Failed to open '%s' for append\n",
                            fn.c_str());
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
Serial.printf("Total Heap: %u, Free Heap: %u\n", ESP.getHeapSize(), ESP.getFreeHeap());
Serial.printf("Total PSRAM: %u, Free PSRAM: %u\n", ESP.getPsramSize(), ESP.getFreePsram());
vTaskSuspend(NULL);
}

void TaskPrintCpuStats(void*) {
  static char buf[1024];
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(5000));
    memset(buf, 0, sizeof(buf));
    vTaskGetRunTimeStats(buf);
    Serial.println(F("------ CPU RUN-TIME STATS ------"));
    Serial.println(buf);
  }
}


void TaskFileSystemCleanup(void *pvParameters) {
  const TickType_t bootDelayTicks = pdMS_TO_TICKS(65000); 
    vTaskDelay(bootDelayTicks);
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        
        if (!g_fileSystemReady) {
          Serial.println("[FS.CleanupTask] Skipping — g_fileSystemReady == false");
          continue;
          }
          
        Serial.println("[FS.CleanupTask] Notification received — running enforceTemperatureLogDiskLimit()");
        //migratePumpLogsToNewFolder();
        enforceTemperatureLogDiskLimit();
        Serial.println("[FS.CleanupTask] Done enforceTemperatureLogDiskLimit()");
    }
}









void startAllTasks() {

AlarmManager_begin();

logQueue = xQueueCreate( 20, sizeof(LogEvent));

xTaskCreate(TaskLogger, "TaskLogger", 4096, NULL, 1, &thTaskLogger);


xTaskCreate(TaskSetupRTC, "SetupRTC", 4096, NULL, 1, &thSetupRTC);

xTaskCreate(TaskSetupNetwork, "SetupNetwork", 8192, NULL, 1, &thSetupNetwork);

xTaskCreate(TaskInitSystemConfigDefaults, "initSystemConfigDefaults", 2048, NULL, 1, &thinitSystemConfigDefaults);

xTaskCreate(TaskInitFileSystem, "InitFileSystem", 4096, NULL, 1, &thInitFileSystem);

xTaskCreate(TaskInitTimeConfigDefaults, "initTimeConfigDefaults", 4096, NULL, 1, &thinitTimeConfigDefaults);

xTaskCreate(TaskLoadSystemConfigFromFS, "loadSystemConfigFromFS", 8192, NULL, 1, &thloadSystemConfigFromFS);

xTaskCreate(TaskInitNTP, "InitNTP", 4096, NULL, 1, &thInitNTP);

xTaskCreate(TaskInitPumps, "InitPumps", 2048, NULL, 1, &thInitPumps);

xTaskCreate(TaskUpdateTemperatures, "UpdateTemperature", 4096, NULL, 5, &thUpdateTemperatures);

xTaskCreate(TaskPumpControl, "PumpControl", 4096, NULL, 5, &thPumpControl);

xTaskCreate(TasksetupPumpBroadcasting, "setupPumpBroadcasting", 4096, NULL, 2, &thsetupPumpBroadcasting);

xTaskCreate(TaskStartServer, "StartServer", 4096, NULL, 1, &thStartServer);

xTaskCreate(TaskSetupFirstPage, "SetupFirstPage", 2048, NULL, 1, &thSetupFirstPage);

xTaskCreate(TaskSetupSecondPage, "SetupSecondPage", 4096, NULL, 1, &thSetupSecondPage);

xTaskCreate(TaskSetupThirdPage, "SetupThirdPage", 4096, NULL, 1, &thSetupThirdPage);

xTaskCreate(TaskrefreshCurrentTime, "refreshCurrentTime", 8192, NULL, 2, &threfreshCurrentTime);

xTaskCreate(TaskSetupLogDataRoute, "SetupLogDataRoute", 2048, NULL, 1, &thSetupLogDataRoute);

xTaskCreate(TaskcheckTimeAndAct, "checkTimeAndAct", 4096, NULL, 2, &thcheckTimeAndAct);

xTaskCreate(TaskcheckAndSyncTime, "checkAndSyncTime", 4096, NULL, 2, &thcheckAndSyncTime);

xTaskCreate(TaskSerialPrint, "SerialPrint", 2048, NULL, 1, &thSerialPrint);

xTaskCreate(TaskbroadcastTemperatures, "broadcastTemperatures", 4096, NULL, 4, &thbroadcastTemperatures);

xTaskCreate(TasklogZeroLengthMessages, "logZeroLengthMessages", 2048, NULL, 1, &thlogZeroLengthMessages);

xTaskCreate(TaskUpdatePumpRuntimes, "UpdatePumpRuntimes", 8192, NULL, 1, &thUpdatePumpRuntimes);

xTaskCreate(TaskTemperatureLogging, "TaskTemperatureLogging", 4096, NULL, 1, &thTemperatureLogging);

//xTaskCreate(TaskmonitorStacks, "monitorStacks", 4096, NULL, 1, &thmonitorStacks); // Displays memory usage

//xTaskCreate(TaskPrintCpuStats, "CPUSTATS", 2048, nullptr, tskIDLE_PRIORITY+1, &thPrintCpuStats); // Displaying CPU usage

xTaskCreate(TaskFileSystemCleanup, "FileSystemCleanup", 4096, NULL, 1, &thFileSystemCleanup);

xTaskCreate(TaskEndofBootup, "EndofBootup", 2048, NULL, 1, &thEndofBootup);

AlarmHistory_begin();


} 


