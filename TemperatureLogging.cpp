#include "TemperatureLogging.h"
#include "Config.h"
#include "FileSystemManager.h"
#include "RTCManager.h"
#include <LittleFS.h>
#include <time.h>
#include <Arduino.h>
#include <esp_task_wdt.h>
#include <math.h>
#include <TimeLib.h>
#include "DiagLog.h"
#include "AlarmManager.h"

extern SemaphoreHandle_t fileSystemMutex;

// External temperature globals
extern float panelT;
extern float CSupplyT;
extern float storageT;
extern float outsideT;
extern float CircReturnT;
extern float supplyT;
extern float CreturnT;
extern float DhwSupplyT;
extern float DhwReturnT;
extern float HeatingSupplyT;
extern float HeatingReturnT;
extern float dhwT;
extern float PotHeatXinletT;
extern float PotHeatXoutletT;

// Use your existing arrays from Config.h
extern const char* SENSOR_FILE_NAMES[15];   // 1..14 valid
extern const char* SENSOR_NAMES[15];        // Human readable

// ---------------------------------------------------------------------------
// CONFIG (from Config.h)
// ---------------------------------------------------------------------------
#ifndef TEMP_LOG_SAMPLE_SEC
#define TEMP_LOG_SAMPLE_SEC   60
#endif
#ifndef TEMP_LOG_DELTA_F
#define TEMP_LOG_DELTA_F      1.0f
#endif
#ifndef TEMP_LOG_FLUSH_MIN
#define TEMP_LOG_FLUSH_MIN    60
#endif

static uint32_t lastAddMs[14] = {0};           // Last add time per sensor
static uint32_t lastFlushMsGlobal = 0;         // Track last successful flush across all sensors
static const char* BASE_DIR    = "/Temperature_Logs";
static const int   NUM_SENSORS = 14;           // 1..14

extern bool g_fileSystemReady;  // from FileSystemManager
extern bool g_timeValid;        // from RTCManager / TimeSync

// Call this **once**, after network, web server, pumps, etc. are all up
static bool g_tempLogEnabled = false;

void enableTemperatureLogging()
{
    g_tempLogEnabled = true;
    LOG_CAT(DBG_TEMPLOG, "[TempLog] Enabled after system boot complete\n");
}

// RAM cache — unlimited size, grows as needed
struct LogEntry {
    DateTime dt;   // RTC local time at moment of sample
    float  value;
};

static LogEntry* cache[NUM_SENSORS]      = { nullptr };
static int       cacheCount[NUM_SENSORS] = { 0 };

// This is the **only** value we compare against for delta checks
// It is updated **every time we write to flash** (boot, hourly flush, midnight)
static float lastWrittenToFlash[NUM_SENSORS];
static int   lastWrittenDay[NUM_SENSORS] = { 0 };

// Last value that *passed* the delta rule (used for comparisons)
static float lastLoggedValue[NUM_SENSORS];
static bool  lastLoggedValid[NUM_SENSORS] = { false };

static uint32_t lastSampleMs = 0;
static uint32_t lastFlushMs  = 0;

// Flag to track if baseline has been written
static bool baselineWritten = false;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static String twoDigit(int v) { return (v < 10 ? "0" : "") + String(v); }

static String buildFilePath(int y, int m, int d, int sensor1) {
    return String(BASE_DIR) + "/" + y + "/" + twoDigit(m) + "/" +
           SENSOR_FILE_NAMES[sensor1] + "/" +
           y + "-" + twoDigit(m) + "-" + twoDigit(d) + ".txt";
}

// Ensure /Temperature_Logs/YYYY/MM/SENSOR_NAME directory hierarchy exists
static bool ensureSensorDir(int year, int month, int sensor1)
{
    String parts[4] = {
        String(BASE_DIR),
        String(year),
        twoDigit(month),
        String(SENSOR_FILE_NAMES[sensor1])
    };

    String cur;
    for (int i = 0; i < 4; ++i) {
        cur += "/" + parts[i];
        if (!LittleFS.exists(cur)) {
            if (!LittleFS.mkdir(cur)) {
#ifdef TEMP_LOG_DEBUG_ERRORS
                LOG_ERR("[TempLog] ERROR: mkdir failed for %s\n", cur.c_str());
#endif
                return false;
            }
        }
    }
    return true;
}

