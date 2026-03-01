// PumpManager.cpp
#include "PumpManager.h"
#include "Config.h"
#include "Logging.h"
#include "WebServerManager.h"
#include "TemperatureControl.h"
#include "SerialPrint.h"
#include <Arduino.h>
#include <FreeRTOS.h>
#include <semphr.h>
#include <Ticker.h>
#include "esp_task_wdt.h"
#include "RTCManager.h"
#include "FileSystemManager.h"
#include "AlarmManager.h"
#include "DiagLog.h"


bool previousDHWCallStatus = false;
bool previousHeatingCallStatus = false;

// WebSocket object is accessible for active heating call on webpage
extern AsyncWebSocket ws;

// Define pump names
const char* pumpNames[NUM_PUMPS] = {
    "Lead Pump",
    "Lag Pump",
    "Heat Tape",
    "Circulation Pump",
    "DHW Pump",
    "Storage Heating Pump",
    "Boiler Circ Pump",
    "Recirculation Valve",
    "Unused",
    "Unused"
};

// Declarations for state tracking
int previousPumpStates[10];
int previousPumpModes[10];
unsigned long lastBroadcastTime = 0;
const unsigned long broadcastInterval = 10000; // 10 seconds

String serialMessage = "";  // Define the global serial message variable
Ticker broadcastPumpTicker; // Ticker for periodic pump state broadcasting
Ticker pumpOffTicker;       // Ticker to handle the 10-minute timeout

// Pump Modes and States
int pumpModes[10] = {PUMP_AUTO, PUMP_AUTO, PUMP_AUTO, PUMP_AUTO, PUMP_AUTO, PUMP_AUTO, PUMP_AUTO, PUMP_AUTO, PUMP_AUTO, PUMP_AUTO};
int pumpStates[10] = {PUMP_OFF, PUMP_OFF, PUMP_OFF, PUMP_OFF, PUMP_OFF, PUMP_OFF, PUMP_OFF, PUMP_OFF, PUMP_OFF, PUMP_OFF};

// Configure to style of relays used. (energized = on or energized = off)
bool pumpOnStateHigh[10] = {false, false, false, false, false, false, false, false, false, false};
bool pumpOffStateHigh[10] = {true, true, true, true, true, true, true, true, true, true};

// External Variables
extern int state_panel_lead;
extern int state_panel_lag;
extern int state_dhw;
extern int state_heat;
extern int state_circ;
extern int state_heat_tape;
extern int state_boiler_circ;
extern int state_recirc_valve;

// Temperature Delta Calculations
#define CollectorTemperatureRise ((CreturnT) - (CSupplyT))
#define DhwTemperatureDrop ((DhwSupplyT) - (DhwReturnT))
#define HeatingTemperatureDrop ((HeatingSupplyT) - (HeatingReturnT))
#define CirculationTemperatureDrop ((supplyT) - (CircReturnT)) 


// ------------------- FREEZE PROTECTION -------------------
extern SystemConfig g_config;

static inline uint32_t minToMs(uint16_t minutes) {
  return (uint32_t)minutes * 60000UL;
}

static uint32_t s_colBelowSinceMs  = 0;
static uint32_t s_colRunUntilMs    = 0;
static bool     s_colRunActive     = false;

static uint32_t s_lineBelowSinceMs = 0;
static uint32_t s_lineRunUntilMs   = 0;
static bool     s_lineRunActive    = false;

static uint32_t s_freezeLeadLastChangeMs = 0;
static uint32_t s_freezeLagLastChangeMs  = 0;
static uint32_t s_freezeCircLastChangeMs = 0;



// Relay States Enumeration
enum RelayState { OFF, ON };
RelayState panelLeadPumpState    = OFF;
RelayState panelLagPumpState     = OFF;
RelayState dhwPumpState          = OFF;
RelayState storageHeatState      = OFF;
RelayState circPumpState         = OFF;
RelayState heatTapeState         = OFF;
RelayState boilerCircState       = OFF;
RelayState recircValveState      = OFF;

// Timestamp Variables for Rate Limiting
unsigned long lastBoilerCircChangeTime    = 0;
unsigned long lastRecircValveChangeTime   = 0;
unsigned long lastPanelLeadPumpChangeTime = 0;
unsigned long lastPanelLagPumpChangeTime  = 0;
unsigned long lastDhwPumpChangeTime       = 0;
unsigned long lastStorageHeatChangeTime   = 0;
unsigned long lastCircPumpChangeTime      = 0;
unsigned long lastHeatTapeChangeTime      = 0;

// External Mutex Handles
extern SemaphoreHandle_t pumpStateMutex;
extern SemaphoreHandle_t temperatureMutex;


// Function Prototypes
void PrintPumpStates();
void turnPumpsBackToAuto();
void turnOnAllPumpsFor10Minutes();
void setupPumpBroadcasting();
void broadcastPumpState(int pumpIndex);
void setPumpState(int pumpIndex, int state);
void togglePumpState(int pumpIndex);
void applyPumpMode(int pumpIndex);
void initializePumps();
void controlPanelLeadPump();
void controlPanelLagPump();
void controlHeatTape();
void controlCirculationPump();
void controlDHWPump();
void controlStorageHeatPump();
void controlBoilerCirculatorPump();
void controlRecirculationValve();
void PumpControl();



// Function to check if any pump state or mode has changed since the last broadcast
bool anyPumpStateChanged() {
  bool hasChanged = false;

  for (int i = 0; i < 10; i++) {
    if (pumpStates[i] != previousPumpStates[i] || pumpModes[i] != previousPumpModes[i]) {
      hasChanged = true;
      // Update previous states to current states
      previousPumpStates[i] = pumpStates[i];
      previousPumpModes[i] = pumpModes[i];
    }
  }
  return hasChanged;
}

