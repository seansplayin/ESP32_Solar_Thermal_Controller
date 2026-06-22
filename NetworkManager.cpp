// NetworkManager.cpp
#include "NetworkManager.h"
#include "Config.h"
#include "Logging.h"
#include <SPI.h>
#include <ETH.h>
#include <Network.h>
#include <esp_task_wdt.h>
#include <esp_netif.h>
#include <esp_log.h>
#include <driver/gpio.h>
#include "DiagLog.h"
#include <string.h>
#include "AlarmManager.h"
#include "WebServerManager.h"
#include <esp_heap_caps.h>

// PHY configuration for W5500
#define ETH_PHY_TYPE ETH_PHY_W5500
#define ETH_PHY_ADDR 1
#define ETH_PHY_CS   W5500_SS
#define ETH_PHY_IRQ  W5500_INT
#define ETH_PHY_RST  W5500_RST

#ifndef ETH_SPI_FREQ_MHZ
#define ETH_SPI_FREQ_MHZ 10 // at 15 mhz 92 network disconnects in 20 hours runtime. at 10 mhz 47 disconnects in 24 hours.
#endif

#ifndef CONFIG_ASYNC_TCP_RUNNING_CORE
#define CONFIG_ASYNC_TCP_RUNNING_CORE -1
#endif
#ifndef CONFIG_ASYNC_TCP_PRIORITY
#define CONFIG_ASYNC_TCP_PRIORITY 10
#endif
#ifndef CONFIG_ASYNC_TCP_QUEUE_SIZE
#define CONFIG_ASYNC_TCP_QUEUE_SIZE 64
#endif
#ifndef CONFIG_ASYNC_TCP_STACK_SIZE
#define CONFIG_ASYNC_TCP_STACK_SIZE 8192
#endif
#ifndef CONFIG_ASYNC_TCP_MAX_ACK_TIME
#define CONFIG_ASYNC_TCP_MAX_ACK_TIME 5000
#endif


// --- Static IP Configuration ---
const bool USE_STATIC_IP = false;          // Set to false to use dynamic DHCP or true to use static

IPAddress staticIP(10, 20, 90, 41);
IPAddress staticGateway(10, 20, 90, 1);   // Update if your router is not .1
IPAddress staticSubnet(255, 255, 255, 0);
IPAddress staticDns1(8, 8, 8, 8);         // Primary DNS (Google)
IPAddress staticDns2(8, 8, 4, 4);         // Secondary DNS

// EXPLICITLY CLAIM FSPI TO LOCK W5500
SPIClass spiW5500(FSPI);

static volatile bool eth_connected = false;
static volatile bool eth_link_up   = false;

#include <NetworkClient.h>

// --- Watchdog Configuration ---
// Set to false if connecting directly to a PC without a router
const bool ENABLE_NETWORK_WATCHDOG = true; 

// ---------------- Network recovery state ----------------
static TaskHandle_t s_netRecoverTask = nullptr;
static volatile bool s_netRecoverPending = false;
static volatile bool s_ethRestartInProgress = false;

// Declare all state variables BEFORE the Watchdog tries to use them
static volatile uint32_t s_lastLinkDownMs = 0;
static volatile uint32_t s_lastLinkUpMs   = 0;
static volatile uint32_t s_lastRecoverMs  = 0;
static volatile uint32_t s_lastGotIpMs    = 0;

static portMUX_TYPE s_netDiagMux = portMUX_INITIALIZER_UNLOCKED;
static NetworkDiagnosticsSnapshot s_netDiag = {};

