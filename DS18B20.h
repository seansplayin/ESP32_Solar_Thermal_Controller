#ifndef DS18B20_H
#define DS18B20_H
#include "Config.h"
#include <DallasTemperature.h>

// Define the number of sensors
#define NUM_SENSORS 13

// Define sensor mapping
struct SensorMapping {
    DeviceAddress address;
    int arrayIndex; // Corresponds to DTempAverage[arrayIndex]
};

extern DallasTemperature sensors1;
extern DallasTemperature sensors2;

extern SensorMapping sensorMappings[NUM_SENSORS];

// Declare global variables to hold temperature readings and averages
extern float DTemp[NUM_SENSORS];        // Holds the most recent temperature readings for 13 sensors

extern float DTempAverage[NUM_SENSORS]; // Holds the rolling average temperature for 13 sensors

// Function declarations
void initDS18B20Sensors();               // Initialize DS18B20 sensors

void updateDS18B20Readings();            // Update readings from DS18B20 sensors

float calculateAverage(float values[], int numReadings);  // Calculate the rolling average for a sensor

void updateDS18B20Temperature(int sensorIndex, float temperature);  // Update temperature and calculate rolling average




#endif
