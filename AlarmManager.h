// AlarmManager.h
#ifndef ALARMMANAGER_H
#define ALARMMANAGER_H

#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>

// Severity
enum AlarmSeverity {
  ALM_INFO  = 0,
  ALM_WARN  = 1,
  ALM_ALARM = 2
};

// Action (what happened)
enum AlarmAction {
  ALM_ACT_SET   = 0,  // state became active
  ALM_ACT_CLEAR = 1,  // state became inactive
  ALM_ACT_EVENT = 2   // one-off event (no state)
};

// Codes (keep <= 32 unless you expand ALM_CODE_MAX in AlarmManager.cpp)
enum AlarmCode {
  ALM_NONE = 0,

  // Hardware / time
  ALM_RTC_MISSING        = 1,
  ALM_TIME_SYNC_FAIL     = 2,

  // Health / resources
  ALM_HEAP_HIGH          = 3,
  ALM_FS_HIGH            = 4,
  ALM_STACK_HIGH         = 5,
  ALM_STORAGE_OVERTEMP   = 6,

  // Outside Freeze Protection
  ALM_COLLECTOR_FREEZE_PROTECT,   
  ALM_LINE_FREEZE_PROTECT,        

  // Network & Connectivity Events
  ALM_NETWORK_FAULT,              
  ALM_WS_DISCONNECT,

  // Sensor & Storage Hardware Faults
  ALM_FS_WRITE_FAIL,
  ALM_SENSOR_FAULT,
  ALM_PT1000_FAULT
};

// Active state
struct AlarmState {
  bool active = false;
  AlarmSeverity sev = ALM_INFO;
  uint32_t sinceEpoch = 0;
  char detail[140] = {0};
};

// History event
struct AlarmEvent {
  uint32_t epoch = 0;
  AlarmSeverity sev = ALM_INFO;
  AlarmCode code = ALM_NONE;
  AlarmAction action = ALM_ACT_EVENT;
  char detail[140] = {0};
};

// Callbacks
typedef void (*AlarmEventSink)(const AlarmEvent* e);
typedef void (*AlarmStateChangedCb)(uint32_t activeCount);

// API
void     AlarmManager_begin();

void     AlarmManager_setEventSink(AlarmEventSink sink);
void     AlarmManager_setStateChangedCallback(AlarmStateChangedCb cb);

bool     AlarmManager_anyActive();
uint32_t AlarmManager_activeCount();
bool     AlarmManager_isActive(AlarmCode code);

void     AlarmManager_set(AlarmCode code, AlarmSeverity sev, const char* fmt, ...);
void     AlarmManager_clear(AlarmCode code, const char* fmt = nullptr, ...);
void     AlarmManager_event(AlarmCode code, AlarmSeverity sev, const char* fmt, ...);

size_t   AlarmManager_getRecentEvents(AlarmEvent* out, size_t max);
size_t   AlarmManager_getActiveStates(AlarmCode* codesOut, AlarmState* statesOut, size_t max);

#endif // ALARMMANAGER_H