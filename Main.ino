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
Serial.begin(115200);

esp_task_wdt_deinit();
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = 15000,
        .idle_core_mask = 0,
        .trigger_panic = true
    };
    esp_task_wdt_init(&wdt_config);
  
  initPT1000Sensor();   // Initialize the Max31865 sensor
  initDS18B20Sensors();   // Initialize DS18B20 sensors
 
    // Create mutexes
    pumpStateMutex = xSemaphoreCreateMutex();
    if (pumpStateMutex == NULL) {
        Serial.println("Failed to create pumpStateMutex");
        while (1); // Halt execution
    }

    temperatureMutex = xSemaphoreCreateMutex();
    if (temperatureMutex == NULL) {
        Serial.println("Failed to create temperatureMutex");
        while (1); // Halt execution
    }

    fileSystemMutex = xSemaphoreCreateMutex();
    if (fileSystemMutex == NULL) {
        Serial.println("Failed to create fileSystemMutex");
        while (1); // Halt execution
    }

    startAllTasks();  // Starts all tasks defined in TaskManager.cpp

   
}

void loop() {
    delay(1);
 }
