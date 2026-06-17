// Logging.h
#ifndef LOGGING_H
#define LOGGING_H

#include "Config.h"
#include <RTClib.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

// Pump START/STOP event used by the RAM-backed pump log buffer.
struct LogEvent {
  uint8_t  pumpIndex;   // zero-based pump index: 0..9
  bool     isStart;     // true=START, false=STOP
  DateTime ts;          // timestamp of the event
};

extern QueueHandle_t logQueue;

// Pump log event capture.  This writes to RAM first; TaskLogger flushes to /Pump_Logs later.
void logPumpEvent(uint8_t pumpIndex, bool isStart, const DateTime &ts);

// Pump log RAM buffering / flush control.
bool bufferPumpLogEvent(const LogEvent& ev, TickType_t waitTicks = pdMS_TO_TICKS(10));
size_t getPendingPumpLogEventCount();
bool flushPendingPumpLogEvents(TickType_t mutexWaitTicks = pdMS_TO_TICKS(2000), uint8_t retries = 3);
void servicePumpLogBufferOnce(uint32_t flushIntervalMs = 600000UL, size_t flushCountThreshold = 64);

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

#endif // LOGGING_H
