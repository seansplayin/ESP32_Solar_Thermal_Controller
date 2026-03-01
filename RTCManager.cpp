// RTCManager.cpp
#include "RTCManager.h"
#include "Config.h" // Include this to use rtc, pinSDA, and pinSCL
#include "Logging.h"
#include "WebServerManager.h" // Include this to access ws
#include "TimeSync.h"
#include "PumpManager.h"
#include <RTClib.h>
#include <Wire.h>
#include <Ticker.h>
#include "AlarmManager.h"
#include "DiagLog.h"


// RTC DS3231
RTC_DS3231 rtc;
//const int pinSDA = 20;
//const int pinSCL = 21;
//const int sqwPin = 47; // GPIO pin connected to DS3231 SQW
DateTime CurrentTime; // This holds the current time updated periodically
//Ticker dateTimeTickerObject; // This is the Ticker object


bool g_timeValid = false;
bool g_rtcOk = false;


void markTimeValid()
{
    g_timeValid = true;
}


void setupRTC() {
    Wire.begin(pinSDA, pinSCL); // Initialize I2C

    if (!rtc.begin()) {
        LOG_ERR("[RTC] Couldn't find RTC\n");

        g_rtcOk = false;
        AlarmManager_set(ALM_RTC_MISSING, ALM_ALARM, "RTC not detected");

        // Keep running so web UI + Alarm Log still work
        return;
    }

    g_rtcOk = true;
    AlarmManager_clear(ALM_RTC_MISSING, "RTC detected");
}



DateTime getCurrentTime() {
     return CurrentTime;
}

DateTime getCurrentTimeAtomic() {
     noInterrupts(); // Disable interrupts to prevent concurrent updates
     DateTime now = CurrentTime;
     interrupts();   // Re-enable interrupts
    return now;
}

void printCurrentTime() {
    LOG_CAT(DBG_RTC,
            "[RTC] Current time: %04d/%02d/%02d %02d:%02d:%02d\n",
            CurrentTime.year(), CurrentTime.month(), CurrentTime.day(),
            CurrentTime.hour(), CurrentTime.minute(), CurrentTime.second());
}

void adjustTime(const DateTime& dt) {
    rtc.adjust(dt);
}

String getCurrentDateString() {
    char dateStr[11]; // Buffer for YYYY-MM-DD format
    sprintf(dateStr, "%04d-%02d-%02d", CurrentTime.year(), CurrentTime.month(), CurrentTime.day());
   return String(dateStr);
}

String getRtcTimeString() {
    char buffer[20];
    snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d",
    CurrentTime.year(), CurrentTime.month(), CurrentTime.day(),
    CurrentTime.hour(), CurrentTime.minute(), CurrentTime.second());
    return String(buffer);
}// Function to get the current year as an integer

int getCurrentYear() {
    return CurrentTime.year();
}

String getCurrentDateStringMDY() {
    char dateStr[11]; // Enough to hold MM-DD-YYYY\0
    sprintf(dateStr, "%02d-%02d-%04d", CurrentTime.month(), CurrentTime.day(), CurrentTime.year());
    return String(dateStr);
}


void broadcastDateTime() {
    // Flag the gatekeeper to broadcast the time
    extern volatile bool g_sendDateTime;
    g_sendDateTime = true;
}

void refreshCurrentTime() {
    CurrentTime = rtc.now(); // Refresh the global CurrentTime variable
    broadcastDateTime(); // Formats time and then broadcasts through ws (websocket)
}
