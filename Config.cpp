// Config.cpp
#include "Config.h"
#include <ArduinoJson.h>
#include "FileSystemManager.h"
#include <LittleFS.h>
#include <string.h>
#include <TarGZ.h>
#include "DiagLog.h"
#include "DiagConfig.h"

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

extern float pt1000Average;

extern float DTempAverage[13];  // Assuming DTempAverage[0..12] for 1-13

SystemConfig g_config;
TimeConfig g_timeConfig;

float getTempByIndex(int idx) {
  if (idx < 1 || idx > 14) return NAN;
  switch (idx) {
    case 1: return pt1000Average;
    case 2: return DTempAverage[0];  // DTemp1Average
    case 3: return DTempAverage[1];  // DTemp2Average
    case 4: return DTempAverage[2];
    case 5: return DTempAverage[4];  // DTemp5Average (skip 3 for DTemp4)
    case 6: return DTempAverage[3];  // DTemp4Average
    case 7: return DTempAverage[5];
    case 8: return DTempAverage[6];
    case 9: return DTempAverage[7];
    case 10: return DTempAverage[8];
    case 11: return DTempAverage[9];
    case 12: return DTempAverage[10];
    case 13: return DTempAverage[11];
    case 14: return DTempAverage[12];
    default: return NAN;
  }
}

void initSystemConfigDefaults() {
  
  g_config.panelTminimumValue = DEFAULT_panelTminimum;
  g_config.panelOnDifferential = DEFAULT_PanelOnDifferential;
  g_config.panelLowDifferential = DEFAULT_PanelLowDifferential;
  g_config.panelOffDifferential = DEFAULT_PanelOffDifferential;
  g_config.boilerCircOn = DEFAULT_Boiler_Circ_On;
  g_config.boilerCircOff = DEFAULT_Boiler_Circ_Off;
  g_config.storageHeatingLimit = DEFAULT_StorageHeatingLimit;
  g_config.circPumpOn = DEFAULT_Circ_Pump_On;
  g_config.circPumpOff = DEFAULT_Circ_Pump_Off;
  g_config.heatTapeOn = DEFAULT_Heat_Tape_On;
  g_config.heatTapeOff = DEFAULT_Heat_Tape_Off;

  // Freeze
  g_config.collectorFreezeTempF = DEFAULT_CollectorFreezeTempF;
  g_config.collectorFreezeConfirmMin = DEFAULT_CollectorFreezeConfirmMin;
  g_config.collectorFreezeRunMin = DEFAULT_CollectorFreezeRunMin;

  memset(g_config.collectorFreezeSensors, 0, sizeof(g_config.collectorFreezeSensors));
  {
  size_t n = sizeof(DEFAULT_COLLECTOR_FREEZE_SENSORS);
  if (n > sizeof(g_config.collectorFreezeSensors)) n = sizeof(g_config.collectorFreezeSensors);
  memcpy(g_config.collectorFreezeSensors, DEFAULT_COLLECTOR_FREEZE_SENSORS, n);
  }

    // Developement Diagnostic Serial Monitor Outputs
  g_config.diagSerialEnable = (DIAG_SERIAL_DEFAULT_ENABLE != 0);
  g_config.diagSerialMask   = (uint32_t)DIAG_SERIAL_DEFAULT_MASK;



  g_config.lineFreezeTempF = DEFAULT_LineFreezeTempF;
  g_config.lineFreezeConfirmMin = DEFAULT_LineFreezeConfirmMin;

  g_config.lineFreezeRunMin = DEFAULT_LineFreezeRunMin;

  memset(g_config.lineFreezeSensors, 0, sizeof(g_config.lineFreezeSensors));
  {
  size_t n = sizeof(DEFAULT_LINE_FREEZE_SENSORS);
  if (n > sizeof(g_config.lineFreezeSensors)) n = sizeof(g_config.lineFreezeSensors);
  memcpy(g_config.lineFreezeSensors, DEFAULT_LINE_FREEZE_SENSORS, n);
  }

}