static void logNetworkCoreTrace(const char* label) {
  LOG_ERR("[NETCORE] %s core=%u heap8=%u min8=%u\n",
          label,
          (unsigned)xPortGetCoreID(),
          (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
          (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT));
}

static void recordNetworkEvent(const char* eventText) {
  uint32_t nowMs = millis();
  portENTER_CRITICAL(&s_netDiagMux);
  s_netDiag.lastEventMs = nowMs;
  strncpy(s_netDiag.lastEvent, eventText ? eventText : "", sizeof(s_netDiag.lastEvent) - 1);
  s_netDiag.lastEvent[sizeof(s_netDiag.lastEvent) - 1] = '\0';
  portEXIT_CRITICAL(&s_netDiagMux);
}

static void recordNetworkIp(const IPAddress& ip) {
  String ipText = ip.toString();
  portENTER_CRITICAL(&s_netDiagMux);
  strncpy(s_netDiag.lastIp, ipText.c_str(), sizeof(s_netDiag.lastIp) - 1);
  s_netDiag.lastIp[sizeof(s_netDiag.lastIp) - 1] = '\0';
  portEXIT_CRITICAL(&s_netDiagMux);
}

static void incrementNetworkCounter(uint32_t* counter) {
  portENTER_CRITICAL(&s_netDiagMux);
  (*counter)++;
  portEXIT_CRITICAL(&s_netDiagMux);
}

void getNetworkDiagnosticsSnapshot(NetworkDiagnosticsSnapshot* out) {
  if (!out) return;
  portENTER_CRITICAL(&s_netDiagMux);
  *out = s_netDiag;
  portEXIT_CRITICAL(&s_netDiagMux);
}

String getNetworkDiagnosticsJson() {
  NetworkDiagnosticsSnapshot snap;
  getNetworkDiagnosticsSnapshot(&snap);

  String json = "{";
  json += "\"ethStartCount\":" + String(snap.ethStartCount);
  json += ",\"ethConnectedCount\":" + String(snap.ethConnectedCount);
  json += ",\"ethGotIpCount\":" + String(snap.ethGotIpCount);
  json += ",\"ethLostIpCount\":" + String(snap.ethLostIpCount);
  json += ",\"ethDisconnectedCount\":" + String(snap.ethDisconnectedCount);
  json += ",\"ethStoppedCount\":" + String(snap.ethStoppedCount);
  json += ",\"gatewayFailCount\":" + String(snap.gatewayFailCount);
  json += ",\"zombieDetectedCount\":" + String(snap.zombieDetectedCount);
  json += ",\"recoveryRequestedCount\":" + String(snap.recoveryRequestedCount);
  json += ",\"recoveryStartedCount\":" + String(snap.recoveryStartedCount);
  json += ",\"recoverySuccessCount\":" + String(snap.recoverySuccessCount);
  json += ",\"recoveryFailedCount\":" + String(snap.recoveryFailedCount);
  json += ",\"lastEventMs\":" + String(snap.lastEventMs);
  json += ",\"lastEvent\":\"";
  json += snap.lastEvent;
  json += "\"";
  json += ",\"lastIp\":\"";
  json += snap.lastIp;
  json += "\"";
  json += "}";
  return json;
}

static const uint32_t NET_DOWN_DEBOUNCE_MS    = 8000; // 8000. After Watchdog spots a crash wait this long to ensure the line is dead before executing the hard reset.
static const uint32_t NET_RECOVER_COOLDOWN_MS = 30000; // 30000. prevents the ESP32 from getting trapped in an infinite loop of restarting the W5500 if the hardware permanently dies.
static const uint32_t NET_RECOVER_WAIT_IP_MS  = 30000; // 30000. maximum time the controller will wait for your router to hand out a DHCP address before giving up.

static void requestNetworkRecovery();
static void NetworkRecoveryTask(void* pvParameters);

// ---------------- Active Socket Watchdog ----------------
void TaskNetworkWatchdog(void *pvParameters) {
    int failedAttempts = 0;
    logNetworkCoreTrace("NetWatchdog task started");
    for(;;) {
        // Ping every 15 seconds instead of 30
        vTaskDelay(pdMS_TO_TICKS(15000)); 

        if (!ENABLE_NETWORK_WATCHDOG) continue;

        // Only test if the hardware link claims to be connected
        if (eth_connected && ETH.localIP() != IPAddress(0,0,0,0) && !s_ethRestartInProgress) {
            NetworkClient client;
            client.setTimeout(3); 
            
            // Dynamically fetch the active Gateway IP (works for both Static and DHCP)
            IPAddress activeGateway = ETH.gatewayIP();

            // Attempt a lightweight TCP connection to the active Gateway on port 53 (DNS) or 80 (HTTP)
            if (activeGateway != IPAddress(0,0,0,0) && 
               (client.connect(activeGateway, 53) || client.connect(activeGateway, 80))) {
                client.stop();
                failedAttempts = 0; // Network is healthy
            } else {
                failedAttempts++;
                incrementNetworkCounter(&s_netDiag.gatewayFailCount);
                recordNetworkEvent("Gateway test failed");
                LOG_ERR("[Watchdog] Gateway test to %s failed! Attempt %d/2\n", activeGateway.toString().c_str(), failedAttempts);
                AlarmManager_event(ALM_NETWORK_FAULT, ALM_INFO,
                                   "Gateway test failed: %s attempt %d/2",
                                   activeGateway.toString().c_str(),
                                   failedAttempts);

                // Trigger recovery after 2 failures (30 seconds total)
                if (failedAttempts >= 2) {
                    incrementNetworkCounter(&s_netDiag.zombieDetectedCount);
                    recordNetworkEvent("ZOMBIE network detected");
                    LOG_ERR("[Watchdog] 2 consecutive failures. ZOMBIE network detected. Forcing recovery.\n");
                    AlarmManager_event(ALM_NETWORK_FAULT, ALM_WARN,
                                       "ZOMBIE network detected after gateway failures");
                    failedAttempts = 0;
                    
                    // CRITICAL FIX: Actively tear down the network state
                    eth_connected = false;
                    eth_link_up = false; // Force link down so recovery loop skips the IP wait period
                    
                    // Manually start the debounce timer
                    s_lastLinkDownMs = millis(); 
                    s_netRecoverPending = true; 
                    
                    // CRITICAL FIX: Wake up the sleeping FreeRTOS recovery task!
                    requestNetworkRecovery();
                }
            }
        } else {
            failedAttempts = 0; 
        }
    }
}



static void hardResetW5500() {
  // --- THE HARDWARE RESET HAMMER ---
  // Bypasses the ESP-IDF driver's ETH_PHY_RST macro to guarantee the exact 
  // physical pin is triggered, using the global W5500_RST defined in Config.h
  pinMode(W5500_RST, OUTPUT);
  digitalWrite(W5500_RST, LOW);                // Pull reset pin LOW to kill the chip
  vTaskDelay(pdMS_TO_TICKS(15));               // Hold it dead for 15ms
  digitalWrite(W5500_RST, HIGH);               // Bring it back to life
  vTaskDelay(pdMS_TO_TICKS(150));              // Give the chip 150ms to fully boot before talking to it over SPI
}


static void closeWebSocketsBeforeEthRestart() {
  // AsyncTCP / ESPAsyncWebServer can still have queued client objects after the
  // W5500 link drops. Close those clients from the recovery task before ETH.end()
  // frees the Ethernet stack underneath them.
  logNetworkCoreTrace("Closing WS clients before ETH.end");
  ws.closeAll();
  vTaskDelay(pdMS_TO_TICKS(300));
  ws.cleanupClients();
  vTaskDelay(pdMS_TO_TICKS(200));
}

static void requestNetworkRecovery() {
  if (s_ethRestartInProgress) {
    return;
  }

  s_netRecoverPending = true;
  incrementNetworkCounter(&s_netDiag.recoveryRequestedCount);
  recordNetworkEvent("Network recovery requested");

  if (s_netRecoverTask != nullptr) {
    xTaskNotifyGive(s_netRecoverTask);
  }
}

static void NetworkRecoveryTask(void* pvParameters) {
  (void)pvParameters;
  logNetworkCoreTrace("NetRecover task started");

  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    if (!s_netRecoverPending) continue;

    while (s_netRecoverPending) {
      uint32_t now = millis();

      if (eth_connected) {
        s_netRecoverPending = false;
        break;
      }

      if (eth_link_up) {
        uint32_t upFor = now - s_lastLinkUpMs;
        if (upFor < NET_RECOVER_WAIT_IP_MS) {
          vTaskDelay(pdMS_TO_TICKS(250));
          continue;
        }
      }

      uint32_t downFor = now - s_lastLinkDownMs;
      if (downFor < NET_DOWN_DEBOUNCE_MS) {
        vTaskDelay(pdMS_TO_TICKS(250));
        continue;
      }

      uint32_t sinceRecover = now - s_lastRecoverMs;
      if (sinceRecover < NET_RECOVER_COOLDOWN_MS) {
        vTaskDelay(pdMS_TO_TICKS(500));
        continue;
      }

      s_netRecoverPending = false;
      s_lastRecoverMs = now;
      s_ethRestartInProgress = true;

      incrementNetworkCounter(&s_netDiag.recoveryStartedCount);
      recordNetworkEvent("Debounced recovery started");
      LOG_ERR("[Network] Debounced recovery: restarting W5500 / ETH stack\n");
      logNetworkCoreTrace("Recovery starting");
      AlarmManager_event(ALM_NETWORK_FAULT, ALM_WARN,
                         "Debounced recovery: restarting W5500 / ETH stack");
      eth_link_up = false;
      eth_connected = false;

      uint32_t heapBefore = heap_caps_get_free_size(MALLOC_CAP_8BIT);
      uint32_t heapMinBefore = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
      bool heapOkBefore = heap_caps_check_integrity_all(false);
      LOG_ERR("[Network] Heap before WS close/ETH.end: ok=%d free8=%u min8=%u\n",
              heapOkBefore ? 1 : 0, (unsigned)heapBefore, (unsigned)heapMinBefore);

      closeWebSocketsBeforeEthRestart();
      bool heapOkAfterWsClose = heap_caps_check_integrity_all(false);
      LOG_ERR("[Network] Heap after WS close: ok=%d\n", heapOkAfterWsClose ? 1 : 0);

      LOG_ERR("[Network] Calling ETH.end() from NetRecover core=%u\n", (unsigned)xPortGetCoreID());
      ETH.end();
      vTaskDelay(pdMS_TO_TICKS(500));
      bool heapOkAfterEthEnd = heap_caps_check_integrity_all(false);
      LOG_ERR("[Network] Heap after ETH.end(): ok=%d\n", heapOkAfterEthEnd ? 1 : 0);

      spiW5500.end(); // USED LOCKED SPI
      vTaskDelay(pdMS_TO_TICKS(50));

      hardResetW5500();
      // gpio_uninstall_isr_service(); // unsafe, removed system interrupt and can cause inter-processor communication core (ipc0) crashes if executed at the wrong time.
      // SURGICAL FIX: Remove ONLY the W5500's specific interrupt handler.
      // This frees the stuck adapter without destroying the global ISR 
      // allocator that the temperature sensors rely on.
      gpio_isr_handler_remove((gpio_num_t)ETH_PHY_IRQ); // Safer than "gpio_uninstall_isr_service();" targeted specifically at W5500 Interrupt 

      vTaskDelay(pdMS_TO_TICKS(20));

      spiW5500.begin(W5500_SCK, W5500_MISO, W5500_MOSI, W5500_SS);

      // Apply static IP configuration before starting Ethernet, if enabled
      if (USE_STATIC_IP) {
          ETH.config(staticIP, staticGateway, staticSubnet, staticDns1, staticDns2);
      }

      LOG_ERR("[Network] Calling ETH.begin() from NetRecover core=%u\n", (unsigned)xPortGetCoreID());
      bool ethStarted = ETH.begin(
        ETH_PHY_TYPE,
        ETH_PHY_ADDR,
        ETH_PHY_CS,
        ETH_PHY_IRQ,
        ETH_PHY_RST,
        spiW5500, // USED LOCKED SPI
        ETH_SPI_FREQ_MHZ
      );
      bool heapOkAfterEthBegin = heap_caps_check_integrity_all(false);
      LOG_ERR("[Network] Heap after ETH.begin(): ok=%d free8=%u\n",
              heapOkAfterEthBegin ? 1 : 0,
              (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT));

      if (!ethStarted) {
        s_ethRestartInProgress = false;
        incrementNetworkCounter(&s_netDiag.recoveryFailedCount);
        recordNetworkEvent("Recovery ETH.begin failed");
        LOG_ERR("[Network] Recovery ETH.begin failed\n");
        AlarmManager_event(ALM_NETWORK_FAULT, ALM_WARN, "Ethernet recovery ETH.begin failed");
        break;
      }

      uint32_t waitStart = millis();
      while (!eth_connected && (millis() - waitStart < NET_RECOVER_WAIT_IP_MS)) {
        vTaskDelay(pdMS_TO_TICKS(250));
      }

      if (eth_connected && ETH.localIP() != IPAddress(0, 0, 0, 0)) {
        incrementNetworkCounter(&s_netDiag.recoverySuccessCount);
        recordNetworkEvent("Recovery successful");
        recordNetworkIp(ETH.localIP());
        LOG_CAT(DBG_NET, "[Network] Recovery successful. IP: %s\n",
                ETH.localIP().toString().c_str());
        AlarmManager_event(ALM_NETWORK_FAULT, ALM_INFO,
                           "Ethernet recovery successful: %s",
                           ETH.localIP().toString().c_str());
      } else {
        incrementNetworkCounter(&s_netDiag.recoveryFailedCount);
        recordNetworkEvent("Recovery timed out waiting for IP");
        LOG_ERR("[Network] Recovery timed out waiting for IP\n");
        AlarmManager_event(ALM_NETWORK_FAULT, ALM_WARN, "Ethernet recovery timed out waiting for IP");
      }

      s_ethRestartInProgress = false;
      break;
    }
  }
}

