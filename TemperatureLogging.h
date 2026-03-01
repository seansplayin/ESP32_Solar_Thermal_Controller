#ifndef TEMPERATURELOGGING_H
#define TEMPERATURELOGGING_H

#include <Arduino.h>

// Set up directories and internal state for temperature logging
void setupTemperatureLogging();

// Long-running FreeRTOS task – call from TaskManager with xTaskCreate()
void TaskTemperatureLogging_Run(void *pvParams);

// Temperature Logging gate - ensures Temperature logging is last to load
void enableTemperatureLogging(); 

#endif // TEMPERATURELOGGING_H
