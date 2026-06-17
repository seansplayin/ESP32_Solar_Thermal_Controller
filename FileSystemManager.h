#ifndef FILE_SYSTEM_MANAGER_H
#define FILE_SYSTEM_MANAGER_H

#include <Arduino.h>
#include <LittleFS.h>
#include <RTClib.h>
#include <FreeRTOS.h>
#include <semphr.h>

// ================================
// ✅ GLOBAL FILESYSTEM MUTEX
// ================================
extern SemaphoreHandle_t fileSystemMutex;

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
String getFreeHeapString();
String getFSStatsString();

// ================================
// TEMPERATURE LOG MAINTENANCE
// ================================

// ✅ DEFAULT ARGUMENT MUST LIVE *ONLY* IN THE HEADER
bool deleteTemperatureLogsRecursive(const char* basePath = "/Temperature_Logs");

// ✅ AUTO CLEANUP WHEN DISK FULL
void enforceTemperatureLogDiskLimit();



extern bool g_fileSystemReady;


bool mountLittleFS();


bool takeFileSystemMutexWithRetry(const char *tag,
                                  TickType_t perAttemptTicks,
                                  int maxAttempts);


#endif // FILE_SYSTEM_MANAGER_H
