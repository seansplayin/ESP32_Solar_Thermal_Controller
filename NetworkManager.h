// NetworkManager.h
#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include <Arduino.h>

struct NetworkDiagnosticsSnapshot {
  uint32_t ethStartCount;
  uint32_t ethConnectedCount;
  uint32_t ethGotIpCount;
  uint32_t ethLostIpCount;
  uint32_t ethDisconnectedCount;
  uint32_t ethStoppedCount;
  uint32_t gatewayFailCount;
  uint32_t zombieDetectedCount;
  uint32_t recoveryRequestedCount;
  uint32_t recoveryStartedCount;
  uint32_t recoverySuccessCount;
  uint32_t recoveryFailedCount;
  uint32_t lastEventMs;
  char lastEvent[96];
  char lastIp[24];
};

void setupNetwork();
bool isNetworkConnected();

void getNetworkDiagnosticsSnapshot(NetworkDiagnosticsSnapshot* out);
String getNetworkDiagnosticsJson();

#endif
