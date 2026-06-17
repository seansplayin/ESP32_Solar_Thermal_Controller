#include "FileSystemManager.h"
#include "Logging.h" // Assuming logMessage() is declared here
#include "Config.h"
#include <LittleFS.h>
#include <RTClib.h>     // for DateTime
#include "RTCManager.h" 
#include "esp_heap_caps.h"
#include "esp_littlefs.h"
#include <Arduino.h>
#include <esp_err.h>
#include <esp_task_wdt.h> 

#define FS_CLEANUP_DEBUG 1   // set to 1 temporarily if you want verbose listing


SemaphoreHandle_t fileSystemMutex = xSemaphoreCreateMutex();
bool g_fileSystemReady = false; // Global Flag to enable Temp Log after FS Mount

// Forward declarations for internal helpers
static String extractDateFromPath(const String &fullPath);
static void   scanDirForOldestDate(const char *dirPath, String &oldestDate);
static bool   findOldestTemperatureLogDate(String &outDate);
static void   deleteLogsForDateRecursive(const char *dirPath,
                                         const String &targetDate);
static void   deleteTemperatureLogsForDate(const String &dateStr);
bool deleteTemperatureLogsRecursive(const char* basePath);
void enforceTemperatureLogDiskLimit();

// Extract first date from a filename/path.
// Supported formats inside the *filename*:
//   - YYYY-MM-DD
//   - YYYY_MM_DD
//   - YYYY.MM.DD
//   - compact YYYYMMDD
// Returns "" if nothing looks like a date.
static String extractDateFromPath(const String &fullPath) {
    // Work only with the *filename* part
    int slash = fullPath.lastIndexOf('/');
    String name = (slash >= 0) ? fullPath.substring(slash + 1) : fullPath;

    int len = name.length();
    if (len < 8) return String();

    // Pass 1: YYYY[-_.]MM[-_.]DD  (10 chars total)
    for (int i = 0; i <= len - 10; ++i) {
        char c0 = name[i];
        if (c0 < '0' || c0 > '9') continue;

        char c4 = name[i + 4];
        char c7 = name[i + 7];
        if (!((c4 == '-') || (c4 == '_') || (c4 == '.'))) continue;
        if (!((c7 == '-') || (c7 == '_') || (c7 == '.'))) continue;

        bool ok =
            (name[i + 0] >= '0' && name[i + 0] <= '9') &&
            (name[i + 1] >= '0' && name[i + 1] <= '9') &&
            (name[i + 2] >= '0' && name[i + 2] <= '9') &&
            (name[i + 3] >= '0' && name[i + 3] <= '9') &&
            (name[i + 5] >= '0' && name[i + 5] <= '9') &&
            (name[i + 6] >= '0' && name[i + 6] <= '9') &&
            (name[i + 8] >= '0' && name[i + 8] <= '9') &&
            (name[i + 9] >= '0' && name[i + 9] <= '9');

        if (ok) {
            String yyyy = name.substring(i,     i + 4);
            String mm   = name.substring(i + 5, i + 7);
            String dd   = name.substring(i + 8, i + 10);
            return yyyy + "-" + mm + "-" + dd;
        }
    }

    // Pass 2: compact YYYYMMDD (8 digits in a row)
    for (int i = 0; i <= len - 8; ++i) {
        char c0 = name[i];
        if (c0 < '0' || c0 > '9') continue;

        bool ok = true;
        for (int j = 0; j < 8; ++j) {
            char cj = name[i + j];
            if (cj < '0' || cj > '9') { ok = false; break; }
        }
        if (!ok) continue;

        String yyyy = name.substring(i,     i + 4);
        String mm   = name.substring(i + 4, i + 6);
        String dd   = name.substring(i + 6, i + 8);
        return yyyy + "-" + mm + "-" + dd;
    }

    return String();
}