void onEvent(arduino_event_id_t event, arduino_event_info_t info) {
  (void)info;

  switch (event) {
    case ARDUINO_EVENT_ETH_START:
      incrementNetworkCounter(&s_netDiag.ethStartCount);
      recordNetworkEvent("Ethernet Started");
      LOG_CAT(DBG_NET, "[Network] Ethernet Started core=%u\n", (unsigned)xPortGetCoreID());
      ETH.setHostname("esp32s3-solar");
      break;

        case ARDUINO_EVENT_ETH_CONNECTED:
      incrementNetworkCounter(&s_netDiag.ethConnectedCount);
      recordNetworkEvent("Ethernet Connected");
      LOG_CAT(DBG_NET, "[Network] Ethernet Connected core=%u\n", (unsigned)xPortGetCoreID());
      eth_link_up = true;
      s_lastLinkUpMs = millis();
      break;

    case ARDUINO_EVENT_ETH_GOT_IP:
      incrementNetworkCounter(&s_netDiag.ethGotIpCount);
      recordNetworkEvent("Ethernet Got IP");
      recordNetworkIp(ETH.localIP());
      LOG_CAT(DBG_NET, "[Network] Ethernet Got IP: %s core=%u\n", ETH.localIP().toString().c_str(), (unsigned)xPortGetCoreID());
      eth_link_up = true;
      eth_connected = true;
      s_lastLinkUpMs = millis();
      s_lastGotIpMs = millis();
      s_netRecoverPending = false;
      break;

    case ARDUINO_EVENT_ETH_LOST_IP:
      incrementNetworkCounter(&s_netDiag.ethLostIpCount);
      recordNetworkEvent("Ethernet Lost IP");
      LOG_ERR("[Network] Ethernet Lost IP core=%u\n", (unsigned)xPortGetCoreID());
      AlarmManager_event(ALM_NETWORK_FAULT, ALM_WARN, "Ethernet Lost IP");
      // IMPORTANT:
      // Keep link_up true here. LOST_IP is not the same as PHY/link-down.
      // Start a grace period for DHCP/IP recovery before allowing a hard restart.
      eth_connected = false;
      s_lastLinkUpMs = millis();
      if (!s_ethRestartInProgress) {
        requestNetworkRecovery();
      }
      break;

    case ARDUINO_EVENT_ETH_DISCONNECTED:
      incrementNetworkCounter(&s_netDiag.ethDisconnectedCount);
      recordNetworkEvent("Ethernet Disconnected");
      LOG_ERR("[Network] Ethernet Disconnected core=%u\n", (unsigned)xPortGetCoreID());
      AlarmManager_event(ALM_NETWORK_FAULT, ALM_WARN, "Ethernet Disconnected");
      eth_link_up = false;
      eth_connected = false;
      s_lastLinkDownMs = millis();
      if (!s_ethRestartInProgress) {
        requestNetworkRecovery();
      }
      break;

    case ARDUINO_EVENT_ETH_STOP:
      incrementNetworkCounter(&s_netDiag.ethStoppedCount);
      recordNetworkEvent("Ethernet Stopped");
      LOG_ERR("[Network] Ethernet Stopped core=%u\n", (unsigned)xPortGetCoreID());
      AlarmManager_event(ALM_NETWORK_FAULT, ALM_WARN, "Ethernet Stopped");
      eth_link_up = false;
      eth_connected = false;
      s_lastLinkDownMs = millis();
      if (!s_ethRestartInProgress) {
        requestNetworkRecovery();
      }
      break;

    default:
      break;
  }
}