// ------------------- FREEZE PROTECTION HELPERS -------------------
static bool tempValidF(float v) {
  return !isnan(v) && v > -50.0f && v < 250.0f;
}

static void forcePumpOnIfAuto(int pumpIndex,
                              uint32_t nowMs,
                              uint32_t minIntervalMs,
                              uint32_t &lastChangeMs)
{
  if (pumpModes[pumpIndex] != PUMP_AUTO) return;

  if (pumpStates[pumpIndex] == PUMP_OFF &&
      (nowMs - lastChangeMs >= minIntervalMs)) {
    setPumpState(pumpIndex, PUMP_ON);
    lastChangeMs = nowMs;
  }
}

static float getMinTemp(const uint8_t* sensors, bool* anyValid) {
  float minT = 999.0f;
  *anyValid = false;
  for (const uint8_t* s = sensors; *s; ++s) {
    float t = getTempByIndex(*s);
    if (tempValidF(t)) {
      *anyValid = true;
      minT = min(minT, t);
    }
  }
  return minT;
}

static bool isAllWarm(const uint8_t* sensors, float clearTempF) {
  bool allWarm = true;
  for (const uint8_t* s = sensors; *s; ++s) {
    float t = getTempByIndex(*s);
    if (tempValidF(t)) {
      allWarm &= (t >= clearTempF);
    }
  }
  return allWarm;
}

static bool isAnyCold(const uint8_t* sensors, float freezeTempF) {
  bool anyCold = false;
  for (const uint8_t* s = sensors; *s; ++s) {
    float t = getTempByIndex(*s);
    if (tempValidF(t)) {
      anyCold |= (t <= freezeTempF);
    }
  }
  return anyCold;
}

// Returns true ONLY when we are actively overriding Lead+Lag (AUTO) for freeze protection.
static bool updateCollectorFreezeProtect(uint32_t nowMs)
{
  const float freezeTempF = g_config.collectorFreezeTempF;
  const float clearTempF  = freezeTempF + FREEZE_HYST_F;
  const uint32_t confirmMs = minToMs(g_config.collectorFreezeConfirmMin);
  const uint32_t runMs     = minToMs(g_config.collectorFreezeRunMin);

  bool anyValid = false;
  float minT = getMinTemp(g_config.collectorFreezeSensors, &anyValid);
  bool isCold = isAnyCold(g_config.collectorFreezeSensors, freezeTempF);
  bool isWarmEnough = isAllWarm(g_config.collectorFreezeSensors, clearTempF);

  if (!anyValid) {
    s_colBelowSinceMs = 0;
    s_colRunActive    = false;
    s_colRunUntilMs   = 0;
    AlarmManager_clear(ALM_COLLECTOR_FREEZE_PROTECT);
    return false;
  }

  if (isWarmEnough) {
    s_colBelowSinceMs = 0;
    s_colRunActive    = false;
    s_colRunUntilMs   = 0;
    AlarmManager_clear(ALM_COLLECTOR_FREEZE_PROTECT);
    return false;
  }

  if (isCold) {
    if (s_colBelowSinceMs == 0) s_colBelowSinceMs = nowMs;

    if (!s_colRunActive && (nowMs - s_colBelowSinceMs >= confirmMs)) {
      s_colRunActive  = true;
      s_colRunUntilMs = nowMs + runMs;

      AlarmManager_event(ALM_COLLECTOR_FREEZE_PROTECT, ALM_WARN,
        "Collector freeze cycle start (min=%.1fF)", (double)minT);
    }
  }

  if (s_colRunActive && nowMs >= s_colRunUntilMs) {
    if (isCold) {
      s_colRunUntilMs = nowMs + runMs;
      AlarmManager_event(ALM_COLLECTOR_FREEZE_PROTECT, ALM_WARN,
        "Collector freeze cycle restart (min=%.1fF)", (double)minT);
    } else {
      s_colRunActive = false;
    }
  }

  const bool leadAuto = (pumpModes[0] == PUMP_AUTO);
  const bool lagAuto  = (pumpModes[1] == PUMP_AUTO);

  if (s_colRunActive && leadAuto && lagAuto) {
    AlarmManager_set(ALM_COLLECTOR_FREEZE_PROTECT, ALM_ALARM,
      "Collector freeze protection active");

    forcePumpOnIfAuto(0, nowMs, LEAD_RELAY_CHANGE_INTERVAL, s_freezeLeadLastChangeMs);
    forcePumpOnIfAuto(1, nowMs, LAG_RELAY_CHANGE_INTERVAL,  s_freezeLagLastChangeMs);
    return true;
  }

  if (s_colRunActive && (!leadAuto || !lagAuto)) {
    AlarmManager_set(ALM_COLLECTOR_FREEZE_PROTECT, ALM_ALARM,
      "Collector freeze protect blocked (Lead/Lag not AUTO)");
  }

  return false;
}

