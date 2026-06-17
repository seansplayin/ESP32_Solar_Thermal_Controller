#include "TimeSync.h"
#include "Logging.h"
#include "RTCManager.h"
#include "Config.h"
#include "PumpManager.h"
#include <Wire.h>
#include <TimeLib.h>
#include <RTClib.h>
#include "time.h"
#include <Ticker.h>
Ticker ntpRetryTicker;
extern DateTime CurrentTime;
bool needToSyncTime = true; // Initially, we need to synchronize time
bool needNtpSync = true; // Flag to indicate if NTP sync is needed
unsigned long lastNtpUpdateAttempt = 0;
const unsigned long ntpRetryInterval = 600000; // 10 minutes in milliseconds
bool isNtpSyncDue = true;

// Set by web layer when TimeConfig changes; checked in checkAndSyncTime()
static volatile bool g_ntpResyncRequested = false;

void requestImmediateNtpResync() {
    g_ntpResyncRequested = true;
}


void checkAndSyncTime() {
    DateTime now = CurrentTime; // Assume CurrentTime is up to date
    static DateTime lastSyncDate;

    // 1) One-shot resync requested by web UI after TZ change
    if (g_ntpResyncRequested) {
        g_ntpResyncRequested = false;
        Serial.println("[TimeSync] TimeConfig changed, re-running initNTP()");
        initNTP();
        lastSyncDate = now;  // Avoid a duplicate sync at the same moment
        return;              // We already synced this second
    }

    // 2) Daily 3AM maintenance sync (unchanged behavior)
    if (now.hour() == 3 && now.minute() == 0 &&
        (lastSyncDate.day()   != now.day() ||
         lastSyncDate.month() != now.month() ||
         lastSyncDate.year()  != now.year())) {

        Serial.print("3AM, calling initNTP to initiate NTP time sync");
        initNTP();
        lastSyncDate = now;
        //turnOnAllPumpsFor10Minutes();  // your existing test hook
    }
}


// this is called in setup to connect to the NTP server
void initNTP() {
    Serial.print("Starting NTP time sync ");
    // Offset 0 here because we use POSIX TZ rule via setenv("TZ", ...)
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    tryNtpUpdate();
}


void tryNtpUpdate() {
    // Use runtime-configured timezone rule from TimeConfig
    String tzRule = getPosixTimeZoneString();
    setenv("TZ", tzRule.c_str(), 1);
    tzset();

    
    Serial.print("Attempting NTP time sync with TZ rule: ");
    Serial.println(tzRule);
    

    struct tm timeinfo;

    if (getLocalTime(&timeinfo, 10000)) { // Try to get the time with a 10-second timeout
        Serial.print("NTP Time synchronize Successful ");

        Serial.printf("NTP Time: %04d-%02d-%02d %02d:%02d:%02d\n",
                      timeinfo.tm_year + 1900, timeinfo.tm_mon + 1,
                      timeinfo.tm_mday, timeinfo.tm_hour,
                      timeinfo.tm_min, timeinfo.tm_sec);

        // Adjust RTC with NTP time
        rtc.adjust(DateTime(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1,
                            timeinfo.tm_mday, timeinfo.tm_hour,
                            timeinfo.tm_min, timeinfo.tm_sec));
        Serial.print("RTC adjusted to NTP time. ");

        // Update CurrentTime with the new RTC time
        CurrentTime = rtc.now();
        printCurrentRtcTime(); // Display the current RTC time

        ntpRetryTicker.detach(); // Stop retrying since we've successfully synchronized time
    } else {
        Serial.println();
        Serial.print(" NTP sync failed, will retry in 10 minutes... ");
        Serial.println();
        CurrentTime = rtc.now();
        printCurrentRtcTime(); // Display the current RTC time
        ntpRetryTicker.once(600, tryNtpUpdate); // Retry after 10 minutes
    }
}





void printCurrentRtcTime() {
Serial.print(" Current time: ");
Serial.print(CurrentTime.year(), DEC);
Serial.print('/');
Serial.print(CurrentTime.month(), DEC);
Serial.print('/');
Serial.print(CurrentTime.day(), DEC);
Serial.print(" ");
Serial.print(CurrentTime.hour(), DEC);
Serial.print(':');
Serial.print(CurrentTime.minute(), DEC);
Serial.print(':');
Serial.println(CurrentTime.second(), DEC);
}
void initializeTime() {
struct tm timeinfo;
if (!getLocalTime(&timeinfo)) {
Serial.println ();
Serial.print(" Failed to obtain time ");
Serial.println ();
} else {
// Optionally, log or process the obtained time
// Serial.print("Time obtained successfully");
}}
