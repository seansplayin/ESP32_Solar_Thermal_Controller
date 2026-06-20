// Config.cpp
#include "Config.h"
#include <ArduinoJson.h>
#include "FileSystemManager.h"
#include <LittleFS.h>
#include <string.h>
#include <stdlib.h>
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
Ds18B20Config g_ds18b20Config;

float getTempByIndex(int idx) {
  if (idx < 1 || idx > 14) return NAN;
  switch (idx) {
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
    default: return NAN;
  }
}

static void copyConfigString(char* dst, size_t dstLen, const char* src) {
  if (!dst || dstLen == 0) return;
  if (!src) src = "";
  strncpy(dst, src, dstLen - 1);
  dst[dstLen - 1] = '\0';
}

static void setDs18B20Assignment(uint8_t slot,
                                 uint64_t rom,
                                 const char* systemTemp,
                                 uint8_t bus,
                                 bool enabled,
                                 float offsetF) {
  if (slot >= DS18B20_ASSIGNMENT_COUNT) return;
  g_ds18b20Config.assignments[slot].rom = rom;
  copyConfigString(g_ds18b20Config.assignments[slot].systemTemp,
                   sizeof(g_ds18b20Config.assignments[slot].systemTemp),
                   systemTemp);
  g_ds18b20Config.assignments[slot].bus = (bus == 1) ? 1 : 2;
  g_ds18b20Config.assignments[slot].enabled = enabled;
  g_ds18b20Config.assignments[slot].offsetF = offsetF;
}

bool isValidSystemTempName(const char* systemTemp) {
  if (!systemTemp || systemTemp[0] == '\0') return false;

  static const char* const validNames[] = {
    "panelT",
    "CSupplyT",
    "storageT",
    "outsideT",
    "CircReturnT",
    "supplyT",
    "CreturnT",
    "DhwSupplyT",
    "DhwReturnT",
    "HeatingSupplyT",
    "HeatingReturnT",
    "dhwT",
    "PotHeatXinletT",
    "PotHeatXoutletT"
  };

  for (size_t i = 0; i < (sizeof(validNames) / sizeof(validNames[0])); i++) {
    if (strcmp(systemTemp, validNames[i]) == 0) return true;
  }
  return false;
}

String ds18b20RomToString(uint64_t rom) {
  char buf[24];
  snprintf(buf, sizeof(buf), "0x%llx", (unsigned long long)rom);
  return String(buf);
}

bool parseDs18b20RomString(const char* text, uint64_t& romOut) {
  if (!text) return false;

  while (*text == ' ' || *text == '	') text++;
  if (text[0] == '\0') return false;

  if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
    text += 2;
  }

  char* endPtr = nullptr;
  uint64_t parsed = strtoull(text, &endPtr, 16);
  if (endPtr == text) return false;

  while (*endPtr == ' ' || *endPtr == '	') endPtr++;
  if (*endPtr != '\0') return false;

  // DS18B20 family code is 0x28 in the low byte with the ROM formatting used here.
  if (parsed != 0ULL && ((parsed & 0xFFULL) != 0x28ULL)) return false;

  romOut = parsed;
  return true;
}

int findDs18B20AssignmentByRom(uint64_t rom) {
  if (rom == 0ULL) return -1;

  for (uint8_t i = 0; i < DS18B20_ASSIGNMENT_COUNT; i++) {
    if (!g_ds18b20Config.assignments[i].enabled) continue;
    if (g_ds18b20Config.assignments[i].rom == rom) return i;
  }
  return -1;
}

int findDs18B20AssignmentBySystemTemp(const char* systemTemp) {
  if (!systemTemp) return -1;

  for (uint8_t i = 0; i < DS18B20_ASSIGNMENT_COUNT; i++) {
    if (!g_ds18b20Config.assignments[i].enabled) continue;
    if (strcmp(g_ds18b20Config.assignments[i].systemTemp, systemTemp) == 0) return i;
  }
  return -1;
}

static float getDs18B20ValueForSystemTemp(const char* systemTemp, float fallbackValue) {
  int slot = findDs18B20AssignmentBySystemTemp(systemTemp);
  if (slot < 0 || slot >= DS18B20_ASSIGNMENT_COUNT) return fallbackValue;
  return DTempAverage[slot];
}

void applyConfiguredSystemTemperatureAssignments() {
  // Default collector manifold source remains PT1000 unless a DS18B20 assignment
  // explicitly maps one of the configured DS18B20 slots to panelT.
  panelT          = getDs18B20ValueForSystemTemp("panelT",          pt1000Average);
  CSupplyT        = getDs18B20ValueForSystemTemp("CSupplyT",        NAN);
  storageT        = getDs18B20ValueForSystemTemp("storageT",        NAN);
  outsideT        = getDs18B20ValueForSystemTemp("outsideT",        NAN);
  CircReturnT     = getDs18B20ValueForSystemTemp("CircReturnT",     NAN);
  supplyT         = getDs18B20ValueForSystemTemp("supplyT",         NAN);
  CreturnT        = getDs18B20ValueForSystemTemp("CreturnT",        NAN);
  DhwSupplyT      = getDs18B20ValueForSystemTemp("DhwSupplyT",      NAN);
  DhwReturnT      = getDs18B20ValueForSystemTemp("DhwReturnT",      NAN);
  HeatingSupplyT  = getDs18B20ValueForSystemTemp("HeatingSupplyT",  NAN);
  HeatingReturnT  = getDs18B20ValueForSystemTemp("HeatingReturnT",  NAN);
  dhwT            = getDs18B20ValueForSystemTemp("dhwT",            NAN);
  PotHeatXinletT  = getDs18B20ValueForSystemTemp("PotHeatXinletT",  NAN);
  PotHeatXoutletT = getDs18B20ValueForSystemTemp("PotHeatXoutletT", NAN);
}