bool loadSystemConfigFromFS() {
  if (!g_fileSystemReady) return false;

  if (!takeFileSystemMutexWithRetry("[Config] load", pdMS_TO_TICKS(1000), 2)) return false;

    // One-time migration: move legacy root file into /Json_Config_Files if needed
  if (!LittleFS.exists(SYSTEM_CONFIG_PATH) && LittleFS.exists(SYSTEM_CONFIG_PATH_OLD)) {
    LittleFS.rename(SYSTEM_CONFIG_PATH_OLD, SYSTEM_CONFIG_PATH);
  }

  if (!LittleFS.exists(SYSTEM_CONFIG_PATH)) {
    xSemaphoreGive(fileSystemMutex);
    return false;
  }

  File f = LittleFS.open(SYSTEM_CONFIG_PATH, "r");
  if (!f) { xSemaphoreGive(fileSystemMutex); return false; }


  DynamicJsonDocument doc(1024);
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  xSemaphoreGive(fileSystemMutex);

  if (err) return false;

  g_config.panelTminimumValue = doc["panelTminimum"] | DEFAULT_panelTminimum;
  g_config.panelOnDifferential = doc["PanelOnDifferential"] | DEFAULT_PanelOnDifferential;
  g_config.panelLowDifferential = doc["PanelLowDifferential"] | DEFAULT_PanelLowDifferential;
  g_config.panelOffDifferential = doc["PanelOffDifferential"] | DEFAULT_PanelOffDifferential;
  g_config.boilerCircOn = doc["Boiler_Circ_On"] | DEFAULT_Boiler_Circ_On;
  g_config.boilerCircOff = doc["Boiler_Circ_Off"] | DEFAULT_Boiler_Circ_Off;
  g_config.storageHeatingLimit = doc["StorageHeatingLimit"] | DEFAULT_StorageHeatingLimit;
  g_config.circPumpOn = doc["Circ_Pump_On"] | DEFAULT_Circ_Pump_On;
  g_config.circPumpOff = doc["Circ_Pump_Off"] | DEFAULT_Circ_Pump_Off;
  g_config.heatTapeOn = doc["Heat_Tape_On"] | DEFAULT_Heat_Tape_On;
  g_config.heatTapeOff = doc["Heat_Tape_Off"] | DEFAULT_Heat_Tape_Off;

  g_config.collectorFreezeTempF = doc["collectorFreezeTempF"] | DEFAULT_CollectorFreezeTempF;
  g_config.collectorFreezeConfirmMin = doc["collectorFreezeConfirmMin"] | DEFAULT_CollectorFreezeConfirmMin;
  g_config.collectorFreezeRunMin = doc["collectorFreezeRunMin"] | DEFAULT_CollectorFreezeRunMin;

  g_config.lineFreezeTempF = doc["lineFreezeTempF"] | DEFAULT_LineFreezeTempF;
  g_config.lineFreezeConfirmMin = doc["lineFreezeConfirmMin"] | DEFAULT_LineFreezeConfirmMin;
  g_config.lineFreezeRunMin = doc["lineFreezeRunMin"] | DEFAULT_LineFreezeRunMin;

  JsonArray cfSensors = doc["collectorFreezeSensors"];
  int i = 0;
  for (JsonVariant v : cfSensors) {
    if (i >= 14) break;
    uint8_t s = v.as<uint8_t>();
    if (s >= 1 && s <= 14) g_config.collectorFreezeSensors[i++] = s;
  }
  g_config.collectorFreezeSensors[i] = 0;

  JsonArray lfSensors = doc["lineFreezeSensors"];
  i = 0;
  for (JsonVariant v : lfSensors) {
    if (i >= 14) break;
    uint8_t s = v.as<uint8_t>();
    if (s >= 1 && s <= 14) g_config.lineFreezeSensors[i++] = s;
  }
  g_config.lineFreezeSensors[i] = 0;

  if (doc.containsKey("diagSerialEnable")) g_config.diagSerialEnable = doc["diagSerialEnable"].as<bool>();
  if (doc.containsKey("diagSerialMask"))   g_config.diagSerialMask   = doc["diagSerialMask"].as<uint32_t>();

  return true;
  
}

