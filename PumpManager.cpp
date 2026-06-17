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

static constexpr float FREEZE_HYST_F = 2.0f;  // °F hysteresis for collector/circ “clear”

static inline uint32_t minToMs(uint16_t minutes) {
  return (uint32_t)minutes * 60000UL;
}

static uint32_t s_colBelowSinceMs  = 0;
static uint32_t s_colRunUntilMs    = 0;
static bool     s_colRunActive     = false;

static uint32_t s_circBelowSinceMs = 0;
static uint32_t s_circRunUntilMs   = 0;
static bool     s_circRunActive    = false;

static uint32_t s_freezeLeadLastChangeMs = 0;
static uint32_t s_freezeLagLastChangeMs  = 0;
static uint32_t s_freezeCircLastChangeMs = 0;


// ------ Heat tape ineffective values come from Web Config (g_config) --------
#define HEAT_TAPE_BAD_F     (g_config.heatTapeBadF)
#define HEAT_TAPE_CLEAR_F   (g_config.heatTapeClearF)
#define HEAT_TAPE_EVAL_MS      (minToMs(g_config.heatTapeEvalMin))

static uint32_t s_htOnSinceMs   = 0;
static bool     s_htAlarmed     = false;


// ---------- Tank freeze values come from Web Config (g_config) ----------
#define TANK_FREEZE_TEMP_F      (g_config.tankFreezeTempF)
#define TANK_FREEZE_CLEAR_F     (g_config.tankFreezeClearF)
#define TANK_FREEZE_CONFIRM_MS (minToMs(g_config.tankFreezeConfirmMin))

static uint32_t s_tankBelowSinceMs = 0;
static bool     s_tankRunActive    = false;



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

// ------------------- FREEZE PROTECTION HELPERS (NEW) -------------------
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

