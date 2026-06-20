#ifndef TASKMANAGER_H
#define TASKMANAGER_H
#include "Config.h"
#include <freertos/task.h>

// -----------------------------------------------------------------------
// Mutex handles as extern to be accessible in other files - do not change
// -----------------------------------------------------------------------
extern SemaphoreHandle_t pumpStateMutex;
extern SemaphoreHandle_t temperatureMutex;
extern SemaphoreHandle_t fileSystemMutex;

// Task function declarations
void TaskRTC(void *pvParameters);
void TaskNetwork(void *pvParameters);
void TaskTimeSync(void *pvParameters);
void TaskFileSystem(void *pvParameters);
void TaskPumps(void *pvParameters);
void TaskServer(void *pvParameters);
void TaskFirstPage(void *pvParameters);
void TaskSecondPage(void *pvParameters);
void TaskLogDataRoute(void *pvParameters);
void TaskUpdateTemperatures(void *pvParameters);
void TaskSerialPrint(void *pvParameters);
void TaskFileSystemCleanup(void *pvParameters);
void startAllTasks();

// Declare flag variables
extern bool flagZeroLengthTime;
  extern bool flagZeroLengthPumpState;
  extern bool flagZeroLengthTemperatures;
  extern TaskHandle_t thFileSystemCleanup;
extern TaskHandle_t thEndofBootup;

// Raw TAR archive producer last-run stack stats (defined in RawTar.cpp)
extern volatile uint32_t rawTarLastStackWords;
extern volatile uint32_t rawTarLastHwmWords;
extern TaskHandle_t thRawTarProducer;
extern QueueHandle_t logQueue;

#endif


