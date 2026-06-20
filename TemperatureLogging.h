#ifndef TEMPERATURELOGGING_H
#define TEMPERATURELOGGING_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>

// -----------------------------------------------------------------------
// Mutex handles as extern to be accessible in other files - do not change
// -----------------------------------------------------------------------
extern SemaphoreHandle_t fileSystemMutex;

// Set up directories and internal state for temperature logging
void setupTemperatureLogging();

// Long-running FreeRTOS task – call from TaskManager with xTaskCreate()
void TaskTemperatureLogging_Run(void *pvParams);

// Temperature Logging gate - ensures Temperature logging is last to load
void enableTemperatureLogging();

// Request a one-shot flush from the TemperatureLogging task and wait for completion.
bool requestTemperatureLogCacheFlush(TickType_t waitTicks = pdMS_TO_TICKS(7000)); 

#endif // TEMPERATURELOGGING_H