void setupNetwork() {
  logNetworkCoreTrace("setupNetwork entered");
  LOG_ERR("[Network] AsyncTCP build config: core=%d priority=%d queue=%d stack=%d ack=%d\n",
          (int)CONFIG_ASYNC_TCP_RUNNING_CORE,
          (int)CONFIG_ASYNC_TCP_PRIORITY,
          (int)CONFIG_ASYNC_TCP_QUEUE_SIZE,
          (int)CONFIG_ASYNC_TCP_STACK_SIZE,
          (int)CONFIG_ASYNC_TCP_MAX_ACK_TIME);

  // Natively mute the noisy W5500 and ETH drivers at the ESP-IDF core level 
  // instead of using a stack-crushing custom vprintf interceptor.
  esp_log_level_set("w5500.mac", ESP_LOG_NONE);
  esp_log_level_set("w5500", ESP_LOG_NONE);
  esp_log_level_set("esp_eth.netif.netif_glue", ESP_LOG_NONE);
  esp_log_level_set("esp_eth", ESP_LOG_NONE);

  if (s_netRecoverTask == nullptr) {
    xTaskCreatePinnedToCore(
      NetworkRecoveryTask,
      "NetRecover",
      8192,
      nullptr,
      3,
      &s_netRecoverTask,
      1
    );
  }

  vTaskDelay(pdMS_TO_TICKS(500));

  // Start explicitly named FSPI BEFORE reset so bus is valid
  spiW5500.begin(W5500_SCK, W5500_MISO, W5500_MOSI, W5500_SS);

  hardResetW5500();

  vTaskDelay(pdMS_TO_TICKS(200)); 
  
  // REMOVED: Manual pinMode for W5500_INT. The esp_eth driver MUST own this pin.

  Network.onEvent(onEvent);
  
  // Spawn the Active Socket Watchdog 
  xTaskCreatePinnedToCore(TaskNetworkWatchdog, "NetWatchdog", 4096, NULL, 2, NULL, 1);

  LOG_CAT(DBG_NET, "[Network] Attempting initial setup...\n");

  // Apply static IP configuration before starting Ethernet, if enabled
  if (USE_STATIC_IP) {
      ETH.config(staticIP, staticGateway, staticSubnet, staticDns1, staticDns2);
  }

  LOG_ERR("[Network] Calling initial ETH.begin() from core=%u\n", (unsigned)xPortGetCoreID());
  bool ethStarted = ETH.begin(
    ETH_PHY_TYPE,
    ETH_PHY_ADDR,
    ETH_PHY_CS,
    ETH_PHY_IRQ,
    ETH_PHY_RST,
    spiW5500, // USED LOCKED SPI
    ETH_SPI_FREQ_MHZ
  );

  if (!ethStarted) {
    LOG_ERR("[Network] ETH.begin failed!\n");
    AlarmManager_event(ALM_NETWORK_FAULT, ALM_WARN, "Initial ETH.begin failed");
    return;
  }

  unsigned long startTime = millis();
  const unsigned long timeout = 15000;
  while (!eth_connected && (millis() - startTime < timeout)) {
    vTaskDelay(pdMS_TO_TICKS(200));
  }

  if (eth_connected && ETH.localIP() != IPAddress(0, 0, 0, 0)) {
    LOG_CAT(DBG_NET, "[Network] Setup successful. IP: %s\n", ETH.localIP().toString().c_str());
  } else {
    LOG_ERR("[Network] Initial IP assignment timeout (Will continue in background).\n");

    // A/B:
    // If the link never came up at all (for example cable unplugged),
    // do not immediately hammer the W5500 with restart cycles.
    // Let the controller continue in degraded mode.
    if (eth_link_up) {
      s_lastLinkDownMs = millis();
      requestNetworkRecovery();
    }
  }
}

bool isNetworkConnected() {
  return eth_link_up && eth_connected && ETH.localIP() != IPAddress(0, 0, 0, 0);
}