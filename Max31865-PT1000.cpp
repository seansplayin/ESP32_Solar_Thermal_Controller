// Max31865-PT1000.cpp
#include "Max31865-PT1000.h"
#include <Adafruit_MAX31865.h>
#include "Config.h"
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "esp_task_wdt.h"
#include "DiagLog.h"
#include "AlarmManager.h"

#define RREF      4300.0
#define RNOMINAL  1000.0

Adafruit_MAX31865 thermo = Adafruit_MAX31865(MAX31865_CS_PIN, MAX31865_DO_PIN, MAX31865_DI_PIN, MAX31865_CLK_PIN);

float pt1000Current = 0.0;
float pt1000Average = 0.0;

#define pt1000NumReadings 3
float pt1000Values[pt1000NumReadings];
int pt1000Index = 0;

void initPT1000Sensor() {
    thermo.begin(MAX31865_4WIRE);

    for (int i = 0; i < pt1000NumReadings; i++) {
        pt1000Values[i] = 32.0f; // Default 0°C (32°F)
    }

    pt1000Current = 32.0f;
    pt1000Average = 32.0f;

    LOG_CAT(DBG_RTD, "[PT1000] MAX31865 init complete (4-wire). Defaults set to 32.0F\n");
}

float calculatePT1000Average(float values[], int numReadings, int currentIndex) {
    float sum = 0.0f;
    int count = 0;

    for (int i = 0; i < numReadings && i <= currentIndex; i++) {
        if (values[i] > -100.0f) {
            sum += values[i];
            count++;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    return (count > 0) ? (sum / (float)count) : -999.0f; // PT1000-specific invalid marker
}

void updatePT1000Readings() {
    // Check for hardware faults first
    uint8_t fault = thermo.readFault();
    if (fault) {
        LOG_ERR("[PT1000] Hardware Fault Detected: 0x%02X\n", fault);
        AlarmManager_set(ALM_PT1000_FAULT, ALM_ALARM, "PT1000 Sensor HW Fault (Code 0x%02X)", fault);
        thermo.clearFault();
        
        // Use a default/error value so the system knows it's invalid
        pt1000Values[pt1000Index] = -999.0f; 
    } else {
        // Clear the alarm if the sensor comes back online
        AlarmManager_clear(ALM_PT1000_FAULT, "PT1000 Online");

        float newF = thermo.temperature(RNOMINAL, RREF) * 1.8f + 32.0f;

    if (newF <= -100.0f || isnan(newF)) {
        // invalid, keep last average
        LOG_ERR("[PT1000] Invalid reading (%.2f). Keeping last average %.2f\n", newF, pt1000Average);
        newF = pt1000Average;
    }

// rolling window
    pt1000Values[pt1000Index] = newF;
    pt1000Current = newF;
    pt1000Index = (pt1000Index + 1) % pt1000NumReadings;
    pt1000Average = calculatePT1000Average(pt1000Values, pt1000NumReadings, pt1000Index);

    LOG_CAT(DBG_RTD, "[PT1000] Current=%.2fF Avg=%.2fF (idx=%d)\n", pt1000Current, pt1000Average, pt1000Index);
}
}

