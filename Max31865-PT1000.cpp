#include "Max31865-PT1000.h"
#include <Adafruit_MAX31865.h>
#include "Config.h"
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "esp_task_wdt.h"

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
        pt1000Values[i] = 32.0; // Default 0°C (32°F)
    }
    pt1000Current = 32.0;
    pt1000Average = 32.0;
}

float calculatePT1000Average(float values[], int numReadings, int currentIndex) {
    float sum = 0;
    int count = 0;
    for (int i = 0; i < numReadings && i <= currentIndex; i++) {
        if (values[i] > -100.0) {
            sum += values[i];
            count++;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    return count > 0 ? sum / count : -999.0; // PT1000-specific invalid marker
}

void updatePT1000Readings() {
    float newF = thermo.temperature(RNOMINAL, RREF) * 1.8 + 32;

    if (newF <= -100.0 || isnan(newF)) {
        // invalid, keep last average
        newF = pt1000Average;
    }

    // rolling window
    pt1000Values[pt1000Index] = newF;
    pt1000Current = newF;
    pt1000Index = (pt1000Index + 1) % pt1000NumReadings;
    pt1000Average = calculatePT1000Average(pt1000Values, pt1000NumReadings, pt1000Index);
}
