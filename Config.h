// Config.h
// This file has system cofriguration settings such as Pin number specification for temperature sensor, relays, W5500, Max31865 and temperature values for Auto Mode Pump Operation. A little note pertaining to the arrays in this system.The inside arrays (pumpStates[10], pumpModes[10], pumpPins[10]), pumpNames[10] are all 0-based and the external arrays (filenames, JSON/UI, websocket commands) are all 1-based arrays with conversion “+1 / -1” happening at the boundries. 

#ifndef CONFIG_H
#define CONFIG_H
#include <RTClib.h>
#include <Arduino.h>
#include <FreeRTOS.h>
#include <semphr.h>
#include <stdint.h>
#include "DiagConfig.h"


// -----------------------------------------------------------------------
// Global Serial diagnostics switch
//  0 = compile-time hard mute (field mode) 
//  1 = allow runtime-configured diagnostics (Developement mode) 
// -----------------------------------------------------------------------
#ifndef ENABLE_SERIAL_DIAGNOSTICS
  #define ENABLE_SERIAL_DIAGNOSTICS 1
#endif

// -----------------------------------------------------------------------
// If ENABLE_SERIAL_DIAGNOSTICS == 1:
//   - On boot, g_config.diagSerialEnable defaults to DIAG_SERIAL_DEFAULT_ENABLE
//   - On boot, g_config.diagSerialMask   defaults to DIAG_SERIAL_DEFAULT_MASK
//   - After LittleFS is mounted, if /Json_Config_Files/diag_serial_config.json exists,
//     it can override diagSerialEnable and diagSerialMask (only if keys exist).
//
// If ENABLE_SERIAL_DIAGNOSTICS == 0:
//   - All Serial diagnostics are hard-disabled regardless of filesystem settings.
// -----------------------------------------------------------------------
#ifndef DIAG_SERIAL_DEFAULT_ENABLE
  #define DIAG_SERIAL_DEFAULT_ENABLE 1  // Debug categories selection: 0 = Field installed Mode Field Mask & 1 = Developement Mode Dev Mask
#endif

// -----------------------------------------------------------------------
//   DBG_DEFAULT_FIELD_MASK = serial outputs (DBG_NET | DBG_RTC | DBG_TIMESYNC);
//   DBG_DEFAULT_DEV_MASK = serial outputs (DBG_MEM | DBG_FS | DBG_PUMPLOG | DBG_TEMPLOG | DBG_ALARMLOG | DBG_NET | DBG_RTC | DBG_TIMESYNC | DBG_WEB | DBG_TIMESYNC | DBG_TASK | DBG_CONFIG | DBG_PUMP_RUN_TIME_UI);
//   System default mask when no json file exists at /Json_Config_Files/diag_serial_config.json. These will later expose in FirstWebpage "Edit" menu.
// -----------------------------------------------------------------------
#ifndef DIAG_SERIAL_DEFAULT_MASK
  #define DIAG_SERIAL_DEFAULT_MASK DBG_DEFAULT_DEV_MASK
#endif

// Where runtime diag config lives (separate from system_config/time_config)
#ifndef DIAG_SERIAL_CONFIG_DIR
  #define DIAG_SERIAL_CONFIG_DIR  "/Json_Config_Files"
#endif
#ifndef DIAG_SERIAL_CONFIG_PATH
  #define DIAG_SERIAL_CONFIG_PATH "/Json_Config_Files/diag_serial_config.json"
#endif

// Move other JSON configs into the same folder
#ifndef SYSTEM_CONFIG_PATH
  #define SYSTEM_CONFIG_PATH      "/Json_Config_Files/system_config.json"
#endif
#ifndef TIME_CONFIG_PATH
  #define TIME_CONFIG_PATH        "/Json_Config_Files/time_config.json"
#endif

// Legacy locations (root) for one-time migration
#ifndef SYSTEM_CONFIG_PATH_OLD
  #define SYSTEM_CONFIG_PATH_OLD  "/system_config.json"
#endif
#ifndef TIME_CONFIG_PATH_OLD
  #define TIME_CONFIG_PATH_OLD    "/time_config.json"
#endif
// -----------------------------------------------------------------------




// -----------------------------------------------------------------------
// Mutex handles as extern to be accessible in other files - do not change
// -----------------------------------------------------------------------
extern SemaphoreHandle_t pumpStateMutex;
extern SemaphoreHandle_t temperatureMutex;
extern SemaphoreHandle_t fileSystemMutex;
// -----------------------------------------------------------------------