bool takeFileSystemMutexWithRetry(const char *tag,
                                  TickType_t perAttemptTicks,
                                  int maxAttempts) {
    for (int attempt = 1; attempt <= maxAttempts; ++attempt) {
        if (xSemaphoreTake(fileSystemMutex, perAttemptTicks) == pdTRUE) {
            return true;
        }
        Serial.printf("%s: attempt %d/%d failed to lock FS mutex\n",
                      tag, attempt, maxAttempts);
        vTaskDelay(pdMS_TO_TICKS(50));  // small backoff
    }

    Serial.printf("%s: giving up acquiring filesystem mutex\n", tag);
    return false;
}




// Recursively scan /Temperature_Logs tree and compute the oldest "YYYY-MM-DD"
// date found in any file name. Updates oldestDate if it finds any.
static void scanDirForOldestDate(const char *dirPath, String &oldestDate) {
    File dir = LittleFS.open(dirPath);
    if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        return;
    }

    File entry = dir.openNextFile();
    while (entry) {
        String entryName = entry.name();
        int lastSlash = entryName.lastIndexOf('/');
        String baseName = (lastSlash >= 0)
                            ? entryName.substring(lastSlash + 1)
                            : entryName;

        // Build full path relative to the directory we were passed
        String fullPath = String(dirPath);
        if (!fullPath.endsWith("/")) fullPath += "/";
        fullPath += baseName;

        if (entry.isDirectory()) {
            entry.close();
            // Recurse into subdirectories (years, months, sensor folders)
            scanDirForOldestDate(fullPath.c_str(), oldestDate);
        } else {
            entry.close();
            String dateStr = extractDateFromPath(baseName);
            if (dateStr.length() == 10) {
                if (oldestDate.isEmpty() || dateStr < oldestDate) {
                    oldestDate = dateStr;
                }
            }
        }

        vTaskDelay(1);  // yield a little
        entry = dir.openNextFile();
    }
    dir.close();
}






static bool findOldestTemperatureLogDate(String &outDate) {
    outDate = String();
    if (!LittleFS.exists("/Temperature_Logs")) {
        Serial.println("[FS] ⚠ /Temperature_Logs does not exist");
        return false;
    }

    scanDirForOldestDate("/Temperature_Logs", outDate);
    if (outDate.isEmpty()) {
        Serial.println("[FS] ⚠ No temperature log files with dates found");
        return false;
    }

    Serial.printf("[FS] Oldest temperature log date found: %s\n", outDate.c_str());
    return true;
}





// Delete a directory tree by path (used from WebServerManager / ThirdWebpage)
// This is *not* the date-based cleanup; it just nukes whatever subtree you pass.
bool deleteTemperatureLogsRecursive(const char* basePath) {
    if (!LittleFS.exists(basePath)) {
        Serial.printf("[FS] deleteTemperatureLogsRecursive: '%s' does not exist\n", basePath);
        return true;  // nothing to do
    }

    File dir = LittleFS.open(basePath);
    if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        bool ok = LittleFS.remove(basePath);
        Serial.printf("[FS] deleteTemperatureLogsRecursive: remove '%s' -> %s\n",
                      basePath, ok ? "OK" : "FAIL");
        return ok;
    }

    File entry = dir.openNextFile();
    while (entry) {
        String entryName = entry.name();
        int lastSlash = entryName.lastIndexOf('/');
        String baseName = (lastSlash >= 0)
                            ? entryName.substring(lastSlash + 1)
                            : entryName;

        String childPath = String(basePath);
        if (!childPath.endsWith("/")) childPath += "/";
        childPath += baseName;

        bool isDir = entry.isDirectory();
        entry.close();

        if (isDir) {
            deleteTemperatureLogsRecursive(childPath.c_str());
        } else {
            if (!LittleFS.remove(childPath)) {
                Serial.printf("[FS] deleteTemperatureLogsRecursive: failed to remove file '%s'\n",
                              childPath.c_str());
            }
        }

        vTaskDelay(1);
        entry = dir.openNextFile();
    }
    dir.close();

    bool ok = LittleFS.rmdir(basePath) || LittleFS.remove(basePath);
    Serial.printf("[FS] deleteTemperatureLogsRecursive: remove dir '%s' -> %s\n",
                  basePath, ok ? "OK" : "FAIL");
    return ok;
}








