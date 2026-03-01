#ifndef LOGGING_H
#define LOGGING_H
#include "Config.h"
void logPumpEvent(int pumpIndex, const String& event);
void listAllFiles();
void readAndPrintLogFile(const String& filename);
void aggregatePumptoDailyLogs(int pumpIndex);
void aggregateDailyToMonthlyLogs(int pumpIndex);
void aggregateMonthlyToYearlyLogs(int pumpIndex);
void performLogAggregation();
void checkTimeAndAct(); 
unsigned long extractRuntimeFromLogLine(String line);
unsigned long extractTimestamp(const String& line);
void logMessage(const String& message);
void logPumpEvent(uint8_t pumpIndex, bool isStart, const DateTime &ts);

// Logging.h
#pragma once
#include <RTClib.h>       // for DateTime
#include <freertos/queue.h>

// ────── Your one and only LogEvent definition ─────────
struct LogEvent {
  uint8_t  pumpIndex;   // 1–10
  bool     isStart;     // true=START, false=STOP
  DateTime ts;          // timestamp of the event
};
extern QueueHandle_t logQueue;

// This now just enqueues (no file I/O here)
void logPumpEvent(uint8_t pumpIndex, bool isStart, const DateTime &ts);





#endif // LOGGING_H