// -----------------------------------------------------------------------
// Ethernet adapter (W5500) pin configurations
// -----------------------------------------------------------------------
const int W5500_MOSI = 11;
const int W5500_MISO = 13;
const int W5500_SCK = 12;
const int W5500_SS = 10;
const int W5500_INT = 4;
const int W5500_RST = 9;
// -----------------------------------------------------------------------



// -----------------------------------------------------------------------
// Pin numbers for DS3231 Real time clock 
// -----------------------------------------------------------------------
const int pinSDA = 40;
const int pinSCL = 41;
const int sqwPin = 42; 
// -----------------------------------------------------------------------



// -----------------------------------------------------------------------
// Heating Call Input Pins - Calls Furnace/DHW when connected to ground
// -----------------------------------------------------------------------
const int FURNACE_HEATING_PIN = 48;
const int DHW_HEATING_PIN     = 3; // was on gpio 36 and home solar controller will need this wire moved when uploading this sketch.
// -----------------------------------------------------------------------



// -----------------------------------------------------------------------
// pin # for one wire temperature sensors - one buss inside one buss outside
// -----------------------------------------------------------------------
const int ONE_WIRE_BUS_1 = 20; // One Wire Bus for DS18B20 Temperature Sensors 
const int ONE_WIRE_BUS_2 = 21; // One Wire Bus 2 for DS18B20 Temperature Sensors
// -----------------------------------------------------------------------



// -----------------------------------------------------------------------
// Adafruit_MAX31865(5, 19, 45, 18); - Pin specifications for Max31865
// -----------------------------------------------------------------------
#define MAX31865_CS_PIN    5   // spi Cable Select
#define MAX31865_DO_PIN    19  // spi MISO/SDO (Serial Data Out)
#define MAX31865_DI_PIN    45  // spi MOSI/SDI (Serial Data In)
#define MAX31865_CLK_PIN   18  // spi Clock
// -----------------------------------------------------------------------



// -----------------------------------------------------------------------
// Pump state variables - Do not change
// -----------------------------------------------------------------------
#define PUMP_OFF 0
#define PUMP_ON 1
#define PUMP_AUTO 2
// -----------------------------------------------------------------------



// -----------------------------------------------------------------------
// Pump/Relay declarations - Increse if adding more Pumps/Relays
// -----------------------------------------------------------------------
extern int pumpModes[10]; // Mode of each pump
extern int pumpStates[10]; // State of each pump
const int numPumps = 10;
// -----------------------------------------------------------------------



// -----------------------------------------------------------------------
// GPIO Pins for Pumps/Relays
// -----------------------------------------------------------------------
const int PANEL_LEAD_PUMP_RELAY = 6;
const int PANEL_LAG_PUMP_RELAY  = 7;
const int HEAT_TAPE_RELAY       = 14;
const int CIRC_PUMP_RELAY       = 15;
const int DHW_PUMP_RELAY        = 16;
const int STORAGE_HEAT_RELAY    = 17;
const int BOILER_CIRC_RELAY     = 47;
const int RECIRC_VALVE_RELAY    = 8; // was on pin 35 house controller will need this changed when uplaoding this sketch
const int Pump_9_Unused_Relay   = 38;
const int Pump_10_Unused_Relay  = 39;
// -----------------------------------------------------------------------



// -----------------------------------------------------------------------
// Pump/Relay pin definitions
// -----------------------------------------------------------------------
const int pumpPins[10] = {
PANEL_LEAD_PUMP_RELAY,  // Pump 0
PANEL_LAG_PUMP_RELAY,   // Pump 1
HEAT_TAPE_RELAY,        // Pump 2
CIRC_PUMP_RELAY,        // Pump 3
DHW_PUMP_RELAY,         // Pump 4
STORAGE_HEAT_RELAY,     // Pump 5
BOILER_CIRC_RELAY,      // Pump 6
RECIRC_VALVE_RELAY,     // Pump 7
Pump_9_Unused_Relay,    // Pump 8
Pump_10_Unused_Relay    // Pump 9
};
// -----------------------------------------------------------------------



