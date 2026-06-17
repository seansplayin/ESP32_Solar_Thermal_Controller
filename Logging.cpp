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

extern DateTime CurrentTime;
extern RTC_DS3231 rtc;  // Use the external 'rtc' declaration
volatile bool Elapsed_Day = false;
extern void runHourlyHealthCheck();

static int lastHourRan = -1;
extern QueueHandle_t logQueue;

void logPumpEvent(uint8_t pumpIndex, bool isStart, const DateTime &ts) {
  LogEvent ev{ pumpIndex, isStart, ts };
  xQueueSend(logQueue, &ev, portMAX_DELAY);
}
// 4 functions in this file that read/write to file system for Pump Logs: LogPumpEvent, aggregatePumptoDailyLogs, aggregateDailyToMonthlyLogs, aggregateMonthlyToYearlyLogs

// Helper function to parse datetime strings
DateTime parseDateTime(String datetimeStr);
// Assuming parseDateTimeFromLog() and rtc.now() are properly defined elsewhere
DateTime parseDateTimeFromLog(const String& datetimeStr);


// New queue based Logging Topology for Pump Runtimes: checkTimeAndAct → setElapsed_Day / setperformLogAggregation → performLogAggregation. queue is written to LittleFS File System using task "TaskLogger" in TaskManager.cpp file.

// Log pump event (writer version — called from processLogQueue)
void logPumpEvent(const LogEvent &ev) {
  String filename = "/Pump_Logs/pump" + String(ev.pumpIndex + 1) + "_Log.txt";  // CHANGE: Updated to new dir

  if (xSemaphoreTake(fileSystemMutex, portMAX_DELAY)) {
    if (!LittleFS.exists("/Pump_Logs")) {  // CHANGE: Create dir if missing
      LittleFS.mkdir("/Pump_Logs");
    }

    File file = LittleFS.open(filename, "a");
    if (!file) {
      Serial.println("Failed to open file: " + filename);
      xSemaphoreGive(fileSystemMutex);
      return;
    }

    char timestamp[20];
    snprintf(timestamp, sizeof(timestamp), "%04d-%02d-%02d %02d:%02d:%02d", ev.ts.year(), ev.ts.month(), ev.ts.day(), ev.ts.hour(), ev.ts.minute(), ev.ts.second());
    file.print(ev.isStart ? "START " : "STOP ");
    file.println(timestamp);

    file.close();
    xSemaphoreGive(fileSystemMutex);
  }
}

// Process log queue
void processLogQueue() {
  LogEvent ev;
  while (xQueueReceive(logQueue, &ev, 0) == pdTRUE) {
    logPumpEvent(ev);
  }
}


//********List files in the LittleFS********
void listAllFiles() {
    if (!takeFileSystemMutexWithRetry("[Logging] listAllFiles",
                                      pdMS_TO_TICKS(2000), 3)) {
        Serial.println("[Logging] Failed to lock FS mutex in listAllFiles");
        return;
    }

    File root = LittleFS.open("/");
    if (!root) {
        Serial.println("[Logging] Failed to open LittleFS root");
        xSemaphoreGive(fileSystemMutex);
        return;
    }

    File file = root.openNextFile();
    Serial.println("Files stored in LittleFS:");
    while (file) {
        Serial.println(file.name());
        file = root.openNextFile();
    }

    xSemaphoreGive(fileSystemMutex);
}



