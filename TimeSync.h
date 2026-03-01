#ifndef TIMESYNC_H
#define TIMESYNC_H
#include "Config.h"
void setupTimeSync();
void initNTP();
void tryNtpUpdate();
void requestImmediateNtpResync(); 
void printCurrentRtcTime();
void initializeTime();
void checkAndSyncTime();
extern bool needToSyncTime;
extern bool needNtpSync;
extern unsigned long lastNtpUpdateAttempt;
extern const unsigned long ntpRetryInterval;
extern bool isNtpSyncDue;
#endif