static float getValue(int idx0) {
    switch (idx0 + 1) {
        case 1:  return panelT;
        case 2:  return CSupplyT;
        case 3:  return storageT;
        case 4:  return outsideT;
        case 5:  return CircReturnT;
        case 6:  return supplyT;
        case 7:  return CreturnT;
        case 8:  return DhwSupplyT;
        case 9:  return DhwReturnT;
        case 10: return HeatingSupplyT;
        case 11: return HeatingReturnT;
        case 12: return dhwT;
        case 13: return PotHeatXinletT;
        case 14: return PotHeatXoutletT;
    }
    return NAN;
}

// Convert an RTC DateTime into a POSIX time_t using RTClib's unixtime()
static time_t dateTimeToTimeT(const DateTime& dt) {
    return (time_t)dt.unixtime();
}

// ---------------------------------------------------------------------------
// Write a single line to flash AND update lastWrittenToFlash[]
// ---------------------------------------------------------------------------
static void writeToFlash(int sensorIdx0, const DateTime& dt, float value) {
    int year   = dt.year();
    int month  = dt.month();
    int day    = dt.day();
    int hour   = dt.hour();
    int minute = dt.minute();

    String path = buildFilePath(year, month, day, sensorIdx0 + 1);

    if (!takeFileSystemMutexWithRetry("[TempLog] writeToFlash",
                                      pdMS_TO_TICKS(2000), 3)) {
#ifdef TEMP_LOG_DEBUG_ERRORS
        LOG_ERR("[TempLog] ERROR: mutex timeout in writeToFlash\n");
#endif
        return;
    }

    // Create full directory tree for this sensor/day
    if (!ensureSensorDir(year, month, sensorIdx0 + 1)) {
        xSemaphoreGive(fileSystemMutex);
        return;
    }

    File f = LittleFS.open(path, "a");  // append + create
    if (!f) {
#ifdef TEMP_LOG_DEBUG_ERRORS
        LOG_ERR("[TempLog] ERROR: cannot open %s\n", path.c_str());
        AlarmManager_event(ALM_FS_WRITE_FAIL, ALM_ALARM, "Log Write Fail: %s", SENSOR_NAMES[sensorIdx0 + 1]);
#endif
        xSemaphoreGive(fileSystemMutex);
        return;
    }

    char line[64];
    snprintf(line, sizeof(line),
             "%04d-%02d-%02d,%02d:%02d:00,%.2f\n",
             year, month, day,
             hour, minute,
             value);

    if (f.print(line)) {
#ifdef TEMP_LOG_DEBUG_FLUSH
        LOG_CAT(DBG_TEMPLOG, "[TempLog] WRITE %s -> %.2fF\n", path.c_str(), value);
#endif
    } else {
#ifdef TEMP_LOG_DEBUG_ERRORS
        LOG_ERR("[TempLog] WRITE FAILED %s\n", path.c_str());
#endif
    }

    f.close();
    xSemaphoreGive(fileSystemMutex);

    // Update the persisted reference
    lastWrittenToFlash[sensorIdx0] = value;
    lastWrittenDay[sensorIdx0]     = day;
}

// ---------------------------------------------------------------------------
// Add to RAM cache and update lastLoggedValue[]
// ---------------------------------------------------------------------------
static void addToCache(int sensorIdx0, float value) {
    DateTime dt = getCurrentTimeAtomic();   // RTC local time

    int newCount = cacheCount[sensorIdx0] + 1;
    LogEntry* newBuf = (LogEntry*)realloc(cache[sensorIdx0], sizeof(LogEntry) * newCount);
    if (!newBuf) {
#ifdef TEMP_LOG_DEBUG_ERRORS
        LOG_ERR("[TempLog] ERROR: realloc failed for cache\n");
#endif
        return;  // keep old cache untouched
    }

    cache[sensorIdx0] = newBuf;
    cache[sensorIdx0][cacheCount[sensorIdx0]].dt    = dt;
    cache[sensorIdx0][cacheCount[sensorIdx0]].value = value;
    cacheCount[sensorIdx0] = newCount;

    lastLoggedValue[sensorIdx0] = value;
    lastLoggedValid[sensorIdx0] = true;

#ifdef TEMP_LOG_DEBUG_CACHE
    LOG_CAT(DBG_TEMPLOG, "[TempLog] CACHED %s = %.2fF\n",
            SENSOR_NAMES[sensorIdx0 + 1], value);
#endif
}