// Returns true ONLY when we are actively overriding Circ Pump (AUTO) for freeze protection.
static bool updateLineFreezeProtect(uint32_t nowMs)
{
  const float freezeTempF  = g_config.lineFreezeTempF;
  const float clearTempF   = freezeTempF + FREEZE_HYST_F;
  const uint32_t confirmMs = minToMs(g_config.lineFreezeConfirmMin);
  const uint32_t runMs     = minToMs(g_config.lineFreezeRunMin);

  bool anyValid = false;
  float minT = getMinTemp(g_config.lineFreezeSensors, &anyValid);
  bool isCold = isAnyCold(g_config.lineFreezeSensors, freezeTempF);
  bool isWarmEnough = isAllWarm(g_config.lineFreezeSensors, clearTempF);

  if (!anyValid) {
    s_lineBelowSinceMs = 0;
    s_lineRunActive    = false;
    s_lineRunUntilMs   = 0;
    AlarmManager_clear(ALM_LINE_FREEZE_PROTECT);
    return false;
  }

  if (isWarmEnough) {
    s_lineBelowSinceMs = 0;
    s_lineRunActive    = false;
    s_lineRunUntilMs   = 0;
    AlarmManager_clear(ALM_LINE_FREEZE_PROTECT);
    return false;
  }

  if (isCold) {
    if (s_lineBelowSinceMs == 0) s_lineBelowSinceMs = nowMs;

    if (!s_lineRunActive && (nowMs - s_lineBelowSinceMs >= confirmMs)) {
      s_lineRunActive  = true;
      s_lineRunUntilMs = nowMs + runMs;

      AlarmManager_event(ALM_LINE_FREEZE_PROTECT, ALM_WARN,
                         "Line freeze cycle start (min=%.1fF)", (double)minT);
    }
  }

  if (s_lineRunActive && nowMs >= s_lineRunUntilMs) {
    if (isCold) {
      s_lineRunUntilMs = nowMs + runMs;
      AlarmManager_event(ALM_LINE_FREEZE_PROTECT, ALM_WARN,
                         "Line freeze cycle restart (min=%.1fF)", (double)minT);
    } else {
      s_lineRunActive = false;
    }
  }

  const bool circAuto = (pumpModes[3] == PUMP_AUTO);

  if (s_lineRunActive && circAuto) {
    AlarmManager_set(ALM_LINE_FREEZE_PROTECT, ALM_ALARM,
                     "Line freeze protection active");
    forcePumpOnIfAuto(3, nowMs, CIRC_RELAY_CHANGE_INTERVAL, s_freezeCircLastChangeMs);
    return true;
  }

  if (s_lineRunActive && !circAuto) {
    AlarmManager_set(ALM_LINE_FREEZE_PROTECT, ALM_ALARM,
                     "Line freeze protect blocked (Circ not AUTO)");
  }

  return false;
}



void sendHeatingCallStatus(bool dhwCallActive, bool heatingCallActive) {
    // Determine the status strings
    String dhwStatus = dhwCallActive ? "ACTIVE" : "INACTIVE";
    String heatingStatus = heatingCallActive ? "ACTIVE" : "INACTIVE";

    // Construct the message
    String heatingCallData = "HeatingCalls:";
    heatingCallData += "DHW:" + dhwStatus + ",";
    heatingCallData += "Heating:" + heatingStatus;

    // Send the message to all connected WebSocket clients
    ws.textAll(heatingCallData);
}




void setPumpMode(int pumpIndex, int mode) {
    // Validate pumpIndex
    if (pumpIndex < 0 || pumpIndex >= 10) {
        LOG_ERR("[Pump] Invalid pump index in setPumpMode.\n");
        return;
    }

    // Acquire the pumpStateMutex before modifying pumpModes
    if (xSemaphoreTake(pumpStateMutex, portMAX_DELAY)) {
        pumpModes[pumpIndex] = mode;

        LOG_CAT(DBG_PUMP,
                "[Pump] Pump %d mode set to %s\n",
                pumpIndex + 1,
                (mode == PUMP_AUTO ? "AUTO" : (mode == PUMP_ON ? "ON" : "OFF")));

        // Apply the mode immediately if not in AUTO
        if (mode == PUMP_ON) {
            setPumpState(pumpIndex, PUMP_ON);
        } else if (mode == PUMP_OFF) {
            setPumpState(pumpIndex, PUMP_OFF);
        }
        // If mode is AUTO, the PumpControl function will handle it

        xSemaphoreGive(pumpStateMutex);
    } else {
        LOG_ERR("[Pump] Failed to take pumpStateMutex in setPumpMode.\n");
    }
}


// ***** PrintPumpStates Function *****
void PrintPumpStates() {
    serialMessage = ""; // Reset the serial message
    for (int i = 0; i < 10; i++) {
        String pumpState = (pumpStates[i] == PUMP_ON) ? "on" : "off";
        String pumpMode = (pumpModes[i] == PUMP_AUTO) ? "auto" :
                          (pumpModes[i] == PUMP_ON) ? "on" : "off";
        // Construct the serial message for each pump
        serialMessage += "Pump " + String(i + 1) + ". State: " + pumpState +
                         ", Mode: " + pumpMode + "\n";
    }
    // Call SerialPrint to print the serialMessage
    SerialPrint(); // Assuming SerialPrint() prints the global serialMessage
}

// ***** Function to Turn All Pumps Back to "Auto" Mode After 10 Minutes *****
void turnPumpsBackToAuto() {
    for (int i = 0; i < 10; i++) {
        setPumpMode(i, PUMP_AUTO); // Set each pump back to "Auto" mode
    }
    LOG_CAT(DBG_PUMP, "[Pump] All pumps returned to Auto mode.\n");
}

// ***** Function to Turn All Pumps On for 10 Minutes *****
void turnOnAllPumpsFor10Minutes() {
    for (int i = 0; i < 10; i++) {
        setPumpMode(i, PUMP_ON); // Turn each pump on
    }
    LOG_CAT(DBG_PUMP, "[Pump] All pumps turned on for 10 minutes.\n");
    // Set a timer to turn the pumps back to "Auto" mode after 10 minutes (600 seconds)
    pumpOffTicker.once(600, turnPumpsBackToAuto);
}