// -----------------------------------------------------------------------
// Operating Parameters for circulation pumps (defaults)
// Single source of truth; used to seed g_config at boot.
// -----------------------------------------------------------------------
inline constexpr float DEFAULT_PanelOnDifferential     = 30.0f;
inline constexpr float DEFAULT_PanelLowDifferential    = 15.0f;
inline constexpr float DEFAULT_PanelOffDifferential    = 3.0f;
inline constexpr float DEFAULT_panelTminimum           = 125.0f;
inline constexpr float DEFAULT_StorageHeatingLimit     = 130.0f;
inline constexpr float DEFAULT_Circ_Pump_On            = 5.0f;  // 5.0f
inline constexpr float DEFAULT_Circ_Pump_Off           = 5.0f;  // 2.0f
inline constexpr float DEFAULT_Heat_Tape_On            = 35.0f;
inline constexpr float DEFAULT_Heat_Tape_Off           = 45.0f;
inline constexpr float DEFAULT_Boiler_Circ_On          = 106.0f;
inline constexpr float DEFAULT_Boiler_Circ_Off         = 110.0f;
// -----------------------------------------------------------------------



// -----------------------------------------------------------------------
// ***** Pump Mode Change Rate Limiting Interval *****
// -----------------------------------------------------------------------
const unsigned long LEAD_RELAY_CHANGE_INTERVAL = 1000; // 1 second
const unsigned long LAG_RELAY_CHANGE_INTERVAL = 5000; // 5 second
const unsigned long HEAT_TAPE_RELAY_CHANGE_INTERVAL = 1000; // 1 second
const unsigned long CIRC_RELAY_CHANGE_INTERVAL = 1000; // 1 second
const unsigned long BOILER_RELAY_CHANGE_INTERVAL = 1000; // 1 second
const unsigned long RECIRC_RELAY_CHANGE_INTERVAL = 1000; // 1 second
const unsigned long DHW_RELAY_CHANGE_INTERVAL = 1000; // 1 second
const unsigned long HEATING_RELAY_CHANGE_INTERVAL = 1000; // 1 second
// -----------------------------------------------------------------------



// -----------------------------------------------------------------------
// TEMPERATURE SENSOR CONFIG (single source of truth)
// -----------------------------------------------------------------------
#define NUM_TEMP_SENSORS 14 // total temp sensors in system 

// SENSOR display names (index 1..14). Index 0 unused.
static const char* SENSOR_NAMES[15] = {
  "", // 0 unused
  "Panel Manifold Temperature (PT1000)",       // 1 panelT -> PT1000Average
  "Collector Supply Temperature (DTemp1)",     // 2 CSupplyT  -> DTemp1Average
  "600 Gal Storage Tank Temperature (DTemp2)", // 3 storageT  -> DTemp2Average
  "Outside Ambient Temperature (DTemp3)",      // 4 outsideT  -> DTemp3Average
  "Circ Loop Return Temperature (DTemp5)",     // 5 CircReturnT -> DTemp5Average
  "Circ Loop Supply Temperature (DTemp4)",     // 6 supplyT -> DTemp4Average
  "Collector Return Temperature (DTemp6)",     // 7 CreturnT -> DTemp6Average
  "DHW Glycol Supply Temperature (DTemp7)",    // 8 DhwSupplyT -> DTemp7Average
  "DHW Glycol Return Temperature (DTemp8)",    // 9 DhwReturnT -> DTemp8Average
  "Furnace Glycol Supply Temperature (DTemp9)",//10 HeatingSupplyT -> DTemp9Average
  "Furnace Glycol Return Temperature (DTemp10)",//11 HeatingReturnT -> DTemp10Average
  "Potable Inline Heater Outlet (DTemp11)",   //12 dhwT -> DTemp11Average
  "Potable Heat Exchanger Inlet (DTemp12)",    //13 PotHeatXinletT -> DTemp12Average
  "Potable Heat Exchanger Outlet (DTemp13)"    //14 PotHeatXoutletT -> DTemp13Average
};
// -----------------------------------------------------------------------



    // ------------------ Freeze Protection defaults -------------------------
    // values below this will not be implemented if settings file exists in
    // memory. Settings file is created specifying alternative values on webpage.
    // Choose "Restore Defaults" from Webpage to implements these values
    // -----------------------------------------------------------------------
    constexpr float    DEFAULT_CollectorFreezeTempF      = 33.0f;
    constexpr uint16_t DEFAULT_CollectorFreezeConfirmMin = 1;
    constexpr uint16_t DEFAULT_CollectorFreezeRunMin     = 10;

    constexpr float    DEFAULT_LineFreezeTempF           = 33.0f;
    constexpr uint16_t DEFAULT_LineFreezeConfirmMin      = 1;   // <-- integer (must be whole number)
    constexpr uint16_t DEFAULT_LineFreezeRunMin          = 10;   // <-- integer (must be whole number)

    constexpr float    FREEZE_HYST_F = 2.0f;  // °F hysteresis for “clear”

    // Default - Specify Temp Sensors to associate with Freeze Protection.
    // If adding/removing sensors reference indice above # from " static const char* SENSOR_NAMES[15 " above in comments next to each system Temp Sensor. Array begins at 1 for PT1000 and ends at 14 for potable heat x outlet. 
    constexpr uint8_t  DEFAULT_COLLECTOR_FREEZE_SENSORS[] = {2, 0}; // 0 is a termninator not a temperature sensor and must be present
    constexpr uint8_t  DEFAULT_LINE_FREEZE_SENSORS[]      = {3, 5, 6, 0};  // 0 is a termninator not a temperature sensor and must be present
    // -----------------------------------------------------------------------

    