// ---------------------------------------------------------------------------
// Flush all caches to flash (called hourly and at midnight)
// ---------------------------------------------------------------------------
static void flushCache() {
    uint32_t nowMs = millis();

    // Gate time value since last flush (Config.h)
    if (nowMs - lastFlushMsGlobal < (TEMP_LOG_SAMPLE_SEC * 1000UL)) {
#ifdef TEMP_LOG_DEBUG_FLUSH
        LOG_CAT(DBG_TEMPLOG, "[TempLog] Flush skipped -- less than 1 min since last\n");
#endif
        return;
    }

    for (int i = 0; i < NUM_SENSORS; i++) {
        if (cacheCount[i] == 0 || cache[i] == nullptr) {
            continue;
        }

        if (!takeFileSystemMutexWithRetry("[TempLog] flushCache",
                                          pdMS_TO_TICKS(2000), 3)) {
#ifdef TEMP_LOG_DEBUG_ERRORS
            LOG_ERR("[TempLog] ERROR: mutex timeout in flushCache for sensor %d\n", i + 1);
#endif
            continue;
        }

        String currentPath;
        File   f;

        for (int j = 0; j < cacheCount[i]; j++) {
            DateTime dt = cache[i][j].dt;

            int year   = dt.year();
            int month  = dt.month();
            int day    = dt.day();
            int hour   = dt.hour();
            int minute = dt.minute();

            String path = buildFilePath(year, month, day, i + 1);

            // If we changed day/file, close the old one and open the new one
            if (!f || path != currentPath) {
                if (f) {
                    f.close();
                }
                currentPath = path;

                // Make sure directory hierarchy exists
                if (!ensureSensorDir(year, month, i + 1)) {
#ifdef TEMP_LOG_DEBUG_ERRORS
                    LOG_ERR("[TempLog] ERROR: ensureSensorDir failed for %s\n", currentPath.c_str());
#endif
                    break;
                }

                f = LittleFS.open(currentPath, "a");
                if (!f) {
#ifdef TEMP_LOG_DEBUG_ERRORS
                    LOG_ERR("[TempLog] ERROR: cannot open %s for flush\n", currentPath.c_str());
#endif
                    break;
                }
            }

            char line[64];
            snprintf(line, sizeof(line),
                     "%04d-%02d-%02d,%02d:%02d:00,%.2f\n",
                     year, month, day,
                     hour, minute,
                     cache[i][j].value);

            f.print(line);

            lastWrittenToFlash[i] = cache[i][j].value;
            lastWrittenDay[i]     = day;

            esp_task_wdt_reset();
            vTaskDelay(1);   // yield
        }

        if (f) {
            f.close();
        }

        xSemaphoreGive(fileSystemMutex);

        free(cache[i]);
        cache[i]      = nullptr;
        cacheCount[i] = 0;

#ifdef TEMP_LOG_DEBUG_FLUSH
        LOG_CAT(DBG_TEMPLOG, "[TempLog] Flushed cache for sensor %d (%s)\n",
                i + 1, SENSOR_NAMES[i + 1]);
#endif
    }

    lastFlushMsGlobal = millis();  // Update global last flush time on success

#ifdef TEMP_LOG_DEBUG_FLUSH
    LOG_CAT(DBG_TEMPLOG, "[TempLog] Cache flush complete\n");
#endif
}

// ---------------------------------------------------------------------------
// Setup — called once at boot
// ---------------------------------------------------------------------------
void setupTemperatureLogging() {
    if (takeFileSystemMutexWithRetry("[TempLog] setupTemperatureLogging",
                                     pdMS_TO_TICKS(2000), 3)) {
        if (!LittleFS.exists(BASE_DIR)) {
            LittleFS.mkdir(BASE_DIR);
        }
        xSemaphoreGive(fileSystemMutex);
    } else {
        LOG_ERR("[TempLog] setupTemperatureLogging: failed to lock FS mutex, skipping BASE_DIR creation\n");
    }

    lastSampleMs    = millis();
    lastFlushMs     = millis();
    baselineWritten = false;   // ensure we’ll do a baseline later
}