// ***** Broadcast Pump State Function *****  
void broadcastPumpState(int pumpIndex) {
    String message = "";
    if (pumpIndex == -1) {
        // Broadcast all pump states for WebSocket
        for (int i = 0; i < 10; i++) {
            String pumpState = (pumpStates[i] == PUMP_ON) ? "on" : "off";
            String pumpMode = (pumpModes[i] == PUMP_AUTO) ? "auto" :
                              (pumpModes[i] == PUMP_ON) ? "on" : "off";
            // Prepare WebSocket message
            String pumpMessage = "pump" + String(i + 1) + "State:" + pumpState +
                                 ",pump" + String(i + 1) + "Mode:" + pumpMode;
            message += pumpMessage;
            if (i < 9) message += ","; // Add comma except for the last pump
        }
        // Use the new PrintPumpStates function to handle serial printing
        PrintPumpStates();
    } else { // Broadcast specific pump state for WebSocket
        if (pumpIndex < 0 || pumpIndex >= 10) {
            // Prepare and set the serial message
            serialMessage = "Invalid pump index in broadcastPumpState.\n";
            SerialPrint(); // Print the serialMessage
            return;
        }
        String pumpState = (pumpStates[pumpIndex] == PUMP_ON) ? "on" : "off";
        String pumpMode = (pumpModes[pumpIndex] == PUMP_AUTO) ? "auto" :
                          (pumpModes[pumpIndex] == PUMP_ON) ? "on" : "off";
        String pumpMessage = "pump" + String(pumpIndex + 1) + "State:" + pumpState +
                             ",pump" + String(pumpIndex + 1) + "Mode:" + pumpMode;
        message = pumpMessage; // For a specific pump, the WebSocket message is just about that pump
        // Prepare and set the serial message for the specific pump
        serialMessage = "Pump " + String(pumpIndex + 1) + ". State: " + pumpState +
                         ", Mode: " + pumpMode + "\n";
        //SerialPrint(); // Print the serialMessage
    }
    // Send the compiled message to all WebSocket clients
    broadcastMessageOverWebSocket(message, "PumpStates");
}


// ***** Setup Pump Broadcasting Function *****
void setupPumpBroadcasting() {
    // Get the current time
    unsigned long currentMillis = millis();

    // If this is the first call, initialize previous pump states
    static bool initialized = false;
    if (!initialized) {
        for (int i = 0; i < 10; i++) {
            previousPumpStates[i] = pumpStates[i];
            previousPumpModes[i] = pumpModes[i];
        }
        initialized = true;
    }

    // Check if it's time to broadcast pump states
    if (currentMillis - lastBroadcastTime >= broadcastInterval) {
        lastBroadcastTime = currentMillis;

        // Send updates if any state or mode has changed
        if (anyPumpStateChanged()) {
            broadcastPumpState(-1); // Broadcast all pump states
        }
        // sendHeatingCallStatus(); // now handled in PumpControl()
    }
}



// ***** Set Pump State Function *****
void setPumpState(int pumpIndex, int state) {
    int arrayIndex = pumpIndex; // Now zero-based index
    // Validate arrayIndex
    if (arrayIndex < 0 || arrayIndex >= 10) {
        LOG_ERR("[Pump] Invalid pump index in setPumpState.\n");
        return;
    }

    // Determine the digital signal to write based on the relay style
    bool isActiveHigh = pumpOnStateHigh[arrayIndex];
    int signal = (state == PUMP_ON) ? (isActiveHigh ? HIGH : LOW) :
                                       (isActiveHigh ? LOW : HIGH);

    // Update the physical state of the pump
    digitalWrite(pumpPins[arrayIndex], signal);
    pumpStates[arrayIndex] = state;

    // Log the event
    LOG_CAT(DBG_PUMP, "[Pump] Pump %d %s\n", pumpIndex + 1, (state == PUMP_ON ? "ON" : "OFF"));

    logPumpEvent(pumpIndex, state == PUMP_ON, getCurrentTimeAtomic());

    // Broadcast the new state
    broadcastPumpState(pumpIndex);
}


// ***** Toggle Pump State Function *****
void togglePumpState(int pumpIndex) {
    // Validate pumpIndex
    if (pumpIndex < 0 || pumpIndex >= 10) {
        LOG_ERR("[Pump] Invalid pump index in togglePumpState.\n");
        return;
    }

    int newState;

    // Acquire the pumpStateMutex before accessing pumpStates
    if (xSemaphoreTake(pumpStateMutex, portMAX_DELAY)) {
        newState = (pumpStates[pumpIndex] == PUMP_ON) ? PUMP_OFF : PUMP_ON;
        xSemaphoreGive(pumpStateMutex);
    } else {
        LOG_ERR("[Pump] Failed to take pumpStateMutex in togglePumpState.\n");
        return;
    }

    // Now call setPumpState outside the mutex to avoid deadlock
    setPumpState(pumpIndex, newState);
}


// ***** Apply Pump Mode Function *****
void applyPumpMode(int pumpIndex) {
    // Validate pumpIndex
    if (pumpIndex < 0 || pumpIndex >= 10) {
        LOG_ERR("[Pump] Invalid pump index in applyPumpMode.\n");
        return;
    }

    // Acquire the pumpStateMutex before accessing pumpModes
    if (xSemaphoreTake(pumpStateMutex, portMAX_DELAY)) {
        if (pumpModes[pumpIndex] == PUMP_AUTO) {
            // In AUTO mode, the PumpControl() function will handle the pump state based on conditions
            LOG_CAT(DBG_PUMP, "[Pump] Pump %d is set to AUTO mode.\n", pumpIndex + 1);
        } else if (pumpModes[pumpIndex] == PUMP_ON) {
            setPumpState(pumpIndex, PUMP_ON);
            LOG_CAT(DBG_PUMP, "[Pump] Pump %d is set to ON.\n", pumpIndex + 1);
        } else if (pumpModes[pumpIndex] == PUMP_OFF) {
            setPumpState(pumpIndex, PUMP_OFF);
            LOG_CAT(DBG_PUMP, "[Pump] Pump %d is set to OFF.\n", pumpIndex + 1);
        } else {
            LOG_ERR("[Pump] Invalid pump mode in applyPumpMode.\n");
        }
        // Release the mutex after accessing pumpModes
        xSemaphoreGive(pumpStateMutex);
    } else {
        LOG_ERR("[Pump] Failed to take pumpStateMutex in applyPumpMode.\n");
    }

    // Broadcasting the new mode state for the specific pump
    broadcastPumpState(pumpIndex);
}


