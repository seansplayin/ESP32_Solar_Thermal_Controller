// DS18B20.h
#ifndef DS18B20_H
#define DS18B20_H

#include "Config.h"
#include <OneWireESP32.h>   // correct header for v2.0.3

#define NUM_SENSORS DS18B20_ASSIGNMENT_COUNT

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
bool ds18B20SensorsReady();
void updateDS18B20Readings();
float calculateAverage(float values[], int numReadings);
void updateDS18B20Temperature(int sensorIndex, float temperature);

// Manual scan support for the DS18B20 assignment webpage.
// Returns JSON containing every valid ROM found on Bus 1 and Bus 2.
// This does not modify the active runtime mapping or saved config.
String buildDS18B20ScanJson();

// Runtime assignment status for the DS18B20 assignment webpage.
// present=true only when the configured ROM was accepted on its configured bus.
// observedBus is the last boot/scan bus where that ROM was actually seen; 0 = not seen.
bool getDS18B20SlotStatus(uint8_t slot, bool* presentOut, uint8_t* observedBusOut, uint32_t* lastGoodMsOut);

#endif