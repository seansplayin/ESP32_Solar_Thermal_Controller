#ifndef PUMPMANAGER_H
#define PUMPMANAGER_H
#include "Config.h"
#include <Arduino.h>

const int NUM_PUMPS = 10;

extern const char* pumpNames[NUM_PUMPS];

void broadcastPumpState(int pumpIndex = -1);
void setPumpState(int pumpIndex, int state);
void togglePumpState(int pumpIndex);
void applyPumpMode(int pumpIndex);
void setPumpMode(int pumpIndex, int mode);
void PumpControl();
void initializePumps();
void setupPumpBroadcasting();
void turnOnAllPumpsFor10Minutes(); // Turn on all pumps for 10 minutes
extern bool pumpOnStateHigh[10];
extern bool pumpOffStateHigh[10];
extern const int numPumps;
#define PUMP_ON 1
#define PUMP_OFF 0
#define PUMP_AUTO 2
extern int pumpModes[10];
extern int pumpStates[10];
extern const int pumpPins[10];
void PrintPumpStates();
extern String serialMessage;

extern bool previousDHWCallStatus;
extern bool previousHeatingCallStatus;
void sendHeatingCallStatus(bool dhwCallActive, bool heatingCallActive);

#endif // PUMPMANAGER_H
