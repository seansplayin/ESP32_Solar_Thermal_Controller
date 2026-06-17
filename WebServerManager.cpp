#include "WebServerManager.h"
#include "Logging.h"
#include "Config.h"
#include "PumpManager.h"
#include <FS.h>
#include <LittleFS.h>
#include "RTCManager.h" 
#include "SecondWebpage.h"
#include "ThirdWebpage.h"
#include <ArduinoJson.h> 
#include <RTClib.h>
#include "uptime_formatter.h"
#include "TemperatureControl.h" 
#include "esp_task_wdt.h" 
#include "FileSystemManager.h"
#include <esp_heap_caps.h> 
#include "TimeSync.h"   
#include "AlarmManager.h"
#include "AlarmWebpage.h"
extern AsyncWebSocket ws;

//  required for user changable perameters 
extern SystemConfig g_config;

// ---- new: time configuration (timezone + DST) ----
extern TimeConfig g_timeConfig;

// Global flag to indicate that pump runtime data needs to be updated
volatile bool needToUpdatePumpRuntimes = false;
extern TaskHandle_t thUpdatePumpRuntimes;

// Extern declarations for global variables
extern int pumpStates[10];
extern int pumpModes[10];

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

extern DateTime CurrentTime; // Assuming this is declared elsewhere

static String validateTemp(float v) {
  if (isnan(v)) return "N/A";
  return String(v, 1);
}

static String wsBytesToString(const uint8_t* data, size_t len) {
  String s;
  if (!data || len == 0) return s;
  s.reserve(len);
  s.concat((const char*)data, len);   // copies exactly len bytes; no NUL needed
  return s;
}




// Initialize the server and WebSocket
AsyncWebServer server(80);

AsyncWebSocket ws("/ws"); // Create a WebSocket endpoint at "/ws"

// Global cache for runtimes
unsigned long cachedRuntimes[10][7] = {0};

// [ADD] Fetch-cache for SecondWebpage (HTTP instead of WS)
static volatile uint32_t g_pumpRuntimeRequestedVersion = 0;
static volatile uint32_t g_pumpRuntimeBuiltVersion     = 0;
static String            g_pumpRuntimeJson             = "{\"version\":0,\"data\":[]}";
static SemaphoreHandle_t g_pumpRuntimeJsonMutex        = nullptr;

static void ensurePumpRuntimeJsonMutex() {
  if (!g_pumpRuntimeJsonMutex) {
    g_pumpRuntimeJsonMutex = xSemaphoreCreateMutex();
  }
}


// Function prototypes
void startServer();
void initWebSocket();
void handleWebSocketEvent(AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type,
                          void* arg, uint8_t* data, size_t len);
void handleWebSocketMessage(void* arg, uint8_t* data, size_t len);
void handleSetPumpMode(String message);
void handleRequestLogData(String message);
void sendPumpStatuses(AsyncWebSocketClient* client);
void sendTemperatures(AsyncWebSocketClient* client);
String getFormattedTime();
String getFormattedDate();
void sendDateTime(AsyncWebSocketClient* client);
void sendUptime(AsyncWebSocketClient* client);
void sendAllData(AsyncWebSocketClient* client);
void sendSystemStats(AsyncWebSocketClient* client); 
void broadcastMessageOverWebSocket(const String& message, const String& messageType);
void sendTimeConfig(AsyncWebSocketClient* client);
DateTime parseDateTimeFromLogFile(const String& datetimeStr);
unsigned long calculateTotalLogRuntime(const String& logFilename);
String prepareLogData(int pumpIndex, String timeframe);
String formatRuntime(long totalSeconds);
unsigned long aggregateDailyLogsReport(int pumpIndex, DateTime currentTime);
unsigned long aggregatePreviousDailyLogsReport(int pumpIndex, DateTime currentTime);
unsigned long aggregateMonthlyLogsReport(int pumpIndex, DateTime currentTime);
unsigned long aggregatePreviousMonthlyLogsReport(int pumpIndex, DateTime currentTime);
unsigned long aggregateYearlyLogsReport(int pumpIndex, DateTime currentTime);
unsigned long aggregatePreviousYearlyLogsReport(int pumpIndex, DateTime currentTime);
unsigned long aggregateDecadeLogsReport(int pumpIndex, DateTime currentTime);
void setupRoutes();
void setupLogDataRoute();
void updateAllRuntimes();
void refreshRuntimeCache();

static void sendAlarmStateWs(uint32_t n);
static void onAlarmStateChanged(uint32_t activeCount);

static void sendAlarmStateWs(uint32_t n) {
  if (n > 0) {
    ws.textAll("AlarmState:ALARM,count=" + String(n));
  } else {
    ws.textAll("AlarmState:OK,count=0");
  }
}

static void onAlarmStateChanged(uint32_t activeCount) {
  // Called by AlarmManager when active count changes (set/clear)
  sendAlarmStateWs(activeCount);
}

void broadcastAlarmStateOverWebSocket() {
  uint32_t n = AlarmManager_activeCount();
  sendAlarmStateWs(n);
}


void sendConfigurationValues(AsyncWebSocketClient* client) {
    if (client && client->queueIsFull()) {
        Serial.println("[Warning] Client queue is full, skipping configuration data transmission.");
        return;
    }

    String configData = "Configuration:";

    auto validateConfigValue = [](float value) -> String {
        if (isnan(value)) {
            return "N/A";
        }
        return String(value, 2);
    };

    // JSON keys stay as your original names; values come from g_config
    configData += "panelTminimum:" + validateConfigValue(g_config.panelTminimumValue);
    configData += ",PanelOnDifferential:" + validateConfigValue(g_config.panelOnDifferential);
    configData += ",PanelLowDifferential:" + validateConfigValue(g_config.panelLowDifferential);
    configData += ",PanelOffDifferential:" + validateConfigValue(g_config.panelOffDifferential);
    configData += ",Boiler_Circ_On:" + validateConfigValue(g_config.boilerCircOn);
    configData += ",Boiler_Circ_Off:" + validateConfigValue(g_config.boilerCircOff);
    configData += ",StorageHeatingLimit:" + validateConfigValue(g_config.storageHeatingLimit);
    configData += ",Circ_Pump_On:" + validateConfigValue(g_config.circPumpOn);
        configData += ",Circ_Pump_Off:" + validateConfigValue(g_config.circPumpOff);
    configData += ",Heat_Tape_On:" + validateConfigValue(g_config.heatTapeOn);
    configData += ",Heat_Tape_Off:" + validateConfigValue(g_config.heatTapeOff);

    // ---------------- Freeze Protection ----------------
    configData += ",collectorFreezeTempF:" + validateConfigValue(g_config.collectorFreezeTempF);
    configData += ",collectorFreezeConfirmMin:" + String((uint32_t)g_config.collectorFreezeConfirmMin);
    configData += ",collectorFreezeRunMin:" + String((uint32_t)g_config.collectorFreezeRunMin);

    configData += ",circFreezeTempF:" + validateConfigValue(g_config.circFreezeTempF);
    configData += ",circFreezeConfirmMin:" + String((uint32_t)g_config.circFreezeConfirmMin);
    configData += ",circFreezeRunMin:" + String((uint32_t)g_config.circFreezeRunMin);

    configData += ",heatTapeBadF:" + validateConfigValue(g_config.heatTapeBadF);
    configData += ",heatTapeClearF:" + validateConfigValue(g_config.heatTapeClearF);
    configData += ",heatTapeEvalMin:" + String((uint32_t)g_config.heatTapeEvalMin);

    configData += ",tankFreezeTempF:" + validateConfigValue(g_config.tankFreezeTempF);
    configData += ",tankFreezeClearF:" + validateConfigValue(g_config.tankFreezeClearF);
    configData += ",tankFreezeConfirmMin:" + String((uint32_t)g_config.tankFreezeConfirmMin);
    // ---------------------------------------------------

    if (client) {
        client->text(configData);
    } else {

        ws.textAll(configData);
    }
}

