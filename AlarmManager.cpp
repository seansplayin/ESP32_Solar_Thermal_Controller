#include "AlarmManager.h"
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include "freertos/semphr.h"
#include "DiagLog.h"


static SemaphoreHandle_t s_alarmMutex = nullptr;

static const uint16_t ALM_CODE_MAX = 32;
static AlarmState s_states[ALM_CODE_MAX + 1];

static const size_t ALM_EVENT_CAP = 80;
static AlarmEvent s_events[ALM_EVENT_CAP];
static uint16_t s_evtHead = 0;
static bool s_evtWrapped = false;

static AlarmEventSink s_sink = nullptr;

static AlarmStateChangedCb s_stateCb = nullptr;

void AlarmManager_setStateChangedCallback(AlarmStateChangedCb cb) {
  s_stateCb = cb;
}

void AlarmManager_setEventSink(AlarmEventSink sink) {
  s_sink = sink;
}

static uint32_t nowEpoch() {
  time_t t = time(nullptr);
  if (t < 100000) t = 0;
  return (uint32_t)t;
}

static void vfmtToBuf(char* out, size_t outSz, const char* fmt, va_list ap) {
  if (!out || outSz == 0) return;
  out[0] = '\0';
  if (!fmt) return;
  vsnprintf(out, outSz, fmt, ap);
  out[outSz-1] = '\0';
}

static void pushEvent(AlarmCode code, AlarmSeverity sev, AlarmAction action, const char* detail) {
  AlarmEvent &e = s_events[s_evtHead];
  e.epoch  = nowEpoch();
  e.sev    = sev;
  e.code   = code;
  e.action = action;
  strncpy(e.detail, detail ? detail : "", sizeof(e.detail)-1);
  e.detail[sizeof(e.detail)-1] = '\0';

  // Advance ring
  s_evtHead = (s_evtHead + 1) % ALM_EVENT_CAP;
  if (s_evtHead == 0) s_evtWrapped = true;

  // Fire sink (fast + non-blocking: sink must NOT block)
  if (s_sink) s_sink(&e);
}

void AlarmManager_begin() {
  if (!s_alarmMutex) s_alarmMutex = xSemaphoreCreateMutex();
  memset(s_states, 0, sizeof(s_states));
  s_evtHead = 0;
  s_evtWrapped = false;
}

bool AlarmManager_anyActive() {
  if (!s_alarmMutex) return false;
  bool any = false;
  if (xSemaphoreTake(s_alarmMutex, pdMS_TO_TICKS(50))) {
    for (uint16_t c = 1; c <= ALM_CODE_MAX; c++) {
      if (s_states[c].active) { any = true; break; }
    }
    xSemaphoreGive(s_alarmMutex);
  }
  return any;
}

uint32_t AlarmManager_activeCount() {
  if (!s_alarmMutex) return 0;
  uint32_t n = 0;
  if (xSemaphoreTake(s_alarmMutex, pdMS_TO_TICKS(50))) {
    for (uint16_t c = 1; c <= ALM_CODE_MAX; c++) if (s_states[c].active) n++;
    xSemaphoreGive(s_alarmMutex);
  }
  return n;
}

bool AlarmManager_isActive(AlarmCode code) {
  uint16_t c = (uint16_t)code;
  if (c == 0 || c > ALM_CODE_MAX || !s_alarmMutex) return false;
  bool a = false;
  if (xSemaphoreTake(s_alarmMutex, pdMS_TO_TICKS(50))) {
    a = s_states[c].active;
    xSemaphoreGive(s_alarmMutex);
  }
  return a;
}

