// DS18B20.h
#ifndef DS18B20_H
#define DS18B20_H

#include "Config.h"
#include <OneWireESP32.h>   // correct header for v2.0.3

#define NUM_SENSORS 13

struct SensorMapping {
    uint64_t address;
    int arrayIndex;
};

extern SensorMapping sensorMappings[NUM_SENSORS];

// Global variables (unchanged API)
extern float DTemp[NUM_SENSORS];
extern float DTempAverage[NUM_SENSORS];
extern uint32_t DTempLastGoodReadMs[NUM_SENSORS];

// Bus objects (correct class)
extern OneWire32 sensors1;
extern OneWire32 sensors2;

// Public functions (exact same signatures as before)
void initDS18B20Sensors();
void updateDS18B20Readings();
float calculateAverage(float values[], int numReadings);
void updateDS18B20Temperature(int sensorIndex, float temperature);

#endif