// ---- TimeConfig sender (new) ----
void sendTimeConfig(AsyncWebSocketClient* client) {
    if (client && client->queueIsFull()) {
        Serial.println("[Warning] Client queue is full, skipping time config transmission.");
        return;
    }

    String msg = "TimeConfig:";
    msg += "timeZoneId=" + g_timeConfig.timeZoneId;
    msg += ",dstEnabled=" + String(g_timeConfig.dstEnabled ? 1 : 0);

    if (client) {
        client->text(msg);
    } else {
        ws.textAll(msg);
    }
}


void serveFavicon(AsyncWebServer& server) {
    // We have access to LittleFS here
    server.serveStatic("/favicon.png", LittleFS, "/favicon.png");
}

// Start the server
void startServer() {
    serveFavicon(server);     // sets up the route
    initWebSocket(); // Initialize WebSocket
    setupRoutes();   // Setup additional routes for listing and downloading files
    ensurePumpRuntimeJsonMutex();
    AlarmManager_setStateChangedCallback(onAlarmStateChanged);
    server.begin();  // Start the server
}

// Initialize the WebSocket
void initWebSocket() {
    ws.onEvent(handleWebSocketEvent);
    server.addHandler(&ws);
}

void setAllPumpsMode(int mode) {
    // Set all pumps to the specified mode
    for (int i = 0; i < 10; i++) {
        pumpModes[i] = mode;
    }

    // Log the action
    if (mode == PUMP_AUTO) {
        Serial.println("All pumps set to AUTO via web button.");
    } else if (mode == PUMP_OFF) {
        Serial.println("All pumps turned OFF via web button.");
    }

    // Notify clients of the updated pump statuses
    sendPumpStatuses(nullptr);
}

// Handle WebSocket events
void handleWebSocketEvent(AsyncWebSocket* server,
                          AsyncWebSocketClient* client,
                          AwsEventType type,
                          void* arg,
                          uint8_t* data,
                          size_t len)
{
  if (type == WS_EVT_CONNECT) {
    Serial.printf("WebSocket client connected (id=%u)\n", client ? client->id() : 0);
    return;
  }

  if (type == WS_EVT_DISCONNECT) {
    Serial.printf("WebSocket client disconnected (id=%u)\n", client ? client->id() : 0);
    return;
  }

  if (type != WS_EVT_DATA) return;

  AwsFrameInfo* info = (AwsFrameInfo*)arg;
  if (!info) return;

  // Ignore fragmented frames for now (good safety)
  if (!info->final || info->index != 0 || info->len != len) return;

  if (info->opcode != WS_TEXT) return;

  String msg = wsBytesToString(data, len);

  // Identify which page connected (your new "hello:" handshake)
  if (msg.startsWith("hello:")) {
    Serial.printf("[WS hello] id=%u msg=%s\n", client ? client->id() : 0, msg.c_str());
    return;  // IMPORTANT: don't pass hello into the generic message handler
  }

  // FirstWebpage sends "init" after it opens
  if (msg == "init") {
    // Restore what you used to do on WS connect
    int dhwCall  = (digitalRead(DHW_HEATING_PIN) == LOW);
    int heatCall = (digitalRead(FURNACE_HEATING_PIN) == LOW);
    sendHeatingCallStatus(dhwCall, heatCall);

        if (client && !client->queueIsFull()) {
      sendAllData(client);
      sendConfigurationValues(client);
      sendTimeConfig(client);
      sendSystemStats(client);

      uint32_t n = AlarmManager_activeCount();
      client->text((n > 0)
        ? ("AlarmState:ALARM,count=" + String(n))
        :  "AlarmState:OK,count=0");
    }
    return;

  }

  if (msg == "getUptime") {
    sendUptime(client);
    return;
  }

  // Everything else goes through your existing handler
  handleWebSocketMessage(arg, data, len);
}





