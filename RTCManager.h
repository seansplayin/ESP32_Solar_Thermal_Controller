#ifndef RTCMANAGER_H
#define RTCMANAGER_H
#include "Config.h"
#include <RTClib.h>

extern DateTime CurrentTime; // Declaration for global access
void setupRTC();
DateTime getCurrentTime();
DateTime getCurrentTimeAtomic();
void printCurrentTime();
void adjustTime(const DateTime& dt);
String getCurrentDateString();
String getRtcTimeString();
int getCurrentYear();
String getCurrentDateStringMDY();
void refreshCurrentTime();
extern RTC_DS3231 rtc;
extern bool g_rtcOk;
void dateTimeTicker();
void broadcastCurrentTime();

extern bool g_timeValid;
void markTimeValid();


#endif // RTCMANAGER_H
