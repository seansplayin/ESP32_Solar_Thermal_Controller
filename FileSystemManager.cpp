// FileSystemManager.cpp
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
#include <freertos/semphr.h>
#include <ESP32-targz.h>
#include "DiagLog.h"


// for Serial.printf diagnostics use LOG_CAT(DBG_FS, "whatever you want said goes here");
// to display larger concerns use LOG_ERR("[FS] whatever you want said goes here");


#define FS_CLEANUP_DEBUG 0  // set to 1 temporarily if you want verbose listing


SemaphoreHandle_t fileSystemMutex = xSemaphoreCreateMutex();
bool g_fileSystemReady = false; // Global Flag to enable Temp Log after FS Mount



// Forward declarations for internal helpers
static String extractDateFromPath(const String &fullPath);
static void   scanDirForOldestDate(const char *dirPath, String &oldestDate);


// Unlocked helpers (caller must already hold fileSystemMutex)
static bool findOldestTemperatureLogDateUnlocked(String &outDate);
static void deleteTemperatureLogsForDateUnlocked(const String &dateStr);
static bool deleteTemperatureLogsRecursiveUnlocked(const char* basePath);

// Safe public wrappers (they acquire fileSystemMutex themselves)
bool deleteTemperatureLogsRecursive(const char* basePath);
void enforceTemperatureLogDiskLimit();

static void   deleteLogsForDateRecursive(const char *dirPath,
                                         const String &targetDate);


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
        LOG_CAT(DBG_FS, "%s: attempt %d/%d failed to lock FS mutex\n",
                tag, attempt, maxAttempts);
        vTaskDelay(pdMS_TO_TICKS(50));  // small backoff
    }

    LOG_CAT(DBG_FS, "%s: giving up acquiring filesystem mutex\n", tag);
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
                
        esp_task_wdt_reset();
        vTaskDelay(1);  // yield a little
        entry = dir.openNextFile();
    }
    dir.close();
}




File openLogFileUnlocked(const String& filename, const char* mode) {
  if (!g_fileSystemReady) return File();
  return LittleFS.open(filename, mode);
}



static bool findOldestTemperatureLogDateUnlocked(String &outDate) {
    outDate = String();
    if (!LittleFS.exists("/Temperature_Logs")) {
        LOG_CAT(DBG_FS, "[FS] ⚠ /Temperature_Logs does not exist\n");
        return false;
    }

    scanDirForOldestDate("/Temperature_Logs", outDate);
    if (outDate.isEmpty()) {
        LOG_CAT(DBG_FS, "[FS] ⚠ No temperature log files with dates found\n");
        return false;
    }

    LOG_CAT(DBG_FS, "[FS] Oldest temperature log date found: %s\n", outDate.c_str());
    return true;
}





// Delete a directory tree by path (used from WebServerManager / ThirdWebpage)
// This is *not* the date-based cleanup; it just nukes whatever subtree you pass.
static bool deleteTemperatureLogsRecursiveUnlocked(const char* basePath) {
    if (!LittleFS.exists(basePath)) {
        LOG_CAT(DBG_FS, "[FS] deleteTemperatureLogsRecursive: '%s' does not exist\n", basePath);
        return true;  // nothing to do
    }

    File dir = LittleFS.open(basePath);
    if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        bool ok = LittleFS.remove(basePath);
        LOG_CAT(DBG_FS, "[FS] deleteTemperatureLogsRecursive: remove '%s' -> %s\n",
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
            deleteTemperatureLogsRecursiveUnlocked(childPath.c_str());
        } else {
            if (!LittleFS.remove(childPath)) {
                LOG_CAT(DBG_FS, "[FS] deleteTemperatureLogsRecursive: failed to remove file '%s'\n",
                        childPath.c_str());
            }
        }

        // WDT friendliness during long delete passes
        esp_task_wdt_reset();
        vTaskDelay(1);
        entry = dir.openNextFile();
    }
    dir.close();

    bool ok = LittleFS.rmdir(basePath) || LittleFS.remove(basePath);
    LOG_CAT(DBG_FS, "[FS] deleteTemperatureLogsRecursive: remove dir '%s' -> %s\n",
                  basePath, ok ? "OK" : "FAIL");
    return ok;
}