// Handle incoming WebSocket messages
void handleWebSocketMessage(void* arg, uint8_t* data, size_t len) {
        AwsFrameInfo* info = (AwsFrameInfo*)arg;
    if (info->opcode == WS_TEXT) {
        String message = wsBytesToString(data, len);
        //Serial.print("Received WS message: ");

        //Serial.println(message);

        if (message == "ping") {
            return;
        }        
                else if (message.startsWith("requestLogData")) {
            handleRequestLogData(message);
        } else if (message.startsWith("setPumpMode:")) {
            handleSetPumpMode(message);
        } else if (message.equals("setAllPumps:auto")) {
            setAllPumpsMode(PUMP_AUTO);
        } else if (message.equals("setAllPumps:off")) {
            setAllPumpsMode(PUMP_OFF);
        } else if (message.equals("getFsStats")) {
            // Send the FS heap JSON back
            String json = getFSStatsString();
            // prefix so the client can handle it easily
            ws.textAll("FsStats:" + json);
        } else if (message.equals("deleteTemperatureLogs")) {
            // dangerous: only use if you intentionally want to delete all logs
            bool ok = deleteTemperatureLogsRecursive("/Temperature_Logs");
            ws.textAll(String("DeleteTempLogsResult:") + (ok ? "OK" : "FAIL"));
                }else if (message.startsWith("setConfig:")) {
                String payload = message.substring(strlen("setConfig:"));
                  // Format: setConfig:key=val,key=val,...
              int start = 0;

              auto clampU16 = [](long v, uint16_t lo, uint16_t hi)->uint16_t {
                  if (v < (long)lo) return lo;
                  if (v > (long)hi) return hi;
                  return (uint16_t)v;
              };
              auto clampF = [](float v, float lo, float hi)->float {
                  if (v < lo) return lo;
                  if (v > hi) return hi;
                  return v;
              };

                while (start < payload.length()) {

                 int comma = payload.indexOf(',', start);
                 String pair = (comma == -1)
                            ? payload.substring(start)
                            : payload.substring(start, comma);

                   pair.trim();
                   if (pair.length() > 0) {
                   int eq = pair.indexOf('=');
                        if (eq > 0) {
                           String key = pair.substring(0, eq);
                           String val = pair.substring(eq + 1);
                           key.trim();
                           val.trim();
                           float f = val.toFloat();
                            long  i = val.toInt();
                              if (key == "panelTminimum") {
                                 g_config.panelTminimumValue = f;
                              } else if (key == "PanelOnDifferential") {
                                 g_config.panelOnDifferential = f;
                              } else if (key == "PanelLowDifferential") {
                                 g_config.panelLowDifferential = f;
                              } else if (key == "PanelOffDifferential") {
                                 g_config.panelOffDifferential = f;
                              } else if (key == "Boiler_Circ_On") {
                                 g_config.boilerCircOn = f;
                              } else if (key == "Boiler_Circ_Off") {
                                 g_config.boilerCircOff = f;
                              } else if (key == "StorageHeatingLimit") {
                                 g_config.storageHeatingLimit = f;
                              } else if (key == "Circ_Pump_On") {
                                 g_config.circPumpOn = f;
                              } else if (key == "Circ_Pump_Off") {
                                 g_config.circPumpOff = f;
                              } else if (key == "Heat_Tape_On") {
                                 g_config.heatTapeOn = f;
                                                            } else if (key == "Heat_Tape_Off") {
                                 g_config.heatTapeOff = f;

                              } else if (key == "collectorFreezeTempF") {
                                 g_config.collectorFreezeTempF = clampF(f, 20.0f, 80.0f);
                              } else if (key == "collectorFreezeConfirmMin") {
                                 g_config.collectorFreezeConfirmMin = clampU16(i, 1, 120);
                              } else if (key == "collectorFreezeRunMin") {
                                 g_config.collectorFreezeRunMin = clampU16(i, 1, 120);

                              } else if (key == "circFreezeTempF") {
                                 g_config.circFreezeTempF = clampF(f, 20.0f, 60.0f);
                              } else if (key == "circFreezeConfirmMin") {
                                 g_config.circFreezeConfirmMin = clampU16(i, 1, 120);
                              } else if (key == "circFreezeRunMin") {
                                 g_config.circFreezeRunMin = clampU16(i, 1, 120);

                              } else if (key == "heatTapeBadF") {
                                 g_config.heatTapeBadF = clampF(f, 20.0f, 60.0f);
                              } else if (key == "heatTapeClearF") {
                                 g_config.heatTapeClearF = clampF(f, 20.0f, 60.0f);
                              } else if (key == "heatTapeEvalMin") {
                                 g_config.heatTapeEvalMin = clampU16(i, 1, 120);

                              } else if (key == "tankFreezeTempF") {
                                 g_config.tankFreezeTempF = clampF(f, 20.0f, 60.0f);
                              } else if (key == "tankFreezeClearF") {
                                 g_config.tankFreezeClearF = clampF(f, 20.0f, 80.0f);
                              } else if (key == "tankFreezeConfirmMin") {
                                 g_config.tankFreezeConfirmMin = clampU16(i, 1, 240);
                              }
                        }
                    } 

                   if (comma == -1) break;
                    start = comma + 1;
                }
             // Persist to LittleFS
             if (!saveSystemConfigToFS()) {
             Serial.println("[Config] ERROR while saving system_config.json");
             ws.textAll("ConfigSave:FAIL");
             } else 
               {
                 Serial.println("[Config] system_config.json saved from WebUI");
                 ws.textAll("ConfigSave:OK");
                 // Re-send configuration so all clients update display
                 sendConfigurationValues(nullptr);
                }
           }

        
            else if (message == "resetConfig") {
                Serial.println("[WS] Reset SystemConfig to defaults requested");

                bool ok = resetSystemConfigToDefaults();  // helper from Config.cpp

                if (ok) {
                    ws.textAll("ConfigReset:OK");
                    // Push fresh values so browsers update all spans/inputs + currentConfig cache
                    sendConfigurationValues(nullptr);
                } else {
                    ws.textAll("ConfigReset:FAIL");
                }
            }

            
            else if (message.startsWith("setTimeConfig:")) {
                String payload = message.substring(strlen("setTimeConfig:"));
                // Format: setTimeConfig:key=val,key=val,...
                int start = 0;
                while (start < payload.length()) {
                    int comma = payload.indexOf(',', start);
                    String pair = (comma == -1)
                                    ? payload.substring(start)
                                    : payload.substring(start, comma);

                    pair.trim();
                    if (pair.length() > 0) {
                        int eq = pair.indexOf('=');
                        if (eq > 0) {
                            String key = pair.substring(0, eq);
                            String val = pair.substring(eq + 1);
                            key.trim();
                            val.trim();

                            if (key == "timeZoneId") {
                                g_timeConfig.timeZoneId = val;
                            } else if (key == "dstEnabled") {
                                g_timeConfig.dstEnabled = (val.toInt() != 0);
                            }
                        }
                    }

                    if (comma == -1) break;
                    start = comma + 1;
                }

                if (!saveTimeConfigToFS()) {
                    Serial.println("[TimeConfig] ERROR while saving time_config.json");
                    ws.textAll("TimeConfigSave:FAIL");
                } else {
                    Serial.println("[TimeConfig] time_config.json saved from WebUI");
                    ws.textAll("TimeConfigSave:OK");
                    // Re-send so all clients update display
                    sendTimeConfig(nullptr);
                    // 🔁 Re-run NTP so RTC + timestamps immediately pick up the new TZ
                    requestImmediateNtpResync();
        
                }
            }
            else if (message.equals("resetTimeConfig")) {
                Serial.println("[WS] Reset TimeConfig to defaults requested");

                bool ok = resetTimeConfigToDefaults();

                if (ok) {
                    ws.textAll("TimeConfigReset:OK");
                    sendTimeConfig(nullptr);
                    
                } else {
                    ws.textAll("TimeConfigReset:FAIL");
                }
            }
    }
}