// ***** Initialize Pumps Function *****
void initializePumps() {

  pinMode (DHW_HEATING_PIN, INPUT_PULLUP);     // Assuming active LOW
  pinMode (FURNACE_HEATING_PIN, INPUT_PULLUP); // Assuming active LOW
  

   
    for (int i = 0; i < 10; i++) {
        pinMode(pumpPins[i], OUTPUT);
        digitalWrite(pumpPins[i], pumpOffStateHigh[i] ? HIGH : LOW);
        pumpStates[i] = PUMP_OFF; // Explicit initialization
    }
}

// ***** Individual Pump Control Functions *****
// Note: These functions assume that both temperatureMutex and pumpStateMutex are already held by PumpControl()

void controlPanelLeadPump() {
    int pumpIndex = 0;
    //int pumpNumber = pumpIndex + 1;
    static unsigned long lastChangeTime = 0;
    unsigned long currentMillis = millis();

    if (pumpModes[pumpIndex] == PUMP_ON) {  // Manual ON
        if (pumpStates[pumpIndex] == PUMP_OFF &&
            (currentMillis - lastChangeTime >= LEAD_RELAY_CHANGE_INTERVAL)) {
            setPumpState(pumpIndex, PUMP_ON);
            lastChangeTime = currentMillis;
            LOG_CAT(DBG_PUMP, "[Pump] Panel Lead Pump ON (Manual)\n");
        }
    } else if (pumpModes[pumpIndex] == PUMP_OFF) {  // Manual OFF
        if (pumpStates[pumpIndex] == PUMP_ON &&
            (currentMillis - lastChangeTime >= LEAD_RELAY_CHANGE_INTERVAL)) {
            setPumpState(pumpIndex, PUMP_OFF);
            lastChangeTime = currentMillis;
            LOG_CAT(DBG_PUMP, "[Pump] Panel Lead Pump OFF (Manual)\n");
        }
    } else {  // Auto Mode
        if (panelT >= g_config.panelTminimumValue &&
                (panelT > (supplyT + g_config.panelOnDifferential))) {  // Turn ON Pump
                if (pumpStates[pumpIndex] == PUMP_OFF &&
                    (currentMillis - lastChangeTime >= LEAD_RELAY_CHANGE_INTERVAL)) {
                    setPumpState(pumpIndex, PUMP_ON);
                    lastChangeTime = currentMillis;
                    LOG_CAT(DBG_PUMP,
                            "[Pump] Panel Lead ON (AUTO) panelT>=min && panelT>(supplyT+onDiff)\n");
                }
            } else if (panelT < (supplyT + g_config.panelOffDifferential) ||
                       storageT >= g_config.storageHeatingLimit) {  // Turn OFF Pump
                if (pumpStates[pumpIndex] == PUMP_ON &&
                    (currentMillis - lastChangeTime >= LEAD_RELAY_CHANGE_INTERVAL)) {
                    setPumpState(pumpIndex, PUMP_OFF);
                    lastChangeTime = currentMillis;
                    LOG_CAT(DBG_PUMP,
                            "[Pump] Panel Lead OFF (AUTO) panelT<(supplyT+offDiff) || storageT>=limit\n");
                }
            }
     }
}

void controlPanelLagPump() {
    int pumpIndex = 1;
    //int pumpNumber = pumpIndex + 1;
    static unsigned long lastChangeTime = 0;
    unsigned long currentMillis = millis();

    if (pumpModes[pumpIndex] == PUMP_ON) {  // Manual ON
        if (pumpStates[pumpIndex] == PUMP_OFF &&
            (currentMillis - lastChangeTime >= LAG_RELAY_CHANGE_INTERVAL)) {
            setPumpState(pumpIndex, PUMP_ON);
            lastChangeTime = currentMillis;
            LOG_CAT(DBG_PUMP, "[Pump] Panel Lag Pump ON (Manual)\n");
        }
    } else if (pumpModes[pumpIndex] == PUMP_OFF) {  // Manual OFF
        if (pumpStates[pumpIndex] == PUMP_ON &&
            (currentMillis - lastChangeTime >= LAG_RELAY_CHANGE_INTERVAL)) {
            setPumpState(pumpIndex, PUMP_OFF);
            lastChangeTime = currentMillis;
            LOG_CAT(DBG_PUMP, "[Pump] Panel Lag Pump OFF (Manual)\n");
        }
    } else {  // Auto Mode
        if (CollectorTemperatureRise < g_config.panelOnDifferential) {  // Turn OFF Pump
            if (pumpStates[pumpIndex] == PUMP_ON &&
                (currentMillis - lastChangeTime >= LAG_RELAY_CHANGE_INTERVAL)) {
                setPumpState(pumpIndex, PUMP_OFF);
                lastChangeTime = currentMillis;
                LOG_CAT(DBG_PUMP, "[Pump] Panel Lag OFF (AUTO) - Low Differential\n");
            }
        } else if (pumpStates[0] == PUMP_ON) {  // Lead pump is running; turn ON Lag pump
            if (pumpStates[pumpIndex] == PUMP_OFF &&
                (currentMillis - lastChangeTime >= LAG_RELAY_CHANGE_INTERVAL)) {
                setPumpState(pumpIndex, PUMP_ON);
                lastChangeTime = currentMillis;
                LOG_CAT(DBG_PUMP, "[Pump] Panel Lag ON (AUTO) - Sufficient Differential\n");
            }
        }
    }
}