bool deleteTemperatureLogsRecursive(const char* basePath) {
    if (!g_fileSystemReady) return false;

    if (!takeFileSystemMutexWithRetry("deleteTemperatureLogsRecursive",
                                      pdMS_TO_TICKS(1000), 5)) {
        LOG_CAT(DBG_FS, "[FS] ❌ deleteTemperatureLogsRecursive: failed to lock mutex\n");
        return false;
    }

    bool ok = deleteTemperatureLogsRecursiveUnlocked(basePath);

    xSemaphoreGive(fileSystemMutex);
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
                    LOG_CAT(DBG_FS, "[FS] ✔ Verified deletion of '%s' (date %s)\n",
                            fullPath.c_str(), targetDate.c_str());

                } else {
                    LOG_CAT(DBG_FS,
                            "[FS] ❌ Deletion check failed for '%s' (removed=%d, exists=%d)\n",
                            fullPath.c_str(),
                            removed ? 1 : 0,
                            stillExists ? 1 : 0);
                }
            }
        }

        esp_task_wdt_reset();
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
                LOG_CAT(DBG_FS, "[FS] 🗑 Removed empty directory '%s'\n", dirPath);

            } else {
                LOG_CAT(DBG_FS, "[FS] ❌ Failed to remove empty directory '%s'\n", dirPath);

            }
        }
    }
}

static void deleteTemperatureLogsForDateUnlocked(const String &dateStr) {
    if (!LittleFS.exists("/Temperature_Logs")) return;
    LOG_CAT(DBG_FS, "[FS] 🔎 Deleting all temperature logs for date %s\n", dateStr.c_str());

    deleteLogsForDateRecursive("/Temperature_Logs", dateStr);
}







void enforceTemperatureLogDiskLimit() {
    if (!g_fileSystemReady) return;
    LOG_CAT(DBG_FS, "[FS] enforceTemperatureLogDiskLimit(): checking disk usage...\n");

    if (!takeFileSystemMutexWithRetry("[FS] enforceTemperatureLogDiskLimit",
                                      pdMS_TO_TICKS(1000), 5)) {

        LOG_ERR("[FS] ❌ Failed to lock filesystem mutex in enforceTemperatureLogDiskLimit()");
        return;
    }

    // WDT friendliness: we can spend time scanning/deleting while holding the mutex
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(1));

    size_t total = LittleFS.totalBytes();
    size_t used  = LittleFS.usedBytes();

    LOG_CAT(DBG_FS, "[FS] used=%u, total=%u\n", (unsigned)used, (unsigned)total);

    if (total == 0) {

        LOG_ERR("[FS] ❌ Failed to get LittleFS size (total == 0)");

        xSemaphoreGive(fileSystemMutex);
        return;
    }
    float pctUsed = ((float)used / (float)total) * 100.0f;
    if (pctUsed < FS_Cleaning_START_LIMIT) {
#if FS_CLEANUP_DEBUG
        LOG_CAT(DBG_FS, "[FS] Disk Usage at %.1f%% — below start limit (%.1f%%), no cleanup needed.\n",
                      pctUsed, FS_Cleaning_START_LIMIT);
#endif
        xSemaphoreGive(fileSystemMutex);
        return;
    }

    LOG_CAT(DBG_FS, "[FS] ⚠ Disk at %.1f%% used (start limit %.1f%%) — beginning temperature log cleanup\n",
                  pctUsed, FS_Cleaning_START_LIMIT);

    const int maxIterations = 32;
    String lastDateTried;
    for (int iteration = 0;
         iteration < maxIterations && pctUsed >= FS_Cleaning_STOP_LIMIT;
         ++iteration)
    {
        String oldestDate;
        if (!findOldestTemperatureLogDateUnlocked(oldestDate)) {

            LOG_CAT(DBG_FS, "[FS] ⚠ No more log dates found; stopping cleanup");

            break;
        }
        if (oldestDate == lastDateTried && iteration > 0) {

            LOG_ERR("[FS] ⚠ Oldest date (%s) repeated; aborting to avoid loop\n",
                    oldestDate.c_str());

            break;
        }
        lastDateTried = oldestDate;
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(1));

        size_t usedBefore = used;
        
        LOG_CAT(DBG_FS, "[FS] 🧹 Iteration %d: deleting logs for oldest date %s\n",
                      iteration + 1, oldestDate.c_str());

        deleteTemperatureLogsForDateUnlocked(oldestDate);

        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(1));

        used = LittleFS.usedBytes();
        pctUsed = ((float)used / (float)total) * 100.0f;

       
        LOG_CAT(DBG_FS, "[FS] After deleting %s: used=%u bytes (%.1f%% of %u bytes)\n",
                      oldestDate.c_str(), (unsigned)used, pctUsed, (unsigned)total);

        if (used == usedBefore) {

            LOG_ERR("[FS] ⚠ No bytes freed; stopping cleanup to avoid infinite loop");

            break;
        }
        if (pctUsed < FS_Cleaning_STOP_LIMIT) {

            LOG_CAT(DBG_FS, "[FS] ✅ Disk usage now %.1f%% (< %.1f%%); cleanup complete\n",
                          pctUsed, FS_Cleaning_STOP_LIMIT);

            break;
        }
        vTaskDelay(1);
    }
    xSemaphoreGive(fileSystemMutex);
}






