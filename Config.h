// This file has system cofriguration settings such as Pin number specification for temperature sensor, relays, W5500, Max31865 and temperature values for Auto Mode Pump Operation. A little note pertaining to the arrays in this system.The inside arrays (pumpStates[10], pumpModes[10], pumpPins[10]), pumpNames[10] are all 0-based and the external arrays (filenames, JSON/UI, websocket commands) are all 1-based arrays with conversion “+1 / -1” happening at the boundries. 
// 06-16-26: some pins declarations have changes since this sketch was oritinally used. updated pin declarations (RTC, one wire, RECIRC_VALVE_RELAY) to match new layout to assist in targz failure to compress Temperature Logs. 


#ifndef CONFIG_H
#define CONFIG_H
#include <RTClib.h>
#include <Arduino.h>
#include <FreeRTOS.h>
#include <semphr.h>

// ---------------------------------------------------------
// Mutex handles as extern to be accessible in other files - do not change
// ---------------------------------------------------------
extern SemaphoreHandle_t pumpStateMutex;
extern SemaphoreHandle_t temperatureMutex;
extern SemaphoreHandle_t fileSystemMutex;

// ---------------------------------------------------------
// Ethernet adapter (W5500) pin configurations
// ---------------------------------------------------------
const int W5500_MOSI = 11;
const int W5500_MISO = 13;
const int W5500_SCK = 12;
const int W5500_SS = 10;
const int W5500_INT = 4;

// ---------------------------------------------------------
// Pin numbers for DS3231 Real time clock 
// ---------------------------------------------------------
const int pinSDA = 8;
const int pinSCL = 9;
const int sqwPin = 42; 

// ---------------------------------------------------------
// Heating Call Input Pins - Calls Furnace/DHW when connected to ground
// ---------------------------------------------------------
const int FURNACE_HEATING_PIN = 48;
const int DHW_HEATING_PIN     = 3; // was on gpio 36 and home solar controller will need this wire moved when uploading this sketch.

// ---------------------------------------------------------
// pin # for one wire temperature sensors - one buss inside one buss outside
// ---------------------------------------------------------
const int ONE_WIRE_BUS_1 = 1; // One Wire Bus for DS18B20 Temperature Sensors 
const int ONE_WIRE_BUS_2 = 2; // One Wire Bus 2 for DS18B20 Temperature Sensors

// ---------------------------------------------------------
// Adafruit_MAX31865(5, 19, 45, 18); - Pin specifications for Max31865
// ---------------------------------------------------------
#define MAX31865_CS_PIN    5   // spi Cable Select
#define MAX31865_DO_PIN    19  // spi MISO/SDO (Serial Data Out)
#define MAX31865_DI_PIN    45  // spi MOSI/SDI (Serial Data In)
#define MAX31865_CLK_PIN   18  // spi Clock

// ---------------------------------------------------------
// Pump state variables - Do not change
// ---------------------------------------------------------
#define PUMP_OFF 0
#define PUMP_ON 1
#define PUMP_AUTO 2

// ---------------------------------------------------------
// Pump/Relay declarations - Increse if adding more Pumps/Relays
// ---------------------------------------------------------
extern int pumpModes[10]; // Mode of each pump
extern int pumpStates[10]; // State of each pump
const int numPumps = 10;


// ---------------------------------------------------------
// GPIO Pins for Pumps/Relays
// ---------------------------------------------------------
const int PANEL_LEAD_PUMP_RELAY = 6;
const int PANEL_LAG_PUMP_RELAY  = 7;
const int HEAT_TAPE_RELAY       = 14;
const int CIRC_PUMP_RELAY       = 15;
const int DHW_PUMP_RELAY        = 16;
const int STORAGE_HEAT_RELAY    = 17;
const int BOILER_CIRC_RELAY     = 47;
const int RECIRC_VALVE_RELAY    = 40; // was on pin 35 house controller will need this changed when uplaoding this sketch
const int Pump_9_Unused_Relay   = 38;
const int Pump_10_Unused_Relay  = 39;

// ---------------------------------------------------------
// Pump/Relay pin definitions
// ---------------------------------------------------------
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