// -----------------------------------------------------------------------
// File-friendly names (no spaces) used for Temperature Logging folder/file creation.
// -----------------------------------------------------------------------
static const char* SENSOR_FILE_NAMES[15] = {
  "",
  "Panel_Manifold_PT1000",
  "Collector_Supply_DTemp1",
  "StorageTank_DTemp2",
  "Outside_DTemp3",
  "CircLoop_Return_DTemp5",
  "CircLoop_Supply_DTemp4",
  "Collector_Return_DTemp6",
  "DHW_Glycol_Supply_DTemp7",
  "DHW_Glycol_Return_DTemp8",
  "Furnace_Glycol_Supply_DTemp9",
  "Furnace_Glycol_Return_DTemp10",
  "Potable_InlineHeater_Out_DTemp11",
  "Potable_HX_In_DTemp12",
  "Potable_HX_Out_DTemp13"
};
// -----------------------------------------------------------------------



// -----------------------------------------------------------------------
// Map logical index -> source id (0 means pt1000Average, otherwise 1..13 -> DTempAverage[index-1])
// If you change wiring later, update SOURCE_MAP here only and everything else will follow.
// -----------------------------------------------------------------------
static const int SOURCE_MAP[15] = {
  -1, // 0 unused
   0, // 1 -> PT1000Average
   1, // 2 -> DTemp1Average
   2, // 3 -> DTemp2Average
   3, // 4 -> DTemp3Average
   5, // 5 -> DTemp5Average
   4, // 6 -> DTemp4Average
   6, // 7 -> DTemp6Average
   7, // 8 -> DTemp7Average
   8, // 9 -> DTemp8Average
   9, //10 -> DTemp9Average
  10, //11 -> DTemp10Average
  11, //12 -> DTemp11Average
  12, //13 -> DTemp12Average
  13  //14 -> DTemp13Average
};
// -----------------------------------------------------------------------



// -----------------------------------------------------------------------
// Function to get temperature by 1-based sensor index
// -----------------------------------------------------------------------
float getTempByIndex(int idx);
// -----------------------------------------------------------------------



// -----------------------------------------------------------------------
// Temperature Logging Configuration
// -----------------------------------------------------------------------
#define TEMPLOG_MIN_YEAR  2025 // RTC reported year must be greater than for Temp Logging enable
#define TEMPLOG_MAX_YEAR  2100 // RTC reported year must be less than for Temp Logging enable

#define TEMP_LOG_SAMPLE_SEC   60    // seconds between delta checks
#define TEMP_LOG_DELTA_F      1.0f  // °F change needed to cache a new point
#define TEMP_LOG_FLUSH_MIN    60    // minutes between cache flushes to flash

// Debug switches — Uncomment writes to Serial Monitor for Diagnostic
#ifndef TEMP_LOG_DEBUG_CACHE
// #define TEMP_LOG_DEBUG_CACHE      // ← uncomment to see cache adds
#endif
#ifndef TEMP_LOG_DEBUG_FLUSH
// #define TEMP_LOG_DEBUG_FLUSH      // ← uncomment to see flash writes
#endif
#define TEMP_LOG_DEBUG_ERRORS         // always show errors
// -----------------------------------------------------------------------