//********Read Files in the LittleFS********
void readAndPrintLogFile(const String& filename) {
    String fullPath = "/Pump_Logs/" + filename; // CHANGE: Updated to new dir

    if (!takeFileSystemMutexWithRetry("[Logging] readAndPrintLogFile",
                                      pdMS_TO_TICKS(2000), 3)) {
        Serial.println("[Logging] Failed to lock FS mutex in readAndPrintLogFile");
        return;
    }

    File logFile = LittleFS.open(fullPath, "r");
    if (!logFile) {
        Serial.println("Failed to open " + fullPath + " for reading");
        xSemaphoreGive(fileSystemMutex);
        return;
    }

    Serial.println("Contents of " + fullPath + ":");
    while (logFile.available()) {
        Serial.println(logFile.readStringUntil('\n'));
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
}}
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
        Serial.println("[Logging] Failed to lock FS mutex in calculateTotalRuntime");
        return 0;
    }

    File logFile = LittleFS.open(logFilename, "r");
    if (!logFile) {
        Serial.println("Failed to open log file for reading: " + logFilename);
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
    String logFilename      = "/Pump_Logs/pump" + String(pumpIndex + 1) + "_Log.txt";  // Updated path
    String dailyLogFilename = "/Pump_Logs/pump" + String(pumpIndex + 1) + "_Daily.txt";  // Updated path

    if (xSemaphoreTake(fileSystemMutex, portMAX_DELAY)) {
      if (!LittleFS.exists("/Pump_Logs")) {  // CHANGE: Create dir if missing
        LittleFS.mkdir("/Pump_Logs");
      }

      // Load existing daily runtimes
      std::map<String, unsigned long> dailyRuntimeMap;
      {
          File dailyLogFile = LittleFS.open(dailyLogFilename, "r");
          if (dailyLogFile) {
              while (dailyLogFile.available()) {
                  String line = dailyLogFile.readStringUntil('\n');
                  int dateEnd = line.indexOf(" Total Runtime: ");
                  if (dateEnd != -1) {
                      String date = line.substring(0, dateEnd);
                      int runtimePos = line.indexOf("Total Runtime: ") + 15;
                      int secondsPos = line.indexOf(" seconds", runtimePos);
                      if (runtimePos != -1 && secondsPos != -1) {
                          String runtimeStr  = line.substring(runtimePos, secondsPos);
                          unsigned long runtimeVal = strtoul(runtimeStr.c_str(), NULL, 10);
                          dailyRuntimeMap[date] = runtimeVal; // overwrite per date
                      }
                  }
              }
              dailyLogFile.close();
          }
      }

      File logFile = LittleFS.open(logFilename, "r");
      if (!logFile) {
          Serial.println("No log file for pump " + String(pumpIndex + 1));
          xSemaphoreGive(fileSystemMutex);
          return; // No events to aggregate
      }

      DateTime lastStartTime;
      bool isPumpRunning = false;

      // Accumulate runtime from START→STOP, attributing to STOP date
      while (logFile.available()) {
          String line = logFile.readStringUntil('\n');
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
          char yesterdayBuffer[11];
          snprintf(yesterdayBuffer, sizeof(yesterdayBuffer), "%04d-%02d-%02d",
                   yesterday.year(), yesterday.month(), yesterday.day());
          String yesterdayDateISO = String(yesterdayBuffer);

          unsigned long runTime = dayBoundary.unixtime() - lastStartTime.unixtime();
          dailyRuntimeMap[yesterdayDateISO] += runTime;

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
          File dailyLogFile = LittleFS.open(dailyLogFilename, "w");
          if (dailyLogFile) {
              for (const auto& entry : dailyRuntimeMap) {
                  dailyLogFile.printf("%s Total Runtime: %lu seconds\n",
                                      entry.first.c_str(), entry.second);
              }
              dailyLogFile.close();
          }
      }

      Serial.println("Aggregation complete for pump " + String(pumpIndex + 1));

      xSemaphoreGive(fileSystemMutex);
    }
}



unsigned long calculateTotalMonthlyRuntime(const String& dailyLogFilename) {
File dailyLogFile = LittleFS.open(dailyLogFilename, "r");
if (!dailyLogFile) {
Serial.println("Failed to open daily log file for reading: " + dailyLogFilename);
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
}}
dailyLogFile.close();
return totalMonthlyRuntime;
}

void aggregateDailyToMonthlyLogs(int pumpIndex)
{
    String dailyLogFilename = "/Pump_Logs/pump" + String(pumpIndex + 1) + "_Daily.txt";  // Updated path
    String monthlyLogFilename = "/Pump_Logs/pump" + String(pumpIndex + 1) + "_Monthly.txt";  // Updated path

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
    }
    else {
        Serial.println("Failed to open " + monthlyLogFilename + " for writing");
    }

    // 7) Clear daily file
    if (LittleFS.remove(dailyLogFilename)) {
        Serial.println("Daily log file cleared.");
    } else {
        Serial.println("Failed to clear daily log file.");
    }
}




