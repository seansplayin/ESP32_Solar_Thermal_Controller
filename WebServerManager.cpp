// WebServerManager.cpp
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
#include "MemoryStats.h"
#include <esp_heap_caps.h> 
#include "TimeSync.h"   
#include "AlarmManager.h"
#include "AlarmWebpage.h"
#include "TarGZ.h"
#include "DiagLog.h"




//  required for user changable perameters 
extern SystemConfig g_config;

// ---- new: time configuration (timezone + DST) ----
extern TimeConfig g_timeConfig;

// --- Gatekeeper Global Flags ---
volatile bool g_sendPumpStatus = false;
volatile bool g_sendAlarmState = false;
volatile bool g_sendConfig = false;
volatile bool g_sendTimeConfig = false;
volatile bool g_sendTemperatures = false;
volatile bool g_sendDateTime = false;

String g_tempWsPayload = "";
SemaphoreHandle_t g_tempWsPayloadMutex = NULL;

// Real outbound WS queue (broadcast + one-client)
static SemaphoreHandle_t g_queuedWsMutex = NULL;

struct QueuedWsMessage {
  uint32_t clientId;   // 0 = broadcast
  uint8_t  retryCount; // small retry budget for one-client init messages
  String   message;
  String   messageType;
};

static constexpr size_t WS_OUTBOUND_QUEUE_LEN = 24;
static QueuedWsMessage g_wsOutboundQueue[WS_OUTBOUND_QUEUE_LEN];
static size_t g_wsOutboundHead = 0;
static size_t g_wsOutboundTail = 0;
static size_t g_wsOutboundCount = 0;

// WS backpressure cooldown
static volatile uint32_t g_wsBackpressureUntilMs = 0;
static volatile uint32_t g_wsLastWritableMs      = 0;

// Staged initAll state (one client at a time by design for this A/B test)
static volatile uint32_t g_initAllClientId = 0;
static volatile uint8_t  g_initAllStep     = 0;
static volatile uint32_t g_initAllNextMs   = 0;
static constexpr uint32_t WS_INITALL_STEP_MS = 250UL;

static bool hasWritableWSClient() {
  ws.cleanupClients();

  for (auto &client : ws.getClients()) {
    if (client.status() != WS_CONNECTED) continue;
    if (client.queueIsFull()) continue;
    if (!client.canSend()) continue;
    return true;
  }
  return false;
}

static bool wsInCooldown() {
  return ((int32_t)(millis() - g_wsBackpressureUntilMs) < 0);
}

static bool ensureQueuedWsMutex() {
  if (g_queuedWsMutex == NULL) {
    g_queuedWsMutex = xSemaphoreCreateMutex();
  }
  return (g_queuedWsMutex != NULL);
}

