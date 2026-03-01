#pragma once
#include <Arduino.h>

// Returns: "used / total bytes (xx.x% used, minFree N)"
String getHeapInternalString();

// Returns: "used / total bytes (xx.x% used, minFree N)" or "N/A" if no PSRAM
String getPsramString();

// PSRAM and HEAP called by TaskEndofBootup
void MemoryStats_printSnapshot(const char* tag);
