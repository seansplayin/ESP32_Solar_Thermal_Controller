#include "Config.h"
#include "SerialPrint.h"
#include "DS18B20.h"
#include "Max31865-PT1000.h"  // Include for PT1000 temperature readings
#include "PumpManager.h" // serialMessage command for pump states
#include <Arduino.h>
#include "TemperatureControl.h"
// ***** Temperature Variable Assignments *****



void SerialPrint() {
  /*
// Print DS18B20 temperatures
Serial.println("DS18B20 Temperature Readings:");
for (int i = 0; i < 13; i++) {
Serial.print("Sensor ");
Serial.print(i + 1);
Serial.print(": ");
Serial.print(DTemp[i]);
Serial.print(" F (Average: ");
Serial.print(DTempAverage[i]);
Serial.println(" F)");
}
// Print PT1000 temperature
Serial.println("\nPT1000 Temperature Reading:");
Serial.print("Current Temp: ");
Serial.println(pt1000Current);  // Use pt1000Current for the current PT1000 temp
Serial.print("Average Temp: ");
Serial.println(pt1000Average);  // Use pt1000Average for the average PT1000 temp
Serial.println();  // Empty line for clarity
Serial.print("panelT = pt1000Average:"); 
Serial.println(panelT);
Serial.print("CSupplyT = DTempAverage[0]:"); 
Serial.println(CSupplyT);
Serial.print("storageT DTempAverage[1]:"); 
Serial.println(storageT);
Serial.print("outsideT = DTempAverage[2]:"); 
Serial.println(outsideT);
Serial.print("CircReturnT = DTempAverage[3]:"); 
Serial.println(CircReturnT);
Serial.print("supplyT = DTempAverage[4]:"); 
Serial.println(supplyT);  
Serial.print("CreturnT = DTempAverage[5]:"); 
Serial.println(CreturnT);
Serial.print("DhwSupplyT = DTempAverage[6]:"); 
Serial.println(DhwSupplyT);
Serial.print("DhwReturnT = DTempAverage[7]:"); 
Serial.println(DhwReturnT); 
Serial.print("HeatingSupplyT = DTempAverage[8]:"); 
Serial.println(HeatingSupplyT); 
Serial.print("HeatingReturnT = DTempAverage[9]:"); 
Serial.println(HeatingReturnT);  
Serial.print("dhwT = DTempAverage[10]:"); 
Serial.println(dhwT);
Serial.print("PotHeatXinletT = DTempAverage[11]:"); 
Serial.println(PotHeatXinletT);  
Serial.print("PotHeatXoutletT = DTempAverage[12]:"); 
Serial.println(PotHeatXoutletT);  
Serial.println();  // Empty line for clarity
Serial.println("Current Pump Status"); // Print the formatted message to the Serial Monitor
Serial.print(serialMessage);
Serial.println(); // Print a blank line
Serial.println("[DS18B20] Sensor " + String(sensorIndex + 1) + " - Temp: " + String(temperature) + "°F, Avg: " + String(DTempAverage[sensorIndex]) + "°F");
*/
}