void initDs18B20ConfigDefaults() {
  g_ds18b20Config.version = 1;

  setDs18B20Assignment(0,  0x3f3c910457bbd028ULL, "CSupplyT",        2, true, 0.0f);
  setDs18B20Assignment(1,  0xa13c690457350428ULL, "storageT",        2, true, 0.0f);
  setDs18B20Assignment(2,  0x13cf60457fee428ULL,  "outsideT",        2, true, 0.0f);
  setDs18B20Assignment(3,  0x770722b2275a8c28ULL, "CircReturnT",     2, true, 0.0f);
  setDs18B20Assignment(4,  0xe23c350457fddc28ULL, "supplyT",         2, true, 0.0f);
  setDs18B20Assignment(5,  0xa13ca704574f5d28ULL, "CreturnT",        2, true, 0.0f);
  setDs18B20Assignment(6,  0x2b3c54045745c028ULL, "DhwSupplyT",      1, true, 0.0f);
  setDs18B20Assignment(7,  0x3c6fe381c97c28ULL,   "DhwReturnT",      1, true, 0.0f);
  setDs18B20Assignment(8,  0xfa3cc80457e29e28ULL, "HeatingSupplyT",  1, true, 0.0f);
  setDs18B20Assignment(9,  0x753ccdf64815f128ULL, "HeatingReturnT",  1, true, 0.0f);
  setDs18B20Assignment(10, 0xa23c330457d1fb28ULL, "dhwT",            1, true, 0.0f);
  setDs18B20Assignment(11, 0x963cf2045776e728ULL, "PotHeatXinletT",  1, true, 0.0f);
  setDs18B20Assignment(12, 0xe80722b24856bf28ULL, "PotHeatXoutletT", 1, true, 0.0f);
}

bool loadDs18B20ConfigFromFS() {
  if (!g_fileSystemReady) return false;

  if (!takeFileSystemMutexWithRetry("[DS18B20Config] load", pdMS_TO_TICKS(1000), 2)) return false;

  if (!LittleFS.exists(DS18B20_CONFIG_PATH)) {
    xSemaphoreGive(fileSystemMutex);
    return false;
  }

  File f = LittleFS.open(DS18B20_CONFIG_PATH, "r");
  if (!f) {
    xSemaphoreGive(fileSystemMutex);
    return false;
  }

  DynamicJsonDocument doc(3072);
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  xSemaphoreGive(fileSystemMutex);

  if (err) return false;

  JsonArray assignments = doc["assignments"];
  if (assignments.isNull()) return false;

  // Keep compile-time defaults for any invalid or missing row.
  for (JsonVariant v : assignments) {
    int slot = v["slot"] | -1;
    if (slot < 0 || slot >= DS18B20_ASSIGNMENT_COUNT) continue;

    const char* systemTemp = v["systemTemp"] | "";
    if (!isValidSystemTempName(systemTemp)) continue;

    const char* romText = v["rom"] | "";
    uint64_t rom = 0ULL;
    if (!parseDs18b20RomString(romText, rom)) continue;

    uint8_t bus = v["bus"] | g_ds18b20Config.assignments[slot].bus;
    if (bus != 1 && bus != 2) continue;

    bool enabled = v["enabled"] | true;
    float offsetF = v["offsetF"] | 0.0f;

    setDs18B20Assignment((uint8_t)slot, rom, systemTemp, bus, enabled, offsetF);
  }

  return true;
}

bool saveDs18B20ConfigToFS() {
  if (!g_fileSystemReady) return false;

  if (!takeFileSystemMutexWithRetry("[DS18B20Config] save", pdMS_TO_TICKS(1000), 2)) return false;

  if (!LittleFS.exists(DIAG_SERIAL_CONFIG_DIR)) {
    LittleFS.mkdir(DIAG_SERIAL_CONFIG_DIR);
  }

  DynamicJsonDocument doc(3072);
  doc["version"] = g_ds18b20Config.version;

  JsonArray assignments = doc.createNestedArray("assignments");
  for (uint8_t i = 0; i < DS18B20_ASSIGNMENT_COUNT; i++) {
    JsonObject row = assignments.createNestedObject();
    row["slot"] = i;
    row["rom"] = ds18b20RomToString(g_ds18b20Config.assignments[i].rom);
    row["systemTemp"] = g_ds18b20Config.assignments[i].systemTemp;
    row["bus"] = g_ds18b20Config.assignments[i].bus;
    row["enabled"] = g_ds18b20Config.assignments[i].enabled;
    row["offsetF"] = g_ds18b20Config.assignments[i].offsetF;
  }

  File f = LittleFS.open(DS18B20_CONFIG_PATH, "w");
  if (!f) {
    xSemaphoreGive(fileSystemMutex);
    return false;
  }

  serializeJson(doc, f);
  f.close();

  xSemaphoreGive(fileSystemMutex);
  return true;
}

bool resetDs18B20ConfigToDefaults() {
  initDs18B20ConfigDefaults();
  return saveDs18B20ConfigToFS();
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

  if (!LittleFS.exists("/Json_Config_Files")) {
    LittleFS.mkdir("/Json_Config_Files");
  }

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

  if (!LittleFS.exists("/Json_Config_Files")) {
    LittleFS.mkdir("/Json_Config_Files");
  }

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