unsigned long calculateTotalYearlyRuntime(const String& yearlyLogFilename) {
File yearlyLogFile = LittleFS.open(yearlyLogFilename, "r");
if (!yearlyLogFile) {
Serial.println("Failed to open yearly log file for reading: " + yearlyLogFilename);
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
    // 1) Reintroduce these paths (the lines missing from your snippet):
    String monthlyLogFilename = "/Pump_Logs/pump" + String(pumpIndex + 1) + "_Monthly.txt";  // Updated path
    String yearlyLogFilename  = "/Pump_Logs/pump" + String(pumpIndex + 1) + "_Yearly.txt";  // Updated path

    // 2) Sum the monthly logs to get totalYearlyRuntime
    unsigned long totalYearlyRuntime = calculateTotalYearlyRuntime(monthlyLogFilename);

    // 3) Decide whether we label the currentYear or (currentYear - 1) if month == 1
    DateTime now = rtc.now();  // or however you get the current RTC date/time
    int year = now.year();
    if (now.month() == 1) {
        // It's January, so label *last* year (the one just ended)
        year = year - 1;
    }

    // Convert the final year into a string
    String labelYear = String(year);

    bool yearExists = false;
    unsigned long existingRuntime = 0;
    String updatedContents;

    // 4) Read existing lines from the yearly file
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
                // Keep other lines from older/future years
                updatedContents += line + "\n";
            }
        }
        yearlyLogFile.close();
    }

    // 5) Combine old + new runtime
    totalYearlyRuntime += existingRuntime;

    // 6) Add or update the line for labelYear
    // e.g. "2023 Total Runtime: 1234 seconds"
    updatedContents += labelYear + " Total Runtime: " + String(totalYearlyRuntime) + " seconds\n";

    // 7) Overwrite the file
    yearlyLogFile = LittleFS.open(yearlyLogFilename, "w");
    if (!yearlyLogFile) {
        Serial.println("Failed to open yearly log file for writing.");
        return;
    }
    yearlyLogFile.print(updatedContents);
    yearlyLogFile.close();

    // 8) Optionally clear the monthly log file
    if (LittleFS.remove(monthlyLogFilename)) {
        Serial.println("Cleared monthly log file.");
    } else {
        Serial.println("Failed to clear monthly log file.");
    }

    Serial.println("Aggregated monthly logs to yearly log for pump " + String(pumpIndex + 1));
}




void performLogAggregation() {
// Aggregate daily logs for each pump
for (int i = 0; i < 10; i++) {
aggregatePumptoDailyLogs(i);
}
String currentDate = getCurrentDateStringMDY();
// Extract month and day from the current date string
int month = currentDate.substring(0, 2).toInt();
int day = currentDate.substring(3, 5).toInt();
// Check if it's the first day of any month
if (day == 1) {
// It's the first day of a month, aggregate daily to monthly logs for each pump
for (int i = 0; i < 10; i++) {
aggregateDailyToMonthlyLogs(i);
}
// Additionally, check if it's the first day of the year (January 1st)
if (month == 1) {
// It's the first day of the year, aggregate monthly to yearly logs for each pump
for (int i = 0; i < 10; i++) {
aggregateMonthlyToYearlyLogs(i);
}}}}



void setElapsed_Day() {
if (!Elapsed_Day) { // Check if Elapsed_Day is false
Elapsed_Day = true;
Serial.println("Elapsed_Day flag set to true");
} }



void setperformLogAggregation() {
if (Elapsed_Day) { // Check if Elapsed_Day is true
performLogAggregation();
Elapsed_Day = false;
Serial.println("Log aggregation performed and Elapsed_Day flag reset");
}}



void maybeRunHealthCheckHourly() {
  time_t now = time(nullptr);
  if (now < 100000) return;

  struct tm t;
  localtime_r(&now, &t);

  // allow a small window so you don't miss it if the tick isn't exact
  if (t.tm_min == 0 && t.tm_sec <= 2) {
    if (t.tm_hour != lastHourRan) {
      lastHourRan = t.tm_hour;
      runHourlyHealthCheck();
    }
  }
}


void checkTimeAndAct() {
  if (CurrentTime.hour() == 23 && CurrentTime.minute() == 59) {
    setElapsed_Day();  
  } 
  if (CurrentTime.hour() == 0 && CurrentTime.minute() == 0 && CurrentTime.second() == 1) {
    setperformLogAggregation();   
  }
  if (CurrentTime.minute() == 30 && CurrentTime.second() == 0) {  // Trigger at XX:30:00
    if (thFileSystemCleanup != NULL) {  // Safety check
      xTaskNotifyGive(thFileSystemCleanup);  // Trigger the cleanup task
    } else {
      Serial.println("[Error] FileSystemCleanup task handle is NULL");
    }
  }

  if (CurrentTime.hour() == 0 && CurrentTime.minute() == 20 && CurrentTime.second() == 00) {
    runHourlyHealthCheck();   
  }



}