// Handle setting pump mode
void handleSetPumpMode(String message) {
    int firstColon = message.indexOf(':');
    int secondColon = message.indexOf(':', firstColon + 1);
    if (firstColon != -1 && secondColon != -1) {
        int pumpIndex = message.substring(firstColon + 1, secondColon).toInt() - 1; // Adjust for 0-based index
        String mode = message.substring(secondColon + 1);
        mode.toLowerCase(); // Ensure mode is in lowercase

        if (pumpIndex >= 0 && pumpIndex < 10) {
            // Only update if there is a change
            int newMode = PUMP_AUTO; // Default to "auto"
            if (mode == "on") {
                newMode = PUMP_ON;
            } else if (mode == "off") {
                newMode = PUMP_OFF;
            }

            if (pumpModes[pumpIndex] != newMode) {
                pumpModes[pumpIndex] = newMode;
                Serial.printf("Pump %d mode set to %s\n", pumpIndex + 1, mode.c_str());
                sendPumpStatuses(nullptr); // Broadcast only if the state changes
            }
        } else {
            Serial.println("Invalid pump index received.");
        }
    }
}


// Handle log data requests
void handleRequestLogData(String message) {
    // Expected format: requestLogData:pumpIndex:timeframe
    int firstColon = message.indexOf(':');
    int secondColon = message.lastIndexOf(':');
    if (firstColon != -1 && secondColon != -1 && secondColon > firstColon) {
        int pumpIndex = message.substring(firstColon + 1, secondColon).toInt() - 1; // Adjusting for 0-based index
        String timeframe = message.substring(secondColon + 1);

        // Prepare and send the log data
        String logData = prepareLogData(pumpIndex, timeframe);
        ws.textAll(logData);
    } else {
        Serial.println("Invalid requestLogData message format.");
    }
}

// Send pump statuses to client
void sendPumpStatuses(AsyncWebSocketClient* client) {
    DynamicJsonDocument doc(2048);
    JsonArray pumps = doc.to<JsonArray>();

    for (int i = 0; i < NUM_PUMPS; i++) {
        JsonObject pump = pumps.createNestedObject();
        pump["pumpIndex"] = i + 1; // Adjust for 1-based indexing if needed
        pump["name"] = pumpNames[i]; // Include pump name
        pump["state"] = pumpStates[i] == PUMP_ON ? "ON" : "OFF";

        String modeStr;
        switch (pumpModes[i]) {
            case PUMP_ON:
                modeStr = "on";
                break;
            case PUMP_OFF:
                modeStr = "off";
                break;
            case PUMP_AUTO:
                modeStr = "auto";
                break;
            default:
                modeStr = "unknown";
                break;
        }
        pump["mode"] = modeStr;
    }

    String pumpStatusData;
    serializeJson(pumps, pumpStatusData);

    if (client) {
        client->text("PumpStatus:" + pumpStatusData);
    } else {
        ws.textAll("PumpStatus:" + pumpStatusData);
    }
}

// **New: Send Updated Temperatures**
void sendUpdatedTemperatures() {
    // This function can be called when changes are detected
    broadcastTemperatures();
}

// Send temperature data to client
void sendTemperatures(AsyncWebSocketClient* client) {
    String tempData = "Temperatures:";

    // Existing temperatures
    tempData += "panelT:" + String(panelT) + ",";
    tempData += "CSupplyT:" + String(CSupplyT) + ",";
    tempData += "storageT:" + String(storageT) + ",";
    tempData += "outsideT:" + String(outsideT) + ",";
    tempData += "CircReturnT:" + String(CircReturnT) + ",";
    tempData += "supplyT:" + String(supplyT) + ",";
    tempData += "CreturnT:" + String(CreturnT) + ",";
    tempData += "DhwSupplyT:" + String(DhwSupplyT) + ",";
    tempData += "DhwReturnT:" + String(DhwReturnT) + ",";
    tempData += "HeatingSupplyT:" + String(HeatingSupplyT) + ",";
    tempData += "HeatingReturnT:" + String(HeatingReturnT) + ",";
    tempData += "dhwT:" + String(dhwT) + ",";
    tempData += "PotHeatXinletT:" + String(PotHeatXinletT) + ",";
    tempData += "PotHeatXoutletT:" + String(PotHeatXoutletT) + ",";

    // **New temperature variables**
    tempData += "pt1000Current:" + String(pt1000Current) + ",";
    tempData += "pt1000Average:" + String(pt1000Average) + ",";

    // DTemp1 to DTemp13 and their averages
    for (int i = 0; i < NUM_SENSORS; i++) {
        tempData += "DTemp" + String(i + 1) + ":" + String(DTemp[i]) + ",";
        tempData += "DTempAverage" + String(i + 1) + ":" + String(DTempAverage[i]) + ",";
    }

    // Remove the trailing comma
    if (tempData.endsWith(",")) {
        tempData.remove(tempData.length() - 1);
    }

    client->text(tempData);
}


// Get formatted time
String getFormattedTime() {
    DateTime now = getCurrentTimeAtomic();
    char buffer[9]; // HH:MM:SS
    snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d", now.hour(), now.minute(), now.second());
    return String(buffer);
}

// Get formatted date
String getFormattedDate() {
    DateTime now = getCurrentTimeAtomic();
    char buffer[11]; // YYYY-MM-DD
    snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d", now.year(), now.month(), now.day());
    return String(buffer);
}

// Send date and time to client
void sendDateTime(AsyncWebSocketClient* client) {
    String dateTimeData = "DateTime:currentTime:" + getFormattedTime() + ",currentDate:" + getFormattedDate();
    client->text(dateTimeData);
}

// Send uptime to client
void sendUptime(AsyncWebSocketClient* client) {
    String uptimeData = "Uptime:" + uptime_formatter::getUptime();
    client->text(uptimeData);
}

// Send heap + filesystem stats to client.
// Uses two WebSocket messages:
//   "Heap:<human-readable string>"
//   "FSStats:{...json...}"
void sendSystemStats(AsyncWebSocketClient* client) {
    String heapStr = getFreeHeapString();   // from FileSystemManager.cpp
    String fsJson  = getFSStatsString();    // JSON string

    if (client) {
        client->text("Heap:" + heapStr);
        client->text("FSStats:" + fsJson);
    } else {
        ws.textAll("Heap:" + heapStr);
        ws.textAll("FSStats:" + fsJson);
    }
}