// ---------------------------------------------------------
// Operating Parameters for circulation pumps (defaults)
// Single source of truth; used to seed g_config at boot.
// ---------------------------------------------------------
inline constexpr float DEFAULT_PanelOnDifferential     = 30.0f;
inline constexpr float DEFAULT_PanelLowDifferential    = 15.0f;
inline constexpr float DEFAULT_PanelOffDifferential    = 3.0f;
inline constexpr float DEFAULT_panelTminimum           = 125.0f;
inline constexpr float DEFAULT_StorageHeatingLimit     = 130.0f;
inline constexpr float DEFAULT_Circ_Pump_On            = -100.0f;  // 5.0f
inline constexpr float DEFAULT_Circ_Pump_Off           = -200.0f;  // 2.0f
inline constexpr float DEFAULT_Heat_Tape_On            = 35.0f;
inline constexpr float DEFAULT_Heat_Tape_Off           = 45.0f;
inline constexpr float DEFAULT_Boiler_Circ_On          = 106.0f;
inline constexpr float DEFAULT_Boiler_Circ_Off         = 110.0f;

// -------------------- Freeze Protection defaults --------------------
inline constexpr float    DEFAULT_CollectorFreezeTempF      = 33.0f;
inline constexpr uint16_t DEFAULT_CollectorFreezeConfirmMin = 10;
inline constexpr uint16_t DEFAULT_CollectorFreezeRunMin     = 10;

inline constexpr float    DEFAULT_CircFreezeTempF           = 33.0f;
inline constexpr uint16_t DEFAULT_CircFreezeConfirmMin      = 10;
inline constexpr uint16_t DEFAULT_CircFreezeRunMin          = 10;

inline constexpr float    DEFAULT_HeatTapeBadF              = 33.0f;
inline constexpr float    DEFAULT_HeatTapeClearF            = 34.0f;
inline constexpr uint16_t DEFAULT_HeatTapeEvalMin           = 10;

inline constexpr float    DEFAULT_TankFreezeTempF           = 35.0f;
inline constexpr float    DEFAULT_TankFreezeClearF          = 40.0f;
inline constexpr uint16_t DEFAULT_TankFreezeConfirmMin      = 10;

// ---------------------------------------------------------
// ***** Pump Mode Change Rate Limiting Interval *****
// ---------------------------------------------------------
const unsigned long LEAD_RELAY_CHANGE_INTERVAL = 1000; // 1 second
const unsigned long LAG_RELAY_CHANGE_INTERVAL = 5000; // 5 second
const unsigned long HEAT_TAPE_RELAY_CHANGE_INTERVAL = 1000; // 1 second
const unsigned long CIRC_RELAY_CHANGE_INTERVAL = 1000; // 1 second
const unsigned long BOILER_RELAY_CHANGE_INTERVAL = 1000; // 1 second
const unsigned long RECIRC_RELAY_CHANGE_INTERVAL = 1000; // 1 second
const unsigned long DHW_RELAY_CHANGE_INTERVAL = 1000; // 1 second
const unsigned long HEATING_RELAY_CHANGE_INTERVAL = 1000; // 1 second


// ----------------------
// TEMPERATURE SENSOR CONFIG (single source of truth)
// ----------------------
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

// File-friendly names (no spaces) used for Temperature Logging folder/file creation.
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

// Map logical index -> source id (0 means pt1000Average, otherwise 1..13 -> DTempAverage[index-1])
// If you change wiring later, update SOURCE_MAP here only and everything else will follow.
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


// ---------------------------------------------------------
// Temperature Logging Configuration
// ---------------------------------------------------------
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



// ---------------------------------------------------------
// File System Cleanup - Deletes oldest Temperature Logging Files
// ---------------------------------------------------------
    const float FS_Cleaning_START_LIMIT = 80.0f; // start cleaning when LittleFS usage ≥ 80%
    const float FS_Cleaning_STOP_LIMIT  = 70.0f; // keep cleaning when LittleFS usage < 70%


// ---------------------------------------------------------
// ThirdWebpage File Browser settings for downloading directories
// ---------------------------------------------------------
// ===== TGZ (tar.gz) streaming settings =====
// PSRAM ring buffer size used while streaming tar.gz downloads.
// Typical good values: 128*1024 .. 512*1024
#ifndef TGZ_RING_BYTES
  #define TGZ_RING_BYTES (256 * 1024)
#endif

