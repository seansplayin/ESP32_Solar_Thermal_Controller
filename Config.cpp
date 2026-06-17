#include "Config.h"
#include "FileSystemManager.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

SystemConfig g_config;

// We already have this in FileSystemManager.cpp:
extern bool g_fileSystemReady;
extern SemaphoreHandle_t fileSystemMutex;

void initSystemConfigDefaults() {
    g_config.panelTminimumValue   = DEFAULT_panelTminimum;
    g_config.panelOnDifferential  = DEFAULT_PanelOnDifferential;
    g_config.panelLowDifferential = DEFAULT_PanelLowDifferential;
    g_config.panelOffDifferential = DEFAULT_PanelOffDifferential;
    g_config.boilerCircOn         = DEFAULT_Boiler_Circ_On;
    g_config.boilerCircOff        = DEFAULT_Boiler_Circ_Off;
    g_config.storageHeatingLimit  = DEFAULT_StorageHeatingLimit;
    g_config.circPumpOn           = DEFAULT_Circ_Pump_On;
    g_config.circPumpOff          = DEFAULT_Circ_Pump_Off;
    g_config.heatTapeOn  = DEFAULT_Heat_Tape_On;
    g_config.heatTapeOff = DEFAULT_Heat_Tape_Off;

    // ---------------- Freeze Protection defaults ----------------
g_config.collectorFreezeTempF      = DEFAULT_CollectorFreezeTempF;
g_config.collectorFreezeConfirmMin = DEFAULT_CollectorFreezeConfirmMin;
g_config.collectorFreezeRunMin     = DEFAULT_CollectorFreezeRunMin;

g_config.circFreezeTempF           = DEFAULT_CircFreezeTempF;
g_config.circFreezeConfirmMin      = DEFAULT_CircFreezeConfirmMin;
g_config.circFreezeRunMin          = DEFAULT_CircFreezeRunMin;

g_config.heatTapeBadF              = DEFAULT_HeatTapeBadF;
g_config.heatTapeClearF            = DEFAULT_HeatTapeClearF;
g_config.heatTapeEvalMin           = DEFAULT_HeatTapeEvalMin;

g_config.tankFreezeTempF           = DEFAULT_TankFreezeTempF;
g_config.tankFreezeClearF          = DEFAULT_TankFreezeClearF;
g_config.tankFreezeConfirmMin      = DEFAULT_TankFreezeConfirmMin;

   

}



static const char* CONFIG_PATH = "/system_config.json";


// ---- TimeConfig globals (new) ----
TimeConfig g_timeConfig;
static const char* TIME_CONFIG_PATH = "/time_config.json";

void initTimeConfigDefaults() {
    g_timeConfig.timeZoneId = DEFAULT_TIMEZONE_ID;
    g_timeConfig.dstEnabled = DEFAULT_DST_ENABLED;
}



