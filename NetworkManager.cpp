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

// 20MHz ensures stable SPI over jumper wires without truncating frames
#ifndef ETH_SPI_FREQ_MHZ
#define ETH_SPI_FREQ_MHZ 20
#endif

static volatile bool eth_connected = false;

// =========================================================================
// --- Custom Rate-Limited Log Interceptor (Smart Cache) ---
// Prevents high-frequency SPI corruption events from overwhelming the UART
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

void onEvent(arduino_event_id_t event, arduino_event_info_t info) {
  switch (event) {
    case ARDUINO_EVENT_ETH_START:
      LOG_CAT(DBG_NET, "[Network] Ethernet Started\n");
      ETH.setHostname("esp32s3-solar");
      break;

    case ARDUINO_EVENT_ETH_CONNECTED:
      LOG_CAT(DBG_NET, "[Network] Ethernet Connected\n");
      break;

    case ARDUINO_EVENT_ETH_GOT_IP:
      LOG_CAT(DBG_NET, "[Network] Ethernet Got IP: %s\n", ETH.localIP().toString().c_str());
      eth_connected = true;
      break;

    case ARDUINO_EVENT_ETH_LOST_IP:
    case ARDUINO_EVENT_ETH_DISCONNECTED:
      LOG_ERR("[Network] Ethernet Disconnected/Lost IP\n");
      eth_connected = false;
      AlarmManager_event(ALM_NETWORK_FAULT, ALM_WARN, "Ethernet Hardware Link Dropped");
      break;

    case ARDUINO_EVENT_ETH_STOP:
      LOG_ERR("[Network] Ethernet Stopped\n");
      eth_connected = false;
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

  // We only reset the W5500 ONCE at boot, before the driver takes over.
  hardResetW5500();
  Network.onEvent(onEvent);
  
  LOG_CAT(DBG_NET, "[Network] Attempting initial setup...\n");
  
  pinMode(W5500_INT, INPUT_PULLUP);
  SPI.begin(W5500_SCK, W5500_MISO, W5500_MOSI, W5500_SS);
  
  bool ethStarted = ETH.begin(ETH_PHY_TYPE, ETH_PHY_ADDR, ETH_PHY_CS, ETH_PHY_IRQ, ETH_PHY_RST, SPI, ETH_SPI_FREQ_MHZ);

  if (!ethStarted) {
    LOG_ERR("[Network] ETH.begin failed!\n");
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
  }
}

bool isNetworkConnected() {
  return eth_connected && ETH.localIP() != IPAddress(0, 0, 0, 0);
}