// Stack size (bytes) for the tar.gz producer task that runs compression.
// If you see stack canary trips, increase to 16384 or 20480.
#ifndef TGZ_PRODUCER_TASK_STACK_BYTES
  #define TGZ_PRODUCER_TASK_STACK_BYTES (12288)
#endif

// Task priority for the tar.gz producer task.
// Typical range on ESP32: 1..5 (higher = more CPU time)
#ifndef TGZ_PRODUCER_TASK_PRIORITY
  #define TGZ_PRODUCER_TASK_PRIORITY 2
#endif

// Core pinning:
//  0 or 1 to pin
// -1 for "no affinity" (not pinned)
#ifndef TGZ_PRODUCER_TASK_CORE
  #define TGZ_PRODUCER_TASK_CORE (-1)
#endif




// ---------------------------------------------------------
// Runtime System Configuration (editable via web UI)
// ---------------------------------------------------------
struct SystemConfig {
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

    // ------------- Freeze Protection (Web Configurable) --------------------
// Collector freeze protection (Lead/Lag run)
float    collectorFreezeTempF;         // default 33.0
uint16_t collectorFreezeConfirmMin;   // whole minutes
uint16_t collectorFreezeRunMin;       // whole minutes

// Circ line freeze protection (Circ pump run)
float    circFreezeTempF;              // default 33.0
uint16_t circFreezeConfirmMin;        // whole minutes
uint16_t circFreezeRunMin;            // whole minutes


// Heat tape ineffective detection
float    heatTapeBadF;                 // default 33.0  (still freezing)
float    heatTapeClearF;               // default 34.0  (clear alarm)
uint16_t heatTapeEvalMin;             // whole minutes

// Tank freeze protection (Circ pump until warm)
float    tankFreezeTempF;              // default 35.0  (trigger)
float    tankFreezeClearF;             // default 40.0  (stop)
uint16_t tankFreezeConfirmMin;        // whole minutes

// ---------------------------------------------------------------------------

};



// -------- TimeConfig function prototypes ----------
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

// Initialize with compile-time defaults
void initTimeConfigDefaults();

// Load / Save from LittleFS: /time_config.json
bool loadTimeConfigFromFS();
bool saveTimeConfigToFS();

// Reset TimeConfig back to compile-time defaults and persist
bool resetTimeConfigToDefaults();

// Convert current TimeConfig to a POSIX TZ string for setenv("TZ", ...)
String getPosixTimeZoneString();


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
//                 W5500_INT_PIN=GPIO4________1=GPIO1    
//               MAX31865_CS_PIN=GPIO5________2=GPIO2    
//         Pump_1_LEAD_RELAY_PIN=GPIO6________GPIO42=RTC DS3231 sqwPin
//          Pump_2_LAD_RELAY_PIN=GPIO7________GPIO41=RTC DS3231 pinSCL
//   Pump_4_CIRC_PUMP_RELAY_PIN=GPIO15________GPIO40=RTC DS3231 pinSDA
//    Pump_5_DHW_PUMP_RELAY_PIN=GPIO16________GPIO39=PUMP_10_Unused_RELAY
//Pump_6_STORAGE_HEAT_RELAY_PIN=GPIO17________GPIO38=PUMP_9_Unused_RELAY
//            MAX31865_CLK_PIN =GPIO18________GPIO37   
//          PUMP_8_RECIRC_VALVE =GPIO8________GPIO36=DHW_HEATING_PIN = 36 >GPIO3
//                               GPIO3________GPIO35=PUMP_8_RECIRC_VALVE >GPIO8
//                              GPIO46________GPIO0  
//                               GPIO9________GPIO45=MAX31865_SDO_MISO_PIN
//            W5500_SS_PIN     =GPIO10________GPIO48=FURANCE_HEATING_PIN
//            W5500_MOSI_PIN   =GPIO11________47=Pump_7_BOILER_CIRC_RELAY_PIN
//            W5500_SCK_PIN    =GPIO12________GPIO21=ONE_WIRE_BUS_2
//            W5500_MISO_PIN   =GPIO13________GPIO20=ONE_WIRE_BUS
// Pump_3__HEAT_TAPE_RELAY_PIN =GPIO14________GPIO19=MAX31865_SDI_MOSI_PIN
//                             5V0--5V________GND
//                              GND--G________GND
//                                  UART     USB