// Recursively delete all files whose name contains `targetDate` (YYYY-MM-DD)
// under dirPath. Also removes empty directories (but never the root
// "/Temperature_Logs" itself).
static void deleteLogsForDateRecursive(const char *dirPath,
                                       const String &targetDate) {
    File dir = LittleFS.open(dirPath);
    if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        return;
    }

    File entry = dir.openNextFile();
    while (entry) {
        String entryName = entry.name();
        int lastSlash = entryName.lastIndexOf('/');
        String baseName = (lastSlash >= 0)
                            ? entryName.substring(lastSlash + 1)
                            : entryName;

        String fullPath = String(dirPath);
        if (!fullPath.endsWith("/")) fullPath += "/";
        fullPath += baseName;

        bool isDir = entry.isDirectory();
        entry.close();

        if (isDir) {
            // Recurse into this subdirectory
            deleteLogsForDateRecursive(fullPath.c_str(), targetDate);
        } else {
            String dateStr = extractDateFromPath(baseName);
            if (dateStr == targetDate) {
    bool removed = LittleFS.remove(fullPath);
    bool stillExists = LittleFS.exists(fullPath);

    if (removed && !stillExists) {
        Serial.printf("[FS] ✔ Verified deletion of '%s' (date %s)\n",
                      fullPath.c_str(), targetDate.c_str());
    } else {
        Serial.printf(
            "[FS] ❌ Deletion check failed for '%s' (removed=%d, exists=%d)\n",
            fullPath.c_str(),
            removed ? 1 : 0,
            stillExists ? 1 : 0
        );
    }
}

        }

        vTaskDelay(1);
        entry = dir.openNextFile();
    }
    dir.close();

    // Second pass: if this directory is now empty, remove it (unless it's the root)
    if (strcmp(dirPath, "/Temperature_Logs") != 0) {
        File check = LittleFS.open(dirPath);
        bool empty = true;
        if (check && check.isDirectory()) {
            File e2 = check.openNextFile();
            if (e2) {
                empty = false;
                e2.close();
            }
            check.close();
        }

        if (empty) {
            if (LittleFS.rmdir(dirPath)) {
                Serial.printf("[FS] 🗑 Removed empty directory '%s'\n", dirPath);
            } else {
                Serial.printf("[FS] ❌ Failed to remove empty directory '%s'\n",
                              dirPath);
            }
        }
    }
}

static void deleteTemperatureLogsForDate(const String &dateStr) {
    if (!LittleFS.exists("/Temperature_Logs")) return;
    Serial.printf("[FS] 🔎 Deleting all temperature logs for date %s\n",
                  dateStr.c_str());
    deleteLogsForDateRecursive("/Temperature_Logs", dateStr);
}







