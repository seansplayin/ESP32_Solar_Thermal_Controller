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

// PHY configuration for W5500
#define ETH_PHY_TYPE ETH_PHY_W5500
#define ETH_PHY_ADDR 1
#define ETH_PHY_CS   W5500_SS
#define ETH_PHY_IRQ  W5500_INT
#define ETH_PHY_RST  W5500_RST

#ifndef ETH_SPI_FREQ_MHZ
#define ETH_SPI_FREQ_MHZ 15
#endif

// EXPLICITLY CLAIM FSPI TO LOCK W5500
SPIClass spiW5500(FSPI);

static volatile bool eth_connected = false;
static volatile bool eth_link_up   = false;

// ---------------- Network recovery state ----------------
static TaskHandle_t s_netRecoverTask = nullptr;
static volatile bool s_netRecoverPending = false;
static volatile bool s_ethRestartInProgress = false;
static volatile uint32_t s_lastLinkDownMs = 0;
static volatile uint32_t s_lastLinkUpMs   = 0;
static volatile uint32_t s_lastRecoverMs  = 0;
static volatile uint32_t s_lastGotIpMs    = 0;

static const uint32_t NET_DOWN_DEBOUNCE_MS    = 8000; //8000
static const uint32_t NET_RECOVER_COOLDOWN_MS = 30000; //30000
static const uint32_t NET_RECOVER_WAIT_IP_MS  = 30000; //30000 A/B: give link/DHCP more time before hard restart

static void requestNetworkRecovery();
static void NetworkRecoveryTask(void* pvParameters);

// =========================================================================
// --- Custom Rate-Limited Log Interceptor (Smart Cache) ---
// =========================================================================
static vprintf_like_t s_old_vprintf = nullptr;

struct W5500LogCache {
    size_t msgLen;
    uint32_t lastPrintMs;
};
static W5500LogCache s_w5500Cache[4] = { {0,0}, {0,0}, {0,0}, {0,0} };

int w5500_rate_limited_vprintf(const char *fmt, va_list ap) {
    char buf[256];
    
    va_list ap_copy;
    va_copy(ap_copy, ap);
    vsnprintf(buf, sizeof(buf), fmt, ap_copy);
    va_end(ap_copy);

    const char* marker = strstr(buf, "w5500.mac");
    if (marker != nullptr) {
        uint32_t now = millis();
        size_t currentLen = strlen(marker); 
        
        int foundIdx = -1;
        int oldestIdx = 0;
        uint32_t oldestTime = now;

        for (int i = 0; i < 4; i++) {
            if (s_w5500Cache[i].msgLen == currentLen) {
                foundIdx = i;
                break;
            }
            if (s_w5500Cache[i].lastPrintMs < oldestTime) {
                oldestTime = s_w5500Cache[i].lastPrintMs;
                oldestIdx = i;
            }
        }

        if (foundIdx != -1) {
            if (now - s_w5500Cache[foundIdx].lastPrintMs > 10000) {
                s_w5500Cache[foundIdx].lastPrintMs = now;
                return s_old_vprintf(fmt, ap); 
            }
            return 0; 
        } else {
            s_w5500Cache[oldestIdx].msgLen = currentLen;
            s_w5500Cache[oldestIdx].lastPrintMs = now;
            return s_old_vprintf(fmt, ap); 
        }
    }
    return s_old_vprintf(fmt, ap);
}
// =========================================================================

static void hardResetW5500() {
  pinMode(ETH_PHY_RST, OUTPUT);
  digitalWrite(ETH_PHY_RST, LOW);
  vTaskDelay(pdMS_TO_TICKS(100));
  digitalWrite(ETH_PHY_RST, HIGH);
  vTaskDelay(pdMS_TO_TICKS(250));
}

static void requestNetworkRecovery() {
  if (s_ethRestartInProgress) {
    return;
  }

  s_netRecoverPending = true;

  if (s_netRecoverTask != nullptr) {
    xTaskNotifyGive(s_netRecoverTask);
  }
}

static void NetworkRecoveryTask(void* pvParameters) {
  (void)pvParameters;

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

      LOG_ERR("[Network] Debounced recovery: restarting W5500 / ETH stack\n");
      eth_link_up = false;
      eth_connected = false;

      ETH.end();
      vTaskDelay(pdMS_TO_TICKS(300));

      spiW5500.end(); // USED LOCKED SPI
      vTaskDelay(pdMS_TO_TICKS(50));

      hardResetW5500();
      gpio_uninstall_isr_service();

      vTaskDelay(pdMS_TO_TICKS(20));

      spiW5500.begin(W5500_SCK, W5500_MISO, W5500_MOSI, W5500_SS);

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
        s_ethRestartInProgress = false;
        LOG_ERR("[Network] Recovery ETH.begin failed\n");
        AlarmManager_event(ALM_NETWORK_FAULT, ALM_WARN, "Ethernet recovery ETH.begin failed");
        break;
      }

      uint32_t waitStart = millis();
      while (!eth_connected && (millis() - waitStart < NET_RECOVER_WAIT_IP_MS)) {
        vTaskDelay(pdMS_TO_TICKS(250));
      }

      if (eth_connected && ETH.localIP() != IPAddress(0, 0, 0, 0)) {
        LOG_CAT(DBG_NET, "[Network] Recovery successful. IP: %s\n",
                ETH.localIP().toString().c_str());
      } else {
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
      LOG_CAT(DBG_NET, "[Network] Ethernet Started\n");
      ETH.setHostname("esp32s3-solar");
      break;

        case ARDUINO_EVENT_ETH_CONNECTED:
      LOG_CAT(DBG_NET, "[Network] Ethernet Connected\n");
      eth_link_up = true;
      s_lastLinkUpMs = millis();
      break;

    case ARDUINO_EVENT_ETH_GOT_IP:
      LOG_CAT(DBG_NET, "[Network] Ethernet Got IP: %s\n", ETH.localIP().toString().c_str());
      eth_link_up = true;
      eth_connected = true;
      s_lastLinkUpMs = millis();
      s_lastGotIpMs = millis();
      s_netRecoverPending = false;
      break;

    case ARDUINO_EVENT_ETH_LOST_IP:
      LOG_ERR("[Network] Ethernet Lost IP\n");
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
      LOG_ERR("[Network] Ethernet Disconnected\n");
      eth_link_up = false;
      eth_connected = false;
      s_lastLinkDownMs = millis();
      if (!s_ethRestartInProgress) {
        requestNetworkRecovery();
      }
      break;

    case ARDUINO_EVENT_ETH_STOP:
      LOG_ERR("[Network] Ethernet Stopped\n");
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
  esp_log_level_set("w5500.mac", ESP_LOG_ERROR);
  esp_log_level_set("w5500", ESP_LOG_ERROR);

  if (s_old_vprintf == nullptr) {
    s_old_vprintf = esp_log_set_vprintf(w5500_rate_limited_vprintf);
  }

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

  LOG_CAT(DBG_NET, "[Network] Attempting initial setup...\n");

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