bool loadSystemConfigFromFS() {
    if (!g_fileSystemReady) {
        Serial.println("[Config] FS not ready, skipping loadSystemConfigFromFS()");
        return false;
    }

    if (!takeFileSystemMutexWithRetry("[Config] loadSystemConfigFromFS",
                                      pdMS_TO_TICKS(2000), 3)) {
        Serial.println("[Config] Failed to lock FS mutex for load after retries");
        return false;
    }

    if (!LittleFS.exists(CONFIG_PATH)) {
        xSemaphoreGive(fileSystemMutex);
        Serial.println("[Config] No system_config.json, using defaults");
        return false;
    }

    File f = LittleFS.open(CONFIG_PATH, "r");
    if (!f) {
        xSemaphoreGive(fileSystemMutex);
        Serial.println("[Config] Failed to open system_config.json");
        return false;
    }

    DynamicJsonDocument doc(2048);
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    xSemaphoreGive(fileSystemMutex);

    if (err) {
        Serial.print("[Config] JSON parse error: ");
        Serial.println(err.c_str());
        return false;
    }

    auto loadFloat = [&](const char* key, float& target) {
        if (doc.containsKey(key)) {
            target = doc[key].as<float>();
        }
    }; // <-- DO NOT lose this

    // Existing loads
    loadFloat("panelTminimumValue",   g_config.panelTminimumValue);
    loadFloat("PanelOnDifferential",  g_config.panelOnDifferential);
    loadFloat("PanelLowDifferential", g_config.panelLowDifferential);
    loadFloat("PanelOffDifferential", g_config.panelOffDifferential);
    loadFloat("Boiler_Circ_On",       g_config.boilerCircOn);
    loadFloat("Boiler_Circ_Off",      g_config.boilerCircOff);
    loadFloat("StorageHeatingLimit",  g_config.storageHeatingLimit);
    loadFloat("Circ_Pump_On",         g_config.circPumpOn);
    loadFloat("Circ_Pump_Off",        g_config.circPumpOff);
    loadFloat("Heat_Tape_On",         g_config.heatTapeOn);
    loadFloat("Heat_Tape_Off",        g_config.heatTapeOff);

    // Freeze Protection loads (missing key -> keep default)
    g_config.collectorFreezeTempF      = doc["collectorFreezeTempF"]      | g_config.collectorFreezeTempF;
    g_config.collectorFreezeConfirmMin = (uint16_t)(doc["collectorFreezeConfirmMin"] | g_config.collectorFreezeConfirmMin);
    g_config.collectorFreezeRunMin     = (uint16_t)(doc["collectorFreezeRunMin"]     | g_config.collectorFreezeRunMin);

    g_config.circFreezeTempF           = doc["circFreezeTempF"]           | g_config.circFreezeTempF;
    g_config.circFreezeConfirmMin      = (uint16_t)(doc["circFreezeConfirmMin"]      | g_config.circFreezeConfirmMin);
    g_config.circFreezeRunMin          = (uint16_t)(doc["circFreezeRunMin"]          | g_config.circFreezeRunMin);

    g_config.heatTapeBadF              = doc["heatTapeBadF"]              | g_config.heatTapeBadF;
    g_config.heatTapeClearF            = doc["heatTapeClearF"]            | g_config.heatTapeClearF;
    g_config.heatTapeEvalMin           = (uint16_t)(doc["heatTapeEvalMin"]           | g_config.heatTapeEvalMin);

    g_config.tankFreezeTempF           = doc["tankFreezeTempF"]           | g_config.tankFreezeTempF;
    g_config.tankFreezeClearF          = doc["tankFreezeClearF"]          | g_config.tankFreezeClearF;
    g_config.tankFreezeConfirmMin      = (uint16_t)(doc["tankFreezeConfirmMin"]      | g_config.tankFreezeConfirmMin);

    // Clamp ranges (recommended)
    auto clampU16 = [](uint16_t v, uint16_t lo, uint16_t hi){ return (v<lo)?lo:(v>hi)?hi:v; };
    auto clampF   = [](float v, float lo, float hi){ return (v<lo)?lo:(v>hi)?hi:v; };

    g_config.collectorFreezeTempF      = clampF(g_config.collectorFreezeTempF, 20.0f, 60.0f);
    g_config.collectorFreezeConfirmMin = clampU16(g_config.collectorFreezeConfirmMin, 1, 120);
    g_config.collectorFreezeRunMin     = clampU16(g_config.collectorFreezeRunMin, 1, 120);

    g_config.circFreezeTempF           = clampF(g_config.circFreezeTempF, 20.0f, 60.0f);
    g_config.circFreezeConfirmMin      = clampU16(g_config.circFreezeConfirmMin, 1, 120);
    g_config.circFreezeRunMin          = clampU16(g_config.circFreezeRunMin, 1, 120);

    g_config.heatTapeBadF              = clampF(g_config.heatTapeBadF, 20.0f, 60.0f);
    g_config.heatTapeClearF            = clampF(g_config.heatTapeClearF, 20.0f, 60.0f);
    g_config.heatTapeEvalMin           = clampU16(g_config.heatTapeEvalMin, 1, 120);

    g_config.tankFreezeTempF           = clampF(g_config.tankFreezeTempF, 20.0f, 60.0f);
    g_config.tankFreezeClearF          = clampF(g_config.tankFreezeClearF, 20.0f, 80.0f);
    g_config.tankFreezeConfirmMin      = clampU16(g_config.tankFreezeConfirmMin, 1, 240);

    Serial.println("[Config] Loaded system_config.json");
    return true;
}


 bool saveSystemConfigToFS() {
    if (!g_fileSystemReady) {
        Serial.println("[Config] FS not ready, cannot save config");
        return false;
    }

    if (!takeFileSystemMutexWithRetry("[Config] saveSystemConfigToFS",
                                      pdMS_TO_TICKS(5000), 3)) {
        Serial.println("[Config] Failed to lock FS mutex for save after retries");
        return false;
    }

    DynamicJsonDocument doc(2048);
    doc["panelTminimumValue"]   = g_config.panelTminimumValue;
    doc["PanelOnDifferential"]  = g_config.panelOnDifferential;
    doc["PanelLowDifferential"] = g_config.panelLowDifferential;
    doc["PanelOffDifferential"] = g_config.panelOffDifferential;
    doc["Boiler_Circ_On"]       = g_config.boilerCircOn;
    doc["Boiler_Circ_Off"]      = g_config.boilerCircOff;
    doc["StorageHeatingLimit"]  = g_config.storageHeatingLimit;
    doc["Circ_Pump_On"]         = g_config.circPumpOn;
    doc["Circ_Pump_Off"]        = g_config.circPumpOff;
    doc["Heat_Tape_On"]         = g_config.heatTapeOn;
    doc["Heat_Tape_Off"]        = g_config.heatTapeOff;

    // ---------------- Freeze Protection saves (NEW) ----------------
    doc["collectorFreezeTempF"]      = g_config.collectorFreezeTempF;
    doc["collectorFreezeConfirmMin"] = g_config.collectorFreezeConfirmMin;
    doc["collectorFreezeRunMin"]     = g_config.collectorFreezeRunMin;

    doc["circFreezeTempF"]           = g_config.circFreezeTempF;
    doc["circFreezeConfirmMin"]      = g_config.circFreezeConfirmMin;
    doc["circFreezeRunMin"]          = g_config.circFreezeRunMin;

    doc["heatTapeBadF"]              = g_config.heatTapeBadF;
    doc["heatTapeClearF"]            = g_config.heatTapeClearF;
    doc["heatTapeEvalMin"]           = g_config.heatTapeEvalMin;

    doc["tankFreezeTempF"]           = g_config.tankFreezeTempF;
    doc["tankFreezeClearF"]          = g_config.tankFreezeClearF;
    doc["tankFreezeConfirmMin"]      = g_config.tankFreezeConfirmMin;
    // ----------------------------------------------------------------

    File f = LittleFS.open(CONFIG_PATH, "w");
    
    if (!f) {
        Serial.println("[Config] Failed to open system_config.json for write");
        xSemaphoreGive(fileSystemMutex);
        return false;
    }


    if (serializeJson(doc, f) == 0) {
        Serial.println("[Config] Failed to write config JSON");
        f.close();
        xSemaphoreGive(fileSystemMutex);
        return false;
    }

    f.close();
    xSemaphoreGive(fileSystemMutex);
    Serial.println("[Config] Saved system_config.json");
    return true;
}

