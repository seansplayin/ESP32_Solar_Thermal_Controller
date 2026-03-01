#ifndef FILE_SYSTEM_MANAGER_H
#define FILE_SYSTEM_MANAGER_H

#include <Arduino.h>
#include <LittleFS.h>
#include <RTClib.h>
#include <FreeRTOS.h>
#include <semphr.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ================================
// ✅ Log File FILESYSTEM MUTEX
// ================================
File openLogFileUnlocked(const String& filename, const char* mode);
File openLogFileLocked(const String& filename, const char* mode);
void closeLogFileLocked(File& f);


// ================================
// ✅ GLOBAL FILESYSTEM MUTEX
// ================================
extern SemaphoreHandle_t fileSystemMutex;
extern bool g_fileSystemReady;

// ================================
// FILESYSTEM CORE CONTROL
// ================================
void initializeFileSystem();
void LittleFSformat();

// ================================
// FILE ACCESS
// ================================
File openLogFile(const String& filename, const char* mode);

// ================================
// PUMP LOGGING
// ================================
void closeAllOpenPumpLogs();

// ================================
// MEMORY / DISK STATS
// ================================
String getFSStatsString();

// ================================
// TEMPERATURE LOG MAINTENANCE
// ================================

// ✅ DEFAULT ARGUMENT MUST LIVE *ONLY* IN THE HEADER
bool deleteTemperatureLogsRecursive(const char* basePath = "/Temperature_Logs");

// ✅ AUTO CLEANUP WHEN DISK FULL
void enforceTemperatureLogDiskLimit();

bool mountLittleFS();


bool takeFileSystemMutexWithRetry(const char *tag,
                                  TickType_t perAttemptTicks,
                                  int maxAttempts);



#endif // FILE_SYSTEM_MANAGER_H