bool saveSystemConfigToFS() {
  if (!g_fileSystemReady) return false;

  if (!takeFileSystemMutexWithRetry("[Config] save", pdMS_TO_TICKS(1000), 2)) return false;

  DynamicJsonDocument doc(1024);

  doc["panelTminimum"] = g_config.panelTminimumValue;
  doc["PanelOnDifferential"] = g_config.panelOnDifferential;
  doc["PanelLowDifferential"] = g_config.panelLowDifferential;
  doc["PanelOffDifferential"] = g_config.panelOffDifferential;
  doc["Boiler_Circ_On"] = g_config.boilerCircOn;
  doc["Boiler_Circ_Off"] = g_config.boilerCircOff;
  doc["StorageHeatingLimit"] = g_config.storageHeatingLimit;
  doc["Circ_Pump_On"] = g_config.circPumpOn;
  doc["Circ_Pump_Off"] = g_config.circPumpOff;
  doc["Heat_Tape_On"] = g_config.heatTapeOn;
  doc["Heat_Tape_Off"] = g_config.heatTapeOff;

  doc["collectorFreezeTempF"] = g_config.collectorFreezeTempF;
  doc["collectorFreezeConfirmMin"] = g_config.collectorFreezeConfirmMin;
  doc["collectorFreezeRunMin"] = g_config.collectorFreezeRunMin;

  doc["lineFreezeTempF"] = g_config.lineFreezeTempF;
  doc["lineFreezeConfirmMin"] = g_config.lineFreezeConfirmMin;
  doc["lineFreezeRunMin"] = g_config.lineFreezeRunMin;

  doc["diagSerialEnable"] = g_config.diagSerialEnable;
  doc["diagSerialMask"]   = g_config.diagSerialMask;


  JsonArray cfSensors = doc.createNestedArray("collectorFreezeSensors");
  for (uint8_t* s = g_config.collectorFreezeSensors; *s; s++) cfSensors.add(*s);

  JsonArray lfSensors = doc.createNestedArray("lineFreezeSensors");
  for (uint8_t* s = g_config.lineFreezeSensors; *s; s++) lfSensors.add(*s);

    File f = LittleFS.open(SYSTEM_CONFIG_PATH, "w");
  if (!f) { xSemaphoreGive(fileSystemMutex); return false; }
  serializeJson(doc, f);
  f.close();

  xSemaphoreGive(fileSystemMutex);
  return true;
}

bool resetSystemConfigToDefaults() {
  initSystemConfigDefaults();
  return saveSystemConfigToFS();
}

void initTimeConfigDefaults() {
  g_timeConfig.timeZoneId = DEFAULT_TIMEZONE_ID;
  g_timeConfig.dstEnabled = DEFAULT_DST_ENABLED;
}

bool loadTimeConfigFromFS() {
  if (!g_fileSystemReady) return false;

  if (!takeFileSystemMutexWithRetry("[TimeConfig] load", pdMS_TO_TICKS(1000), 2)) return false;

    // One-time migration: move legacy root file into /Json_Config_Files if needed
  if (!LittleFS.exists(TIME_CONFIG_PATH) && LittleFS.exists(TIME_CONFIG_PATH_OLD)) {
    LittleFS.rename(TIME_CONFIG_PATH_OLD, TIME_CONFIG_PATH);
  }

  if (!LittleFS.exists(TIME_CONFIG_PATH)) {
    xSemaphoreGive(fileSystemMutex);
    return false;
  }

  File f = LittleFS.open(TIME_CONFIG_PATH, "r");
  if (!f) { xSemaphoreGive(fileSystemMutex); return false; }


  DynamicJsonDocument doc(256);
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  xSemaphoreGive(fileSystemMutex);

  if (err) return false;

  g_timeConfig.timeZoneId = doc["timeZoneId"] | DEFAULT_TIMEZONE_ID;
  g_timeConfig.dstEnabled = doc["dstEnabled"] | DEFAULT_DST_ENABLED;

  return true;
}