// Returns true ONLY when we are actively overriding Lead+Lag (AUTO) for freeze protection.
static bool updateCollectorFreezeProtect(float cSupplyF, uint32_t nowMs)
{
  const float freezeTempF = g_config.collectorFreezeTempF;
  const float clearTempF  = freezeTempF + FREEZE_HYST_F;
  const uint32_t confirmMs = minToMs(g_config.collectorFreezeConfirmMin);
  const uint32_t runMs     = minToMs(g_config.collectorFreezeRunMin);

  if (!tempValidF(cSupplyF)) {
    s_colBelowSinceMs = 0;
    s_colRunActive    = false;
    s_colRunUntilMs   = 0;
    AlarmManager_clear(ALM_COLLECTOR_FREEZE_PROTECT);
    return false;
  }

  if (cSupplyF >= clearTempF) {
    s_colBelowSinceMs = 0;
    s_colRunActive    = false;
    s_colRunUntilMs   = 0;
    AlarmManager_clear(ALM_COLLECTOR_FREEZE_PROTECT);
    return false;
  }

  if (cSupplyF <= freezeTempF) {
    if (s_colBelowSinceMs == 0) s_colBelowSinceMs = nowMs;

    if (!s_colRunActive && (nowMs - s_colBelowSinceMs >= confirmMs)) {
      s_colRunActive  = true;
      s_colRunUntilMs = nowMs + runMs;

      AlarmManager_event(ALM_COLLECTOR_FREEZE_PROTECT, ALM_WARN,
        "Collector freeze cycle start (CSupply=%.1fF)", (double)cSupplyF);
    }
  }

  if (s_colRunActive && nowMs >= s_colRunUntilMs) {
    if (cSupplyF <= freezeTempF) {
      s_colRunUntilMs = nowMs + runMs;
      AlarmManager_event(ALM_COLLECTOR_FREEZE_PROTECT, ALM_WARN,
        "Collector freeze cycle restart (CSupply=%.1fF)", (double)cSupplyF);
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
static bool updateCircFreezeProtect(float supplyF, float circReturnF, uint32_t nowMs)
{
  const float freezeTempF  = g_config.circFreezeTempF;
  const float clearTempF   = freezeTempF + FREEZE_HYST_F;
  const uint32_t confirmMs = minToMs(g_config.circFreezeConfirmMin);
  const uint32_t runMs     = minToMs(g_config.circFreezeRunMin);

  const bool supplyValid = tempValidF(supplyF);
  const bool retValid    = tempValidF(circReturnF);

  // If both invalid, don't keep stale state
  if (!supplyValid && !retValid) {
    s_circBelowSinceMs = 0;
    s_circRunActive    = false;
    s_circRunUntilMs   = 0;
    AlarmManager_clear(ALM_CIRC_FREEZE_PROTECT);
    return false;
  }

  // Determine “cold” using whichever sensors are valid
  bool isCold = false;
  float minT = 999.0f;

  if (supplyValid) { isCold |= (supplyF <= freezeTempF);     minT = min(minT, supplyF); }
  if (retValid)    { isCold |= (circReturnF <= freezeTempF); minT = min(minT, circReturnF); }

  // Clear when ALL valid temps are safely warm
  bool isWarmEnough = true;
  if (supplyValid) isWarmEnough &= (supplyF >= clearTempF);
  if (retValid)    isWarmEnough &= (circReturnF >= clearTempF);

  if (isWarmEnough) {
    s_circBelowSinceMs = 0;
    s_circRunActive    = false;
    s_circRunUntilMs   = 0;
    AlarmManager_clear(ALM_CIRC_FREEZE_PROTECT);
    return false;
  }

  if (isCold) {
    if (s_circBelowSinceMs == 0) s_circBelowSinceMs = nowMs;

    if (!s_circRunActive && (nowMs - s_circBelowSinceMs >= confirmMs)) {
      s_circRunActive  = true;
      s_circRunUntilMs = nowMs + runMs;

      AlarmManager_event(ALM_CIRC_FREEZE_PROTECT, ALM_WARN,
                         "Circ freeze cycle start (min=%.1fF)", (double)minT);
    }
  }

  if (s_circRunActive && nowMs >= s_circRunUntilMs) {
    if (isCold) {
      s_circRunUntilMs = nowMs + runMs;
      AlarmManager_event(ALM_CIRC_FREEZE_PROTECT, ALM_WARN,
                         "Circ freeze cycle restart (min=%.1fF)", (double)minT);
    } else {
      s_circRunActive = false;
    }
  }

  const bool circAuto = (pumpModes[3] == PUMP_AUTO);

  if (s_circRunActive && circAuto) {
    AlarmManager_set(ALM_CIRC_FREEZE_PROTECT, ALM_ALARM,
                     "Circ freeze protection active");
    forcePumpOnIfAuto(3, nowMs, CIRC_RELAY_CHANGE_INTERVAL, s_freezeCircLastChangeMs);
    return true;
  }

  if (s_circRunActive && !circAuto) {
    AlarmManager_set(ALM_CIRC_FREEZE_PROTECT, ALM_ALARM,
                     "Circ freeze protect blocked (Circ not AUTO)");
  }

  return false;
}


// ------------------- HEAT TAPE INEFFECTIVE -------------------
static void updateHeatTapeIneffective(float cSupplyF, uint32_t nowMs)
{
  const bool htOn = (pumpStates[2] == PUMP_ON);

  const float badF   = g_config.heatTapeBadF;
  const float clearF = g_config.heatTapeClearF;
  const uint32_t evalMs = minToMs(g_config.heatTapeEvalMin);

  if (!htOn || !tempValidF(cSupplyF)) {
    s_htOnSinceMs = 0;
    s_htAlarmed   = false;
    AlarmManager_clear(ALM_HEAT_TAPE_INEFFECTIVE);
    return;
  }

  if (s_htOnSinceMs == 0) {
    s_htOnSinceMs = nowMs;
    s_htAlarmed   = false;
  }

  if (!s_htAlarmed && (nowMs - s_htOnSinceMs >= evalMs)) {
    if (cSupplyF <= badF) {
      AlarmManager_set(ALM_HEAT_TAPE_INEFFECTIVE, ALM_ALARM,
                       "Heat tape ineffective (CSupply still freezing)");
      AlarmManager_event(ALM_HEAT_TAPE_INEFFECTIVE, ALM_ALARM,
                         "Heat tape ON %lus, CSupply=%.1fF",
                         (unsigned long)((nowMs - s_htOnSinceMs) / 1000UL),
                         (double)cSupplyF);
      s_htAlarmed = true;
    }
  }

  if (s_htAlarmed && cSupplyF >= clearF) {
    AlarmManager_clear(ALM_HEAT_TAPE_INEFFECTIVE, "Heat tape recovered");
    s_htAlarmed   = false;
    s_htOnSinceMs = nowMs;
  }
}


// ------------------- TANK FREEZE PROTECTION  -------------------
static bool updateTankFreezeProtect(float storageF, uint32_t nowMs)
{
  const float triggerF  = g_config.tankFreezeTempF;
  const float clearF    = g_config.tankFreezeClearF;
  const uint32_t confirmMs = minToMs(g_config.tankFreezeConfirmMin);

  if (!tempValidF(storageF)) {
    s_tankBelowSinceMs = 0;
    s_tankRunActive    = false;
    AlarmManager_clear(ALM_TANK_FREEZE_PROTECT);
    return false;
  }

  if (s_tankRunActive) {
    if (storageF >= clearF) {
      s_tankRunActive = false;
      s_tankBelowSinceMs = 0;
      AlarmManager_clear(ALM_TANK_FREEZE_PROTECT, "Tank warmed above clear temp");
      AlarmManager_event(ALM_TANK_FREEZE_PROTECT, ALM_WARN,
                         "Tank freeze protect ended (storageT=%.1fF)", (double)storageF);
      return false;
    }

    if (pumpModes[3] == PUMP_AUTO) {
      AlarmManager_set(ALM_TANK_FREEZE_PROTECT, ALM_ALARM,
                       "Tank freeze protection active");
      forcePumpOnIfAuto(3, nowMs, CIRC_RELAY_CHANGE_INTERVAL, s_freezeCircLastChangeMs);
      return true;
    } else {
      AlarmManager_set(ALM_TANK_FREEZE_PROTECT, ALM_ALARM,
                       "Tank freeze protect blocked (Circ not AUTO)");
      return false;
    }
  }

  if (storageF <= triggerF) {
    if (s_tankBelowSinceMs == 0) s_tankBelowSinceMs = nowMs;

    if (nowMs - s_tankBelowSinceMs >= confirmMs) {
      s_tankRunActive = true;
      AlarmManager_event(ALM_TANK_FREEZE_PROTECT, ALM_WARN,
                         "Tank freeze protect started (storageT=%.1fF)", (double)storageF);

      if (pumpModes[3] == PUMP_AUTO) {
        AlarmManager_set(ALM_TANK_FREEZE_PROTECT, ALM_ALARM,
                         "Tank freeze protection active");
        forcePumpOnIfAuto(3, nowMs, CIRC_RELAY_CHANGE_INTERVAL, s_freezeCircLastChangeMs);
        return true;
      } else {
        AlarmManager_set(ALM_TANK_FREEZE_PROTECT, ALM_ALARM,
                         "Tank freeze protect blocked (Circ not AUTO)");
        return false;
      }
    }
  } else {
    s_tankBelowSinceMs = 0;
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
        Serial.println("Invalid pump index in setPumpMode.");
        return;
    }

    // Acquire the pumpStateMutex before modifying pumpModes
    if (xSemaphoreTake(pumpStateMutex, portMAX_DELAY)) {
        pumpModes[pumpIndex] = mode;
        Serial.println("Pump " + String(pumpIndex + 1) + " mode set to " + 
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
        Serial.println("Failed to take pumpStateMutex in setPumpMode.");
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
    Serial.println("All pumps returned to Auto mode.");
}

// ***** Function to Turn All Pumps On for 10 Minutes *****
void turnOnAllPumpsFor10Minutes() {
    for (int i = 0; i < 10; i++) {
        setPumpMode(i, PUMP_ON); // Turn each pump on
    }
    Serial.println("All pumps turned on for 10 minutes.");
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
        // Send heating call statuses
       // sendHeatingCallStatus(); // now handled in PumpControl()
    }
}



// ***** Set Pump State Function *****
void setPumpState(int pumpIndex, int state) {
    int arrayIndex = pumpIndex; // Now zero-based index
    // Validate arrayIndex
    if (arrayIndex < 0 || arrayIndex >= 10) {
        Serial.println("Invalid pump index in setPumpState.");
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
    String event = "Pump " + String(pumpIndex + 1) + " " +
                   (state == PUMP_ON ? "ON" : "OFF");
    Serial.println(event);

    logPumpEvent( pumpIndex, state == PUMP_ON, getCurrentTimeAtomic() );
    //logPumpEvent(pumpIndex, state == PUMP_ON ? "START" : "STOP");

    // Broadcast the new state
    broadcastPumpState(pumpIndex);
}

// ***** Toggle Pump State Function *****
void togglePumpState(int pumpIndex) {
    // Validate pumpIndex
    if (pumpIndex < 0 || pumpIndex >= 10) {
        Serial.println("Invalid pump index in togglePumpState.");
        return;
    }

    int newState;

    // Acquire the pumpStateMutex before accessing pumpStates
    if (xSemaphoreTake(pumpStateMutex, portMAX_DELAY)) {
        newState = (pumpStates[pumpIndex] == PUMP_ON) ? PUMP_OFF : PUMP_ON;
        xSemaphoreGive(pumpStateMutex);
    } else {
        Serial.println("Failed to take pumpStateMutex in togglePumpState.");
        return;
    }

    // Now call setPumpState outside the mutex to avoid deadlock
    setPumpState(pumpIndex, newState);
}

// ***** Apply Pump Mode Function *****
void applyPumpMode(int pumpIndex) {
    // Validate pumpIndex
    if (pumpIndex < 0 || pumpIndex >= 10) {
        Serial.println("Invalid pump index in applyPumpMode.");
        return;
    }

    // Acquire the pumpStateMutex before accessing pumpModes
    if (xSemaphoreTake(pumpStateMutex, portMAX_DELAY)) {
        if (pumpModes[pumpIndex] == PUMP_AUTO) {
            // In AUTO mode, the PumpControl() function will handle the pump state based on conditions
            Serial.println("Pump " + String(pumpIndex + 1) + " is set to AUTO mode.");
        } else if (pumpModes[pumpIndex] == PUMP_ON) {
            setPumpState(pumpIndex, PUMP_ON);
            Serial.println("Pump " + String(pumpIndex + 1) + " is set to ON.");
        } else if (pumpModes[pumpIndex] == PUMP_OFF) {
            setPumpState(pumpIndex, PUMP_OFF);
            Serial.println("Pump " + String(pumpIndex + 1) + " is set to OFF.");
        } else {
            Serial.println("Invalid pump mode in applyPumpMode.");
        }
        // Release the mutex after accessing pumpModes
        xSemaphoreGive(pumpStateMutex);
    } else {
        Serial.println("Failed to take pumpStateMutex in applyPumpMode.");
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

/*
void ALARM() {
  if dhwT < Storage_Tank_Temp_Limit {
    ALARM == ALARM_OFF
  }
  }
  else {
    ALARM == ALARM_ON
  }
  */
// ***** Individual Pump Control Functions *****

// Note: These functions assume that both temperatureMutex and pumpStateMutex are already held by PumpControl()

void controlPanelLeadPump() {
    int pumpIndex = 0;
    int pumpNumber = pumpIndex + 1;
    static unsigned long lastChangeTime = 0;
    unsigned long currentMillis = millis();

    if (pumpModes[pumpIndex] == PUMP_ON) {  // Manual ON
        if (pumpStates[pumpIndex] == PUMP_OFF &&
            (currentMillis - lastChangeTime >= LEAD_RELAY_CHANGE_INTERVAL)) {
            setPumpState(pumpIndex, PUMP_ON);
            lastChangeTime = currentMillis;
            Serial.println("Panel Lead Pump turned ON (Manual)");
            //broadcastPumpState(pumpNumber);
        }
    } else if (pumpModes[pumpIndex] == PUMP_OFF) {  // Manual OFF
        if (pumpStates[pumpIndex] == PUMP_ON &&
            (currentMillis - lastChangeTime >= LEAD_RELAY_CHANGE_INTERVAL)) {
            setPumpState(pumpIndex, PUMP_OFF);
            lastChangeTime = currentMillis;
            Serial.println("Panel Lead Pump turned OFF (Manual)");
            //broadcastPumpState(pumpNumber);
        }
    } else {  // Auto Mode
        if (panelT >= g_config.panelTminimumValue &&
                (panelT > (supplyT + g_config.panelOnDifferential))) {  // Turn ON Pump
                if (pumpStates[pumpIndex] == PUMP_OFF &&
                    (currentMillis - lastChangeTime >= LEAD_RELAY_CHANGE_INTERVAL)) {
                    setPumpState(pumpIndex, PUMP_ON);
                    lastChangeTime = currentMillis;
                    Serial.println("Panel Lead Pump turned ON (AUTO) panelT >= g_config.panelTminimumValue && panelT > (supplyT + g_config.panelOnDifferential)");
                    //broadcastPumpState(pumpNumber);
                }
            } else if (panelT < (supplyT + g_config.panelOffDifferential) ||
                       storageT >= g_config.storageHeatingLimit) {  // Turn OFF Pump
                if (pumpStates[pumpIndex] == PUMP_ON &&
                    (currentMillis - lastChangeTime >= LEAD_RELAY_CHANGE_INTERVAL)) {
                    setPumpState(pumpIndex, PUMP_OFF);
                    lastChangeTime = currentMillis;
                    Serial.println("Panel Lead Pump turned OFF (AUTO) (panelT < (supplyT + g_config.panelOffDifferential) || storageT >= g_config.storageHeatingLimit)");
                    //broadcastPumpState(pumpNumber);
                }
            }
     }
}

void controlPanelLagPump() {
    int pumpIndex = 1;
    int pumpNumber = pumpIndex + 1;
    static unsigned long lastChangeTime = 0;
    unsigned long currentMillis = millis();

    if (pumpModes[pumpIndex] == PUMP_ON) {  // Manual ON
        if (pumpStates[pumpIndex] == PUMP_OFF &&
            (currentMillis - lastChangeTime >= LAG_RELAY_CHANGE_INTERVAL)) {
            setPumpState(pumpIndex, PUMP_ON);
            lastChangeTime = currentMillis;
            Serial.println("Panel Lag Pump turned ON (Manual)");
            //broadcastPumpState(pumpNumber);
        }
    } else if (pumpModes[pumpIndex] == PUMP_OFF) {  // Manual OFF
        if (pumpStates[pumpIndex] == PUMP_ON &&
            (currentMillis - lastChangeTime >= LAG_RELAY_CHANGE_INTERVAL)) {
            setPumpState(pumpIndex, PUMP_OFF);
            lastChangeTime = currentMillis;
            Serial.println("Panel Lag Pump turned OFF (Manual)");
            //broadcastPumpState(pumpNumber);
        }
    } else {  // Auto Mode
        if (CollectorTemperatureRise < g_config.panelOnDifferential) {  // Turn OFF Pump
            if (pumpStates[pumpIndex] == PUMP_ON &&
                (currentMillis - lastChangeTime >= LAG_RELAY_CHANGE_INTERVAL)) {
                setPumpState(pumpIndex, PUMP_OFF);
                lastChangeTime = currentMillis;
                Serial.println("Panel Lag Pump turned OFF (AUTO) - Low Differential");
                //broadcastPumpState(pumpNumber);
            }
        } else if (pumpStates[0] == PUMP_ON) {  // Lead pump is running; turn ON Lag pump
            if (pumpStates[pumpIndex] == PUMP_OFF &&
                (currentMillis - lastChangeTime >= LAG_RELAY_CHANGE_INTERVAL)) {
                setPumpState(pumpIndex, PUMP_ON);
                lastChangeTime = currentMillis;
                Serial.println("Panel Lag Pump turned ON (AUTO) - Sufficient Differential");
                //broadcastPumpState(pumpNumber);
            }
        }
    }
}

void controlHeatTape() {
    int pumpIndex = 2;
    int pumpNumber = pumpIndex + 1;
    static unsigned long lastChangeTime = 0;
    unsigned long currentMillis = millis();

    if (pumpModes[pumpIndex] == PUMP_ON) {  // Manual ON
        if (pumpStates[pumpIndex] == PUMP_OFF &&
            (currentMillis - lastChangeTime >= HEAT_TAPE_RELAY_CHANGE_INTERVAL)) {
            setPumpState(pumpIndex, PUMP_ON);
            lastChangeTime = currentMillis;
            Serial.println("Heat Tape turned ON (Manual)");
            //broadcastPumpState(pumpNumber);
        }
    } else if (pumpModes[pumpIndex] == PUMP_OFF) {  // Manual OFF
        if (pumpStates[pumpIndex] == PUMP_ON &&
            (currentMillis - lastChangeTime >= HEAT_TAPE_RELAY_CHANGE_INTERVAL)) {
            setPumpState(pumpIndex, PUMP_OFF);
            lastChangeTime = currentMillis;
            Serial.println("Heat Tape turned OFF (Manual)");
            //broadcastPumpState(pumpNumber);
        }
    } else {  // Auto Mode
        if (CSupplyT <= g_config.heatTapeOn) {
            // Turn ON Heat Tape
            if (pumpStates[pumpIndex] == PUMP_OFF &&
                (currentMillis - lastChangeTime >= HEAT_TAPE_RELAY_CHANGE_INTERVAL)) {
                setPumpState(pumpIndex, PUMP_ON);
                lastChangeTime = currentMillis;
                Serial.println("Heat Tape turned ON (AUTO) - Supply Temp <= On Threshold");
                //broadcastPumpState(pumpNumber);
            }
        } else if (CSupplyT >= g_config.heatTapeOff &&
                   pumpStates[pumpIndex] == PUMP_ON &&
                   (currentMillis - lastChangeTime >= HEAT_TAPE_RELAY_CHANGE_INTERVAL)) {
            // Turn OFF Heat Tape
            setPumpState(pumpIndex, PUMP_OFF);
            lastChangeTime = currentMillis;
            Serial.println("Heat Tape turned OFF (AUTO) - Supply Temp >= Off Threshold");
            //broadcastPumpState(pumpNumber);
        }
    }
}

void controlCirculationPump() {
    int pumpIndex = 3;
    int pumpNumber = pumpIndex + 1;
    static unsigned long lastChangeTime = 0;
    unsigned long currentMillis = millis();

    if (pumpModes[pumpIndex] == PUMP_ON) {  // Manual ON
        if (pumpStates[pumpIndex] == PUMP_OFF &&
            (currentMillis - lastChangeTime >= CIRC_RELAY_CHANGE_INTERVAL)) {
            setPumpState(pumpIndex, PUMP_ON);
            lastChangeTime = currentMillis;
            Serial.println("Circulation Pump turned ON (Manual)");
            //broadcastPumpState(pumpNumber);
        }
    } else if (pumpModes[pumpIndex] == PUMP_OFF) {  // Manual OFF
        if (pumpStates[pumpIndex] == PUMP_ON &&
            (currentMillis - lastChangeTime >= CIRC_RELAY_CHANGE_INTERVAL)) {
            setPumpState(pumpIndex, PUMP_OFF);
            lastChangeTime = currentMillis;
            Serial.println("Circulation Pump turned OFF (Manual)");
            //broadcastPumpState(pumpNumber);
        }
    } else {  // Auto Mode
        if (pumpStates[4] == PUMP_ON || pumpStates[5] == PUMP_ON) { // DHW or Storage Heating is ON
            if (pumpStates[pumpIndex] == PUMP_ON &&
                (currentMillis - lastChangeTime >= CIRC_RELAY_CHANGE_INTERVAL)) {
                setPumpState(pumpIndex, PUMP_OFF);
                lastChangeTime = currentMillis;
                Serial.println("Circulation Pump turned OFF due to Heating/DHW Call (AUTO)");
                //broadcastPumpState(pumpNumber);
            }
        } else {  // Normal Circ Pump Auto Control based on temperature differential
            if (CirculationTemperatureDrop >= g_config.circPumpOn) {
                if (pumpStates[pumpIndex] == PUMP_OFF &&
                    (currentMillis - lastChangeTime >= CIRC_RELAY_CHANGE_INTERVAL)) {
                    setPumpState(pumpIndex, PUMP_ON);
                    lastChangeTime = currentMillis;
                    Serial.println("Circulation Pump turned ON (AUTO) - Temp Drop >= On Threshold");
                    //broadcastPumpState(pumpNumber);
                }
            } else if (CirculationTemperatureDrop <= g_config.circPumpOff) {
                if (pumpStates[pumpIndex] == PUMP_ON &&
                    (currentMillis - lastChangeTime >= CIRC_RELAY_CHANGE_INTERVAL)) {
                    setPumpState(pumpIndex, PUMP_OFF);
                    lastChangeTime = currentMillis;
                    Serial.println("Circulation Pump turned OFF (AUTO) - Temp Drop <= Off Threshold");
                    //broadcastPumpState(pumpNumber);
                }
            }
        }
    }
}

void controlDHWPump() {
    int pumpIndex = 4;
    int pumpNumber = pumpIndex + 1;
    static unsigned long lastChangeTime = 0;
    unsigned long currentMillis = millis();
    int DHW_Heating_Call = digitalRead(DHW_HEATING_PIN); // LOW when active
    

    if (pumpModes[pumpIndex] == PUMP_ON) {  // Manual ON
        if (pumpStates[pumpIndex] == PUMP_OFF &&
            (currentMillis - lastChangeTime >= DHW_RELAY_CHANGE_INTERVAL)) {
            setPumpState(pumpIndex, PUMP_ON);
            lastChangeTime = currentMillis;
            Serial.println("DHW Pump turned ON (Manual)");
            //broadcastPumpState(pumpNumber);
        }
    } else if (pumpModes[pumpIndex] == PUMP_OFF) {  // Manual OFF
        if (pumpStates[pumpIndex] == PUMP_ON &&
            (currentMillis - lastChangeTime >= DHW_RELAY_CHANGE_INTERVAL)) {
            setPumpState(pumpIndex, PUMP_OFF);
            lastChangeTime = currentMillis;
            Serial.println("DHW Pump turned OFF (Manual)");
            //broadcastPumpState(pumpNumber);
        }
    } else {  // Auto Mode
        if (DHW_Heating_Call == LOW) {  // Turn ON Pump
            if (pumpStates[pumpIndex] == PUMP_OFF &&
                ((currentMillis - lastChangeTime >= DHW_RELAY_CHANGE_INTERVAL) || lastChangeTime == 0)) { 
                setPumpState(pumpIndex, PUMP_ON);
                lastChangeTime = currentMillis;
                Serial.println("DHW Pump turned ON (AUTO) - Heating Call Active (pin LOW)");
                //broadcastPumpState(pumpNumber);
            }
        } else if (DHW_Heating_Call == HIGH &&
                   pumpStates[pumpIndex] == PUMP_ON &&
                   (currentMillis - lastChangeTime >= DHW_RELAY_CHANGE_INTERVAL)) { // Turn OFF Pump
            setPumpState(pumpIndex, PUMP_OFF);
            lastChangeTime = currentMillis;
            Serial.println("DHW Pump turned OFF (AUTO) - Heating Call Inactive (pin HIGH)");
            //broadcastPumpState(pumpNumber);
        }
    }
}

void controlStorageHeatPump() {
    int pumpIndex = 5;
    int pumpNumber = pumpIndex + 1;
    static unsigned long lastChangeTime = 0;
    unsigned long currentMillis = millis();
    int Furnace_Heating_Call = digitalRead(FURNACE_HEATING_PIN); // LOW when active

    if (pumpModes[pumpIndex] == PUMP_ON) {  // Manual ON
        if (pumpStates[pumpIndex] == PUMP_OFF &&
            (currentMillis - lastChangeTime >= HEATING_RELAY_CHANGE_INTERVAL)) {
            setPumpState(pumpIndex, PUMP_ON);
            lastChangeTime = currentMillis;
            Serial.println("Storage Heat Pump turned ON (Manual)");
            //broadcastPumpState(pumpNumber);
        }
    } else if (pumpModes[pumpIndex] == PUMP_OFF) { // Manual OFF
        if (pumpStates[pumpIndex] == PUMP_ON &&
            (currentMillis - lastChangeTime >= HEATING_RELAY_CHANGE_INTERVAL)) {
            setPumpState(pumpIndex, PUMP_OFF);
            lastChangeTime = currentMillis;
            Serial.println("Storage Heat Pump turned OFF (Manual)");
            //broadcastPumpState(pumpNumber);
        }
    } else { // Auto Mode
        if (pumpStates[4] == PUMP_OFF) { // DHW Pump is OFF
            if (Furnace_Heating_Call == LOW) {  // Turn ON Pump
                if (pumpStates[pumpIndex] == PUMP_OFF &&
                   ((currentMillis - lastChangeTime >= HEATING_RELAY_CHANGE_INTERVAL) || lastChangeTime == 0)) { 
                    setPumpState(pumpIndex, PUMP_ON);
                    lastChangeTime = currentMillis;
                    Serial.println("Storage Heat Pump turned ON (AUTO) - Heating Call Active (pin LOW)");
                    //broadcastPumpState(pumpNumber);
                }
            } else if (pumpStates[pumpIndex] == PUMP_ON &&
                       (currentMillis - lastChangeTime >= HEATING_RELAY_CHANGE_INTERVAL)) { // Turn OFF Pump
                setPumpState(pumpIndex, PUMP_OFF);
                lastChangeTime = currentMillis;
                Serial.println("Storage Heat Pump turned OFF (AUTO) - Heating Call Inactive (pin HIGH)");
                //broadcastPumpState(pumpNumber);
            }
        } else { // DHW Pump is ON; ensure Storage Heat Pump is OFF
            if (pumpStates[pumpIndex] == PUMP_ON &&
                (currentMillis - lastChangeTime >= HEATING_RELAY_CHANGE_INTERVAL)) {
                setPumpState(pumpIndex, PUMP_OFF);
                lastChangeTime = currentMillis;
                Serial.println("Storage Heat Pump turned OFF due to DHW ON");
                //broadcastPumpState(pumpNumber);
            }
        }
    }
}

void controlBoilerCirculatorPump() {
    int pumpIndex = 6;
    int pumpNumber = pumpIndex + 1;
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
            Serial.println("Boiler Circulator turned ON (Manual)");
            //broadcastPumpState(pumpNumber);
        }
    } else if (pumpModes[pumpIndex] == PUMP_OFF) { // Manual OFF
        if (pumpStates[pumpIndex] == PUMP_ON &&
            (currentMillis - lastChangeTime >= BOILER_RELAY_CHANGE_INTERVAL)) {
            setPumpState(pumpIndex, PUMP_OFF);
            lastChangeTime = currentMillis;
            Serial.println("Boiler Circulator turned OFF (Manual)");
            //broadcastPumpState(pumpNumber);
        }
    } else { // Auto Mode
        if (heatingCallActive && tempBelowOnThreshold) {
            // Turn ON Pump
            if (pumpStates[pumpIndex] == PUMP_OFF &&
                (currentMillis - lastChangeTime >= BOILER_RELAY_CHANGE_INTERVAL)) {
                setPumpState(pumpIndex, PUMP_ON);
                lastChangeTime = currentMillis;
                Serial.println("Boiler Circulator turned ON (AUTO) storageT <  g_config.boilerCircOn");
                //broadcastPumpState(pumpNumber);
            }
        } else if ((!heatingCallActive || tempAboveOffThreshold) &&
                   pumpStates[pumpIndex] == PUMP_ON &&
                   (currentMillis - lastChangeTime >= BOILER_RELAY_CHANGE_INTERVAL)) {
            // Turn OFF Pump
            setPumpState(pumpIndex, PUMP_OFF);
            lastChangeTime = currentMillis;
            Serial.println("Boiler Circulator turned OFF (AUTO)");
            //broadcastPumpState(pumpNumber);
        }
    }
}

void controlRecirculationValve() {
    int pumpIndex = 7;
    int pumpNumber = pumpIndex + 1;
    static unsigned long lastChangeTime = 0;
    unsigned long currentMillis = millis();

    if (pumpModes[pumpIndex] == PUMP_ON) { // Manual ON
        if (pumpStates[pumpIndex] == PUMP_OFF &&
            (currentMillis - lastChangeTime >= RECIRC_RELAY_CHANGE_INTERVAL)) {
            setPumpState(pumpIndex, PUMP_ON); // Valve Closed
            lastChangeTime = currentMillis;
            Serial.println("Recirculation Valve turned ON (Manual)");
            //broadcastPumpState(pumpNumber);
        }
    } else if (pumpModes[pumpIndex] == PUMP_OFF) { // Manual OFF
        if (pumpStates[pumpIndex] == PUMP_ON &&
            (currentMillis - lastChangeTime >= RECIRC_RELAY_CHANGE_INTERVAL)) {
            setPumpState(pumpIndex, PUMP_OFF); // Valve Open
            lastChangeTime = currentMillis;
            Serial.println("Recirculation Valve turned OFF (Manual)");
            //broadcastPumpState(pumpNumber);
        }
    } else { // Auto Mode
        if (pumpStates[6] == PUMP_ON && (DhwReturnT > storageT || HeatingReturnT > storageT)) {
            // Turn ON Valve (Closed)
            if (pumpStates[pumpIndex] == PUMP_OFF &&
                (currentMillis - lastChangeTime >= RECIRC_RELAY_CHANGE_INTERVAL)) {
                setPumpState(pumpIndex, PUMP_ON);
                lastChangeTime = currentMillis;
                Serial.println("Recirculation Valve turned ON (AUTO) DhwReturnT > storageT or HeatingReturnT > storageT");
                //broadcastPumpState(pumpNumber);
            }
        } else { // Turn OFF Valve (Open)
            if (pumpStates[pumpIndex] == PUMP_ON &&
                (currentMillis - lastChangeTime >= RECIRC_RELAY_CHANGE_INTERVAL)) {
                setPumpState(pumpIndex, PUMP_OFF);
                lastChangeTime = currentMillis;
                Serial.println("Recirculation Valve turned OFF (AUTO)");
                //broadcastPumpState(pumpNumber);
            }
        }
    }
}

void PumpControl() {
  esp_task_wdt_reset();

  // 1) Lock temperature first (consistent ordering)
  if (!xSemaphoreTake(temperatureMutex, portMAX_DELAY)) {
    Serial.println("Failed to take temperatureMutex in PumpControl.");
    return;
  }
  esp_task_wdt_reset();

  // 2) Then lock pump state
  if (!xSemaphoreTake(pumpStateMutex, portMAX_DELAY)) {
    xSemaphoreGive(temperatureMutex);
    Serial.println("Failed to take pumpStateMutex in PumpControl.");
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

  const bool collectorOverride = updateCollectorFreezeProtect(CSupplyT, nowMs);
  const bool tankOverride      = updateTankFreezeProtect(storageT, nowMs);

  const bool circOverride = tankOverride
                              ? true
                              : updateCircFreezeProtect(supplyT, CircReturnT, nowMs);

  // 5) Normal control (skipped if freeze override owns it)
  if (!collectorOverride) controlPanelLeadPump();
  if (!collectorOverride) controlPanelLagPump();

  controlHeatTape();
  updateHeatTapeIneffective(CSupplyT, nowMs);

  if (!circOverride) controlCirculationPump();

  controlDHWPump();
  controlStorageHeatPump();
  controlBoilerCirculatorPump();
  controlRecirculationValve();

  // 6) Release locks
  xSemaphoreGive(pumpStateMutex);
  xSemaphoreGive(temperatureMutex);

  esp_task_wdt_reset();
}




/*
void PumpControl() {
    // 0) Early keep‐alive in case TaskPumpControl registered WDT at startup
    esp_task_wdt_reset();
    

    // 1) Acquire temperature mutex
    if (xSemaphoreTake(temperatureMutex, portMAX_DELAY)) {
        esp_task_wdt_reset();
        

        // 2) Acquire pump state mutex
        if (xSemaphoreTake(pumpStateMutex, portMAX_DELAY)) {
            esp_task_wdt_reset();
            

            // 3) Read the heating call pins
            int DHW_Heating_Call     = digitalRead(DHW_HEATING_PIN);         // LOW when active
            int Furnace_Heating_Call = digitalRead(FURNACE_HEATING_PIN);     // LOW when active
            esp_task_wdt_reset();
            

            bool currentDHWCallStatus     = (DHW_Heating_Call == LOW);
            bool currentHeatingCallStatus = (Furnace_Heating_Call == LOW);

            // 4) If changed, send new status
            if (currentDHWCallStatus != previousDHWCallStatus ||
                currentHeatingCallStatus != previousHeatingCallStatus) {
                previousDHWCallStatus     = currentDHWCallStatus;
                previousHeatingCallStatus = currentHeatingCallStatus;

                esp_task_wdt_reset();
                sendHeatingCallStatus(currentDHWCallStatus, currentHeatingCallStatus);
                esp_task_wdt_reset();
                
            }

            // 5) Control each pump—reset WDT around each in case they do I/O or delays
            esp_task_wdt_reset();
            controlPanelLeadPump();
            esp_task_wdt_reset();
            

            esp_task_wdt_reset();
            controlPanelLagPump();
            esp_task_wdt_reset();
            

            esp_task_wdt_reset();
            controlHeatTape();
            esp_task_wdt_reset();
            

            esp_task_wdt_reset();
            controlCirculationPump();
            esp_task_wdt_reset();
            

            esp_task_wdt_reset();
            controlDHWPump();
            esp_task_wdt_reset();
            

            esp_task_wdt_reset();
            controlStorageHeatPump();
            esp_task_wdt_reset();
            

            esp_task_wdt_reset();
            controlBoilerCirculatorPump();
            esp_task_wdt_reset();
            

            esp_task_wdt_reset();
            controlRecirculationValve();
            esp_task_wdt_reset();
            

            // 6) Release pumpStateMutex
            xSemaphoreGive(pumpStateMutex);
            esp_task_wdt_reset();
            
        } else {
            Serial.println("Failed to take pumpStateMutex in PumpControl.");
            esp_task_wdt_reset();
            
        }

        // 7) Release temperatureMutex
        xSemaphoreGive(temperatureMutex);
        esp_task_wdt_reset();
        
    } else {
        Serial.println("Failed to take temperatureMutex in PumpControl.");
        esp_task_wdt_reset();
        
    }
}
*/
