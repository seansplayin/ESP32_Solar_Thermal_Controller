// TimeSync.cpp
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
#include "DiagLog.h"
#include "AlarmManager.h"


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
                LOG_CAT(DBG_TIMESYNC, "[TimeSync] TimeConfig changed, re-running initNTP()\n");
        initNTP();
        lastSyncDate = now;  // Avoid a duplicate sync at the same moment
        return;              // We already synced this second


    // 2) Daily 3AM maintenance sync (unchanged behavior)
    if (now.hour() == 3 && now.minute() == 0 &&
        (lastSyncDate.day()   != now.day() ||
         lastSyncDate.month() != now.month() ||
         lastSyncDate.year()  != now.year())) {

                LOG_CAT(DBG_TIMESYNC, "3AM, calling initNTP to initiate NTP time sync\n");
        initNTP();
        lastSyncDate = now;

        //turnOnAllPumpsFor10Minutes();  // your existing test hook
    }
}
}

// this is called in setup to connect to the NTP server
void initNTP() {
        LOG_CAT(DBG_TIMESYNC, "Starting NTP time sync\n");
    // Offset 0 here because we use POSIX TZ rule via setenv("TZ", ...)
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");

    tryNtpUpdate();
}


void tryNtpUpdate() {
    // Use runtime-configured timezone rule from TimeConfig
    String tzRule = getPosixTimeZoneString();
    setenv("TZ", tzRule.c_str(), 1);
    tzset();

        LOG_CAT(DBG_TIMESYNC, "Attempting NTP time sync with TZ rule: %s\n", tzRule.c_str());


    struct tm timeinfo;

    if (getLocalTime(&timeinfo)) {
        LOG_CAT(DBG_TIMESYNC, "Time synchronized successfully.\n");
        
        // Clear any previous time sync alarms
        AlarmManager_clear(ALM_TIME_SYNC_FAIL, "NTP Sync Restored");

        // Adjust RTC with NTP time
        rtc.adjust(DateTime(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1,
                            timeinfo.tm_mday, timeinfo.tm_hour,
                            timeinfo.tm_min, timeinfo.tm_sec));
        LOG_CAT(DBG_RTC, "RTC adjusted to NTP time.\n");

        // Update CurrentTime with the new RTC time
        CurrentTime = rtc.now();

        printCurrentRtcTime(); 
        ntpRetryTicker.detach(); 
    } else {
        LOG_ERR("[TimeSync] NTP sync failed, will retry in 10 minutes...\n");
        
        // Log the failure to the Alarm Webpage
        AlarmManager_set(ALM_TIME_SYNC_FAIL, ALM_WARN, "NTP Server Unreachable - Retrying");

        CurrentTime = rtc.now();
        printCurrentRtcTime(); 
        ntpRetryTicker.once(600, tryNtpUpdate); 
    }
}





void printCurrentRtcTime() {
LOG_CAT(DBG_RTC, " Current time: %04d/%02d/%02d %02d:%02d:%02d\n",
        CurrentTime.year(), CurrentTime.month(), CurrentTime.day(),
        CurrentTime.hour(), CurrentTime.minute(), CurrentTime.second());
}



void initializeTime() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
LOG_ERR("[TimeSync] Failed to obtain time\n");
} else {

        // Optionally, log or process the obtained time
        // LOG_CAT(DBG_TIMESYNC, "[TimeSync] Time obtained successfully\n");
    }
}
