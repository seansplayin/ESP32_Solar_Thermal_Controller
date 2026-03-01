// DS18B20.cpp
#include "DS18B20.h"
#include <OneWire.h>
#include <DallasTemperature.h>
#include "Config.h"
#include <esp_task_wdt.h>
#include "DiagLog.h"
#include "AlarmManager.h"


// Define the number of sensors
#define NUM_SENSORS 13

// OneWire bus pins (adjust these based on your configuration)
OneWire oneWire1(ONE_WIRE_BUS_1);
OneWire oneWire2(ONE_WIRE_BUS_2);

// DallasTemperature instances for each bus
DallasTemperature sensors1(&oneWire1);
DallasTemperature sensors2(&oneWire2);

// Temperature arrays to store readings and averages
float DTemp[NUM_SENSORS] = {0};
float DTempAverage[NUM_SENSORS] = {0.0};

// Sensor offsets (adjust these values for each sensor)
float sensorOffsets[NUM_SENSORS] = {
    0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0
};

// Rolling average settings
const int numReadings = 3;
float DTempValues[NUM_SENSORS][numReadings] = {0};
int readingsIndex[NUM_SENSORS] = {0};

// Define sensor mapping
// Desktop Sensors
SensorMapping sensorMappings[NUM_SENSORS] = {
    { {0x28, 0x5D, 0x4F, 0x57, 0x04, 0xA7, 0x3C, 0xA1}, 0 },  // DSensor1
    { {0x28, 0x8C, 0x5A, 0x27, 0xB2, 0x22, 0x07, 0x77}, 1 },  // DSensor2
    { {0x28, 0x04, 0x35, 0x57, 0x04, 0x69, 0x3C, 0xA1}, 2 },  // DSensor3
    { {0x28, 0xDC, 0xFD, 0x57, 0x04, 0x35, 0x3C, 0xE2}, 3 },  // DSensor4
    { {0x28, 0xE4, 0xFE, 0x57, 0x04, 0xF6, 0x3C, 0x01}, 4 },  // DSensor5
    { {0x28, 0xD0, 0xBB, 0x57, 0x04, 0x91, 0x3C, 0x3F}, 5 },  // DSensor6
    { {0x28, 0xE7, 0x76, 0x57, 0x04, 0xF2, 0x3C, 0x96}, 6 },  // DSensor7
    { {0x28, 0xFB, 0xD1, 0x57, 0x04, 0x33, 0x3C, 0xA2}, 7 },  // DSensor8
    { {0x28, 0x9E, 0xE2, 0x57, 0x04, 0xC8, 0x3C, 0xFA}, 8 },  // DSensor9
    { {0x28, 0xC0, 0x45, 0x57, 0x04, 0x54, 0x3C, 0x2B}, 9 },  // DSensor10
    { {0x28, 0x7C, 0xC9, 0x81, 0xE3, 0x6F, 0x3C, 0x00}, 10 }, // DSensor11
    { {0x28, 0xF1, 0x15, 0x48, 0xF6, 0xCD, 0x3C, 0x75}, 11 }, // DSensor12
    { {0x28, 0xBF, 0x56, 0x48, 0xB2, 0x22, 0x07, 0xE8}, 12 }  // DSensor13
};
/*
// Solar System Sensors
SensorMapping sensorMappings[NUM_SENSORS] = {
    { {0x28, 0x37, 0x16, 0x49, 0xF6, 0x0D, 0x3C, 0x2D}, 0 },
    { {0x28, 0x69, 0x9A, 0x48, 0xF6, 0x7A, 0x3C, 0xAD}, 1 },
    { {0x28, 0x52, 0x16, 0x96, 0xF0, 0x01, 0x3C, 0x02}, 2 },
    { {0x28, 0x85, 0xEA, 0x81, 0xE3, 0x2B, 0x3C, 0xF2}, 3 },
    { {0x28, 0xF6, 0xA9, 0x0E, 0xB2, 0x22, 0x07, 0x2B}, 4 },
    { {0x28, 0x2A, 0x84, 0x96, 0xF0, 0x01, 0x3C, 0x0F}, 5 },
    { {0x28, 0x29, 0x1A, 0x81, 0xE3, 0x53, 0x3C, 0xB5}, 6 },
    { {0x28, 0x22, 0xC6, 0x81, 0xE3, 0x17, 0x3C, 0x92}, 7 },
    { {0x28, 0x66, 0xF1, 0x81, 0xE3, 0xD2, 0x3C, 0xA5}, 8 },
    { {0x28, 0x22, 0x14, 0x81, 0xE3, 0xDC, 0x3C, 0xD8}, 9 },
    { {0x28, 0x03, 0xBD, 0x08, 0xB2, 0x22, 0x09, 0xC7}, 10 },
    { {0x28, 0xDD, 0x9B, 0x96, 0xF0, 0x01, 0x3C, 0xEB}, 11 },
    { {0x28, 0x37, 0xE2, 0x48, 0xF6, 0xE6, 0x3C, 0x4D}, 12 },
};
*/