void controlHeatTape() {
    int pumpIndex = 2;
    //int pumpNumber = pumpIndex + 1;
    static unsigned long lastChangeTime = 0;
    unsigned long currentMillis = millis();

    if (pumpModes[pumpIndex] == PUMP_ON) {  // Manual ON
        if (pumpStates[pumpIndex] == PUMP_OFF &&
            (currentMillis - lastChangeTime >= HEAT_TAPE_RELAY_CHANGE_INTERVAL)) {
            setPumpState(pumpIndex, PUMP_ON);
            lastChangeTime = currentMillis;
            LOG_CAT(DBG_PUMP, "[Pump] Heat Tape ON (Manual)\n");
        }
    } else if (pumpModes[pumpIndex] == PUMP_OFF) {  // Manual OFF
        if (pumpStates[pumpIndex] == PUMP_ON &&
            (currentMillis - lastChangeTime >= HEAT_TAPE_RELAY_CHANGE_INTERVAL)) {
            setPumpState(pumpIndex, PUMP_OFF);
            lastChangeTime = currentMillis;
            LOG_CAT(DBG_PUMP, "[Pump] Heat Tape OFF (Manual)\n");
        }
    } else {  // Auto Mode
        if (CSupplyT <= g_config.heatTapeOn) {
            // Turn ON Heat Tape
            if (pumpStates[pumpIndex] == PUMP_OFF &&
                (currentMillis - lastChangeTime >= HEAT_TAPE_RELAY_CHANGE_INTERVAL)) {
                setPumpState(pumpIndex, PUMP_ON);
                lastChangeTime = currentMillis;
                LOG_CAT(DBG_PUMP, "[Pump] Heat Tape ON (AUTO) - CSupplyT <= On\n");
            }
        } else if (CSupplyT >= g_config.heatTapeOff &&
                   pumpStates[pumpIndex] == PUMP_ON &&
                   (currentMillis - lastChangeTime >= HEAT_TAPE_RELAY_CHANGE_INTERVAL)) {
            // Turn OFF Heat Tape
            setPumpState(pumpIndex, PUMP_OFF);
            lastChangeTime = currentMillis;
            LOG_CAT(DBG_PUMP, "[Pump] Heat Tape OFF (AUTO) - CSupplyT >= Off\n");
        }
    }
}

void controlCirculationPump() {
    int pumpIndex = 3;
    //int pumpNumber = pumpIndex + 1;
    static unsigned long lastChangeTime = 0;
    unsigned long currentMillis = millis();

    if (pumpModes[pumpIndex] == PUMP_ON) {  // Manual ON
        if (pumpStates[pumpIndex] == PUMP_OFF &&
            (currentMillis - lastChangeTime >= CIRC_RELAY_CHANGE_INTERVAL)) {
            setPumpState(pumpIndex, PUMP_ON);
            lastChangeTime = currentMillis;
            LOG_CAT(DBG_PUMP, "[Pump] Circulation Pump ON (Manual)\n");
        }
    } else if (pumpModes[pumpIndex] == PUMP_OFF) {  // Manual OFF
        if (pumpStates[pumpIndex] == PUMP_ON &&
            (currentMillis - lastChangeTime >= CIRC_RELAY_CHANGE_INTERVAL)) {
            setPumpState(pumpIndex, PUMP_OFF);
            lastChangeTime = currentMillis;
            LOG_CAT(DBG_PUMP, "[Pump] Circulation Pump OFF (Manual)\n");
        }
    } else {  // Auto Mode
        if (pumpStates[4] == PUMP_ON || pumpStates[5] == PUMP_ON) { // DHW or Storage Heating is ON
            if (pumpStates[pumpIndex] == PUMP_ON &&
                (currentMillis - lastChangeTime >= CIRC_RELAY_CHANGE_INTERVAL)) {
                setPumpState(pumpIndex, PUMP_OFF);
                lastChangeTime = currentMillis;
                LOG_CAT(DBG_PUMP, "[Pump] Circ OFF due to Heating/DHW Call (AUTO)\n");
            }
        } else {  // Normal Circ Pump Auto Control based on temperature differential
            if (CirculationTemperatureDrop >= g_config.circPumpOn) {
                if (pumpStates[pumpIndex] == PUMP_OFF &&
                    (currentMillis - lastChangeTime >= CIRC_RELAY_CHANGE_INTERVAL)) {
                    setPumpState(pumpIndex, PUMP_ON);
                    lastChangeTime = currentMillis;
                    LOG_CAT(DBG_PUMP, "[Pump] Circ ON (AUTO) - TempDrop >= On\n");
                }
            } else if (CirculationTemperatureDrop <= g_config.circPumpOff) {
                if (pumpStates[pumpIndex] == PUMP_ON &&
                    (currentMillis - lastChangeTime >= CIRC_RELAY_CHANGE_INTERVAL)) {
                    setPumpState(pumpIndex, PUMP_OFF);
                    lastChangeTime = currentMillis;
                    LOG_CAT(DBG_PUMP, "[Pump] Circ OFF (AUTO) - TempDrop <= Off\n");
                }
            }
        }
    }
}