static bool hasQueuedWsMessages() {
  if (!ensureQueuedWsMutex()) return false;

  bool hasMessages = false;
  if (xSemaphoreTake(g_queuedWsMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
    hasMessages = (g_wsOutboundCount > 0);
    xSemaphoreGive(g_queuedWsMutex);
  }
  return hasMessages;
}

static AsyncWebSocketClient* findWsClientById(uint32_t clientId) {
  if (clientId == 0) return nullptr;

  ws.cleanupClients();

  for (auto &client : ws.getClients()) {
    if (client.id() == clientId && client.status() == WS_CONNECTED) {
      return &client;
    }
  }
  return nullptr;
}

static bool enqueueWsMessage(uint32_t clientId,
                             const String& message,
                             const String& messageType,
                             uint8_t retryCount = 0) {
  if (message.length() == 0) return false;
  if (!ensureQueuedWsMutex()) return false;

  if (xSemaphoreTake(g_queuedWsMutex, pdMS_TO_TICKS(20)) != pdTRUE) {
    return false;
  }

  if (g_wsOutboundCount >= WS_OUTBOUND_QUEUE_LEN) {
    LOG_CAT(DBG_WEB,
            "[WS] Outbound queue full, dropping type=%s len=%u\n",
            messageType.c_str(), (unsigned)message.length());
    xSemaphoreGive(g_queuedWsMutex);
    return false;
  }

  QueuedWsMessage &slot = g_wsOutboundQueue[g_wsOutboundTail];
  slot.clientId   = clientId;
  slot.retryCount = retryCount;
  slot.message    = message;
  slot.messageType = messageType;

  g_wsOutboundTail = (g_wsOutboundTail + 1) % WS_OUTBOUND_QUEUE_LEN;
  g_wsOutboundCount++;

  xSemaphoreGive(g_queuedWsMutex);
  return true;
}

static bool dequeueWsMessage(QueuedWsMessage& out) {
  if (!ensureQueuedWsMutex()) return false;

  if (xSemaphoreTake(g_queuedWsMutex, pdMS_TO_TICKS(20)) != pdTRUE) {
    return false;
  }

  if (g_wsOutboundCount == 0) {
    xSemaphoreGive(g_queuedWsMutex);
    return false;
  }

  out = g_wsOutboundQueue[g_wsOutboundHead];
  g_wsOutboundQueue[g_wsOutboundHead].message = "";
  g_wsOutboundQueue[g_wsOutboundHead].messageType = "";

  g_wsOutboundHead = (g_wsOutboundHead + 1) % WS_OUTBOUND_QUEUE_LEN;
  g_wsOutboundCount--;

  xSemaphoreGive(g_queuedWsMutex);
  return true;
}

static void queueWsClient(AsyncWebSocketClient* client,
                          const String& message,
                          const String& messageType) {
  if (!client) return;
  if (client->status() != WS_CONNECTED) return;

  enqueueWsMessage(client->id(), message, messageType, 0);
}

static void queueWsToClientOrBroadcast(AsyncWebSocketClient* client,
                                       const String& message,
                                       const String& messageType) {
  if (client) {
    queueWsClient(client, message, messageType);
  } else {
    enqueueWsMessage(0, message, messageType, 0);
  }
}

void queueWsBroadcast(const String& message, const String& messageType) {
  enqueueWsMessage(0, message, messageType, 0);
}

// Global flag to indicate that pump runtime data needs to be updated
volatile bool needToUpdatePumpRuntimes = false;
extern TaskHandle_t thUpdatePumpRuntimes;

// Extern declarations for global variables
//extern int pumpStates[10];
//extern int pumpModes[10];
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
extern bool g_fileSystemReady;


// Temperature broadcast task telemetry (defined in TaskManager.cpp)
extern volatile uint32_t g_tempBcastCalled;
extern volatile uint32_t g_tempBcastSkipped;

static SemaphoreHandle_t g_logDataMutex = nullptr;

static bool takeLogDataMutex(TickType_t waitTicks) {
  if (!g_logDataMutex) {
    g_logDataMutex = xSemaphoreCreateMutex();
  }
  if (!g_logDataMutex) return false;
  return (xSemaphoreTake(g_logDataMutex, waitTicks) == pdTRUE);
}

static void giveLogDataMutex() {
  if (g_logDataMutex) xSemaphoreGive(g_logDataMutex);
}

static bool takeFsMutex(TickType_t waitTicks) {
  if (!g_fileSystemReady || !fileSystemMutex) return false;
  return (xSemaphoreTake(fileSystemMutex, waitTicks) == pdTRUE);
}

static void giveFsMutex() {
  if (fileSystemMutex) xSemaphoreGive(fileSystemMutex);
}

static String validateTemp(float v) {
  if (isnan(v)) return "N/A";
  return String(v, 1);
}

static String formatReadAgeSeconds(uint32_t lastGoodMs) {
  if (lastGoodMs == 0) return "N/A";
  return String((uint32_t)((millis() - lastGoodMs) / 1000UL));
}

static String wsBytesToString(const uint8_t* data, size_t len) {
  String s;
  if (!data || len == 0) return s;
  s.reserve(len);
  s.concat((const char*)data, len);   // copies exactly len bytes; no NUL needed
  return s;
}

// [ADD] Accept both comma and pipe as separators (backward compatible)
static int findNextListSep(const String& s, int start) {
  int c = s.indexOf(',', start);
  int p = s.indexOf('|', start);
  if (c == -1) return p;
  if (p == -1) return c;
  return (c < p) ? c : p;
}



// --- Robust parsing helpers for setConfig: ---
// Returns the next "key=value" pair starting at 'start'.
// If a value contains commas, this merges tokens until the next token contains '='.
// Updates 'start' to the beginning of the next pair (or payload.length()).
static String nextConfigPairMerged(const String& payload, int& start) {
  const int n = payload.length();
  // skip leading commas/spaces
  while (start < n && (payload[start] == ',' || payload[start] == ' ')) start++;

  String pair;
  int i = start;

  while (i <= n) {
    int comma = payload.indexOf(',', i);
    int end   = (comma == -1) ? n : comma;

    String tok = payload.substring(i, end);
    tok.trim();

    if (tok.length() > 0) {
      if (pair.length() == 0) {
        pair = tok;
      } else {
        // If the token looks like the start of a new pair, stop and leave start at token begin
        if (tok.indexOf('=') != -1) {
          start = i;   // next call will start here
          return pair;
        }
        // Otherwise it was part of the previous value (old comma-list case)
        pair += ",";
        pair += tok;
      }
    }

    if (comma == -1) {
      start = n;
      return pair;
    }
    i = comma + 1;
  }

  start = n;
  return pair;
}

// Parse a list like "2|5|6" (or "2,5,6") into a uint8_t[] terminated with 0.
// outMax is the total size of the out buffer (including terminator slot).
static void parseU8List_PipeOrComma(const String& val, uint8_t* out, size_t outMax) {
  if (!out || outMax < 2) return;

  int j = 0;
  int pos = 0;
  const int n = val.length();

  while (pos < n && j < (int)(outMax - 1)) {
    // find next delimiter: either '|' or ','
    int p = val.indexOf('|', pos);
    int c = val.indexOf(',', pos);

    int cut;
    if (p == -1) cut = c;
    else if (c == -1) cut = p;
    else cut = (p < c) ? p : c;

    String token = (cut == -1) ? val.substring(pos) : val.substring(pos, cut);
    token.trim();

    if (token.length() > 0) {
      out[j++] = (uint8_t) token.toInt();
    }

    if (cut == -1) break;
    pos = cut + 1;
  }

  out[j] = 0; // terminator
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


static uint16_t clampU16(long v, uint16_t lo, uint16_t hi) {
  if (v < (long)lo) return lo;
  if (v > (long)hi) return hi;
  return (uint16_t)v;
}

static float clampF(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
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
String getFSStatsString();
void sendSystemStats(AsyncWebSocketClient* client); 
bool safeHasValue(float temp);
void sendConfigurationValues(AsyncWebSocketClient* client);
void sendTimeConfig(AsyncWebSocketClient* client);
String wsBytesToString(const uint8_t* data, size_t len);
void handleWebSocketMessage(void* arg, uint8_t* data, size_t len);

void sendAllData(AsyncWebSocketClient* client);
void sendSystemStats(AsyncWebSocketClient* client); 
void broadcastMessageOverWebSocket(const String& message, const String& messageType);

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
static void startInitAllForClient(AsyncWebSocketClient* client);
static bool processInitAllStep(uint32_t now);

static void startInitAllForClient(AsyncWebSocketClient* client) {
  if (!client) return;
  if (client->status() != WS_CONNECTED) return;

  g_initAllClientId = client->id();
  g_initAllStep     = 1;
  g_initAllNextMs   = millis();

  LOG_CAT(DBG_WEB, "[WS] initAll scheduled for client id=%u\n",
          (unsigned)g_initAllClientId);
}

static bool processInitAllStep(uint32_t now) {
  if (g_initAllClientId == 0 || g_initAllStep == 0) return false;

  // Keep the queue shallow: only schedule the next init step when the
  // outbound queue is currently empty.
  if (hasQueuedWsMessages()) return false;

  if ((int32_t)(now - g_initAllNextMs) < 0) return false;

  AsyncWebSocketClient* client = findWsClientById(g_initAllClientId);
  if (client == nullptr) {
    LOG_CAT(DBG_WEB,
            "[WS] initAll cancelled; client id=%u no longer connected\n",
            (unsigned)g_initAllClientId);
    g_initAllClientId = 0;
    g_initAllStep = 0;
    g_initAllNextMs = 0;
    return false;
  }

  switch (g_initAllStep) {
    case 1:
      sendPumpStatuses(client);
      break;

    case 2: {
      String heatingCallData = "HeatingCalls:";
      heatingCallData += "DHW:";
      heatingCallData += (digitalRead(DHW_HEATING_PIN) == LOW) ? "ACTIVE" : "INACTIVE";
      heatingCallData += ",Heating:";
      heatingCallData += (digitalRead(FURNACE_HEATING_PIN) == LOW) ? "ACTIVE" : "INACTIVE";
      queueWsClient(client, heatingCallData, "HeatingCalls");
      break;
    }

    case 3: {
      uint32_t n = AlarmManager_activeCount();
      queueWsClient(
        client,
        (n > 0) ? ("AlarmState:ALARM,count=" + String(n))
                :  "AlarmState:OK,count=0",
        "AlarmState"
      );
      break;
    }

    case 4:
      sendConfigurationValues(client);
      break;

    case 5:
      sendTimeConfig(client);
      break;

    case 6:
      sendDateTime(client);
      break;

    case 7:
      sendUptime(client);
      break;

    case 8:
      sendSystemStats(client);
      break;

    case 9:
      sendTemperatures(client);
      break;

    default:
      g_initAllClientId = 0;
      g_initAllStep = 0;
      g_initAllNextMs = 0;
      return false;
  }

  LOG_CAT(DBG_WEB,
          "[WS] initAll step %u queued for client id=%u\n",
          (unsigned)g_initAllStep,
          (unsigned)g_initAllClientId);

  if (g_initAllStep >= 9) {
    g_initAllClientId = 0;
    g_initAllStep = 0;
    g_initAllNextMs = 0;
  } else {
    g_initAllStep++;
    g_initAllNextMs = now + WS_INITALL_STEP_MS;
  }

  return true;
}

static void sendAlarmStateWs(uint32_t n) {
  if (n > 0) {
    queueWsBroadcast("AlarmState:ALARM,count=" + String(n), "AlarmState");
  } else {
    queueWsBroadcast("AlarmState:OK,count=0", "AlarmState");
  }
}

static void onAlarmStateChanged(uint32_t activeCount) {
  (void)activeCount;
  g_sendAlarmState = true;
}

void broadcastAlarmStateOverWebSocket() {
  uint32_t n = AlarmManager_activeCount();
  sendAlarmStateWs(n);
}


void sendConfigurationValues(AsyncWebSocketClient* client) {
    if (client && client->status() != WS_CONNECTED) {
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

    configData += ",lineFreezeTempF:" + validateConfigValue(g_config.lineFreezeTempF);
    configData += ",lineFreezeConfirmMin:" + String((uint32_t)g_config.lineFreezeConfirmMin);
    configData += ",lineFreezeRunMin:" + String((uint32_t)g_config.lineFreezeRunMin);

    // Sensors
    configData += ",collectorFreezeSensors:";
    bool first = true;
    for (uint8_t* s = g_config.collectorFreezeSensors; *s; s++) {
      if (!first) configData += "|";
      configData += String(*s);
      first = false;
    }

    configData += ",lineFreezeSensors:";
    first = true;
    for (uint8_t* s = g_config.lineFreezeSensors; *s; s++) {
      if (!first) configData += "|";
      configData += String(*s);
      first = false;
    }

    // ---------------------------------------------------

    queueWsToClientOrBroadcast(client, configData, "Configuration");
}

// ---- TimeConfig sender (new) ----
void sendTimeConfig(AsyncWebSocketClient* client) {
    if (client && client->status() != WS_CONNECTED) {
        return;
    }

    String msg = "TimeConfig:";
    msg += "timeZoneId=" + g_timeConfig.timeZoneId;
    msg += ",dstEnabled=" + String(g_timeConfig.dstEnabled ? 1 : 0);

    queueWsToClientOrBroadcast(client, msg, "TimeConfig");
}

void serveStaticAssets(AsyncWebServer& server) {
  server.serveStatic("/static/", LittleFS, "/static/");
  // optional caching:
  // server.serveStatic("/static/", LittleFS, "/static/").setCacheControl("max-age=86400");
}

void serveFavicon(AsyncWebServer& server) {
    // We have access to LittleFS here
    server.serveStatic("/favicon.png", LittleFS, "/favicon.png");
}

// Start the server
void startServer() {
    serveStaticAssets(server);
    serveFavicon(server);     // sets up the route
    initWebSocket();          // Initialize WebSocket
    setupRoutes();            // Setup additional routes
    ensurePumpRuntimeJsonMutex();
    AlarmManager_setStateChangedCallback(onAlarmStateChanged);
    server.begin();           // Start the server
}


// Initialize the WebSocket
void initWebSocket() {
    ws.onEvent(handleWebSocketEvent);
    server.addHandler(&ws);
}

void setAllPumpsMode(int mode) {
    for (int i = 0; i < numPumps; i++) {
        pumpModes[i] = mode;
    }

    if (mode == PUMP_AUTO) {
        LOG_CAT(DBG_PUMP, "All pumps set to AUTO via web button.\n");
    } else if (mode == PUMP_OFF) {
        LOG_CAT(DBG_PUMP, "All pumps turned OFF via web button.\n");
    }

    // Gatekeeper-only: do NOT directly broadcast here
    g_sendPumpStatus = true;
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
    LOG_CAT(DBG_WEB, "WebSocket client connected (id=%u)\n", client ? client->id() : 0);
    return;
  }

  if (type == WS_EVT_DISCONNECT) {
    LOG_CAT(DBG_WEB, "WebSocket client disconnected (id=%u)\n", client ? client->id() : 0);

    if (client && g_initAllClientId == client->id()) {
      g_initAllClientId = 0;
      g_initAllStep = 0;
      g_initAllNextMs = 0;
      LOG_CAT(DBG_WEB, "[WS] initAll cancelled on disconnect for id=%u\n", client->id());
    }
    
    // Log the client disconnect to the Alarm History
    AlarmManager_event(ALM_WS_DISCONNECT, ALM_INFO, "WS Client ID %u Disconnected", client ? client->id() : 0);
    
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
    LOG_CAT(DBG_WEB, "[WS hello] id=%u msg=%s\n", client ? client->id() : 0, msg.c_str());
    return;  // IMPORTANT: don't pass hello into the generic message handler
  }


  // Legacy init (kept tiny on purpose)
  if (msg == "init") {
    if (client) {
      sendPumpStatuses(client);
      sendDateTime(client);
      sendUptime(client);
    }
    return;
  }

  // Consolidated FirstWebpage startup:
  // browser sends one init request; server now schedules the full response
  // set in paced steps instead of queueing everything immediately.
  if (msg == "initAll") {
    if (client) {
      startInitAllForClient(client);
    }
    return;
  }

  if (msg == "initPumpStatus") {
    if (client) {
      sendPumpStatuses(client);
    }
    return;
  }

  if (msg == "initHeatingCalls") {
    if (client) {
      String heatingCallData = "HeatingCalls:";
      heatingCallData += "DHW:";
      heatingCallData += (digitalRead(DHW_HEATING_PIN) == LOW) ? "ACTIVE" : "INACTIVE";
      heatingCallData += ",Heating:";
      heatingCallData += (digitalRead(FURNACE_HEATING_PIN) == LOW) ? "ACTIVE" : "INACTIVE";
      queueWsClient(client, heatingCallData, "HeatingCalls");
    }
    return;
  }

  if (msg == "initTemperatures") {
    if (client) {
      sendTemperatures(client);
    }
    return;
  }

  if (msg == "initConfig") {
    if (client) {
      sendConfigurationValues(client);
    }
    return;
  }

  if (msg == "initTimeConfig") {
    if (client) {
      sendTimeConfig(client);
    }
    return;
  }

  if (msg == "initAlarmState") {
    if (client) {
      uint32_t n = AlarmManager_activeCount();
      queueWsClient(
        client,
        (n > 0) ? ("AlarmState:ALARM,count=" + String(n))
                :  "AlarmState:OK,count=0",
        "AlarmState"
      );
    }
    return;
  }

  if (msg == "initSystemStats") {
    if (client) {
      sendSystemStats(client);
    }
    return;
  }

  if (msg == "initDateTime") {
    if (client) {
      sendDateTime(client);
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

        LOG_CAT(DBG_WEB, "[WS] Received message: %s\n", message.c_str());

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
            queueWsBroadcast("FsStats:" + json, "FsStats");
        } else if (message == "deleteTemperatureLogs") {
               // dangerous: only use if you intentionally want to delete all logs
                  bool ok = deleteTemperatureLogsRecursive("/Temperature_Logs");
                   queueWsBroadcast(String("DeleteTempLogsResult:") + (ok ? "OK" : "FAIL"),
                                    "DeleteTempLogsResult");
        }                 else if (message.startsWith("setConfig:")) {
                 String payload = message.substring(strlen("setConfig:"));
                 // Format: setConfig:key=val,key=val,... (values may contain commas)
                 int start = 0;

                 while (start < payload.length()) {

                   String pair = nextConfigPairMerged(payload, start);
                   pair.trim();
                   if (pair.length() == 0) continue;

                   int eq = pair.indexOf('=');
                   if (eq <= 0) continue;

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

                   } else if (key == "lineFreezeTempF") {
                     g_config.lineFreezeTempF = clampF(f, 20.0f, 80.0f);
                   } else if (key == "lineFreezeConfirmMin") {
                     g_config.lineFreezeConfirmMin = clampU16(i, 1, 120);
                   } else if (key == "lineFreezeRunMin") {
                     g_config.lineFreezeRunMin = clampU16(i, 1, 120);

                   } else if (key == "collectorFreezeSensors") {
                     parseU8List_PipeOrComma(val, g_config.collectorFreezeSensors,
                                             sizeof(g_config.collectorFreezeSensors));
                   } else if (key == "lineFreezeSensors") {
                     parseU8List_PipeOrComma(val, g_config.lineFreezeSensors,
                                             sizeof(g_config.lineFreezeSensors));
                   }
                 }

                 // Persist to LittleFS
                              if (!saveSystemConfigToFS()) {
                              LOG_ERR("[Config] ERROR while saving system_config.json\n");
                              queueWsBroadcast("ConfigSave:FAIL", "ConfigSave");
                               } else 
                              {
                              LOG_CAT(DBG_CONFIG, "[Config] system_config.json saved from WebUI\n");
                              queueWsBroadcast("ConfigSave:OK", "ConfigSave");

                   // Re-send configuration so all clients update display
                   g_sendConfig = true;
                 }
                 }   
                    else if (message == "resetConfig") {

                LOG_CAT(DBG_CONFIG, "[WS] Reset SystemConfig to defaults requested\n");


                bool ok = resetSystemConfigToDefaults();  // helper from Config.cpp

                                if (ok) {
                    queueWsBroadcast("ConfigReset:OK", "ConfigReset");
                    // Push fresh values so browsers update all spans/inputs + currentConfig cache
                    g_sendConfig = true;
                } else {
                    queueWsBroadcast("ConfigReset:FAIL", "ConfigReset");
                }
                        }   else if (message.startsWith("setTimeConfig:")) {
                 String payload = message.substring(strlen("setTimeConfig:"));
                 // Format: setTimeConfig:key=val,key=val,...
                 int start = 0;

                 while (start < payload.length()) {
                   String pair = nextConfigPairMerged(payload, start);
                   pair.trim();
                   if (pair.length() == 0) continue;

                   int eq = pair.indexOf('=');
                   if (eq <= 0) continue;

                   String key = pair.substring(0, eq);
                   String val = pair.substring(eq + 1);
                   key.trim();
                   val.trim();

                   if (key == "timeZoneId") {
                     // e.g. "America/Los_Angeles" or your own IDs
                     g_timeConfig.timeZoneId = val;
                   } else if (key == "dstEnabled") {
                     // accept 0/1, true/false
                     val.toLowerCase();
                     g_timeConfig.dstEnabled = (val == "1" || val == "true" || val == "yes" || val == "on");
                   }
                 }

                    if (!saveTimeConfigToFS()) {
                    LOG_ERR("[TimeConfig] ERROR while saving time_config.json\n");
                    queueWsBroadcast("TimeConfigSave:FAIL", "TimeConfigSave");
                } else {
                    LOG_CAT(DBG_CONFIG, "[TimeConfig] time_config.json saved from WebUI\n");
                    queueWsBroadcast("TimeConfigSave:OK", "TimeConfigSave");

                   // Re-send so all clients update display
                   g_sendTimeConfig = true;
                   // 🔁 Re-run NTP so RTC + timestamps immediately pick up the new TZ
                   requestImmediateNtpResync();
                 }
            }
            
                else if (message.equals("resetTimeConfig")) {

                LOG_CAT(DBG_CONFIG, "[WS] Reset TimeConfig to defaults requested\n");


                bool ok = resetTimeConfigToDefaults();

                                if (ok) {
                    queueWsBroadcast("TimeConfigReset:OK", "TimeConfigReset");
                    g_sendTimeConfig = true;
                    
                } else {
                    queueWsBroadcast("TimeConfigReset:FAIL", "TimeConfigReset");
                }
            }
    }
}




void handleSetPumpMode(String message) {
    int firstColon = message.indexOf(':');
    int secondColon = message.indexOf(':', firstColon + 1);
    if (firstColon != -1 && secondColon != -1) {
        int pumpIndex = message.substring(firstColon + 1, secondColon).toInt() - 1; 
        String mode = message.substring(secondColon + 1);
        mode.toLowerCase(); 

        if (pumpIndex >= 0 && pumpIndex < numPumps) {
            int newMode = PUMP_AUTO; 
            if (mode == "on") {
                newMode = PUMP_ON;
            } else if (mode == "off") {
                newMode = PUMP_OFF;
            }

            if (pumpModes[pumpIndex] != newMode) {
                pumpModes[pumpIndex] = newMode;
                LOG_CAT(DBG_PUMP, "Pump %d mode set to %s\n", pumpIndex + 1, mode.c_str());
                g_sendPumpStatus = true; // Flag the gatekeeper
            }
        } else {
            LOG_CAT(DBG_PUMP, "Invalid pump index received.\n");
        }
    }
}

// Handle log data requests
void handleRequestLogData(String message) {

  // Serialize ALL WS log-data requests
    if (!takeLogDataMutex(pdMS_TO_TICKS(5000))) {
        LOG_ERR("[LogData] BUSY (WS) - mutex timeout\n");
    queueWsBroadcast("{\"error\":\"BUSY\"}", "LogDataBusy");
    return;

  }

  // Expected format: requestLogData:pumpIndex:timeframe
  int firstColon  = message.indexOf(':');
  int secondColon = message.lastIndexOf(':');

  if (firstColon != -1 && secondColon != -1 && secondColon > firstColon) {
    int pumpIndex  = message.substring(firstColon + 1, secondColon).toInt() - 1; // 0-based
    String timeframe = message.substring(secondColon + 1);

        String logData = prepareLogData(pumpIndex, timeframe);
    queueWsBroadcast(logData, "LogData");
  } else {
    LOG_CAT(DBG_WEB, "[WS] Invalid requestLogData message format\n");

  }
  giveLogDataMutex();
}


// Send pump statuses to client
// Send pump statuses to client (Optimized with Local Cache)
void sendPumpStatuses(AsyncWebSocketClient* client) {
    // For broadcast path, do not even build the JSON unless a client is writable.
    if (client == nullptr && !hasWritableWSClient()) {
        return;
    }

    // For one-client init path, only require a connected client.
    if (client && client->status() != WS_CONNECTED) {
        return;
    }

    static String cachedPayload = "";
    static bool needsRebuild = true;

    // If client == nullptr, it means this was triggered by g_sendPumpStatus = true
    // (a state actually changed). Therefore, we MUST rebuild the cache.
    if (client == nullptr) {
        needsRebuild = true;
    }

    // Only do the heavy JSON serialization if the state changed, or cache is empty
    if (needsRebuild || cachedPayload.length() == 0) {
        DynamicJsonDocument doc(2048);
        JsonArray pumps = doc.to<JsonArray>();

        for (int i = 0; i < numPumps; i++) {
            JsonObject pump = pumps.createNestedObject();
            pump["pumpIndex"] = i + 1;
            pump["name"] = pumpNames[i];
            pump["state"] = pumpStates[i] == PUMP_ON ? "ON" : "OFF";

            String modeStr;
            switch (pumpModes[i]) {
                case PUMP_ON:   modeStr = "on"; break;
                case PUMP_OFF:  modeStr = "off"; break;
                case PUMP_AUTO: modeStr = "auto"; break;
                default:        modeStr = "unknown"; break;
            }
            pump["mode"] = modeStr;
        }

        String pumpStatusData;
        serializeJson(pumps, pumpStatusData);
        cachedPayload = "PumpStatus:" + pumpStatusData;
        needsRebuild = false; // Cache is now fresh
    }

    // Fire the pre-baked string instantly
    queueWsToClientOrBroadcast(client, cachedPayload, "PumpStatus");
}

// **New: Send Updated Temperatures**
void sendUpdatedTemperatures() {
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
    tempData += "pt1000Current:" + String(pt1000Current) + ",";
    tempData += "pt1000Average:" + String(pt1000Average) + ",";
    tempData += "pt1000GoodAge:" + formatReadAgeSeconds(pt1000LastGoodReadMs) + ",";

    // DTemp1 to DTemp13, their averages, and last-good-read ages
    for (int i = 0; i < NUM_SENSORS; i++) {
        tempData += "DTemp" + String(i + 1) + ":" + String(DTemp[i]) + ",";
        tempData += "DTempAverage" + String(i + 1) + ":" + String(DTempAverage[i]) + ",";
        tempData += "DTempGoodAge" + String(i + 1) + ":" + formatReadAgeSeconds(DTempLastGoodReadMs[i]) + ",";
    }

    // Remove the trailing comma
    if (tempData.endsWith(",")) {
        tempData.remove(tempData.length() - 1);
    }

    queueWsToClientOrBroadcast(client, tempData, "Temperatures");
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
    queueWsToClientOrBroadcast(client, dateTimeData, "DateTime");
}

// Send uptime to client
void sendUptime(AsyncWebSocketClient* client) {
    String uptimeData = "Uptime:" + uptime_formatter::getUptime();
    queueWsToClientOrBroadcast(client, uptimeData, "Uptime");
}

// Send heap + filesystem stats to client.
// Uses two WebSocket messages:
//   "SysStats:heap=...|psram=..."
//   "FSStats:{...json...}"
void sendSystemStats(AsyncWebSocketClient* client) {
    String heapStr  = getCachedHeapInternalString(); // cached INTERNAL heap only
    String psramStr = getCachedPsramString();        // cached PSRAM only
    String fsJson   = getFSStatsString();            // cached JSON string

    String sysStatsMsg = "SysStats:heap=" + heapStr + "|psram=" + psramStr;

    if (client) {
        queueWsClient(client, sysStatsMsg, "SysStats");
        queueWsClient(client, "FSStats:" + fsJson, "FSStats");
    } else {
        queueWsBroadcast(sysStatsMsg, "SysStats");
        queueWsBroadcast("FSStats:" + fsJson, "FSStats");
    }
}



// ===== The Gatekeeper: Single WebSocket Transmitter =====
void TaskWebSocketTransmitter(void* pvParameters) {
    (void)pvParameters;

    esp_task_wdt_add(NULL);

        if (g_tempWsPayloadMutex == NULL) {
        g_tempWsPayloadMutex = xSemaphoreCreateMutex();
    }
    if (!ensureQueuedWsMutex()) {
        LOG_ERR("[WS] Failed to create outbound queue mutex\n");
        vTaskDelete(NULL);
        return;
    }

    uint32_t lastFsMs        = millis();
    uint32_t lastFastMs      = millis();
    uint32_t lastDateTimeMs  = millis();

    String lastHeapStr;
    String lastPsramStr;

    for (;;) {
        esp_task_wdt_reset();

        bool didWork = false;
        const uint32_t now = millis();

        // ------------------------------------------------------------
        // 1. THE APP NAP DEFENSE (Gatekeeper)
        // ------------------------------------------------------------
        bool buffersReady = false;
        
        if (ws.count() > 0) {
            if (ws.availableForWriteAll()) {
                buffersReady = true;
            } else {
                // Clients are connected, but the AsyncTCP buffer is choked!
                // This happens when Safari goes into "App Nap" and stops ACKing packets.
                // We MUST forcefully clean up dead clients to prevent W5500 overflow.
                ws.cleanupClients();
            }
        }

        // Track whether we currently have any writable clients
        if (hasWritableWSClient()) {
            g_wsLastWritableMs = now;
        }

        // If all clients are clogged/stale OR the buffers are choked, skip sending
        if (wsInCooldown() || !buffersReady) {
            g_sendDateTime = false;  // drop accumulated time spam during cooldown
            vTaskDelay(pdMS_TO_TICKS(50));
            continue; 
        }

        // ------------------------------------------------------------
        // Priority 1: immediate state changes
        // Service at most ONE queued event per loop, then pace.
        // ------------------------------------------------------------

        if (g_sendPumpStatus) {
            g_sendPumpStatus = false;
            sendPumpStatuses(nullptr);
            didWork = true;
        }
        else if (g_sendAlarmState) {
            g_sendAlarmState = false;
            broadcastAlarmStateOverWebSocket();
            didWork = true;
        }
        else if (processInitAllStep(now)) {
            didWork = true;
        }
        else if (hasQueuedWsMessages()) {
            QueuedWsMessage queuedItem;

            if (dequeueWsMessage(queuedItem)) {
                if (queuedItem.clientId == 0) {
                    broadcastMessageOverWebSocket(queuedItem.message, queuedItem.messageType);
                    didWork = true;
                } else {
                    AsyncWebSocketClient* target = findWsClientById(queuedItem.clientId);

                    if (target == nullptr) {
                        LOG_CAT(DBG_WEB,
                                "[WS] Dropping queued one-client message; client id=%u no longer connected. type=%s\n",
                                (unsigned)queuedItem.clientId,
                                queuedItem.messageType.c_str());
                    } else if (target->queueIsFull() || !target->canSend()) {
                        if (queuedItem.retryCount < 3) {
                            enqueueWsMessage(queuedItem.clientId,
                                             queuedItem.message,
                                             queuedItem.messageType,
                                             queuedItem.retryCount + 1);
                            didWork = true;  // enforce pacing even on retry
                        } else {
                            LOG_CAT(DBG_WEB,
                                    "[WS] Dropping queued one-client message after retries. id=%u type=%s len=%u\n",
                                    (unsigned)queuedItem.clientId,
                                    queuedItem.messageType.c_str(),
                                    (unsigned)queuedItem.message.length());
                        }
                    } else {
                        target->text(queuedItem.message);
                        didWork = true;
                    }
                }
            }
        }
        else if (g_sendConfig) {
            g_sendConfig = false;
            sendConfigurationValues(nullptr);
            didWork = true;
        }
        else if (g_sendTimeConfig) {
            g_sendTimeConfig = false;
            sendTimeConfig(nullptr);
            didWork = true;
        }
        else if (g_sendTemperatures) {
            g_sendTemperatures = false;

            String payload;
            if (g_tempWsPayloadMutex &&
                xSemaphoreTake(g_tempWsPayloadMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                payload = g_tempWsPayload;
                xSemaphoreGive(g_tempWsPayloadMutex);
            }

            if (payload.length() > 0) {
                broadcastMessageOverWebSocket(payload, "Temperatures");
                didWork = true;
            }
        }
        // Rate-limit DateTime to 5s max, even if RTC task flags it every second
        else if (g_sendDateTime && (uint32_t)(now - lastDateTimeMs) >= 5000UL) {
            g_sendDateTime = false;
            lastDateTimeMs = now;
            sendDateTime(nullptr);
            didWork = true;
        }
        else if (g_sendDateTime) {
            // Drop extra queued DateTime ticks instead of piling them up
            g_sendDateTime = false;
        }

        // ------------------------------------------------------------
        // Priority 2: periodic stats
        // Only run if no higher-priority work was sent this iteration.
        // (No need to check hasWritableWSClient() here since buffersReady checked it above)
        // ------------------------------------------------------------

                if (!didWork && (uint32_t)(now - lastFastMs) >= 5000UL) {
            lastFastMs = now;

            String heapStr  = getCachedHeapInternalString();
            String psramStr = getCachedPsramString();

            if (heapStr != lastHeapStr || psramStr != lastPsramStr) {
                lastHeapStr  = heapStr;
                lastPsramStr = psramStr;

                String msg = "SysStats:heap=" + heapStr + "|psram=" + psramStr;
                broadcastMessageOverWebSocket(msg, "SysStats");
                didWork = true;
            }
        }

        if (!didWork && (uint32_t)(now - lastFsMs) >= 30000UL) {
            lastFsMs = now;

            String fsMsg = "FSStats:" + getFSStatsString();
            broadcastMessageOverWebSocket(fsMsg, "FSStats");
            didWork = true;
        }

        

        if (didWork) {
            // YIELD: Increase pacing slightly and ensure the Idle task/Network stack 
            // can run between every single WebSocket frame sent.
            vTaskDelay(pdMS_TO_TICKS(50)); 
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}


void sendAllData(AsyncWebSocketClient* client) {
  if (client && client->status() != WS_CONNECTED) {
    LOG_CAT(DBG_WEB, "[WS] Client not connected, skipping sendAllData.\n");
    return;
  }

  sendPumpStatuses(client);

  if (xSemaphoreTake(temperatureMutex, portMAX_DELAY)) {
    String tempData = "Temperatures:";

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
    tempData += ",pt1000Current:" + validateTemp(pt1000Current);
    tempData += ",pt1000Average:" + validateTemp(pt1000Average);
    tempData += ",pt1000GoodAge:" + formatReadAgeSeconds(pt1000LastGoodReadMs);

    for (int i = 0; i < NUM_SENSORS; i++) {
      tempData += ",DTemp" + String(i + 1) + ":" + validateTemp(DTemp[i]);
      tempData += ",DTempAverage" + String(i + 1) + ":" + validateTemp(DTempAverage[i]);
      tempData += ",DTempGoodAge" + String(i + 1) + ":" + formatReadAgeSeconds(DTempLastGoodReadMs[i]);
    }

    // IMPORTANT:
    // Do NOT do live Heap/PSRAM reads here.
    // Those values are now sent separately through the cached SysStats path.
    tempData.replace("Temperatures:,", "Temperatures:");

    if (tempData.length() > strlen("Temperatures:")) {
      queueWsToClientOrBroadcast(client, tempData, "Temperatures");
    } else {
      LOG_CAT(DBG_WEB, "[Warning] No valid temperature data to broadcast.\n");
    }

    xSemaphoreGive(temperatureMutex);
  } else {
    LOG_CAT(DBG_WEB, "[WS] Failed to take temperatureMutex in sendAllData.\n");
  }

  sendDateTime(client);
  sendUptime(client);
}






void broadcastMessageOverWebSocket(const String& message, const String& messageType) {
  if (message.length() == 0) {
    LOG_ERR("[Error] Attempted to send zero-length WebSocket message: %s\n", messageType.c_str());
    return;
  }

  ws.cleanupClients();

  size_t sent = 0;
  size_t skipped = 0;
  size_t connected = 0;

  for (auto &client : ws.getClients()) {
    if (client.status() != WS_CONNECTED) {
      skipped++;
      continue;
    }

    connected++;

    if (client.queueIsFull() || !client.canSend()) {
      skipped++;
      continue;
    }

    if (client.text(message)) {
      sent++;
    } else {
      skipped++;
    }
  }

  if (sent > 0) {
    g_wsLastWritableMs = millis();
    return;
  }

  if (connected > 0 && skipped >= connected) {
    g_wsBackpressureUntilMs = millis() + 5000UL;

    LOG_CAT(DBG_WEB,
            "[WS] broadcastMessageOverWebSocket: all clients skipped (queue full / cannot send). type=%s len=%u\n",
            messageType.c_str(), (unsigned)message.length());
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

    if (!takeFsMutex(pdMS_TO_TICKS(2000))) {
    LOG_ERR("[FS] calculateTotalLogRuntime: FS mutex timeout\n");
    return 0;
  }


  if (!LittleFS.exists(logFilename)) {
    giveFsMutex();
    return 0;
  }

  File logFile = LittleFS.open(logFilename, "r");
    if (!logFile) {
    LOG_ERR("[FS] Failed to open: %s\n", logFilename.c_str());
    giveFsMutex();
    return 0;
  }


  unsigned long totalRuntime = 0;
  DateTime lastStartTime;
  bool isPumpRunning = false;

  uint32_t lineCount = 0;
  while (logFile.available()) {
    String line = logFile.readStringUntil('\n');
    line.trim();

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

    if ((++lineCount % 50) == 0) (void)0;
  }

  logFile.close();
  giveFsMutex();

  // If still running, add runtime up to now (no FS needed)
  if (isPumpRunning) {
    DateTime currentTime = getCurrentTimeAtomic();
    totalRuntime += (currentTime.unixtime() - lastStartTime.unixtime());
  }

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
        LOG_CAT(DBG_PUMP_RUN_TIME_UI, "Invalid timeframe requested: %s\n", timeframe.c_str());
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

  String dailyLogFilename = "/Pump_Logs/pump" + String(pumpIndex + 1) + "_Daily.txt";
  unsigned long prevDayRuntimeSeconds = 0;

  if (!takeFsMutex(pdMS_TO_TICKS(2000))) {
    LOG_CAT(DBG_PUMP_RUN_TIME_UI, "[FS] month: FS mutex timeout\n");
    return 0;

  }

  if (!LittleFS.exists(dailyLogFilename)) {
    giveFsMutex();
    return 0;
  }

  File dailyLogFile = LittleFS.open(dailyLogFilename, "r");
  if (!dailyLogFile) {
    LOG_CAT(DBG_PUMP_RUN_TIME_UI, "[FS] Failed to open: %s\n", dailyLogFilename.c_str());
    giveFsMutex();

    return 0;
  }

  DateTime prevDay = currentTime - TimeSpan(1, 0, 0, 0);
  String prevDayStr = String(prevDay.year()) + "-" +
                      (prevDay.month() < 10 ? "0" : "") + String(prevDay.month()) + "-" +
                      (prevDay.day() < 10 ? "0" : "") + String(prevDay.day());

  while (dailyLogFile.available()) {
    String line = dailyLogFile.readStringUntil('\n');
    line.trim();

    int spaceIndex = line.indexOf(' ');
    if (spaceIndex != -1) {
      String datePart = line.substring(0, spaceIndex);
      if (datePart == prevDayStr) {
        int runtimeStartIndex = line.indexOf("Total Runtime: ") + 15;
        int secondsIndex = line.indexOf(" seconds", runtimeStartIndex);
        if (runtimeStartIndex != -1 && secondsIndex != -1) {
          String runtimeStr = line.substring(runtimeStartIndex, secondsIndex);
          runtimeStr.trim();
          prevDayRuntimeSeconds += runtimeStr.toInt();
        }
      }
    }
  }

  dailyLogFile.close();
  giveFsMutex();
  return prevDayRuntimeSeconds;
}


// Function to aggregate monthly logs
unsigned long aggregateMonthlyLogsReport(int pumpIndex, DateTime currentTime) {

  String dailyLogFilename = "/Pump_Logs/pump" + String(pumpIndex + 1) + "_Daily.txt";
  unsigned long totalRuntimeSeconds = 0;

  // Read Daily.txt under FS lock
  if (!takeFsMutex(pdMS_TO_TICKS(2000))) {
    LOG_CAT(DBG_PUMP_RUN_TIME_UI, "[FS] month: FS mutex timeout\n");
    return 0;

  }

  if (LittleFS.exists(dailyLogFilename)) {
    File dailyLogFile = LittleFS.open(dailyLogFilename, "r");
    if (dailyLogFile) {
      char currentMonth[8];
      snprintf(currentMonth, sizeof(currentMonth), "%04d-%02d", currentTime.year(), currentTime.month());

      while (dailyLogFile.available()) {
        String line = dailyLogFile.readStringUntil('\n');
        line.trim();

        int dateSeparatorIndex = line.indexOf(' ');
        if (dateSeparatorIndex != -1) {
          String date = line.substring(0, dateSeparatorIndex);
          if (date.startsWith(currentMonth)) {
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
    }
  }

  giveFsMutex();

  // Add today runtime (separate FS operation inside calculateTotalLogRuntime)
  totalRuntimeSeconds += aggregateDailyLogsReport(pumpIndex, currentTime);
  return totalRuntimeSeconds;
}


// Function to aggregate previous month's logs
unsigned long aggregatePreviousMonthlyLogsReport(int pumpIndex, DateTime currentTime) {

  String monthlyLogFilename = "/Pump_Logs/pump" + String(pumpIndex + 1) + "_Monthly.txt";
  unsigned long prevMonthRuntimeSeconds = 0;

  if (!takeFsMutex(pdMS_TO_TICKS(2000))) {
    LOG_CAT(DBG_PUMP_RUN_TIME_UI, "[FS] prevmonth: FS mutex timeout\n");
    return 0;

  }

  if (!LittleFS.exists(monthlyLogFilename)) {
    giveFsMutex();
    return 0;
  }

  File monthlyLogFile = LittleFS.open(monthlyLogFilename, "r");
  if (!monthlyLogFile) {
    LOG_CAT(DBG_PUMP_RUN_TIME_UI, "[FS] Failed to open: %s\n", monthlyLogFilename.c_str());
    giveFsMutex();
    return 0;
  }

  int prevMonth = currentTime.month() - 1;
  int prevYear  = currentTime.year();
  if (prevMonth == 0) { prevMonth = 12; prevYear -= 1; }

  char prevMonthStr[8];
  snprintf(prevMonthStr, sizeof(prevMonthStr), "%04d-%02d", prevYear, prevMonth);

  while (monthlyLogFile.available()) {
    String line = monthlyLogFile.readStringUntil('\n');
    line.trim();

    int s = line.indexOf(' ');
    if (s != -1) {
      String date = line.substring(0, s);
      if (date == prevMonthStr) {
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
  giveFsMutex();
  return prevMonthRuntimeSeconds;
}


// Function to aggregate yearly logs
unsigned long aggregateYearlyLogsReport(int pumpIndex, DateTime currentTime) {

  unsigned long totalRuntimeSeconds = aggregateMonthlyLogsReport(pumpIndex, currentTime);

  String monthlyLogFilename = "/Pump_Logs/pump" + String(pumpIndex + 1) + "_Monthly.txt";

  if (!takeFsMutex(pdMS_TO_TICKS(2000))) {
    LOG_CAT(DBG_PUMP_RUN_TIME_UI, "[FS] year: FS mutex timeout\n");
    return totalRuntimeSeconds;

  }

  if (!LittleFS.exists(monthlyLogFilename)) {
    giveFsMutex();
    return totalRuntimeSeconds;
  }

  File monthlyLogFile = LittleFS.open(monthlyLogFilename, "r");
  if (!monthlyLogFile) {
    LOG_CAT(DBG_PUMP_RUN_TIME_UI, "[FS] Failed to open: %s\n", monthlyLogFilename.c_str());
    giveFsMutex();

    return totalRuntimeSeconds;
  }

  char currentYear[5];
  snprintf(currentYear, sizeof(currentYear), "%04d", currentTime.year());

  while (monthlyLogFile.available()) {
    String line = monthlyLogFile.readStringUntil('\n');
    line.trim();

    int s = line.indexOf(' ');
    if (s != -1) {
      String date = line.substring(0, s);
      if (date.startsWith(currentYear)) {
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
  giveFsMutex();
  return totalRuntimeSeconds;
}


// Function to aggregate previous year's logs
unsigned long aggregatePreviousYearlyLogsReport(int pumpIndex, DateTime currentTime) {

  String yearlyLogFilename = "/Pump_Logs/pump" + String(pumpIndex + 1) + "_Yearly.txt";
  unsigned long prevYearRuntimeSeconds = 0;

  if (!takeFsMutex(pdMS_TO_TICKS(2000))) {
    LOG_CAT(DBG_PUMP_RUN_TIME_UI, "[FS] prevYear: FS mutex timeout\n");
    return 0;

  }

  if (!LittleFS.exists(yearlyLogFilename)) {
    giveFsMutex();
    return 0;
  }

  File yearlyLogFile = LittleFS.open(yearlyLogFilename, "r");
  if (!yearlyLogFile) {
    LOG_CAT(DBG_PUMP_RUN_TIME_UI, "[FS] Failed to open: %s\n", yearlyLogFilename.c_str());
    giveFsMutex();

    return 0;
  }

  int prevYear = currentTime.year() - 1;
  char prevYearStr[5];
  snprintf(prevYearStr, sizeof(prevYearStr), "%04d", prevYear);

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
  giveFsMutex();
  return prevYearRuntimeSeconds;
}


// Function to aggregate decade logs
unsigned long aggregateDecadeLogsReport(int pumpIndex, DateTime currentTime) {
  (void)currentTime; // not used right now
  unsigned long runtime = 0;

  String filename = "/Pump_Logs/pump" + String(pumpIndex + 1) + "_Yearly.txt";

  // ✅ Never block forever on FS mutex (tgz can hold it for a long time)
    if (!takeFileSystemMutexWithRetry("aggregateDecadeLogsReport",
                                    pdMS_TO_TICKS(200), 10)) {
    LOG_CAT(DBG_PUMPLOG, "[PumpLogs] aggregateDecadeLogsReport: FS busy, skipping\n");
    return 0;
  }


  // Ensure dir exists
  if (!LittleFS.exists("/Pump_Logs")) {
    LittleFS.mkdir("/Pump_Logs");
  }

    if (!LittleFS.exists(filename)) {
    xSemaphoreGive(fileSystemMutex);
    return 0;
  }

  File file = LittleFS.open(filename, "r");
  if (!file || file.size() == 0) {
    if (file) file.close();
    LOG_CAT(DBG_FS, "Invalid or empty file: %s\n", filename.c_str());
    xSemaphoreGive(fileSystemMutex);
    return 0;
  }

  int lineCount = 0;
  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();
    if (line.isEmpty()) continue;

    // ✅ Correct parsing (your old +15 logic made runtimeStartIndex never -1)
    int idx = line.indexOf("Total Runtime: ");
    if (idx >= 0) {
      int runtimeStartIndex = idx + 15;
      int secondsIndex = line.indexOf(" seconds", runtimeStartIndex);
      if (secondsIndex > runtimeStartIndex) {
        String runtimeStr = line.substring(runtimeStartIndex, secondsIndex);
        runtimeStr.trim();
        runtime += runtimeStr.toInt();
      }
    }

    lineCount++;
    if ((lineCount % 200) == 0) {
vTaskDelay(1);
    }
  }

  file.close();
  xSemaphoreGive(fileSystemMutex);
  return runtime;
}




// Setup routes for the server
void setupRoutes() {
      
                server.on("/hello", HTTP_GET, [](AsyncWebServerRequest* req){
          String from = req->hasParam("from") ? req->getParam("from")->value() 
          : "unknown";
                    LOG_CAT(DBG_WEB, "[HTTP hello] from=%s\n", from.c_str());
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
    LOG_CAT(DBG_WEB, "Sending file: %s\n", filePath.c_str());
    request->send(LittleFS, filePath, String(), true);

            } else {
    LOG_CAT(DBG_WEB, "[HTTP] File not found: %s\n", filePath.c_str());
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

    // Serialize ALL HTTP log-data requests too
    if (!takeLogDataMutex(pdMS_TO_TICKS(5000))) {
      request->send(503, "application/json", "{\"error\":\"BUSY\"}");
      return;
    }

    // Always release mutex before exiting this handler
    if (!(request->hasParam("pumpIndex") && request->hasParam("timeframe"))) {
      request->send(400, "application/json", "{\"error\":\"Missing parameters\"}");
      giveLogDataMutex();
      return;
    }

    String pumpIndexParam = request->getParam("pumpIndex")->value();
    String timeframe      = request->getParam("timeframe")->value();
    int pumpIndex         = pumpIndexParam.toInt() - 1;  // 0-based

    unsigned long runtime = 0;
    DateTime currentTime  = getCurrentTimeAtomic();

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
      giveLogDataMutex();
      return;
    }

    DynamicJsonDocument doc(256);
    doc["runtime"] = runtime;

    String response;
    serializeJson(doc, response);

    AsyncWebServerResponse* resp =
      request->beginResponse(200, "application/json; charset=UTF-8", response);
    resp->addHeader("Cache-Control", "no-store");
    request->send(resp);

    giveLogDataMutex();
  });
}


// Respond to 'Update All' request from Webpage
// In WebServerManager.cpp

void refreshRuntimeCache() {
    DateTime currentTime = getCurrentTimeAtomic();

    for (int i = 0; i < numPumps; i++) {
        // This task is WDT-monitored in TaskManager.cpp.
        // Reset between filesystem-heavy aggregation calls so the runtime refresh
        // can safely process larger LittleFS log files without tripping WDT.
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(1));

        cachedRuntimes[i][0] = aggregateDailyLogsReport(i, currentTime);
        esp_task_wdt_reset();

        cachedRuntimes[i][1] = aggregatePreviousDailyLogsReport(i, currentTime);
        esp_task_wdt_reset();

        cachedRuntimes[i][2] = aggregateMonthlyLogsReport(i, currentTime);
        esp_task_wdt_reset();

        cachedRuntimes[i][3] = aggregatePreviousMonthlyLogsReport(i, currentTime);
        esp_task_wdt_reset();

        cachedRuntimes[i][4] = aggregateYearlyLogsReport(i, currentTime);
        esp_task_wdt_reset();

        cachedRuntimes[i][5] = aggregatePreviousYearlyLogsReport(i, currentTime);
        esp_task_wdt_reset();

        cachedRuntimes[i][6] = aggregateDecadeLogsReport(i, currentTime);
        esp_task_wdt_reset();

        vTaskDelay(pdMS_TO_TICKS(5));
    }

    // Clear inactive internal cache slots so pump 9/10 stale data cannot leak
    // into a later JSON response if numPumps is reduced.
    for (int i = numPumps; i < 10; i++) {
        for (int j = 0; j < 7; j++) {
            cachedRuntimes[i][j] = 0;
        }
    }
}
  
        



void updateAllRuntimes() {
    // Capture the version that caused this build before doing the long filesystem work.
    // If another page requests a newer version while this build is running, the task
    // will process that next queued notification afterward.
    uint32_t version = g_pumpRuntimeRequestedVersion;

    refreshRuntimeCache();  // Refresh cache before sending

    esp_task_wdt_reset();

    // [ArduinoJson v7 Fix] 
    // 1. No size argument needed (it grows automatically).
    // 2. We use 'new' to keep the document object off the stack (prevents stack overflow).
    JsonDocument* doc = new JsonDocument();

    if (!doc) {
        LOG_ERR("[PumpRuntimes] Failed to allocate runtime JSON document.\n");
        return;
    }

    (*doc)["version"] = version;
    
    // v7 Syntax: use ["key"].to<JsonArray>() instead of createNestedArray("key")
    JsonArray data = (*doc)["data"].to<JsonArray>();

    for (int i = 0; i < numPumps; i++) {
        esp_task_wdt_reset();

        // v7 Syntax: use add<JsonObject>() instead of createNestedObject()
        JsonObject pumpData = data.add<JsonObject>();
        
        pumpData["pumpIndex"] = i + 1;
        pumpData["day"]       = cachedRuntimes[i][0];
        pumpData["prevDay"]   = cachedRuntimes[i][1];
        pumpData["month"]     = cachedRuntimes[i][2];
        pumpData["prevMonth"] = cachedRuntimes[i][3];
        pumpData["year"]      = cachedRuntimes[i][4];
        pumpData["prevYear"]  = cachedRuntimes[i][5];
        pumpData["total"]     = cachedRuntimes[i][6];

        vTaskDelay(pdMS_TO_TICKS(1));
    }

    String jsonString;
    serializeJson(*doc, jsonString);

    // CRITICAL: Free the heap memory
    delete doc; 

    esp_task_wdt_reset();

    // Store for HTTP fetch clients
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