void enforceTemperatureLogDiskLimit() {
    size_t total = LittleFS.totalBytes();
    size_t used  = LittleFS.usedBytes();

    #if FS_CLEANUP_DEBUG
    Serial.printf("[FS] enforceTemperatureLogDiskLimit(): used=%u, total=%u\n",
                  (unsigned)used, (unsigned)total);
#endif

    if (total == 0) {
        Serial.println("[FS] ❌ Failed to get LittleFS size (total == 0)");
        return;
    }

    float pctUsed = ((float)used / (float)total) * 100.0f;
    

     if (pctUsed < FS_Cleaning_START_LIMIT) {
#if FS_CLEANUP_DEBUG
        Serial.printf("[FS] Disk Usage at %.1f%% — below start limit (%.1f%%), no cleanup needed.\n",
                      pctUsed, FS_Cleaning_START_LIMIT);
#endif
        return;
    }

        Serial.printf(
        "[FS] ⚠ Disk at %.1f%% used (start limit %.1f%%) — beginning "
        "temperature log cleanup\n",
        pctUsed, FS_Cleaning_START_LIMIT
    );

    // Try for up to ~5 seconds total (5 × 1s attempts) instead of one 5s block.
    if (!takeFileSystemMutexWithRetry("[FS] enforceTemperatureLogDiskLimit",
                                      pdMS_TO_TICKS(1000),   // per attempt
                                      5)) {                  // maxAttempts
        Serial.println("[FS] ❌ Failed to lock filesystem mutex in "
                       "enforceTemperatureLogDiskLimit()");
        return;
    }

    const int maxIterations = 32;  // safety: max ~32 days per cleanup run
    String lastDateTried;


    for (int iteration = 0;
         iteration < maxIterations && pctUsed >= FS_Cleaning_STOP_LIMIT;
         ++iteration)
    {
        String oldestDate;
        if (!findOldestTemperatureLogDate(oldestDate)) {
            Serial.println("[FS] ⚠ No more log dates found; stopping cleanup");
            break;
        }

        if (oldestDate == lastDateTried && iteration > 0) {
            Serial.printf(
                "[FS] ⚠ Oldest date (%s) is same as previous iteration and "
                "did not free enough space; aborting to avoid loop\n",
                oldestDate.c_str()
            );
            break;
        }
        lastDateTried = oldestDate;

        size_t usedBefore = used;

        Serial.printf(
            "[FS] 🧹 Iteration %d: deleting logs for oldest date %s\n",
            iteration + 1, oldestDate.c_str()
        );
        deleteTemperatureLogsForDate(oldestDate);

        used = LittleFS.usedBytes();
        pctUsed = ((float)used / (float)total) * 100.0f;

        Serial.printf(
            "[FS] After deleting %s: used=%u bytes (%.1f%% of %u bytes)\n",
            oldestDate.c_str(),
            (unsigned)used,
            pctUsed,
            (unsigned)total
        );

        if (used == usedBefore) {
            Serial.println(
                "[FS] ⚠ No bytes freed in this iteration; stopping cleanup "
                "to avoid infinite loop"
            );
            break;
        }

        if (pctUsed < FS_Cleaning_STOP_LIMIT) {
            Serial.printf(
                "[FS] ✅ Disk usage now %.1f%% (< %.1f%%); cleanup complete\n",
                pctUsed, FS_Cleaning_STOP_LIMIT
            );
            break;
        }

        vTaskDelay(1); // small breather between days
    }

    xSemaphoreGive(fileSystemMutex);
}





void LittleFSformat()  {
    if (LittleFS.format()) {
      Serial.println("Formatting LittleFS succeeded. Attempting to mount again...");
       if (LittleFS.begin()) {
        Serial.println("LittleFS mounted successfully after formatting.");
         } else { Serial.println("Mounting LittleFS failed even after formatting.");
          }} else {
          Serial.println("Formatting LittleFS failed.");
  }
  }

void initializeFileSystem() {
   // Attempt to mount LittleFS. If fail, provide instructions for manual formatting.
   Serial.println("Attempting to mount LittleFS file system.");
      if (!LittleFS.begin()) {
       Serial.println("Mounting LittleFS failed. If you wish to format the filesystem to LittleFS,");
       Serial.println("uncomment the 'LittleFS.format()' line in the 'initializeFileSystem()' function");
      Serial.println("in the FileSystemManager.cpp file and re-upload your sketch.");
      g_fileSystemReady = false; // sets flag false on mount failure
        return;

// Uncomment the next line to enable formatting LittleFS automatically. Use with caution.

//LittleFSformat(); // Enabling LittleFSformat(); will format the flash 
       
       } Serial.println("LittleFS mounted successfully.");
       g_fileSystemReady = true; // ✅ temp logger waits for this flag
       
}