void controlDHWPump() {
    int pumpIndex = 4;
    //int pumpNumber = pumpIndex + 1;
    static unsigned long lastChangeTime = 0;
    unsigned long currentMillis = millis();
    int DHW_Heating_Call = digitalRead(DHW_HEATING_PIN); // LOW when active

    if (pumpModes[pumpIndex] == PUMP_ON) {  // Manual ON
        if (pumpStates[pumpIndex] == PUMP_OFF &&
            (currentMillis - lastChangeTime >= DHW_RELAY_CHANGE_INTERVAL)) {
            setPumpState(pumpIndex, PUMP_ON);
            lastChangeTime = currentMillis;
            LOG_CAT(DBG_PUMP, "[Pump] DHW Pump ON (Manual)\n");
        }
    } else if (pumpModes[pumpIndex] == PUMP_OFF) {  // Manual OFF
        if (pumpStates[pumpIndex] == PUMP_ON &&
            (currentMillis - lastChangeTime >= DHW_RELAY_CHANGE_INTERVAL)) {
            setPumpState(pumpIndex, PUMP_OFF);
            lastChangeTime = currentMillis;
            LOG_CAT(DBG_PUMP, "[Pump] DHW Pump OFF (Manual)\n");
        }
    } else {  // Auto Mode
        if (DHW_Heating_Call == LOW) {  // Turn ON Pump
            if (pumpStates[pumpIndex] == PUMP_OFF &&
                ((currentMillis - lastChangeTime >= DHW_RELAY_CHANGE_INTERVAL) || lastChangeTime == 0)) {
                setPumpState(pumpIndex, PUMP_ON);
                lastChangeTime = currentMillis;
                LOG_CAT(DBG_PUMP, "[Pump] DHW Pump ON (AUTO) - Call Active (pin LOW)\n");
            }
        } else if (DHW_Heating_Call == HIGH &&
                   pumpStates[pumpIndex] == PUMP_ON &&
                   (currentMillis - lastChangeTime >= DHW_RELAY_CHANGE_INTERVAL)) { // Turn OFF Pump
            setPumpState(pumpIndex, PUMP_OFF);
            lastChangeTime = currentMillis;
            LOG_CAT(DBG_PUMP, "[Pump] DHW Pump OFF (AUTO) - Call Inactive (pin HIGH)\n");
        }
    }
}

void controlStorageHeatPump() {
    int pumpIndex = 5;
    //int pumpNumber = pumpIndex + 1;
    static unsigned long lastChangeTime = 0;
    unsigned long currentMillis = millis();
    int Furnace_Heating_Call = digitalRead(FURNACE_HEATING_PIN); // LOW when active

    if (pumpModes[pumpIndex] == PUMP_ON) {  // Manual ON
        if (pumpStates[pumpIndex] == PUMP_OFF &&
            (currentMillis - lastChangeTime >= HEATING_RELAY_CHANGE_INTERVAL)) {
            setPumpState(pumpIndex, PUMP_ON);
            lastChangeTime = currentMillis;
            LOG_CAT(DBG_PUMP, "[Pump] Storage Heat Pump ON (Manual)\n");
        }
    } else if (pumpModes[pumpIndex] == PUMP_OFF) { // Manual OFF
        if (pumpStates[pumpIndex] == PUMP_ON &&
            (currentMillis - lastChangeTime >= HEATING_RELAY_CHANGE_INTERVAL)) {
            setPumpState(pumpIndex, PUMP_OFF);
            lastChangeTime = currentMillis;
            LOG_CAT(DBG_PUMP, "[Pump] Storage Heat Pump OFF (Manual)\n");
        }
    } else { // Auto Mode
        if (pumpStates[4] == PUMP_OFF) { // DHW Pump is OFF
            if (Furnace_Heating_Call == LOW) {  // Turn ON Pump
                if (pumpStates[pumpIndex] == PUMP_OFF &&
                   ((currentMillis - lastChangeTime >= HEATING_RELAY_CHANGE_INTERVAL) || lastChangeTime == 0)) {
                    setPumpState(pumpIndex, PUMP_ON);
                    lastChangeTime = currentMillis;
                    LOG_CAT(DBG_PUMP, "[Pump] Storage Heat Pump ON (AUTO) - Call Active (pin LOW)\n");
                }
            } else if (pumpStates[pumpIndex] == PUMP_ON &&
                       (currentMillis - lastChangeTime >= HEATING_RELAY_CHANGE_INTERVAL)) { // Turn OFF Pump
                setPumpState(pumpIndex, PUMP_OFF);
                lastChangeTime = currentMillis;
                LOG_CAT(DBG_PUMP, "[Pump] Storage Heat Pump OFF (AUTO) - Call Inactive (pin HIGH)\n");
            }
        } else { // DHW Pump is ON; ensure Storage Heat Pump is OFF
            if (pumpStates[pumpIndex] == PUMP_ON &&
                (currentMillis - lastChangeTime >= HEATING_RELAY_CHANGE_INTERVAL)) {
                setPumpState(pumpIndex, PUMP_OFF);
                lastChangeTime = currentMillis;
                LOG_CAT(DBG_PUMP, "[Pump] Storage Heat Pump OFF due to DHW ON\n");
            }
        }
    }
}

