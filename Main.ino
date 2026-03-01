// Main.ino
#include <Arduino.h>
#include <FreeRTOS.h>
#include <Ticker.h>
#include "Config.h"
#include "Logging.h"
#include "NetworkManager.h"
#include "PumpManager.h"
#include "RTCManager.h"
#include "TemperatureControl.h"
#include "TimeSync.h"
//#include "WebServerManager.h"
#include "FileSystemManager.h"
#include "FirstWebpage.h"
#include "SecondWebpage.h"
#include "TaskManager.h"
#include <esp_task_wdt.h>
#include "Max31865-PT1000.h"
#include "DS18B20.h"
#include "TemperatureLogging.h"
#include "DiagLog.h"
#include <esp_system.h>



#define configGENERATE_RUN_TIME_STATS        1
#define configUSE_STATS_FORMATTING_FUNCTIONS 1
#include "esp_timer.h"
#define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS()  /* nothing */
#define portGET_RUN_TIME_COUNTER_VALUE()         ( (uint32_t) esp_timer_get_time() )

struct TempEntry {
    char timestamp[20];   // "YYYY-MM-DD,HH:MM:SS"
    float value;
};








void setup() { 
#if ENABLE_SERIAL_DIAGNOSTICS
  Serial.begin(115200);

  // Early boot defaults (may be overridden later by config loads)
  g_config.diagSerialEnable = (DIAG_SERIAL_DEFAULT_ENABLE != 0);
  g_config.diagSerialMask   = (uint32_t)DIAG_SERIAL_DEFAULT_MASK;

  // Crash detector: if last reset looks like a crash/WDT/panic, force ALL categories for this boot only
  esp_reset_reason_t rr = esp_reset_reason();
  bool crashy =
      (rr == ESP_RST_PANIC)     ||
      (rr == ESP_RST_INT_WDT)   ||
      (rr == ESP_RST_TASK_WDT)  ||
      (rr == ESP_RST_WDT);

  if (crashy) {
    g_config.diagSerialEnable = true;
    g_config.diagSerialMask   = DBG_ALL;

    Serial.println();
    Serial.printf("[BOOT] Crashy reset detected (reason=%d). Forcing DBG_ALL for THIS BOOT ONLY.\n", (int)rr);
  } else {
    Serial.println();
    Serial.println("[BOOT] Serial enabled; using Config.h diag defaults until FS config loads.");
  }
#endif

  esp_task_wdt_deinit();
  esp_task_wdt_config_t wdt_config = {


      .timeout_ms = 15000,
      .idle_core_mask = 0,
      .trigger_panic = true
  };
  esp_task_wdt_init(&wdt_config);

  initPT1000Sensor();     // Initialize the Max31865 sensor
  initDS18B20Sensors();   // Initialize DS18B20 sensors

  // Create mutexes
  pumpStateMutex = xSemaphoreCreateMutex();
  if (pumpStateMutex == NULL) {
    LOG_ERR("[BOOT] Failed to create pumpStateMutex\n");
    while (1) { vTaskDelay(pdMS_TO_TICKS(1000)); } // Halt execution
  }

  temperatureMutex = xSemaphoreCreateMutex();
  if (temperatureMutex == NULL) {
    LOG_ERR("[BOOT] Failed to create temperatureMutex\n");
    while (1) { vTaskDelay(pdMS_TO_TICKS(1000)); } // Halt execution
  }

  fileSystemMutex = xSemaphoreCreateMutex();
  if (fileSystemMutex == NULL) {
    LOG_ERR("[BOOT] Failed to create fileSystemMutex\n");
    while (1) { vTaskDelay(pdMS_TO_TICKS(1000)); } // Halt execution
  }

  LOG_CAT(DBG_TASK, "[BOOT] Mutexes created; starting all tasks...\n");

  startAllTasks();  // Starts all tasks defined in TaskManager.cpp
}

void loop() {
  delay(1);
}
