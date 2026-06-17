// AlarmHistory.h
#pragma once
#include <Arduino.h>
#include <stddef.h>  // for size_t
#include <stdint.h>
#include "AlarmManager.h"

// Call early (ok before FS)
void AlarmHistory_begin();

// Call once LittleFS is mounted (best place: TaskInitFileSystem)
void AlarmHistory_onFileSystemReady();

// Build grouped JSON into a Print (no giant String allocations)
void AlarmHistory_writeJson(Print& out);

// Delete selected IDs (persistent)
bool AlarmHistory_deleteIds(const uint32_t* ids, size_t n);

// Clear all history
bool AlarmHistory_clearAll();