void controlBoilerCirculatorPump() {
    int pumpIndex = 6;
    //int pumpNumber = pumpIndex + 1;
    static unsigned long lastChangeTime = 0;
    unsigned long currentMillis = millis();

    // Read Heating Calls
    int DHW_Heating_Call     = digitalRead(DHW_HEATING_PIN);     // LOW when active
    int Furnace_Heating_Call = digitalRead(FURNACE_HEATING_PIN); // LOW when active
    bool heatingCallActive = (DHW_Heating_Call == LOW || Furnace_Heating_Call == LOW);

    // Check Temperature Thresholds
    bool tempBelowOnThreshold = (storageT <  g_config.boilerCircOn);
    bool tempAboveOffThreshold = (storageT >= g_config.boilerCircOff);

    if (pumpModes[pumpIndex] == PUMP_ON) { // Manual ON
        if (pumpStates[pumpIndex] == PUMP_OFF &&
            (currentMillis - lastChangeTime >= BOILER_RELAY_CHANGE_INTERVAL)) {
            setPumpState(pumpIndex, PUMP_ON);
            lastChangeTime = currentMillis;
            LOG_CAT(DBG_PUMP, "[Pump] Boiler Circulator ON (Manual)\n");
        }
    } else if (pumpModes[pumpIndex] == PUMP_OFF) { // Manual OFF
        if (pumpStates[pumpIndex] == PUMP_ON &&
            (currentMillis - lastChangeTime >= BOILER_RELAY_CHANGE_INTERVAL)) {
            setPumpState(pumpIndex, PUMP_OFF);
            lastChangeTime = currentMillis;
            LOG_CAT(DBG_PUMP, "[Pump] Boiler Circulator OFF (Manual)\n");
        }
    } else { // Auto Mode
        if (heatingCallActive && tempBelowOnThreshold) {
            // Turn ON Pump
            if (pumpStates[pumpIndex] == PUMP_OFF &&
                (currentMillis - lastChangeTime >= BOILER_RELAY_CHANGE_INTERVAL)) {
                setPumpState(pumpIndex, PUMP_ON);
                lastChangeTime = currentMillis;
                LOG_CAT(DBG_PUMP, "[Pump] Boiler Circ ON (AUTO) storageT < boilerCircOn\n");
            }
        } else if ((!heatingCallActive || tempAboveOffThreshold) &&
                   pumpStates[pumpIndex] == PUMP_ON &&
                   (currentMillis - lastChangeTime >= BOILER_RELAY_CHANGE_INTERVAL)) {
            // Turn OFF Pump
            setPumpState(pumpIndex, PUMP_OFF);
            lastChangeTime = currentMillis;
            LOG_CAT(DBG_PUMP, "[Pump] Boiler Circ OFF (AUTO)\n");
        }
    }
}

void controlRecirculationValve() {
    int pumpIndex = 7;
    //int pumpNumber = pumpIndex + 1;
    static unsigned long lastChangeTime = 0;
    unsigned long currentMillis = millis();

    if (pumpModes[pumpIndex] == PUMP_ON) { // Manual ON
        if (pumpStates[pumpIndex] == PUMP_OFF &&
            (currentMillis - lastChangeTime >= RECIRC_RELAY_CHANGE_INTERVAL)) {
            setPumpState(pumpIndex, PUMP_ON); // Valve Closed
            lastChangeTime = currentMillis;
            LOG_CAT(DBG_PUMP, "[Pump] Recirculation Valve ON (Manual)\n");
        }
    } else if (pumpModes[pumpIndex] == PUMP_OFF) { // Manual OFF
        if (pumpStates[pumpIndex] == PUMP_ON &&
            (currentMillis - lastChangeTime >= RECIRC_RELAY_CHANGE_INTERVAL)) {
            setPumpState(pumpIndex, PUMP_OFF); // Valve Open
            lastChangeTime = currentMillis;
            LOG_CAT(DBG_PUMP, "[Pump] Recirculation Valve OFF (Manual)\n");
        }
    } else { // Auto Mode
        if (pumpStates[6] == PUMP_ON && (DhwReturnT > storageT || HeatingReturnT > storageT)) {
            // Turn ON Valve (Closed)
            if (pumpStates[pumpIndex] == PUMP_OFF &&
                (currentMillis - lastChangeTime >= RECIRC_RELAY_CHANGE_INTERVAL)) {
                setPumpState(pumpIndex, PUMP_ON);
                lastChangeTime = currentMillis;
                LOG_CAT(DBG_PUMP, "[Pump] Recirc Valve ON (AUTO) return > storage\n");
            }
        } else { // Turn OFF Valve (Open)
            if (pumpStates[pumpIndex] == PUMP_ON &&
                (currentMillis - lastChangeTime >= RECIRC_RELAY_CHANGE_INTERVAL)) {
                setPumpState(pumpIndex, PUMP_OFF);
                lastChangeTime = currentMillis;
                LOG_CAT(DBG_PUMP, "[Pump] Recirc Valve OFF (AUTO)\n");
            }
        }
    }
}

void PumpControl() {
  esp_task_wdt_reset();

  // 1) Lock temperature first (consistent ordering)
  if (!xSemaphoreTake(temperatureMutex, portMAX_DELAY)) {
    LOG_ERR("[Pump] Failed to take temperatureMutex in PumpControl.\n");
    return;
  }
  esp_task_wdt_reset();

  // 2) Then lock pump state
  if (!xSemaphoreTake(pumpStateMutex, portMAX_DELAY)) {
    xSemaphoreGive(temperatureMutex);
    LOG_ERR("[Pump] Failed to take pumpStateMutex in PumpControl.\n");
    return;
  }
  esp_task_wdt_reset();

  // 3) Heating call pins
  bool dhwCall  = (digitalRead(DHW_HEATING_PIN)     == LOW);
  bool heatCall = (digitalRead(FURNACE_HEATING_PIN) == LOW);

  if (dhwCall != previousDHWCallStatus || heatCall != previousHeatingCallStatus) {
    previousDHWCallStatus     = dhwCall;
    previousHeatingCallStatus = heatCall;
    sendHeatingCallStatus(dhwCall, heatCall);
  }

  // 4) Freeze protection
  const uint32_t nowMs = (uint32_t)millis();

  const bool collectorOverride = updateCollectorFreezeProtect(nowMs);
  const bool lineOverride      = updateLineFreezeProtect(nowMs);

  // 5) Normal control (skipped if freeze override owns it)
  if (!collectorOverride) controlPanelLeadPump();
  if (!collectorOverride) controlPanelLagPump();

  controlHeatTape();

  if (!lineOverride) controlCirculationPump();

  controlDHWPump();
  controlStorageHeatPump();
  controlBoilerCirculatorPump();
  controlRecirculationValve();

  // 6) Release locks
  xSemaphoreGive(pumpStateMutex);
  xSemaphoreGive(temperatureMutex);

  esp_task_wdt_reset();
}