// ---------------------------------------------------------------------------
// Main task — runs forever
// ---------------------------------------------------------------------------
void TaskTemperatureLogging_Run(void*)
{
    LOG_CAT(DBG_TEMPLOG, "[TempLog] Attempting to start Temperature Logging...\n");

    for (;;) {
        esp_task_wdt_reset();

        // 1) Don’t do anything until we’re explicitly enabled
        if (!g_tempLogEnabled) {
            LOG_CAT(DBG_TEMPLOG, "[TempLog] Waiting for g_tempLogEnabled...\n");
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        // 2) Don’t do anything until LittleFS is mounted
        if (!g_fileSystemReady) {
            LOG_CAT(DBG_TEMPLOG, "[TempLog] Waiting for filesystem...\n");
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        // 3) Don’t do anything until time is valid and sane
        DateTime dt = getCurrentTimeAtomic();
        if (!g_timeValid) {
            int year = dt.year();
            if (year < 2025 || year > 2100) {
                LOG_CAT(DBG_TEMPLOG,
                        "[TempLog] Waiting for valid time, g_timeValid=0, RTC year=%d\n",
                        year);
                vTaskDelay(pdMS_TO_TICKS(5000));
                continue;   // stay alive, keep waiting
            } else {
                LOG_CAT(DBG_TEMPLOG,
                        "[TempLog] RTC year %d looks valid, enabling time.\n", year);
                markTimeValid();   // let the rest of the system know time is OK
            }
        }

        // 4) Respect the sample interval (based on millis, not wall clock)
        uint32_t nowMs = millis();
        if (nowMs - lastSampleMs < (TEMP_LOG_SAMPLE_SEC * 1000UL)) {
            vTaskDelay(pdMS_TO_TICKS(200));  // small yield, don’t hammer CPU
            continue;
        }
        lastSampleMs = nowMs;

        // 5) One-time baseline if not done yet
        if (!baselineWritten) {
            for (int i = 0; i < NUM_SENSORS; i++) {
                float v = getValue(i);
                if (!isnan(v)) {
                    writeToFlash(i, dt, v);  // dt = current RTC time
                }
            }
            baselineWritten = true;
            LOG_CAT(DBG_TEMPLOG, "[TempLog] Baseline values written after valid time acquired\n");
        }

        // 6) Hourly flush (still driven by millis)
        if (nowMs - lastFlushMs >= (TEMP_LOG_FLUSH_MIN * 60UL * 1000UL)) {
            flushCache();
            lastFlushMs = nowMs;
        }

        // 7) Midnight rollover using RTC date/time directly
        static int lastDay = -1;
        int hour   = dt.hour();
        int minute = dt.minute();
        int day    = dt.day();

        if (hour == 0 && minute < 5 && day != lastDay) {
            // Step 1: flush all cache entries for the old day
            flushCache();

            // Step 2: write current value as the first point of the new day
            DateTime dt2 = getCurrentTimeAtomic();  // fresh timestamp
            for (int i = 0; i < NUM_SENSORS; i++) {
                float v = getValue(i);
                if (!isnan(v)) {
                    writeToFlash(i, dt2, v);
                }
            }

            lastDay = day;
        }

        // 8) Delta check — compare against last value **written to flash**
        for (int i = 0; i < NUM_SENSORS; i++) {
            float current = getValue(i);
            if (isnan(current)) continue;

            uint32_t msNow = millis();
            bool timeElapsed = (msNow - lastAddMs[i] >= (TEMP_LOG_SAMPLE_SEC * 1000UL));
            bool changed = fabsf(current - lastWrittenToFlash[i]) >= TEMP_LOG_DELTA_F;

            // Add if changed OR (cache empty AND 1 min elapsed)
            if (changed || (cacheCount[i] == 0 && timeElapsed)) {
                addToCache(i, current);
                lastAddMs[i] = msNow;
            }
        }

        // Small yield so we don’t hog CPU
        vTaskDelay(1);
    }
}