static bool onBus1[NUM_SENSORS];

// Helper function to check if a sensor is on a given bus
bool isSensorOnBus(const DeviceAddress& addr, DallasTemperature& busSensors) {
    DeviceAddress foundAddress;
    for (int i = 0; i < busSensors.getDeviceCount(); i++) {
        if (busSensors.getAddress(foundAddress, i)) {
            if (memcmp(addr, foundAddress, 8) == 0) {
                return true;
            }
        }
    }
    return false;
}

void initDS18B20Sensors() {
  sensors1.setWaitForConversion(false);
  sensors2.setWaitForConversion(false);
  sensors1.begin();
  sensors2.begin();

  // set all resolutions to 10-bit
  for (int i = 0; i < NUM_SENSORS; i++) {
    if (onBus1[i] = sensors1.setResolution(sensorMappings[i].address, 10)) {
      // no print
    } else {
      sensors2.setResolution(sensorMappings[i].address, 10);
    }
  }

  // build our one-time bus lookup
  for (int i = 0; i < NUM_SENSORS; i++) {
    onBus1[i] = isSensorOnBus(sensorMappings[i].address, sensors1);
  }

  LOG_CAT(DBG_1WIRE, "[DS18B20] Init complete: %d sensors mapped\n", NUM_SENSORS);
}

// Calculate rolling average for a sensor
float calculateAverage(float values[], int numReadings) {
    float sum = 0;
    int count = 0;
    for (int i = 0; i < numReadings; i++) {
        if (values[i] > -100) {  // Exclude invalid readings
            sum += values[i];
            count++;
        }
    }
    return count > 0 ? sum / count : -196.60;  // Return average or invalid if no valid readings
}

// Read DS18B20 temperatures and apply offsets, update globals
void updateDS18B20Readings() {
  sensors1.requestTemperatures();
  sensors2.requestTemperatures();
  vTaskDelay(pdMS_TO_TICKS(94));    // ~10-bit conversion time
  esp_task_wdt_reset();

  for (int i = 0; i < NUM_SENSORS; i++) {
    // pick the right bus once (onBus1[i] was set in init)
    float rawF = onBus1[i]
      ? sensors1.getTempF(sensorMappings[i].address)
      : sensors2.getTempF(sensorMappings[i].address);

    if (rawF == DEVICE_DISCONNECTED_F) {
      LOG_ERR("[DS18B20] Sensor %d disconnected\n", i + 1);
      
      // Fire an event to the Alarm Log so you have a timestamp of exactly when it died
      AlarmManager_event(ALM_SENSOR_FAULT, ALM_WARN, "DS18B20 Sensor %d Offline", i + 1);
      continue;
    }

    // apply any per-sensor offset
    rawF += sensorOffsets[sensorMappings[i].arrayIndex];

    // now push it into your rolling buffer & compute the new average
    updateDS18B20Temperature(sensorMappings[i].arrayIndex, rawF);
  }
}

// Update temperature readings and calculate rolling average
void updateDS18B20Temperature(int sensorIndex, float temperature) {
    // Check for invalid reading
    if (temperature == DEVICE_DISCONNECTED_F) {
        LOG_ERR("[DS18B20] Invalid temperature reading for Sensor %d\n", sensorIndex + 1);
        // Optionally, retain the last valid temperature or set to a default value
        return;
    }

    // Store the reading in the rolling array
    DTempValues[sensorIndex][readingsIndex[sensorIndex]] = temperature;
    readingsIndex[sensorIndex] = (readingsIndex[sensorIndex] + 1) % numReadings;

    // Update the current temperature and calculate the average
    DTemp[sensorIndex] = temperature;
    DTempAverage[sensorIndex] = calculateAverage(DTempValues[sensorIndex], numReadings);

    // LOG_CAT(DBG_1WIRE, "[DS18B20] Sensor %d - Temp: %.2fF, Avg: %.2fF\n",
    //         sensorIndex + 1, temperature, DTempAverage[sensorIndex]);
}