// Function definition
File openLogFile(const String& filename, const char* mode) {
    if (!LittleFS.exists(filename)) {
        // Optionally, you can enable this message
        // Serial.println("File does not exist: " + filename);
        return File(); // Return an empty File object
    }
    File file = LittleFS.open(filename, mode);
    if (!file) {
        Serial.println("Failed to open " + filename + " for reading.");
    }
    return file;
}

// Append STOP event entry to open pump logs via TaskInitFileSystem at Bootup
void closeAllOpenPumpLogs() {
  DateTime now = getCurrentTimeAtomic();   // must be after initNTP or your RTC is set
  for (uint8_t i = 0; i < numPumps; ++i) {
    logPumpEvent(i, false, now);
  }
}



static String formatBytes(size_t v) {
  // returns human friendly: B, KB, MB, GB with 1 decimal
  const char* units[] = {"B","KB","MB","GB","TB"};
  double val = (double)v;
  int unit = 0;
  while (val >= 1024.0 && unit < 4) { val /= 1024.0; unit++; }
  char buf[32];
  if (val < 10.0 && unit > 0) {
    snprintf(buf, sizeof(buf), "%.1f %s", val, units[unit]);
  } else {
    snprintf(buf, sizeof(buf), "%.0f %s", val, units[unit]);
  }
  return String(buf);
}

// Returns heap as JSON-like string (free, total, pctUsed) or plain formatted string
String getFreeHeapString() {
    // Raw values from the ESP32 heap API
    size_t freeHeap  = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
    size_t totalHeap = heap_caps_get_total_size(MALLOC_CAP_DEFAULT);

    // Calculate USED bytes
    size_t usedHeap  = (totalHeap > freeHeap) ? (totalHeap - freeHeap) : 0;

    // Calculate USED percentage
    float pctUsed = (totalHeap > 0)
        ? (static_cast<float>(usedHeap) / static_cast<float>(totalHeap)) * 100.0f
        : 0.0f;

    // Format: "used / total bytes (xx.x% used)"
    char buf[96];
    snprintf(buf, sizeof(buf),
             "%u / %u bytes (%.1f%% used)",
             static_cast<unsigned>(usedHeap),
             static_cast<unsigned>(totalHeap),
             pctUsed);

    return String(buf);
}


// Returns file system stats: JSON string with used, total, free, pctUsed plus friendly labels
String getFSStatsString() {
  size_t total = 0;
  size_t used  = 0;

  // Simple, Arduino-style LittleFS API
  total = LittleFS.totalBytes();
  used  = LittleFS.usedBytes();

  // If total is zero, the FS implementation didn't give us anything useful
  if (total == 0) {
    return String("FS: unknown");
  }

  size_t freeBytes = (total > used) ? (total - used) : 0;
  float pctUsed    = (total > 0) ? ((float)used / (float)total) * 100.0f : 0.0f;

  auto formatBytes = [](size_t v) -> String {
    const char* units[] = {"B","KB","MB","GB","TB"};
    double val = (double)v;
    int unit = 0;
    while (val >= 1024.0 && unit < 4) {
      val /= 1024.0;
      unit++;
    }
    char buf[32];
    if (val < 10.0 && unit > 0) {
      snprintf(buf, sizeof(buf), "%.1f %s", val, units[unit]);
    } else {
      snprintf(buf, sizeof(buf), "%.0f %s", val, units[unit]);
    }
    return String(buf);
  };

  String totalStr = formatBytes(total);
  String usedStr  = formatBytes(used);
  String freeStr  = formatBytes(freeBytes);

  // Proper JSON that the browser can parse
  String out = "{\"usedBytes\":" + String(used) +
               ",\"totalBytes\":" + String(total) +
               ",\"freeBytes\":" + String(freeBytes) +
               ",\"pctUsed\":" + String(pctUsed, 1) +
               ",\"usedLabel\":\"" + usedStr + "\"" +
               ",\"freeLabel\":\"" + freeStr + "\"" +
               ",\"totalLabel\":\"" + totalStr + "\"}";

  return out;
}


