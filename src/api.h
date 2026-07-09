#ifndef API_H
#define API_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include "website.h"
#include "display.h"
#include <ArduinoJson.h>

typedef enum {
    API_IDLE,
    API_TRANSMITTING
} filamanApiStateType;

typedef enum {
    API_REQUEST_REGISTER,
    API_REQUEST_HEARTBEAT,
    API_REQUEST_SYNC_BAMBUDDY // Bambuddy: Umbenannt von API_REQUEST_WEIGHT, Rest entfernt
} FilamanApiRequestType;

extern volatile filamanApiStateType filamanApiState;
extern bool filamanConnected;

// State for the new assignment workflow
extern bool isAssignmentPending;
extern String pendingTagForAssignment;
extern float pendingWeightForAssignment;


// API functions (angepasst für Bambuddy Integration)
bool initFilaman();
bool registerDevice(const String& deviceCode); // Dummy für Kompatibilität
void sendHeartbeatAsync();
void syncBambuddySpoolAsync(String tagUuid, float weight, bool isBambuTag);

// Internal blocking functions (used by async task)
bool sendHeartbeat(); // Dummy
bool syncBambuddySpool(String tagUuid, float weight); // Bambuddy Weiche
String getUntaggedSpools();
bool linkTag(int spoolId, const String& tagUid);
void clearPendingAssignment();

// Helper functions
void saveFilamanConfig();
void loadFilamanConfig();
bool checkFilamanRegistration();

#endif