void sendAllData(AsyncWebSocketClient* client) {
if (client && client->queueIsFull()) {
Serial.println("[Warning] Client queue is full, skipping data transmission.");
return;
}
sendPumpStatuses(client);
// Add mutex and validation to temperature broadcasting
if (xSemaphoreTake(temperatureMutex, portMAX_DELAY)) {
String tempData = "Temperatures:";
// Helper lambda to validate temperature


// Build the temperature message
tempData += "panelT:" + validateTemp(panelT);
tempData += ",CSupplyT:" + validateTemp(CSupplyT);
tempData += ",storageT:" + validateTemp(storageT);
tempData += ",outsideT:" + validateTemp(outsideT);
tempData += ",CircReturnT:" + validateTemp(CircReturnT);
tempData += ",supplyT:" + validateTemp(supplyT);
tempData += ",CreturnT:" + validateTemp(CreturnT);
tempData += ",DhwSupplyT:" + validateTemp(DhwSupplyT);
tempData += ",DhwReturnT:" + validateTemp(DhwReturnT);
tempData += ",HeatingSupplyT:" + validateTemp(HeatingSupplyT);
tempData += ",HeatingReturnT:" + validateTemp(HeatingReturnT);
tempData += ",dhwT:" + validateTemp(dhwT);
tempData += ",PotHeatXinletT:" + validateTemp(PotHeatXinletT);
tempData += ",PotHeatXoutletT:" + validateTemp(PotHeatXoutletT);
// Include new temperatures
tempData += ",pt1000Current:" + validateTemp(pt1000Current);
tempData += ",pt1000Average:" + validateTemp(pt1000Average);
// Include DTemp1 to DTemp13 and their averages
for (int i = 0; i < NUM_SENSORS; i++) {
tempData += ",DTemp" + String(i + 1) + ":" + validateTemp(DTemp[i]);
tempData += ",DTempAverage" + String(i + 1) + ":" + validateTemp(DTempAverage[i]);
}
// Add FS and heap stats inline (so front-end can parse them)
// JSON returned by getFSStatsString() - no spaces expected
String fsJson = getFSStatsString(); // {"usedBytes":...,"totalBytes":...,...}
tempData += ",fsStats:" + fsJson;
// Add heap info
size_t freeHeap = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
size_t totalHeap = heap_caps_get_total_size(MALLOC_CAP_DEFAULT);
float pctHeapUsed = (totalHeap ? ((float)(totalHeap - freeHeap) / (float)totalHeap) * 100.0f : 0.0f);
String heapJson =
  "{\"freeBytes\":" + String(freeHeap) +
  ",\"totalBytes\":" + String(totalHeap) +
  ",\"pctUsed\":" + String(pctHeapUsed, 1) + "}";

tempData += ",heapStats:" + heapJson;
// Remove any unintended leading spaces or commas
tempData.replace("Temperatures:,", "Temperatures:");
// Check if any temperature is valid
if (tempData.length() > strlen("Temperatures:")) {
client->text(tempData);
} else {
Serial.println("[Warning] No valid temperature data to broadcast.");
}
// Release the mutex after operation
xSemaphoreGive(temperatureMutex);
} else {
Serial.println("Failed to take temperatureMutex in sendAllData.");
}
sendDateTime(client);
sendUptime(client);
}




// Broadcast a message over WebSocket
void broadcastMessageOverWebSocket(const String& message, const String& messageType) {
    if (message.length() > 0) {
        ws.textAll(message);
    } else {
        Serial.println("[Error] Attempted to send zero-length WebSocket message: " + messageType);
    }
}

// Parse date and time from log file
DateTime parseDateTimeFromLogFile(const String& datetimeStr) {
    String datetimeStrTrim = datetimeStr;
    datetimeStrTrim.trim();
    // Parses datetime string in "YYYY-MM-DD HH:MM:SS" format and returns a DateTime object
    int year = datetimeStrTrim.substring(0, 4).toInt();
    int month = datetimeStrTrim.substring(5, 7).toInt();
    int day = datetimeStrTrim.substring(8, 10).toInt();
    int hour = datetimeStrTrim.substring(11, 13).toInt();
    int minute = datetimeStrTrim.substring(14, 16).toInt();
    int second = datetimeStrTrim.substring(17).toInt(); // Assuming the rest of the string is seconds
    return DateTime(year, month, day, hour, minute, second);
}

// Calculate total log runtime
unsigned long calculateTotalLogRuntime(const String& logFilename) {
    File logFile = openLogFile(logFilename, "r");
    if (!logFile) {
        // Error message already handled in openLogFile
        return 0;
    }
    unsigned long totalRuntime = 0;
    DateTime lastStartTime;
    bool isPumpRunning = false;
    while (logFile.available()) {
        String line = logFile.readStringUntil('\n');
        if (line.startsWith("START")) {
            lastStartTime = parseDateTimeFromLogFile(line.substring(6));
            isPumpRunning = true;
        } else if (line.startsWith("STOP")) {
            DateTime stopTime = parseDateTimeFromLogFile(line.substring(5));
            if (isPumpRunning) {
                totalRuntime += (stopTime.unixtime() - lastStartTime.unixtime());
                isPumpRunning = false;
            }
        }
    }
    if (isPumpRunning) {
        DateTime currentTime = getCurrentTimeAtomic();
        totalRuntime += (currentTime.unixtime() - lastStartTime.unixtime());
    }
    logFile.close();
    return totalRuntime;
}

// Prepare log data for a given pump and timeframe
String prepareLogData(int pumpIndex, String timeframe) {
    unsigned long runtimeSeconds = 0;
    DateTime currentTime = getCurrentTimeAtomic(); // Get the current time

    if (timeframe == "day") {
        runtimeSeconds = aggregateDailyLogsReport(pumpIndex, currentTime);
    } else if (timeframe == "month") {
        runtimeSeconds = aggregateMonthlyLogsReport(pumpIndex, currentTime);
    } else if (timeframe == "year") {
        runtimeSeconds = aggregateYearlyLogsReport(pumpIndex, currentTime);
    } else if (timeframe == "decade") {
        runtimeSeconds = aggregateDecadeLogsReport(pumpIndex, currentTime);
    } else if (timeframe == "prevDay" || timeframe == "yesterday") {
        runtimeSeconds = aggregatePreviousDailyLogsReport(pumpIndex, currentTime);
    } else if (timeframe == "prevMonth" || timeframe == "lastMonth") {
        runtimeSeconds = aggregatePreviousMonthlyLogsReport(pumpIndex, currentTime);
    } else if (timeframe == "prevYear" || timeframe == "lastYear") {
        runtimeSeconds = aggregatePreviousYearlyLogsReport(pumpIndex, currentTime);
    } else {
        // Handle invalid timeframe
        Serial.println("Invalid timeframe requested: " + timeframe);
    }
    return String(runtimeSeconds);
}

