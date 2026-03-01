#include "Logging.h"
#include "TimeSync.h"
#include "RTCManager.h"
#include "PumpManager.h"
#include "Config.h"
#include <map>
#include "Config.h"
#include <LittleFS.h>
#include <FS.h>
#include <RTClib.h>
#include <stdlib.h>
#include "FileSystemManager.h"
#include "TaskManager.h"
#include "DiagLog.h"
#include "MemoryStats.h"
#include <esp_heap_caps.h>
#include "FileSystemManager.h"
#include <LittleFS.h>

static void appendMemMarkCsv(const char* tag) {
  if (!tag) tag = "tag";
  if (!g_fileSystemReady) return;
  if (!fileSystemMutex) return;

  if (xSemaphoreTake(fileSystemMutex, pdMS_TO_TICKS(50)) != pdTRUE) return;

  File f = LittleFS.open("/mem_marks.csv", "a");
  if (!f) { xSemaphoreGive(fileSystemMutex); return; }

  const uint32_t ms = millis();

  const uint32_t iTotal = heap_caps_get_total_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  const uint32_t iFree  = heap_caps_get_free_size (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  const uint32_t iMin   = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  const uint32_t iLfb   = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

  const uint32_t pTotal = heap_caps_get_total_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  const uint32_t pFree  = heap_caps_get_free_size (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  const uint32_t pMin   = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  const uint32_t pLfb   = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

  // ms,tag,iFree,iTotal,iMin,iLfb,pFree,pTotal,pMin,pLfb
  f.printf("%lu,%s,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu\n",
           (unsigned long)ms, tag,
           (unsigned long)iFree, (unsigned long)iTotal, (unsigned long)iMin, (unsigned long)iLfb,
           (unsigned long)pFree, (unsigned long)pTotal, (unsigned long)pMin, (unsigned long)pLfb);

  f.close();
  xSemaphoreGive(fileSystemMutex);
}

static void memMark(const char* tag) {
  if (!tag) tag = "?";

  LOG_CAT(DBG_MEM, "\n=== MEM MARK: %s ===\n", tag);

  // Snapshot (free/total/min/lfb)
  MemoryStats_printSnapshot(tag);

  // EXACT UI wording (matches your webpage strings)
  LOG_CAT(DBG_MEM, "[%s] Heap (Internal): %s\n", tag, getHeapInternalString().c_str());
  LOG_CAT(DBG_MEM, "[%s] PSRAM:          %s\n", tag, getPsramString().c_str());

  // Optional: persist so you don’t need Serial connected overnight
  appendMemMarkCsv(tag);
}

extern DateTime CurrentTime;
extern RTC_DS3231 rtc;  // Use the external 'rtc' declaration
volatile bool Elapsed_Day = false;
extern void runHourlyHealthCheck();

static int lastHourRan = -1;

// ===== Periodic scheduling safety (RTC-based; no time(nullptr) dependency) =====
// Small execution window so you don't miss events if the tick isn't exact.
static const uint8_t kScheduleWindowSec = 2;

// "once" guards
static int g_lastHealthHourRan = -1;         // 0..23
static int g_lastFsCleanHourRan = -1;        // 0..23
static int g_lastElapsedDayY = -1, g_lastElapsedDayM = -1, g_lastElapsedDayD = -1;
static int g_lastAggY = -1, g_lastAggM = -1, g_lastAggD = -1;

// job bitmask (executed by the checkTimeAndAct worker)
static volatile uint32_t g_periodicJobs = 0;

enum : uint32_t {
  JOB_NONE        = 0,
  JOB_FS_CLEAN    = (1u << 0),
  JOB_HEALTHCHECK = (1u << 1),
  JOB_AGGREGATE   = (1u << 2),
};

static inline bool rtcTimeLooksValid(const DateTime& t) {
  // Adjust if you want; your project already uses 2024-2099 gates elsewhere.
  return (t.year() >= 2024 && t.year() <= 2099);
}

static inline bool withinWindow(const DateTime& t) {
  return (t.second() <= kScheduleWindowSec);
}

static inline void scheduleJob(uint32_t jobBit) {
  g_periodicJobs |= jobBit;
}

static uint32_t claimJobs() {
  uint32_t jobs = g_periodicJobs;
  g_periodicJobs = 0;
  return jobs;
}

extern QueueHandle_t logQueue;

// 4 functions in this file that read/write to file system for Pump Logs:
// logPumpEvent, aggregatePumptoDailyLogs, aggregateDailyToMonthlyLogs, aggregateMonthlyToYearlyLogs

// Helper function to parse datetime strings
DateTime parseDateTime(String datetimeStr);
// Assuming parseDateTimeFromLog() and rtc.now() are properly defined elsewhere
DateTime parseDateTimeFromLog(const String& datetimeStr);

// New queue based Logging Topology for Pump Runtimes: checkTimeAndAct → setElapsed_Day / setperformLogAggregation → performLogAggregation.
// queue is written to LittleFS File System using task "TaskLogger" in TaskManager.cpp file.
// Log pump event (writer version — called from processLogQueue)
void logPumpEvent(uint8_t pumpIndex, bool isStart, const DateTime &ts) {
  if (!logQueue) return;
  LogEvent ev{ pumpIndex, isStart, ts };

  // Never block here; TaskLogger is the single consumer.
  (void)xQueueSend(logQueue, &ev, 0);
}

//********List files in the LittleFS********
void listAllFiles() {
  if (!takeFileSystemMutexWithRetry("[Logging] listAllFiles",
                                    pdMS_TO_TICKS(2000), 3)) {
    LOG_ERR("[Logging] Failed to lock FS mutex in listAllFiles\n");
    return;
  }

  File root = LittleFS.open("/");
  if (!root) {
    LOG_ERR("[Logging] Failed to open LittleFS root\n");
    xSemaphoreGive(fileSystemMutex);
    return;
  }

  File file = root.openNextFile();
  LOG_CAT(DBG_FS, "Files stored in LittleFS:\n");
  while (file) {
    LOG_CAT(DBG_FS, "%s\n", file.name());
    file = root.openNextFile();
  }

  xSemaphoreGive(fileSystemMutex);
}

//********Read Files in the LittleFS********
void readAndPrintLogFile(const String& filename) {
  String fullPath = "/Pump_Logs/" + filename; // CHANGE: Updated to new dir

  if (!takeFileSystemMutexWithRetry("[Logging] readAndPrintLogFile",
                                    pdMS_TO_TICKS(2000), 3)) {
    LOG_ERR("[Logging] Failed to lock FS mutex in readAndPrintLogFile\n");
    return;
  }

  File logFile = LittleFS.open(fullPath, "r");
  if (!logFile) {
    LOG_ERR("Failed to open %s for reading\n", fullPath.c_str());
    xSemaphoreGive(fileSystemMutex);
    return;
  }

  LOG_CAT(DBG_PUMPLOG, "Contents of %s:\n", fullPath.c_str());
  while (logFile.available()) {
    String line = logFile.readStringUntil('\n');
    LOG_CAT(DBG_PUMPLOG, "%s\n", line.c_str());
  }
  logFile.close();

  xSemaphoreGive(fileSystemMutex);
}

//********This section is for managing the logs********
unsigned long extractTimestamp(const String& line) {
  // Example implementation, extract and convert the timestamp from the line
  // Assume the timestamp is at the beginning of the line followed by a space
  int index = line.indexOf(' ');
  if (index != -1) {
    String timestampStr = line.substring(0, index);
    // Convert the extracted part of the line to an unsigned long
    // This is just an example; the actual conversion depends on your timestamp format
    return timestampStr.toInt();
  }
  return 0; // Return 0 or an appropriate error value if extraction fails
}

// Helper function to get current month as a string (e.g., "January")
String getCurrentMonthString() {
  DateTime now = rtc.now(); // Assuming you have an RTC object named rtc
  char monthName[12];
  snprintf(monthName, sizeof(monthName), "%04d-%02d", now.year(), now.month());
  return String(monthName);
}

unsigned long extractRuntimeFromLogLine(String line) {
  // Find the position of "Total Runtime: " in the line
  int start = line.indexOf("Total Runtime: ") + 15;
  if (start != -1) {
    // Extract the substring from this position to the end, excluding " seconds"
    int end = line.lastIndexOf(" seconds");
    if (end > start) {
      String runtimeStr = line.substring(start, end);
      return runtimeStr.toInt(); // Convert this substring to an unsigned long and return
    }
  }
  return 0; // If parsing fails, return 0
}

// Helper function to parse datetime string and return a DateTime object
DateTime parseDateTimeFromLog(const String& datetimeStr) {
  // Parses datetime string in "YYYY-MM-DD HH:MM:SS" format and returns a DateTime object
  int year = datetimeStr.substring(0, 4).toInt();
  int month = datetimeStr.substring(5, 7).toInt();
  int day = datetimeStr.substring(8, 10).toInt();
  int hour = datetimeStr.substring(11, 13).toInt();
  int minute = datetimeStr.substring(14, 16).toInt();
  int second = datetimeStr.substring(17, 19).toInt();
  return DateTime(year, month, day, hour, minute, second);
}

unsigned long calculateTotalRuntime(const String& logFilename) {
  if (!takeFileSystemMutexWithRetry("[Logging] calculateTotalRuntime",
                                    pdMS_TO_TICKS(2000), 3)) {
    LOG_ERR("[Logging] Failed to lock FS mutex in calculateTotalRuntime\n");
    return 0;
  }

  File logFile = LittleFS.open(logFilename, "r");
  if (!logFile) {
    LOG_ERR("Failed to open log file for reading: %s\n", logFilename.c_str());
    xSemaphoreGive(fileSystemMutex);
    return 0;
  }

  unsigned long totalRuntime = 0;
  DateTime lastStartTime;
  bool isPumpRunning = false;

  while (logFile.available()) {
    String line = logFile.readStringUntil('\n');
    // Check for START or STOP events and parse the datetime
    if (line.startsWith("START")) {
      String timestampStr = line.substring(6); // Adjust based on your log format
      lastStartTime = parseDateTimeFromLog(timestampStr);
      isPumpRunning = true;
    } else if (line.startsWith("STOP") && isPumpRunning) {
      String timestampStr = line.substring(5); // Adjust based on your log format
      DateTime stopTime = parseDateTimeFromLog(timestampStr);
      totalRuntime += (stopTime.unixtime() - lastStartTime.unixtime());
      isPumpRunning = false;
    }
  }
  logFile.close();

  xSemaphoreGive(fileSystemMutex);
  return totalRuntime;
}

// Aggregate pump to daily logs
void aggregatePumptoDailyLogs(int pumpIndex) {
  memMark("AGG_pump2daily_start");

  String logFilename      = "/Pump_Logs/pump" + String(pumpIndex + 1) + "_Log.txt";
  String dailyLogFilename = "/Pump_Logs/pump" + String(pumpIndex + 1) + "_Daily.txt";

  if (!takeFileSystemMutexWithRetry("[AGG] pump2daily", pdMS_TO_TICKS(2000), 3)) {
    LOG_CAT(DBG_PUMPLOG, "[AGG] pump2daily: FS busy, skipping this run\n");
    return;
  }

  // Ensure directory exists
  if (!LittleFS.exists("/Pump_Logs")) {
    LittleFS.mkdir("/Pump_Logs");
  }

  // Load existing daily runtimes
  std::map<String, unsigned long> dailyRuntimeMap;
  {
    File dailyLogFile = LittleFS.open(dailyLogFilename, "r");
    if (dailyLogFile) {
      while (dailyLogFile.available()) {
        String line = dailyLogFile.readStringUntil('\n');
        line.trim();

        int dateEnd = line.indexOf(" Total Runtime: ");
        if (dateEnd == -1) continue;

        String date = line.substring(0, dateEnd);

        int runtimeKey = line.indexOf("Total Runtime: ");
        if (runtimeKey == -1) continue;

        int runtimePos = runtimeKey + 15;
        int secondsPos = line.indexOf(" seconds", runtimePos);
        if (secondsPos == -1) continue;

        String runtimeStr = line.substring(runtimePos, secondsPos);
        unsigned long runtimeVal = strtoul(runtimeStr.c_str(), NULL, 10);
        dailyRuntimeMap[date] = runtimeVal;
      }
      dailyLogFile.close();
    }
  }

  File logFile = LittleFS.open(logFilename, "r");
  if (!logFile) {
    LOG_CAT(DBG_PUMPLOG, "No log file for pump %d\n", pumpIndex + 1);
    xSemaphoreGive(fileSystemMutex);
    memMark("AGG_pump2daily_end");
    return;
  }

  DateTime lastStartTime;
  bool isPumpRunning = false;

  // Accumulate runtime from START→STOP, attributing to STOP date
  while (logFile.available()) {
    String line = logFile.readStringUntil('\n');
    line.trim();

    if (line.startsWith("START ")) {
      lastStartTime = parseDateTimeFromLog(line.substring(6));
      isPumpRunning = true;
    } else if (line.startsWith("STOP ") && isPumpRunning) {
      DateTime stopTime = parseDateTimeFromLog(line.substring(5));
      unsigned long runTime = stopTime.unixtime() - lastStartTime.unixtime();
      String stopDate = line.substring(5, 15); // "YYYY-MM-DD"
      dailyRuntimeMap[stopDate] += runTime;
      isPumpRunning = false;
    }
  }
  logFile.close();

  // If still running at aggregation time, split across midnight
  if (isPumpRunning) {
    DateTime aggregationTime = CurrentTime;
    DateTime dayBoundary(aggregationTime.year(), aggregationTime.month(),
                         aggregationTime.day(), 0, 0, 0);

    DateTime yesterday = dayBoundary - TimeSpan(1, 0, 0, 0);
    char ybuf[11];
    snprintf(ybuf, sizeof(ybuf), "%04d-%02d-%02d",
             yesterday.year(), yesterday.month(), yesterday.day());
    String yesterdayISO = String(ybuf);

    unsigned long runTime = dayBoundary.unixtime() - lastStartTime.unixtime();
    dailyRuntimeMap[yesterdayISO] += runTime;

    File newLog = LittleFS.open(logFilename, "w");
    if (newLog) {
      char buffer[32];
      snprintf(buffer, sizeof(buffer),
               "START %04d-%02d-%02d %02d:%02d:%02d",
               dayBoundary.year(), dayBoundary.month(), dayBoundary.day(),
               dayBoundary.hour(), dayBoundary.minute(), dayBoundary.second());
      newLog.println(buffer);
      newLog.close();
    }
  } else {
    // If pump not running, clear the log file
    LittleFS.remove(logFilename);
  }

  // Rewrite daily log with updated runtimes
  {
    File dailyOut = LittleFS.open(dailyLogFilename, "w");
    if (dailyOut) {
      for (const auto& entry : dailyRuntimeMap) {
        dailyOut.printf("%s Total Runtime: %lu seconds\n",
                        entry.first.c_str(), entry.second);
      }
      dailyOut.close();
    }
  }

  LOG_CAT(DBG_PUMPLOG, "Aggregation complete for pump %d\n", pumpIndex + 1);

  xSemaphoreGive(fileSystemMutex);
  memMark("AGG_pump2daily_end");
}

unsigned long calculateTotalMonthlyRuntime(const String& dailyLogFilename) {
  File dailyLogFile = LittleFS.open(dailyLogFilename, "r");
  if (!dailyLogFile) {
    LOG_ERR("Failed to open daily log file for reading: %s\n", dailyLogFilename.c_str());
    return 0;
  }
  unsigned long totalMonthlyRuntime = 0;
  while (dailyLogFile.available()) {
    String line = dailyLogFile.readStringUntil('\n');
    // Assuming the line format is "YYYY-MM-DD Total Runtime: XXX seconds"
    int start = line.indexOf("Total Runtime: ") + 15;
    int end = line.lastIndexOf(" seconds");
    if (start != -1 && end != -1 && end > start) {
      String runtimeStr = line.substring(start, end);
      totalMonthlyRuntime += runtimeStr.toInt();
    }
  }
  dailyLogFile.close();
  return totalMonthlyRuntime;
}

void aggregateDailyToMonthlyLogs(int pumpIndex) {
  memMark("AGG_daily2monthly_start");
  String dailyLogFilename   = "/Pump_Logs/pump" + String(pumpIndex + 1) + "_Daily.txt";
  String monthlyLogFilename = "/Pump_Logs/pump" + String(pumpIndex + 1) + "_Monthly.txt";

  // 1) Sum daily logs into totalMonthlyRuntime
  unsigned long totalMonthlyRuntime = calculateTotalMonthlyRuntime(dailyLogFilename);

  // 2) Figure out which month to label: *last* month, not current
  DateTime now = rtc.now();
  int year  = now.year();
  int month = now.month() - 1;
  if (month < 1) {
    month = 12;
    year--;
  }

  // Build the "YYYY-MM" string for last month
  char prevMonthStr[8];
  snprintf(prevMonthStr, sizeof(prevMonthStr), "%04d-%02d", year, month);
  String previousMonth = String(prevMonthStr);

  // 3) Read existing monthly file for that pump
  bool monthExists = false;
  unsigned long existingRuntime = 0;
  String updatedContents;

  if (LittleFS.exists(monthlyLogFilename)) {
    File monthlyLogFile = LittleFS.open(monthlyLogFilename, "r");
    while (monthlyLogFile.available()) {
      String line = monthlyLogFile.readStringUntil('\n');
      if (line.startsWith(previousMonth)) {
        // parse existing line to get the old runtime
        int startPos = line.indexOf("Total Runtime: ") + 15;
        int endPos   = line.indexOf(" seconds", startPos);
        if (startPos > 0 && endPos > startPos) {
          existingRuntime = line.substring(startPos, endPos).toInt();
        }
        monthExists = true;
      } else {
        updatedContents += line + "\n"; // keep other months
      }
    }
    monthlyLogFile.close();
  }

  // 4) Combine old + new runtime
  if (monthExists) {
    totalMonthlyRuntime += existingRuntime;
  }

  // 5) Add or update the previousMonth line
  updatedContents += previousMonth + " Total Runtime: " + String(totalMonthlyRuntime) + " seconds\n";

  // 6) Write it back
  File monthlyLogFile = LittleFS.open(monthlyLogFilename, "w");
  if (monthlyLogFile) {
    monthlyLogFile.print(updatedContents);
    monthlyLogFile.close();
  } else {
    LOG_ERR("Failed to open %s for writing\n", monthlyLogFilename.c_str());
  }

  // 7) Clear daily file
  if (LittleFS.remove(dailyLogFilename)) {
    LOG_CAT(DBG_PUMPLOG, "Daily log file cleared.\n");
  } else {
    LOG_ERR("Failed to clear daily log file.\n");
    memMark("AGG_daily2monthly_end");
  }
}

unsigned long calculateTotalYearlyRuntime(const String& yearlyLogFilename) {
  File yearlyLogFile = LittleFS.open(yearlyLogFilename, "r");
  if (!yearlyLogFile) {
    LOG_ERR("Failed to open yearly log file for reading: %s\n", yearlyLogFilename.c_str());
    return 0;
  }
  unsigned long totalYearlyRuntime = 0;
  while (yearlyLogFile.available()) {
    String line = yearlyLogFile.readStringUntil('\n');
    // Use the existing `extractRuntimeFromLogLine` function
    totalYearlyRuntime += extractRuntimeFromLogLine(line);
  }
  yearlyLogFile.close();
  return totalYearlyRuntime;
}

void aggregateMonthlyToYearlyLogs(int pumpIndex) {
  memMark("AGG_monthly2yearly_start");

  String monthlyLogFilename = "/Pump_Logs/pump" + String(pumpIndex + 1) + "_Monthly.txt";
  String yearlyLogFilename  = "/Pump_Logs/pump" + String(pumpIndex + 1) + "_Yearly.txt";

  // Sum the monthly logs to get totalYearlyRuntime
  unsigned long totalYearlyRuntime = calculateTotalYearlyRuntime(monthlyLogFilename);

  // Decide whether we label the currentYear or (currentYear - 1) if month == 1
  DateTime now = rtc.now();
  int year = now.year();
  if (now.month() == 1) {
    year = year - 1;
  }

  String labelYear = String(year);

  bool yearExists = false;
  unsigned long existingRuntime = 0;
  String updatedContents;

  // Read existing lines from the yearly file
  File yearlyLogFile = LittleFS.open(yearlyLogFilename, "r");
  if (yearlyLogFile) {
    while (yearlyLogFile.available()) {
      String line = yearlyLogFile.readStringUntil('\n');
      if (line.startsWith(labelYear)) {
        int start = line.indexOf("Total Runtime: ") + 15;
        int end   = line.indexOf(" seconds", start);
        if (start > 0 && end > start) {
          existingRuntime = line.substring(start, end).toInt();
        }
        yearExists = true;
      } else {
        updatedContents += line + "\n";
      }
    }
    yearlyLogFile.close();
  }

  // Combine old + new runtime
  if (yearExists) {
    totalYearlyRuntime += existingRuntime;
  }

  // Add or update the line for labelYear
  updatedContents += labelYear + " Total Runtime: " + String(totalYearlyRuntime) + " seconds\n";

  // Overwrite the file
  yearlyLogFile = LittleFS.open(yearlyLogFilename, "w");
  if (!yearlyLogFile) {
    LOG_ERR("Failed to open yearly log file for writing.\n");
    return;
  }
  yearlyLogFile.print(updatedContents);
  yearlyLogFile.close();

  // Clear the monthly log file
  if (LittleFS.remove(monthlyLogFilename)) {
    LOG_CAT(DBG_PUMPLOG, "Cleared monthly log file.\n");
  } else {
    LOG_ERR("Failed to clear monthly log file.\n");
  }

  LOG_CAT(DBG_PUMPLOG, "Aggregated monthly logs to yearly log for pump %d\n", pumpIndex + 1);
  memMark("AGG_monthly2yearly_end");
}

void performLogAggregation() {
  // Aggregate daily logs for each pump
  memMark("AGG_start");
  for (int i = 0; i < 10; i++) {
    aggregatePumptoDailyLogs(i);
  }

  String currentDate = getCurrentDateStringMDY();

  // Extract month and day from the current date string
  int month = currentDate.substring(0, 2).toInt();
  int day   = currentDate.substring(3, 5).toInt();

  // Check if it's the first day of any month
  if (day == 1) {
    for (int i = 0; i < 10; i++) {
      aggregateDailyToMonthlyLogs(i);
    }

    // Additionally, check if it's the first day of the year (January 1st)
    if (month == 1) {
      for (int i = 0; i < 10; i++) {
        aggregateMonthlyToYearlyLogs(i);
        memMark("AGG_end");
      }
    }
  }
}

void setElapsed_Day() {
  // Only set once per calendar day (prevents spam if checkTimeAndAct hits this multiple times)
  if (!rtcTimeLooksValid(CurrentTime)) return;

  const int y = CurrentTime.year();
  const int m = CurrentTime.month();
  const int d = CurrentTime.day();

  const bool alreadySetToday =
    (g_lastElapsedDayY == y && g_lastElapsedDayM == m && g_lastElapsedDayD == d);

  if (alreadySetToday) return;

  Elapsed_Day = true;
  g_lastElapsedDayY = y; g_lastElapsedDayM = m; g_lastElapsedDayD = d;

  LOG_CAT(DBG_PUMPLOG, "Elapsed_Day flag set to true\n");
}

void setperformLogAggregation() {
  if (!Elapsed_Day) return;

  if (!rtcTimeLooksValid(CurrentTime)) {
    LOG_CAT(DBG_PUMPLOG, "[AGG] Skipping aggregation: RTC time not valid\n");
    return;
  }

  if (!g_fileSystemReady) {
    LOG_CAT(DBG_PUMPLOG, "[AGG] Skipping aggregation: file system not ready\n");
    return;
  }

  const int y = CurrentTime.year();
  const int m = CurrentTime.month();
  const int d = CurrentTime.day();

  const bool alreadyRanToday =
    (g_lastAggY == y && g_lastAggM == m && g_lastAggD == d);

  if (alreadyRanToday) {
    // If we somehow got here again, clear Elapsed_Day so we don't keep attempting.
    Elapsed_Day = false;
    LOG_CAT(DBG_PUMPLOG, "[AGG] Aggregation already ran today; Elapsed_Day cleared\n");
    return;
  }

  // Run it
  performLogAggregation();

  // Mark success + clear flag
  g_lastAggY = y; g_lastAggM = m; g_lastAggD = d;
  Elapsed_Day = false;

  LOG_CAT(DBG_PUMPLOG, "Log aggregation performed and Elapsed_Day flag reset\n");
}

void maybeRunHealthCheckHourly() {
  if (!rtcTimeLooksValid(CurrentTime)) return;

  // example: run on the hour within a small window
  if (CurrentTime.minute() == 0 && withinWindow(CurrentTime)) {
    int h = CurrentTime.hour();
    if (h != lastHourRan) {
      lastHourRan = h;
      runHourlyHealthCheck();
    }
  }
}

void checkTimeAndAct() {
  // If RTC isn't valid yet, do not run time-based jobs
  if (!rtcTimeLooksValid(CurrentTime)) return;

  // -----------------------------
  // 1) Schedule jobs (windowed)
  // -----------------------------

  // Set Elapsed_Day near end of day (allow window so we don't miss it)
  if (CurrentTime.hour() == 23 && CurrentTime.minute() == 59 && withinWindow(CurrentTime)) {
    setElapsed_Day(); // already "once per day" guarded internally
  }

  // Kick aggregation just after midnight (windowed)
  // (We schedule it; worker executes it below)
  if (CurrentTime.hour() == 0 && CurrentTime.minute() == 0 && withinWindow(CurrentTime)) {
    scheduleJob(JOB_AGGREGATE);
  }

  // FS cleanup hourly at :30 (windowed + once-per-hour guard)
  if (CurrentTime.minute() == 30 && withinWindow(CurrentTime)) {
    int h = CurrentTime.hour();
    if (h != g_lastFsCleanHourRan) {
      g_lastFsCleanHourRan = h;
      scheduleJob(JOB_FS_CLEAN);
    }
  }

  // Healthcheck hourly at :35 (windowed + once-per-hour guard)
  if (CurrentTime.minute() == 35 && withinWindow(CurrentTime)) {
    int h = CurrentTime.hour();
    if (h != g_lastHealthHourRan) {
      g_lastHealthHourRan = h;
      scheduleJob(JOB_HEALTHCHECK);
    }
  }

  // -----------------------------
  // 2) Execute scheduled jobs (single worker context)
  // -----------------------------
  uint32_t jobs = claimJobs();
  if (!jobs) return;

  if (jobs & JOB_AGGREGATE) {
    // Only aggregates if Elapsed_Day true; function includes safety gates
    setperformLogAggregation();
  }

  if (jobs & JOB_FS_CLEAN) {
    if (!g_fileSystemReady) {
      LOG_CAT(DBG_FS, "[FS.CleanupWorker] Skipping — g_fileSystemReady == false\n");
    } else {
      LOG_CAT(DBG_FS, "[FS.CleanupWorker] Running enforceTemperatureLogDiskLimit()\n");
      enforceTemperatureLogDiskLimit();
      LOG_CAT(DBG_FS, "[FS.CleanupWorker] Done enforceTemperatureLogDiskLimit()\n");
    }
  }

  if (jobs & JOB_HEALTHCHECK) {
    runHourlyHealthCheck();
  }
}