// -----------------------------------------------------------------------
// File System Cleanup - Deletes oldest Temperature Logging Files
// -----------------------------------------------------------------------
    const float FS_Cleaning_START_LIMIT = 80.0f; // start cleaning when LittleFS usage ≥ 80%
    const float FS_Cleaning_STOP_LIMIT  = 70.0f; // keep cleaning when LittleFS usage < 70%
// -----------------------------------------------------------------------


// -----------------------------------------------------------------------
// ThirdWebpage File Browser settings for downloading directories
// ===== TGZ (tar.gz) streaming settings located in TarGZ.cpp=====
// PSRAM ring buffer size used while streaming tar.gz downloads.
// Typical good values: 128*1024 .. 512*1024
// -----------------------------------------------------------------------
  
  // TarGZ constants (adjust values as needed)
#define TGZ_DELETE_DELAY_MS 5000  // Delay before deleting session (ms)
#define TGZ_HTTP_CHUNK_BYTES 4096 // Max bytes per HTTP chunk. tune based on Fingbuf memory allocation

// TGZ ring buffer placement: 
  #ifndef TGZ_RING_CACHE_LOCATION
    #define TGZ_RING_CACHE_LOCATION 1 // 0 = Internal Heap, 1 = PSRAM (falls back to Internal if PSRAM not available)
  #endif

// Ringbuf Memory suggestions: Mininum: 128 KB =(128 * 1024), Decent: 256 KB =(256 * 1024), Extra Smothing: 512 KB = (512 * 1024), Excessive unless downloading Large Directories 4 MB =(4 * 1024 * 1024)
#ifndef TGZ_RINGBUF_BYTES
  #if TGZ_RING_CACHE_LOCATION == 1  // Don't change this value
    #define TGZ_RINGBUF_BYTES (4 * 1024 * 1024)    // PSRAM default:
  #else
    #define TGZ_RINGBUF_BYTES (16 * 1024)  // Internal heap default: recomended 256 KB (Must = at least 16 KB+ and Fragmentation will possibly occur, very fragile with less than 33 KB)
  #endif
#endif

// Stack size (bytes) for the tar.gz producer task that runs compression.
#ifndef TGZ_PRODUCER_TASK_STACK_BYTES
  #define TGZ_PRODUCER_TASK_STACK_BYTES (12288)      // If you see stack canary trips, increase to 16384 or 20480.  
#endif

// Task priority for the tar.gz producer task.
#ifndef TGZ_PRODUCER_TASK_PRIORITY
  #define TGZ_PRODUCER_TASK_PRIORITY 1      // Typical range on ESP32: 1..5 (higher = more CPU time)
#endif

// Core pinning:
#ifndef TGZ_PRODUCER_TASK_CORE
  #define TGZ_PRODUCER_TASK_CORE (-1)    //  0 or 1 to pin to specific core or -1 for "no affinity" (not pinned)
#endif
// -----------------------------------------------------------------------



// -----------------------------------------------------------------------
// Runtime System Configuration (editable via web UI)
// -----------------------------------------------------------------------
struct SystemConfig {
    // ---- Serial diagnostics (runtime toggles; future web UI) ----
    bool     diagSerialEnable;  // master runtime enable
    uint32_t diagSerialMask;    // category bitmask (see DiagConfig.h)
    float panelTminimumValue;   // Min Lead Start Temp(PT1000), runtime value
    float panelOnDifferential;
    float panelLowDifferential;
    float panelOffDifferential;
    float boilerCircOn;
    float boilerCircOff;
    float storageHeatingLimit;
    float circPumpOn;
    float circPumpOff;
    float heatTapeOn;
    float heatTapeOff;
         
    // ------ Freeze Protection (Enables Web Configurable) Do Not Change -------
    // Collector & Collector Supply freeze protection runs - Lead/Lag Pumps
    float    collectorFreezeTempF;         // default 33.0
    uint16_t collectorFreezeConfirmMin;   // whole minutes
    uint16_t collectorFreezeRunMin;       // whole minutes
    uint8_t  collectorFreezeSensors[15];  // 1-based indices, 0-terminated