void LittleFSformat()  {
    if (LittleFS.format()) {
        LOG_ERR("[FS] Formatting LittleFS succeeded. Attempting to mount again...\n");
        if (LittleFS.begin()) {
            LOG_ERR("[FS] LittleFS mounted successfully after formatting.\n");
        } else {
            LOG_ERR("[FS] Mounting LittleFS failed even after formatting.\n");
        }
    } else {
        LOG_ERR("[FS] Formatting LittleFS failed.\n");
    }
}

void initializeFileSystem() {
    // Attempt to mount LittleFS. If fail, provide instructions for manual formatting.
    LOG_CAT(DBG_FS, "[FS] Attempting to mount LittleFS file system.\n");
    if (!LittleFS.begin()) {
        LOG_ERR("[FS] Mounting LittleFS failed. If you wish to format the filesystem to LittleFS,\n");
        LOG_ERR("[FS] uncomment the 'LittleFS.format()' line in the 'initializeFileSystem()' function\n");
        LOG_ERR("[FS] in the FileSystemManager.cpp file and re-upload your sketch.\n");
        g_fileSystemReady = false; // sets flag false on mount failure
        return;

        // Uncomment the next line to enable formatting LittleFS automatically. Use with caution.
        // LittleFSformat(); // Enabling LittleFSformat(); will format the flash 
    }

        LOG_CAT(DBG_FS, "[FS] LittleFS mounted successfully.\n");

    // Ensure JSON config directory exists (shared by multiple config files)
    if (!LittleFS.exists(DIAG_SERIAL_CONFIG_DIR)) {
        if (LittleFS.mkdir(DIAG_SERIAL_CONFIG_DIR)) {
            LOG_CAT(DBG_FS, "[FS] Created %s\n", DIAG_SERIAL_CONFIG_DIR);
        } else {
            LOG_ERR("[FS] ERROR: Failed to create %s\n", DIAG_SERIAL_CONFIG_DIR);
        }
    }

    g_fileSystemReady = true; // ✅ temp logger waits for this flag
       
}



File openLogFileLocked(const String& filename, const char* mode) {
  if (!g_fileSystemReady) return File();

  if (!takeFileSystemMutexWithRetry("openLogFileLocked", pdMS_TO_TICKS(50), 10)) {
    return File();
  }

  // We intentionally KEEP the mutex held while returning the File.
  File f = openLogFileUnlocked(filename, mode);

  // If open failed, release immediately so we don't deadlock.
  if (!f) {
    xSemaphoreGive(fileSystemMutex);
  }

  return f;
}

void closeLogFileLocked(File& f) {
  if (f) {
    f.close();
  }
  // Always release the mutex that openLogFileLocked acquired.
  xSemaphoreGive(fileSystemMutex);
}


// Function definition
File openLogFile(const String& filename, const char* mode) {

  if (!g_fileSystemReady) return File();

  if (!takeFileSystemMutexWithRetry("openLogFile", pdMS_TO_TICKS(50), 10)) {
    return File();
  }

  File f = openLogFileUnlocked(filename, mode);

  xSemaphoreGive(fileSystemMutex);
  return f;
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


// Returns file system stats: JSON string with used, total, free, pctUsed plus friendly labels
String getFSStatsString() {
  static size_t s_total = 0;
  static size_t s_used  = 0;
  static uint32_t s_lastMs = 0;

  const uint32_t now = millis();
  const uint32_t intervalMs = 30000;

  size_t total = s_total;
  size_t used  = s_used;

  // Refresh cache at most every intervalMs
  if (g_fileSystemReady && (s_total == 0 || (uint32_t)(now - s_lastMs) >= intervalMs)) {

    // Non-blocking (short) lock attempt — if busy, return cached values
    if (xSemaphoreTake(fileSystemMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
      s_total  = LittleFS.totalBytes();
      s_used   = LittleFS.usedBytes();
      s_lastMs = now;

      total = s_total;
      used  = s_used;

      xSemaphoreGive(fileSystemMutex);
    }
  }

  if (total == 0) {
    return String("FS: unknown");
  }

  size_t freeBytes = (total > used) ? (total - used) : 0;
  float pctUsed    = (total > 0) ? ((float)used / (float)total) * 100.0f : 0.0f;

  String totalStr = formatBytes(total);
  String usedStr  = formatBytes(used);
  String freeStr  = formatBytes(freeBytes);

  String out = "{\"usedBytes\":" + String(used) +
               ",\"totalBytes\":" + String(total) +
               ",\"freeBytes\":" + String(freeBytes) +
               ",\"pctUsed\":" + String(pctUsed, 1) +
               ",\"usedLabel\":\"" + usedStr + "\"" +
               ",\"freeLabel\":\"" + freeStr + "\"" +
               ",\"totalLabel\":\"" + totalStr + "\"}";

  return out;
}
