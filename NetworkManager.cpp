#include "NetworkManager.h"
#include "Config.h"
#include "Logging.h"
#include <Ticker.h>
#include <SPI.h>
#include <ETH.h>
#include <Network.h>
#include <esp_task_wdt.h>
#include <esp_netif.h>

// Ethernet settings for W5500
//byte mac[] = { 0xFE, 0xED, 0xDE, 0xAD, 0xBE, 0xEF };// 10.20.90.14
uint8_t customMAC[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };// 10.20.90.33
IPAddress ip(10, 20, 90, 33); // Use DHCP
// Optional static IP (uncomment to use)
// IPAddress localIP(10, 20, 90, 33);
// IPAddress gateway(10, 20, 90, 1);
// IPAddress subnet(255, 255, 255, 0);
// IPAddress dns(8, 8, 8, 8);

// PHY configuration for W5500
#define ETH_PHY_TYPE ETH_PHY_W5500
#define ETH_PHY_ADDR 1
#define ETH_PHY_CS   W5500_SS  // GPIO 10
#define ETH_PHY_IRQ  W5500_INT // GPIO 4
#define ETH_PHY_RST  -1        // No reset pin
#define ETH_SPI_FREQ_MHZ 20    // W5500 SPI frequency (20 MHz)

Ticker networkRetryTicker;
static volatile bool eth_connected = false;

// Forward declaration
void retryNetworkSetup();

// Ethernet event handler
void onEvent(arduino_event_id_t event, arduino_event_info_t info) {
  switch (event) {
    case ARDUINO_EVENT_ETH_START:
      Serial.println(F("[Network] Ethernet Started"));
      ETH.setHostname("esp32s3-solar");
      break;
    case ARDUINO_EVENT_ETH_CONNECTED:
      Serial.println(F("[Network] Ethernet Connected"));
      break;
    case ARDUINO_EVENT_ETH_GOT_IP: {
      Serial.printf("[Network] Ethernet Got IP: '%s'\n", esp_netif_get_desc(info.got_ip.esp_netif));
      Serial.print(F("IP Address: "));
      Serial.println(ETH.localIP());
      eth_connected = true;
      networkRetryTicker.detach(); // Stop retries
      // Verify MAC address
      String currentMac = ETH.macAddress();
      Serial.print(F("[Network] MAC Address: "));
      Serial.println(currentMac);
      break;
    }
    case ARDUINO_EVENT_ETH_LOST_IP:
      Serial.println(F("[Network] Ethernet Lost IP"));
      eth_connected = false;
      networkRetryTicker.once(30, retryNetworkSetup); // Retry in 30s
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
      Serial.println(F("[Network] Ethernet Disconnected"));
      eth_connected = false;
      networkRetryTicker.once(30, retryNetworkSetup); // Retry in 30s
      break;
    case ARDUINO_EVENT_ETH_STOP:
      Serial.println(F("[Network] Ethernet Stopped"));
      eth_connected = false;
      break;
    default:
      break;
  }
  vTaskDelay(pdMS_TO_TICKS(10)); // Yield to tasks like TaskPumpControl
}

// Attempt network setup with WDT resets
void attemptNetworkSetup() {
  esp_task_wdt_reset(); // Reset WDT

  // Initialize SPI for W5500 (SPI2)
  SPI.begin(W5500_SCK, W5500_MISO, W5500_MOSI, W5500_SS);

  // Configure static IP (uncomment for static IP)
 //  ETH.config(10.20.9.33, 10.20.90.1, 8.8.8.8, 10.20.90.1);
// Set MAC address before ETH.begin()
 // ETH.setMac(customMAC);

  // Start Ethernet with interrupt (based on working example)
  bool ethStarted = ETH.begin(ETH_PHY_TYPE, ETH_PHY_ADDR, ETH_PHY_CS, ETH_PHY_IRQ, ETH_PHY_RST, SPI, ETH_SPI_FREQ_MHZ);
  if (!ethStarted) {
    Serial.println(F("[Network] ETH Begin failed!"));
    networkRetryTicker.once(30, retryNetworkSetup); // Retry in 30s
    return;
  }

  // Wait for IP assignment
  unsigned long startTime = millis();
  const unsigned long timeout = 10000; // 10s timeout
  while (!eth_connected && (millis() - startTime < timeout)) {
    vTaskDelay(pdMS_TO_TICKS(200)); // Yield to other tasks
    esp_task_wdt_reset(); // Reset WDT
  }

  if (eth_connected && ETH.localIP() != IPAddress(0, 0, 0, 0)) {
    Serial.print(F("[Network] Setup successful. IP: "));
    Serial.println(ETH.localIP());
  } else {
    Serial.println(F("[Network] Setup failed. Retrying in 30 seconds..."));
    networkRetryTicker.once(30, retryNetworkSetup);
  }
}

// Retry network setup
void retryNetworkSetup() {
  Serial.println(F("[Network] Retrying setup..."));
  attemptNetworkSetup();
}

// Initialize Ethernet connection
void setupNetwork() {
  Network.onEvent(onEvent);
  Serial.println(F("[Network] Attempting initial setup..."));
  attemptNetworkSetup();
}

// Check Ethernet status for WebServerManager
bool isNetworkConnected() {
  return eth_connected && ETH.localIP() != IPAddress(0, 0, 0, 0);
}