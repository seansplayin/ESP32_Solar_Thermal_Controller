// TemperatureControl.cpp
#include "TemperatureControl.h"
#include "Logging.h"
#include "WebServerManager.h"
#include "PumpManager.h"
#include "Config.h"
#include "Max31865-PT1000.h"
#include "DS18B20.h"
#include <Ticker.h>
#include <esp_task_wdt.h>
#include "DiagLog.h"



// Mutex for temperature data access
extern SemaphoreHandle_t temperatureMutex;

Ticker temperatureTicker;

// Define the temperature variables
float panelT = 0.0;
float CSupplyT = 0.0;
float storageT = 0.0;
float outsideT = 0.0;
float CircReturnT = 0.0;
float supplyT = 0.0;
float CreturnT = 0.0;
float DhwSupplyT = 0.0;
float DhwReturnT = 0.0;
float HeatingSupplyT = 0.0;
float HeatingReturnT = 0.0;
float dhwT = 0.0;
float PotHeatXinletT = 0.0;
float PotHeatXoutletT = 0.0;

// Previous values for change detection
float prev_pt1000Current = NAN;
float prev_pt1000Average = NAN;
float prev_DTemp[NUM_SENSORS] = {NAN};
float prev_DTempAverage[NUM_SENSORS] = {NAN};

// Previous Temperature Values for Change Detection
float prev_panelT = NAN;
float prev_CSupplyT = NAN;
float prev_storageT = NAN;
float prev_outsideT = NAN;
float prev_CircReturnT = NAN;
float prev_supplyT = NAN;
float prev_CreturnT = NAN;
float prev_DhwSupplyT = NAN;
float prev_DhwReturnT = NAN;
float prev_HeatingSupplyT = NAN;
float prev_HeatingReturnT = NAN;
float prev_dhwT = NAN;
float prev_PotHeatXinletT = NAN;
float prev_PotHeatXoutletT = NAN;

// Function to update temperature readings
void updateTemperatureReadings() {
    vTaskDelay(1);

    panelT          = pt1000Average;      // PT1000
    CSupplyT        = DTempAverage[0];    // DTemp1
    storageT        = DTempAverage[1];    // DTemp2
    outsideT        = DTempAverage[2];    // DTemp3
    supplyT         = DTempAverage[3];    // DTemp4
    CircReturnT     = DTempAverage[4];    // DTemp5
    CreturnT        = DTempAverage[5];    // DTemp6
    DhwSupplyT      = DTempAverage[6];    // DTemp7
    DhwReturnT      = DTempAverage[7];    // DTemp8
    HeatingSupplyT  = DTempAverage[8];    // DTemp9
    HeatingReturnT  = DTempAverage[9];    // DTemp10
    dhwT            = DTempAverage[10];   // DTemp11
    PotHeatXinletT  = DTempAverage[11];   // DTemp12
    PotHeatXoutletT = DTempAverage[12];   // DTemp13
}


// Function to broadcast temperatures with change detection
void broadcastTemperatures() {
    // Acquire the mutex before accessing temperature variables
    if (xSemaphoreTake(temperatureMutex, portMAX_DELAY)) {
        bool hasChanged = false;
        String message = "Temperatures:";
                bool first = true;

        // Compare at the same precision we DISPLAY (2 decimals) to avoid float jitter
        auto fixed2Int = [](float v) -> int32_t {
            if (isnan(v)) return (-2147483647 - 1);  // NaN sentinel
            float scaled = v * 100.0f;
            return (scaled >= 0.0f) ? (int32_t)(scaled + 0.5f) : (int32_t)(scaled - 0.5f);
        };

        auto appendTemperature = [&](const String& name, float current, float& previous) {
            const int32_t cur100  = fixed2Int(current);
            const int32_t prev100 = fixed2Int(previous);

            if (cur100 != prev100) {
                if (!first) {
                    message += ",";
                }
                message += name + ":" + (isnan(current) ? "N/A" : String(current, 2));

                // Store previous at the same 2-decimal precision we display
                previous = isnan(current) ? NAN : ((float)cur100 / 100.0f);

                hasChanged = true;
                first = false;
            }
        };

        // Append existing temperatures

        appendTemperature("panelT", panelT, prev_panelT);
        appendTemperature("supplyT", supplyT, prev_supplyT);
        appendTemperature("CSupplyT", CSupplyT, prev_CSupplyT);
        appendTemperature("storageT", storageT, prev_storageT);
        appendTemperature("outsideT", outsideT, prev_outsideT);
        appendTemperature("CircReturnT", CircReturnT, prev_CircReturnT);
        appendTemperature("CreturnT", CreturnT, prev_CreturnT);
        appendTemperature("DhwSupplyT", DhwSupplyT, prev_DhwSupplyT);
        appendTemperature("DhwReturnT", DhwReturnT, prev_DhwReturnT);
        appendTemperature("HeatingSupplyT", HeatingSupplyT, prev_HeatingSupplyT);
        appendTemperature("HeatingReturnT", HeatingReturnT, prev_HeatingReturnT);
        appendTemperature("dhwT", dhwT, prev_dhwT);
        appendTemperature("PotHeatXinletT", PotHeatXinletT, prev_PotHeatXinletT);
        appendTemperature("PotHeatXoutletT", PotHeatXoutletT, prev_PotHeatXoutletT);

        // Append PT1000 temperatures
        appendTemperature("pt1000Current", pt1000Current, prev_pt1000Current);
        appendTemperature("pt1000Average", pt1000Average, prev_pt1000Average);

        // Append DTemp1 to DTemp13
        for (int i = 0; i < NUM_SENSORS; i++) {
            String tempName = "DTemp" + String(i + 1);
            appendTemperature(tempName, DTemp[i], prev_DTemp[i]);

            tempName = "DTempAverage" + String(i + 1);
            appendTemperature(tempName, DTempAverage[i], prev_DTempAverage[i]);
        }

        if (hasChanged) {
            // Hand the payload to the gatekeeper safely
            if (g_tempWsPayloadMutex != NULL && xSemaphoreTake(g_tempWsPayloadMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                g_tempWsPayload = message;
                xSemaphoreGive(g_tempWsPayloadMutex);
                g_sendTemperatures = true; // Flag the gatekeeper
            }
        }
        xSemaphoreGive(temperatureMutex);
    }
}

// This function gets temperatures and performs computation outside of the Mutex and then only takes Mutex to update Global Variables
void updateTemperatures() {
  // 1) take the lock for _all_ shared‐state work
    if (! xSemaphoreTake(temperatureMutex, portMAX_DELAY)) {
    LOG_ERR("[Temp] Failed to take temperatureMutex\n");
    return;
  }


  // 2) tell the DS18B20s to convert (blocking vs. nonblock flag is in init)
  sensors1.requestTemperatures();
  sensors2.requestTemperatures();

  // 3) read & average the PT1000
  updatePT1000Readings();

  // 4) read & average the DS18B20s
  updateDS18B20Readings();

  // 5) now update any derived/shared variables (e.g. System Temperatures)
  updateTemperatureReadings();

  // 6) release the lock
  xSemaphoreGive(temperatureMutex);
}