bool resetSystemConfigToDefaults() {
    // 1) Re-apply your inline constexpr defaults from Config.h
    initSystemConfigDefaults();

    // 2) Persist to /system_config.json so it survives reboot
    return saveSystemConfigToFS();
}


// ---- TimeConfig: load / save / reset (new) ----

bool loadTimeConfigFromFS() {
    if (!g_fileSystemReady) {
        Serial.println("[TimeConfig] FS not ready, skipping loadTimeConfigFromFS()");
        return false;
    }

    if (!takeFileSystemMutexWithRetry("[TimeConfig] loadTimeConfigFromFS",
                                      pdMS_TO_TICKS(2000), 3)) {
        Serial.println("[TimeConfig] Failed to lock FS mutex for load after retries");
        return false;
    }

    if (!LittleFS.exists(TIME_CONFIG_PATH)) {
        xSemaphoreGive(fileSystemMutex);
        Serial.println("[TimeConfig] No time_config.json, using defaults");
        return false;
    }

    File f = LittleFS.open(TIME_CONFIG_PATH, "r");

    if (!f) {
        xSemaphoreGive(fileSystemMutex);
        Serial.println("[TimeConfig] Failed to open time_config.json");
        return false;
    }

    DynamicJsonDocument doc(512);
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    xSemaphoreGive(fileSystemMutex);

    if (err) {
        Serial.print("[TimeConfig] JSON parse error: ");
        Serial.println(err.c_str());
        return false;
    }

    if (doc.containsKey("timeZoneId")) {
        g_timeConfig.timeZoneId = String(doc["timeZoneId"].as<const char*>());
    }
    if (doc.containsKey("dstEnabled")) {
        g_timeConfig.dstEnabled = doc["dstEnabled"].as<bool>();
    }

    Serial.println("[TimeConfig] Loaded time_config.json");
    return true;
}

