#ifndef MAX31865_PT1000_H
#define MAX31865_PT1000_H
#include "Config.h"
void initPT1000Sensor();
void updatePT1000Readings();
// Declare the current and average temperature variables as extern
extern float pt1000Current;
extern float pt1000Average;
#endif