bool saveTimeConfigToFS() {
  if (!g_fileSystemReady) return false;

  if (!takeFileSystemMutexWithRetry("[TimeConfig] save", pdMS_TO_TICKS(1000), 2)) return false;

  DynamicJsonDocument doc(256);
  doc["timeZoneId"] = g_timeConfig.timeZoneId;
  doc["dstEnabled"] = g_timeConfig.dstEnabled;

    File f = LittleFS.open(TIME_CONFIG_PATH, "w");
  if (!f) { xSemaphoreGive(fileSystemMutex); return false; }
  serializeJson(doc, f);
  f.close();

  xSemaphoreGive(fileSystemMutex);
  return true;
}

bool resetTimeConfigToDefaults() {
  initTimeConfigDefaults();
  return saveTimeConfigToFS();
}

String getPosixTimeZoneString() {
  String tz = "";
  if (g_timeConfig.timeZoneId == "UTC") {
    tz = "UTC0";
  } else if (g_timeConfig.timeZoneId == "US_PACIFIC") {
    tz = "PST8PDT,M3.2.0,M11.1.0";
  } else if (g_timeConfig.timeZoneId == "US_MOUNTAIN") {
    tz = "MST7MDT,M3.2.0,M11.1.0";
  } else if (g_timeConfig.timeZoneId == "US_CENTRAL") {
    tz = "CST6CDT,M3.2.0,M11.1.0";
  } else if (g_timeConfig.timeZoneId == "US_EASTERN") {
    tz = "EST5EDT,M3.2.0,M11.1.0";
  }
  if (!g_timeConfig.dstEnabled) {
    // Remove DST rules
    int comma = tz.indexOf(',');
    if (comma != -1) tz = tz.substring(0, comma);
  }
  return tz;
}

bool loadDiagSerialConfigFromFS() {
#if !ENABLE_SERIAL_DIAGNOSTICS
  return false; // compile-time hard mute
#else
  if (!g_fileSystemReady) return false;

  if (!takeFileSystemMutexWithRetry("[Config] loadDiagSerial",
                                    pdMS_TO_TICKS(2000), 3)) {
    return false;
  }

  if (!LittleFS.exists(DIAG_SERIAL_CONFIG_PATH)) {
    xSemaphoreGive(fileSystemMutex);
    return false;
  }

  File f = LittleFS.open(DIAG_SERIAL_CONFIG_PATH, "r");
  if (!f) {
    xSemaphoreGive(fileSystemMutex);
    return false;
  }

  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();

  if (err) {
    xSemaphoreGive(fileSystemMutex);
    return false;
  }

  // Only override if keys exist
  if (doc.containsKey("diagSerialEnable")) g_config.diagSerialEnable = doc["diagSerialEnable"].as<bool>();
  if (doc.containsKey("diagSerialMask"))   g_config.diagSerialMask   = doc["diagSerialMask"].as<uint32_t>();

  xSemaphoreGive(fileSystemMutex);
  return true;
#endif
}

// When this gets implemented from FirstWebpage make certain this does not overwrite the crash detector that calls DBG_ALL after a crash is detected and inadvertently silence the serial prints. 
bool saveDiagSerialConfigToFS() {
#if !ENABLE_SERIAL_DIAGNOSTICS
  return false;
#else
  if (!g_fileSystemReady) return false;

  if (!takeFileSystemMutexWithRetry("[Config] saveDiagSerial",
                                    pdMS_TO_TICKS(2000), 3)) {
    return false;
  }

  // Ensure directory exists
  if (!LittleFS.exists(DIAG_SERIAL_CONFIG_DIR)) {
    LittleFS.mkdir(DIAG_SERIAL_CONFIG_DIR);
  }

  StaticJsonDocument<256> doc;
  doc["diagSerialEnable"] = g_config.diagSerialEnable;
  doc["diagSerialMask"]   = g_config.diagSerialMask;

  File f = LittleFS.open(DIAG_SERIAL_CONFIG_PATH, "w");
  if (!f) { xSemaphoreGive(fileSystemMutex); return false; }

  serializeJson(doc, f);
  f.close();

  xSemaphoreGive(fileSystemMutex);
  return true;
#endif
}