void AlarmManager_set(AlarmCode code, AlarmSeverity sev, const char* fmt, ...) {
  uint16_t c = (uint16_t)code;
  if (c == 0 || c > ALM_CODE_MAX || !s_alarmMutex) return;

  char detail[140];
    va_list ap; va_start(ap, fmt);
  vfmtToBuf(detail, sizeof(detail), fmt, ap);
  va_end(ap);

  bool changed = false;


    if (xSemaphoreTake(s_alarmMutex, pdMS_TO_TICKS(100))) {
    AlarmState &st = s_states[c];
    const bool wasActive = st.active;

    if (!st.active || st.sev != sev || strncmp(st.detail, detail, sizeof(st.detail)) != 0) {
      if (!st.active) st.sinceEpoch = nowEpoch();

      st.active = true;
      st.sev = sev;
      strncpy(st.detail, detail, sizeof(st.detail)-1);
      st.detail[sizeof(st.detail)-1] = '\0';
      changed = true;
      pushEvent(code, sev, ALM_ACT_SET, st.detail);
    }



    xSemaphoreGive(s_alarmMutex);
  }

  if (changed && s_stateCb) {
    uint32_t n = AlarmManager_activeCount();
    s_stateCb(n);
  }
}

void AlarmManager_clear(AlarmCode code, const char* fmt, ...) {
  uint16_t c = (uint16_t)code;
  if (c == 0 || c > ALM_CODE_MAX || !s_alarmMutex) return;

  char detail[140] = "";
    if (fmt) {
    va_list ap; va_start(ap, fmt);
    vfmtToBuf(detail, sizeof(detail), fmt, ap);
    va_end(ap);
  }

    bool changed = false;

  if (xSemaphoreTake(s_alarmMutex, pdMS_TO_TICKS(100))) {
    AlarmState &st = s_states[c];
    if (st.active) {
      AlarmSeverity sev = st.sev;
      st.active = false;
      changed = true;
      pushEvent(code, sev, ALM_ACT_CLEAR, detail[0] ? detail : st.detail);
    }
    xSemaphoreGive(s_alarmMutex);
  }

  if (changed && s_stateCb) {
    uint32_t n = AlarmManager_activeCount();
    s_stateCb(n);
  }
}



void AlarmManager_event(AlarmCode code, AlarmSeverity sev, const char* fmt, ...) {
  uint16_t c = (uint16_t)code;
  if (c == 0 || c > ALM_CODE_MAX || !s_alarmMutex) return;

  char detail[140];
  va_list ap; va_start(ap, fmt);
  vfmtToBuf(detail, sizeof(detail), fmt, ap);
  va_end(ap);

  if (xSemaphoreTake(s_alarmMutex, pdMS_TO_TICKS(50))) {
    pushEvent(code, sev, ALM_ACT_EVENT, detail);
    xSemaphoreGive(s_alarmMutex);
  }
}

size_t AlarmManager_getRecentEvents(AlarmEvent* out, size_t max) {
  if (!out || max == 0 || !s_alarmMutex) return 0;

  size_t n = 0;
  if (xSemaphoreTake(s_alarmMutex, pdMS_TO_TICKS(100))) {
    uint16_t start = s_evtWrapped ? s_evtHead : 0;
    uint16_t count = s_evtWrapped ? ALM_EVENT_CAP : s_evtHead;

    for (uint16_t i = 0; i < count && n < max; i++) {
      uint16_t idx = (start + i) % ALM_EVENT_CAP;
      out[n++] = s_events[idx];
    }
    xSemaphoreGive(s_alarmMutex);
  }
  return n;
}

size_t AlarmManager_getActiveStates(AlarmCode* codesOut, AlarmState* statesOut, size_t max) {
  if (!codesOut || !statesOut || max == 0 || !s_alarmMutex) return 0;
  size_t n = 0;
  if (xSemaphoreTake(s_alarmMutex, pdMS_TO_TICKS(100))) {
    for (uint16_t c = 1; c <= ALM_CODE_MAX && n < max; c++) {
      if (s_states[c].active) {
        codesOut[n] = (AlarmCode)c;
        statesOut[n] = s_states[c];
        n++;
      }
    }
    xSemaphoreGive(s_alarmMutex);
  }
  return n;
}