// Format runtime from seconds into "2h 15m 30s" format
String formatRuntime(long totalSeconds) {
    long hours = totalSeconds / 3600;
    long minutes = (totalSeconds % 3600) / 60;
    long seconds = totalSeconds % 60;
    // Format the string as "2h 15m 30s"
    String formattedRuntime = "";
    if (hours > 0) formattedRuntime += String(hours) + "h ";
    if (minutes > 0 || hours > 0) formattedRuntime += String(minutes) + "m ";
    formattedRuntime += String(seconds) + "s";
    return formattedRuntime;
}

// Function to aggregate daily logs
unsigned long aggregateDailyLogsReport(int pumpIndex, DateTime currentTime) {
    String logFilename = "/Pump_Logs/pump" + String(pumpIndex + 1) + "_Log.txt";  // Updated path
    // Calculate the total runtime directly from the log file
    unsigned long totalRuntime = calculateTotalLogRuntime(logFilename);
    return totalRuntime; // Return total runtime in seconds
}

// Function to aggregate previous day's logs
unsigned long aggregatePreviousDailyLogsReport(int pumpIndex, DateTime currentTime) {
    String dailyLogFilename = "/Pump_Logs/pump" + String(pumpIndex + 1) + "_Daily.txt";  // Updated path
    File dailyLogFile = openLogFile(dailyLogFilename, "r");
    unsigned long prevDayRuntimeSeconds = 0;
    if (!dailyLogFile) {
        // Error message already handled in openLogFile
        return prevDayRuntimeSeconds;
    }

    // Get the previous day's date
    DateTime prevDay = currentTime - TimeSpan(1, 0, 0, 0);
    String prevDayStr = String(prevDay.year()) + "-" +
                        (prevDay.month() < 10 ? "0" : "") + String(prevDay.month()) + "-" +
                        (prevDay.day() < 10 ? "0" : "") + String(prevDay.day());

    while (dailyLogFile.available()) {
        String line = dailyLogFile.readStringUntil('\n');
        line.trim(); // Remove any leading/trailing whitespace

        // Extract date and runtime
        int spaceIndex = line.indexOf(' ');
        if (spaceIndex != -1) {
            String datePart = line.substring(0, spaceIndex);
            if (datePart == prevDayStr) {
                // Find "Total Runtime:" and extract the runtime value
                int runtimeStartIndex = line.indexOf("Total Runtime: ") + 15;
                int secondsIndex = line.indexOf(" seconds", runtimeStartIndex);
                if (runtimeStartIndex != -1 && secondsIndex != -1) {
                    String runtimeStr = line.substring(runtimeStartIndex, secondsIndex);
                    runtimeStr.trim();
                    unsigned long runtime = runtimeStr.toInt();
                    prevDayRuntimeSeconds += runtime;
                }
            }
        }
    }
    dailyLogFile.close();
    return prevDayRuntimeSeconds;
}

// Function to aggregate monthly logs
unsigned long aggregateMonthlyLogsReport(int pumpIndex, DateTime currentTime) {
    String dailyLogFilename = "/Pump_Logs/pump" + String(pumpIndex + 1) + "_Daily.txt";  // Updated path
    File dailyLogFile = openLogFile(dailyLogFilename, "r");
    if (!dailyLogFile) {
        // Error message already handled in openLogFile
        return 0; // Return 0 seconds if the file is not found
    }

    unsigned long totalRuntimeSeconds = 0;

    // Create a buffer to hold the current month as a string (e.g., "2024-09")
    char currentMonth[8];
    snprintf(currentMonth, sizeof(currentMonth), "%04d-%02d", currentTime.year(), currentTime.month());

    // Process each line in the daily log file
    while (dailyLogFile.available()) {
        String line = dailyLogFile.readStringUntil('\n');
        line.trim(); // Remove leading/trailing whitespace

        // Find the date and runtime in the line
        int dateSeparatorIndex = line.indexOf(' ');
        if (dateSeparatorIndex != -1) {
            String date = line.substring(0, dateSeparatorIndex);
            // Check if the log entry belongs to the current month
            if (date.startsWith(currentMonth)) {
                // Extract the runtime for this entry
                int runtimeStartIndex = line.indexOf("Total Runtime: ") + 15;
                int secondsIndex = line.indexOf(" seconds", runtimeStartIndex);
                if (runtimeStartIndex != -1 && secondsIndex != -1) {
                    String runtimeStr = line.substring(runtimeStartIndex, secondsIndex);
                    runtimeStr.trim();
                    totalRuntimeSeconds += runtimeStr.toInt();
                }
            }
        }
    }
    dailyLogFile.close();

    // Add today's runtime
    unsigned long todayRuntime = aggregateDailyLogsReport(pumpIndex, currentTime);
    totalRuntimeSeconds += todayRuntime;

    return totalRuntimeSeconds;
}

// Function to aggregate previous month's logs
unsigned long aggregatePreviousMonthlyLogsReport(int pumpIndex, DateTime currentTime) {
    String monthlyLogFilename = "/Pump_Logs/pump" + String(pumpIndex + 1) + "_Monthly.txt";  // Updated path
    File monthlyLogFile = openLogFile(monthlyLogFilename, "r");
    unsigned long prevMonthRuntimeSeconds = 0;
    if (!monthlyLogFile) {
        // Error message already handled in openLogFile
        return prevMonthRuntimeSeconds;
    }

    // Get the previous month string (e.g., "2023-08")
    int prevMonth = currentTime.month() - 1;
    int prevYear = currentTime.year();
    if (prevMonth == 0) {
        prevMonth = 12;
        prevYear -= 1;
    }
    char prevMonthStr[8];
    snprintf(prevMonthStr, sizeof(prevMonthStr), "%04d-%02d", prevYear, prevMonth);

    // Read all lines and aggregate runtime values for the previous month
    while (monthlyLogFile.available()) {
        String line = monthlyLogFile.readStringUntil('\n');
        line.trim();
        int s = line.indexOf(' ');
        if (s != -1) {
            String date = line.substring(0, s);
            if (date == prevMonthStr) {  // Check for exact match
                int runtimeStartIndex = line.indexOf("Total Runtime: ") + 15;
                int secondsIndex = line.indexOf(" seconds", runtimeStartIndex);
                if (runtimeStartIndex != -1 && secondsIndex != -1) {
                    String runtimeStr = line.substring(runtimeStartIndex, secondsIndex);
                    runtimeStr.trim();
                    prevMonthRuntimeSeconds += runtimeStr.toInt();
                }
            }
        }
    }
    monthlyLogFile.close();
    return prevMonthRuntimeSeconds;
}

