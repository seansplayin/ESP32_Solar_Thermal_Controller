// MemoryStats.h
#pragma once

#include <Arduino.h>

// Live reads
String getHeapInternalString();
String getPsramString();

// Cached strings for UI / websocket use
void updateHeapStatsCache();
void updatePsramStatsCache();

String getCachedHeapInternalString();
String getCachedPsramString();

// PSRAM and HEAP called by TaskEndofBootup
void MemoryStats_printSnapshot(const char* tag);