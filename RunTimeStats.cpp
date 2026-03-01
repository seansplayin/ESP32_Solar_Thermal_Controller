#include <esp_timer.h>
#include "Config.h"
#include "DiagLog.h"


extern "C" void vConfigureTimerForRunTimeStats() {
  // we don’t need to do anything — esp_timer is already running
}

extern "C" uint32_t portGetRunTimeCounterValue() {
  // return microseconds since boot
  return (uint32_t)esp_timer_get_time();
}