// Function to aggregate yearly logs
unsigned long aggregateYearlyLogsReport(int pumpIndex, DateTime currentTime) {
    unsigned long monthRuntimeSeconds = aggregateMonthlyLogsReport(pumpIndex, currentTime);
    String monthlyLogFilename = "/Pump_Logs/pump" + String(pumpIndex + 1) + "_Monthly.txt";  // Updated path
    File monthlyLogFile = openLogFile(monthlyLogFilename, "r");
    unsigned long totalRuntimeSeconds = monthRuntimeSeconds; // Start with the current month's runtime
    if (!monthlyLogFile) {
        // Error message already handled in openLogFile
        return totalRuntimeSeconds;
    }

    // Current year as a string (e.g., "2024")
    char currentYear[5];
    snprintf(currentYear, sizeof(currentYear), "%04d", currentTime.year());

    // Read all lines and aggregate runtime values for the current year
    while (monthlyLogFile.available()) {
        String line = monthlyLogFile.readStringUntil('\n');
        line.trim();
        int s = line.indexOf(' ');
        if (s != -1) {
            String date = line.substring(0, s);
            if (date.startsWith(currentYear)) { // Check if the log entry belongs to the current year
                int runtimeStartIndex = line.indexOf("Total Runtime: ") + 15;
                int secondsIndex = line.indexOf(" seconds", runtimeStartIndex);
                if (runtimeStartIndex != -1 && secondsIndex != -1) {
                    String runtimeStr = line.substring(runtimeStartIndex, secondsIndex);
                    runtimeStr.trim();
                    totalRuntimeSeconds += runtimeStr.toInt();
                }
            }
        }
    }
    monthlyLogFile.close();
    return totalRuntimeSeconds;
}

// Function to aggregate previous year's logs
unsigned long aggregatePreviousYearlyLogsReport(int pumpIndex, DateTime currentTime) {
    String yearlyLogFilename = "/Pump_Logs/pump" + String(pumpIndex + 1) + "_Yearly.txt";  // Updated path
    File yearlyLogFile = openLogFile(yearlyLogFilename, "r");
    unsigned long prevYearRuntimeSeconds = 0;
    if (!yearlyLogFile) {
        // Error message already handled in openLogFile
        return prevYearRuntimeSeconds;
    }

    // Get the previous year string (e.g., "2023")
    int prevYear = currentTime.year() - 1;
    char prevYearStr[5];
    snprintf(prevYearStr, sizeof(prevYearStr), "%04d", prevYear);

    // Read all lines and aggregate runtime values for the previous year
    while (yearlyLogFile.available()) {
        String line = yearlyLogFile.readStringUntil('\n');
        line.trim();
        int s = line.indexOf(' ');
        if (s != -1) {
            String date = line.substring(0, s);
            if (date == prevYearStr) {
                int runtimeStartIndex = line.indexOf("Total Runtime: ") + 15;
                int secondsIndex = line.indexOf(" seconds", runtimeStartIndex);
                if (runtimeStartIndex != -1 && secondsIndex != -1) {
                    String runtimeStr = line.substring(runtimeStartIndex, secondsIndex);
                    runtimeStr.trim();
                    prevYearRuntimeSeconds += runtimeStr.toInt();
                }
            }
        }
    }
    yearlyLogFile.close();
    return prevYearRuntimeSeconds;
}

// Function to aggregate decade logs
unsigned long aggregateDecadeLogsReport(int pumpIndex, DateTime currentTime) {
    unsigned long runtime = 0;
    String filename = "/Pump_Logs/pump" + String(pumpIndex + 1) + "_Yearly.txt";  // Updated path

    if (xSemaphoreTake(fileSystemMutex, portMAX_DELAY)) {
      if (!LittleFS.exists("/Pump_Logs")) {  // CHANGE: Create dir if missing
        LittleFS.mkdir("/Pump_Logs");
      }

      if (!LittleFS.exists(filename)) {
          Serial.println("Skipping missing file: " + filename);
          xSemaphoreGive(fileSystemMutex);
          return 0;
      }

      File file = LittleFS.open(filename, "r");
      if (!file || file.size() == 0) {
          if (file) file.close();
          Serial.println("Invalid or empty file: " + filename);
          xSemaphoreGive(fileSystemMutex);
          return 0;
      }

      int lineCount = 0;
      while (file.available()) {
          String line = file.readStringUntil('\n');
          line.trim();
          if (line.isEmpty()) continue;

          int runtimeStartIndex = line.indexOf("Total Runtime: ") + 15;
          int secondsIndex = line.indexOf(" seconds", runtimeStartIndex);
          if (runtimeStartIndex != -1 && secondsIndex != -1) {
              String runtimeStr = line.substring(runtimeStartIndex, secondsIndex);
              runtimeStr.trim();
              runtime += runtimeStr.toInt();
          }

          lineCount++;
          if (lineCount % 10 == 0) {
              esp_task_wdt_reset();
          }
      }
      file.close();
      xSemaphoreGive(fileSystemMutex);
    }
    return runtime;
}


