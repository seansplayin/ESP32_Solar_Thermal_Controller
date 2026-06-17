#ifndef TEMPERATURECONTROL_H
#define TEMPERATURECONTROL_H
#include "Config.h"
#include "DS18B20.h"
#include "Max31865-PT1000.h"

// Existing temperature variables
extern float panelT;           
extern float CSupplyT;         
extern float storageT;         
extern float outsideT;         
extern float CircReturnT;      
extern float supplyT;          
extern float CreturnT;         
extern float DhwSupplyT;       
extern float DhwReturnT;       
extern float HeatingSupplyT;   
extern float HeatingReturnT;   
extern float dhwT;             
extern float PotHeatXinletT;   
extern float PotHeatXoutletT;  

// **New temperature variables**
extern float pt1000Current;
extern float pt1000Average;
extern float DTemp[NUM_SENSORS];        // DTemp1 to DTemp13
extern float DTempAverage[NUM_SENSORS]; // DTempAverage1 to DTempAverage13

// Previous values for change detection
extern float prev_pt1000Current;
extern float prev_pt1000Average;
extern float prev_DTemp[NUM_SENSORS];
extern float prev_DTempAverage[NUM_SENSORS];

// Declare functions
void AssignTemperatureProbes();
void broadcastTemperatures();
void updateTemperature(int pumpIndex, float newTemperature);
void updateTemperatures();
void updateTemperatureReadings();

#endif // TEMPERATURECONTROL_H
