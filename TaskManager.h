#ifndef TASKMANAGER_H
#define TASKMANAGER_H
#include "Config.h"
#include <freertos/task.h>

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
extern TaskHandle_t thTgzProducer;

// tgzProducer last-run stack stats (captured when the on-demand task exits)
extern volatile uint32_t tgzLastStackWords;
extern volatile uint32_t tgzLastHwmWords;


// Spawns a task either pinned (core 0/1) or not pinned (core < 0).
BaseType_t spawnTaskOptionalCore(
  TaskFunction_t fn,
  const char* name,
  uint32_t stackBytes,
  void* arg,
  UBaseType_t priority,
  TaskHandle_t* outHandle,
  int core
);

#endif