// Setup routes for the server
void setupRoutes() {
      
                server.on("/hello", HTTP_GET, [](AsyncWebServerRequest* req){
          String from = req->hasParam("from") ? req->getParam("from")->value() 
          : "unknown";
          Serial.println("[HTTP hello] from=" + from);
          req->send(200, "text/plain", "ok");
        });

        // [ADD] SecondWebpage runtimes via fetch (no WS)
        server.on("/api/pump-runtimes", HTTP_GET, [](AsyncWebServerRequest* request) {

            // If refresh=1, kick the existing background task and return the requested version
            bool refresh = request->hasParam("refresh") &&
                           (request->getParam("refresh")->value() == "1");

            if (refresh) {
                g_pumpRuntimeRequestedVersion++;
                needToUpdatePumpRuntimes = true;
                xTaskNotifyGive(thUpdatePumpRuntimes);

                DynamicJsonDocument meta(256);
                meta["requestedVersion"] = g_pumpRuntimeRequestedVersion;
                meta["builtVersion"]     = g_pumpRuntimeBuiltVersion;

                String out;
                serializeJson(meta, out);

                AsyncWebServerResponse* resp = request->beginResponse(202, "application/json; charset=UTF-8", out);
                resp->addHeader("Cache-Control", "no-store");
                request->send(resp);
                return;
            }

            // Otherwise, return the last built JSON blob
            ensurePumpRuntimeJsonMutex();

            String out;
            if (g_pumpRuntimeJsonMutex &&
                xSemaphoreTake(g_pumpRuntimeJsonMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                out = g_pumpRuntimeJson;
                xSemaphoreGive(g_pumpRuntimeJsonMutex);
            } else {
                // Fallback if mutex is unavailable
                out = g_pumpRuntimeJson;
            }

            AsyncWebServerResponse* resp = request->beginResponse(200, "application/json; charset=UTF-8", out);
            resp->addHeader("Cache-Control", "no-store");
            request->send(resp);
        });

    // Existing route handlers
    server.on("/list-logs", HTTP_GET, [](AsyncWebServerRequest* request) {
        File root = LittleFS.open("/");

        if (!root || !root.isDirectory()) {
            request->send(500, "text/plain", "Failed to open directory");
            return;
        }
        String json = "[";
        File file = root.openNextFile();
        bool first = true;
        while (file) {
            if (!first) json += ",";
            json += "\"" + String(file.name()) + "\"";
            first = false;
            file = root.openNextFile();
        }
        json += "]";
        request->send(200, "application/json", json);
    });

    // Serve a specific log file for download from the root directory
    server.on("/download-log", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (request->hasParam("file")) {
            String filename = request->getParam("file")->value();
            // Security check: avoid directory traversal
            if (filename.indexOf('/') != -1 || filename.indexOf('\\') != -1) {
                request->send(400, "text/plain", "Invalid file path");
                return;
            }
            // Debug: Check if file exists
            String filePath = "/" + filename; // Assuming files are in the root directory
            if (LittleFS.exists(filePath)) {
                Serial.println("Sending file: " + filePath);
                request->send(LittleFS, filePath, String(), true);
            } else {
                Serial.println("File not found: " + filePath);
                request->send(404, "text/plain", "File not found");
            }
        } else {
            request->send(400, "text/plain", "Missing file parameter");
        }
    });

        // -- FS stats route --
    server.on("/fs-stats", HTTP_GET, [](AsyncWebServerRequest* request) {
        String json = getFSStatsString(); // function from FileSystemManager.cpp
        request->send(200, "application/json", json);
    });

    setupAlarmRoutes();
    
    // Setup log data route
    setupLogDataRoute();
}

// Setup log data route
void setupLogDataRoute() {
    server.on("/get-log-data", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (request->hasParam("pumpIndex") && request->hasParam("timeframe")) {
            String pumpIndexParam = request->getParam("pumpIndex")->value();
            String timeframe = request->getParam("timeframe")->value();
            int pumpIndex = pumpIndexParam.toInt() - 1; // Adjusting for 0-based index
            unsigned long runtime = 0;
            DateTime currentTime = getCurrentTimeAtomic(); // Get the current time
            if (timeframe == "day") {
                runtime = aggregateDailyLogsReport(pumpIndex, currentTime);
            } else if (timeframe == "prevDay" || timeframe == "yesterday") {
                runtime = aggregatePreviousDailyLogsReport(pumpIndex, currentTime);
            } else if (timeframe == "month") {
                runtime = aggregateMonthlyLogsReport(pumpIndex, currentTime);
            } else if (timeframe == "prevMonth" || timeframe == "lastMonth") {
                runtime = aggregatePreviousMonthlyLogsReport(pumpIndex, currentTime);
            } else if (timeframe == "year") {
                runtime = aggregateYearlyLogsReport(pumpIndex, currentTime);
            } else if (timeframe == "prevYear" || timeframe == "lastYear") {
                runtime = aggregatePreviousYearlyLogsReport(pumpIndex, currentTime);
            } else if (timeframe == "total" || timeframe == "decade") {
                runtime = aggregateDecadeLogsReport(pumpIndex, currentTime);
            } else {
                request->send(400, "application/json", "{\"error\":\"Invalid timeframe\"}");
                return;
            }
            // Prepare the JSON response
            DynamicJsonDocument doc(1024);
            doc["runtime"] = runtime;
            String response;
            serializeJson(doc, response);
            request->send(200, "application/json", response);
        } else {
            request->send(400, "application/json", "{\"error\":\"Missing parameters\"}");
        }
    });
}

// Respond to 'Update All' request from Webpage
// In WebServerManager.cpp

void refreshRuntimeCache() {
    DateTime currentTime = getCurrentTimeAtomic();
    for (int i = 0; i < 10; i++) {
        cachedRuntimes[i][0] = aggregateDailyLogsReport(i, currentTime);
        cachedRuntimes[i][1] = aggregatePreviousDailyLogsReport(i, currentTime);
        cachedRuntimes[i][2] = aggregateMonthlyLogsReport(i, currentTime);
        cachedRuntimes[i][3] = aggregatePreviousMonthlyLogsReport(i, currentTime);
        cachedRuntimes[i][4] = aggregateYearlyLogsReport(i, currentTime);
        cachedRuntimes[i][5] = aggregatePreviousYearlyLogsReport(i, currentTime);
        cachedRuntimes[i][6] = aggregateDecadeLogsReport(i, currentTime);
        
        esp_task_wdt_reset();
          
    }
}

void updateAllRuntimes() {
    refreshRuntimeCache();  // Refresh cache before sending

    uint32_t version = g_pumpRuntimeRequestedVersion;

    DynamicJsonDocument doc(8192);
    doc["version"] = version;
    JsonArray data = doc.createNestedArray("data");

    for (int i = 0; i < 10; i++) {

        JsonObject pumpData = data.createNestedObject();
        pumpData["pumpIndex"] = i + 1;
        pumpData["day"] = cachedRuntimes[i][0];
        pumpData["prevDay"] = cachedRuntimes[i][1];
        pumpData["month"] = cachedRuntimes[i][2];
        pumpData["prevMonth"] = cachedRuntimes[i][3];
        pumpData["year"] = cachedRuntimes[i][4];
        pumpData["prevYear"] = cachedRuntimes[i][5];
        pumpData["total"] = cachedRuntimes[i][6];

        esp_task_wdt_reset();  // Reset WDT
        
    }

      String jsonString;
    serializeJson(doc, jsonString);

    // [ADD] store for HTTP fetch clients
    ensurePumpRuntimeJsonMutex();
    if (g_pumpRuntimeJsonMutex &&
        xSemaphoreTake(g_pumpRuntimeJsonMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        g_pumpRuntimeJson = jsonString;
        xSemaphoreGive(g_pumpRuntimeJsonMutex);
    } else {
        g_pumpRuntimeJson = jsonString; // best-effort fallback
    }
    g_pumpRuntimeBuiltVersion = version;

    
}