bool saveTimeConfigToFS() {
    if (!g_fileSystemReady) {
        Serial.println("[TimeConfig] FS not ready, cannot save time config");
        return false;
    }

    if (!takeFileSystemMutexWithRetry("[TimeConfig] saveTimeConfigToFS",
                                      pdMS_TO_TICKS(5000), 3)) {
        Serial.println("[TimeConfig] Failed to lock FS mutex for TimeConfig save after retries");
        return false;
    }

    DynamicJsonDocument doc(512);
    doc["timeZoneId"]  = g_timeConfig.timeZoneId;
    doc["dstEnabled"]  = g_timeConfig.dstEnabled;

    File f = LittleFS.open(TIME_CONFIG_PATH, "w");

    if (!f) {
        Serial.println("[TimeConfig] Failed to open time_config.json for write");
        xSemaphoreGive(fileSystemMutex);
        return false;
    }

    if (serializeJson(doc, f) == 0) {
        Serial.println("[TimeConfig] Failed to write TimeConfig JSON");
        f.close();
        xSemaphoreGive(fileSystemMutex);
        return false;
    }

    f.close();
    xSemaphoreGive(fileSystemMutex);
    Serial.println("[TimeConfig] Saved time_config.json");
    return true;
}

bool resetTimeConfigToDefaults() {
    initTimeConfigDefaults();
    return saveTimeConfigToFS();
}

// Map current TimeConfig -> POSIX TZ string used by setenv("TZ", ...).
// Currently supports UTC + basic US zones; extend as needed.
String getPosixTimeZoneString() {
    // If user forgot to initialize, fall back to defaults
    if (g_timeConfig.timeZoneId.length() == 0) {
        initTimeConfigDefaults();
    }

    const String id  = g_timeConfig.timeZoneId;
    const bool   dst = g_timeConfig.dstEnabled;

    if (id == "UTC") {
        return "UTC0";
    } else if (id == "US_PACIFIC") {
        return dst ? "PST8PDT,M3.2.0/2,M11.1.0/2"
                   : "PST8";
    } else if (id == "US_MOUNTAIN") {
        return dst ? "MST7MDT,M3.2.0/2,M11.1.0/2"
                   : "MST7";
    } else if (id == "US_CENTRAL") {
        return dst ? "CST6CDT,M3.2.0/2,M11.1.0/2"
                   : "CST6";
    } else if (id == "US_EASTERN") {
        return dst ? "EST5EDT,M3.2.0/2,M11.1.0/2"
                   : "EST5";
    }

    // Fallback if unknown ID
    Serial.print("[TimeConfig] Unknown timeZoneId '");
    Serial.print(id);
    Serial.println("', falling back to UTC");
    return "UTC0";
}