    // Tank & Circ Supply & Circ Return freeze protection runs - Circ pump
    float    lineFreezeTempF;              // default 33.0
    uint16_t lineFreezeConfirmMin;        // whole minutes
    uint16_t lineFreezeRunMin;            // whole minutes
    uint8_t  lineFreezeSensors[15];       // 1-based indices, 0-terminated
};
// -----------------------------------------------------------------------



// -----------------------------------------------------------------------
// -------- TimeConfig function prototypes ----------
// -----------------------------------------------------------------------
void initTimeConfigDefaults();
bool loadTimeConfigFromFS();
bool saveTimeConfigToFS();
bool resetTimeConfigToDefaults();

// Map TimeConfig → POSIX TZ string used by setenv("TZ", ...)
// Examples: "MST7MDT,M3.2.0/2,M11.1.0/2", "PST8PDT,...", "UTC0"
String getPosixTimeZoneString();

// Initialize with compile-time defaults (#defines above)
void initSystemConfigDefaults();

// Load / Save from LittleFS: /system_config.json
bool loadSystemConfigFromFS();
bool saveSystemConfigToFS();

// Diagnostic serial config (separate JSON file)
bool loadDiagSerialConfigFromFS();
bool saveDiagSerialConfigToFS();

// Reset just the SystemConfig block back to compile-time defaults and persist
bool resetSystemConfigToDefaults();


extern SystemConfig g_config;

struct TimeConfig {
    String timeZoneId;   // e.g. "US_MOUNTAIN", "US_PACIFIC", "UTC"
    bool   dstEnabled;   // true = observe DST, false = standard-only
};

constexpr const char* DEFAULT_TIMEZONE_ID = "US_MOUNTAIN";
constexpr bool        DEFAULT_DST_ENABLED = true;

// Global instance (defined in Config.cpp)
extern TimeConfig g_timeConfig;
// -----------------------------------------------------------------------



#endif // CONFIG_H

// ---------------------------------------------------------
// ESP32-S3_DevkitC-1 Top View Pin Identification
// ---------------------------------------------------------
//  https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/_images/ESP32-S3_DevKitC-1_pinlayout_v1.1.jpg
//                               ESP32-S3-DevkitC-1
//                                ESP32-S3-WROOM-1
//                            ________________________    
//.                                3V3________GND
//                                 3V3________TX=GPIO43
//                          RST----RST________RX=GPIO44
//                W5500_INT_PIN=GPIoO4________1=GPIoO1    
//              MAX31865_CS_PIN=GPIoO5________2=GPIoO2    
//        Pump_1_LEAD_RELAY_PIN=GPIoO6________GPIo42=RTC DS3231 sqwPin
//         Pump_2_LAD_RELAY_PIN=GPIoO7________GPIo41=RTC DS3231 pinSCL
//   Pump_4_CIRC_PUMP_RELAY_PIN=GPIo15________GPIo40=RTC DS3231 pinSDA
//    Pump_5_DHW_PUMP_RELAY_PIN=GPIo16________GPIo39=PUMP_10_Unused_RELAY
//Pump_6_STORAGE_HEAT_RELAY_PIN=GPIo17________GPIo38=PUMP_9_Unused_RELAY
//            MAX31865_CLK_PIN =GPIo18________GPIo37 (Reserved for PSRAM Now)  
//         PUMP_8_RECIRC_VALVE =GPIoO8________GPIo36=formorly DHW_HEATING_PIN = 36 >GPIO3 (Reserved for PSRAM Now)
//              DHW_Heating_Pin =GPIO3________GPIo35=PUMP_8_RECIRC_VALVE >GPIO8
//                              GPIO46________GPIoO0  
//            W5500_Reset_Pin  =GPioO9________GPIo45=MAX31865_SDO_MISO_PIN
//            W5500_SS_PIN     =GPIo10________GPIo48=FURANCE_HEATING_PIN
//            W5500_MOSI_PIN   =GPIo11________GPIo47=Pump_7_BOILER_CIRC_RELAY_PIN
//            W5500_SCK_PIN    =GPIo12________GPIo21=ONE_WIRE_BUS_2
//            W5500_MISO_PIN   =GPIo13________GPIo20=ONE_WIRE_BUS
// Pump_3__HEAT_TAPE_RELAY_PIN =GPIo14________GPIo19=MAX31865_SDI_MOSI_PIN
//                             5V0--5V________GND
//                              GND--G________GND
//                                  UART     USB