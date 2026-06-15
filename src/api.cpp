#include "api.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "commonFS.h"
#include <Preferences.h>
#include "debug.h"
#include "scale.h"
#include "nfc.h"
#include "config.h"
#include <WiFi.h>
#include "display.h"
#include "lang.h"

volatile filamanApiStateType filamanApiState = API_IDLE;
bool filamanConnected = false;

struct ApiRequest {
    FilamanApiRequestType type;
    int id1;
    int id2;
    String str1;
    String str2;
    float val;
    bool bool1; // success
    String str3; // error message
    float remainingWeight; // remaining weight from rfid-result
    bool active = false;
};

#define MAX_API_QUEUE 10
ApiRequest apiQueue[MAX_API_QUEUE];
SemaphoreHandle_t queueMutex;

void saveFilamanConfig() {
    Preferences preferences;
    preferences.begin(NVS_NAMESPACE_API, false);
    preferences.putString(NVS_KEY_FILAMAN_URL, filamanUrl);
    preferences.putString(NVS_KEY_FILAMAN_TOKEN, filamanToken);
    preferences.putBool(NVS_KEY_FILAMAN_REGISTERED, filamanRegistered);
    preferences.end();
}

void loadFilamanConfig() {
    Preferences preferences;
    preferences.begin(NVS_NAMESPACE_API, true);
    filamanUrl = preferences.getString(NVS_KEY_FILAMAN_URL, "");
    filamanToken = preferences.getString(NVS_KEY_FILAMAN_TOKEN, "");
    filamanRegistered = preferences.getBool(NVS_KEY_FILAMAN_REGISTERED, false);
    preferences.end();
}

bool checkFilamanRegistration() {
    // Bambuddy: Benötigt URL und API-Key (in filamanToken gespeichert) im NVS.
    return filamanUrl.length() > 0 && filamanToken.length() > 0;
}

bool registerDevice(const String& deviceCode) {
    // Bambuddy: Speichere den via Webinterface übergebenen API-Key
    filamanToken = deviceCode;
    filamanRegistered = true;
    saveFilamanConfig();
    return true;
}

bool sendHeartbeat() {
    // Bambuddy: Dummy-Funktion. Wir brauchen keinen ständigen Device-Status.
    filamanConnected = true;
    return true;
}

// Heartbeat mit Retry-Logik für mehr Stabilität
bool sendHeartbeatWithRetry(int maxRetries = 2) {
    return true;
}

// Bambuddy: Zentrale Logik-Weiche für Workflow 1 & 2
bool syncBambuddySpool(String tagUuid, float measuredWeight) {
    Serial.printf("syncBambuddy: starte API-Abfrage - tagUuid=%s, weight=%.1f\n", tagUuid.c_str(), measuredWeight);
    if (!checkFilamanRegistration() || WiFi.status() != WL_CONNECTED) {
        Serial.println("ERROR: Keine URL konfiguriert oder WiFi nicht verbunden");
        return false;
    }

    HTTPClient http;
    http.setTimeout(10000);
    
    // =========================================================
    // 1. GET Request: Prüfen, ob Spule in Bambuddy existiert
    // =========================================================
    String getUrl = filamanUrl + "/api/v1/inventory/spools?tag_uid=" + tagUuid;
    http.begin(getUrl);
    
    http.addHeader("Authorization", "Bearer " + filamanToken);
    http.addHeader("X-Api-Key", filamanToken);
    
    int httpCode = http.GET();
    String response = http.getString();
    http.end();

    int foundSpoolId = -1;

    if (httpCode == 200) {
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, response);
        if (!error) {
            // Bambuddy API JSON parsen (flexibel für Objekt oder Array)
            if (doc.is<JsonArray>() && doc.size() > 0) {
                foundSpoolId = doc[0]["id"] | doc[0]["spool_id"] | -1;
            } else if (doc.containsKey("data") && doc["data"].is<JsonArray>() && doc["data"].size() > 0) {
                foundSpoolId = doc["data"][0]["id"] | doc["data"][0]["spool_id"] | -1;
            } else if (doc.containsKey("id")) {
                foundSpoolId = doc["id"] | -1;
            } else if (doc.containsKey("spool_id")) {
                foundSpoolId = doc["spool_id"] | -1;
            }
        }
    }

    http.setReuse(true);

    // =========================================================
    // 2. Weiche: Workflow 1 (Update) oder Workflow 2 (Neu anlegen)
    // =========================================================
    if (foundSpoolId > 0) {
        // WORKFLOW 1: Bekannte Spule -> Gewicht updaten
        Serial.printf("syncBambuddy: Spule gefunden (ID: %d). Sende Update...\n", foundSpoolId);
        http.begin(filamanUrl + "/api/v1/spoolbuddy/scale/update-spool-weight");
        http.addHeader("Content-Type", "application/json");
        http.addHeader("Authorization", "Bearer " + filamanToken);
        http.addHeader("X-Api-Key", filamanToken);

        JsonDocument postDoc;
        postDoc["spool_id"] = foundSpoolId;
        postDoc["weight"] = measuredWeight;
        String payload;
        serializeJson(postDoc, payload);

        int postCode = http.POST(payload);
        http.end();
        
        if (postCode == 200 || postCode == 201 || postCode == 204) {
            oledShowRemainingWeight((int)measuredWeight);
            oledSetPriority(DISPLAY_PRIORITY_ACTION, 3000);
            vTaskDelay(pdMS_TO_TICKS(3000));
            oledClearPriority();
            return true;
        }
    } else {
        // WORKFLOW 2: Unbekannte Spule -> Neu anlegen (Auto-gen)
        Serial.println("syncBambuddy: Spule unbekannt. Erstelle neue Auto-gen Spule...");
        http.begin(filamanUrl + "/api/v1/inventory/spools");
        http.addHeader("Content-Type", "application/json");
        http.addHeader("Authorization", "Bearer " + filamanToken);
        http.addHeader("X-Api-Key", filamanToken);

        JsonDocument postDoc;
        postDoc["tag_uid"] = tagUuid;
        postDoc["material"] = "Auto-gen";
        postDoc["weight"] = measuredWeight; 
        
        String payload;
        serializeJson(postDoc, payload);

        int postCode = http.POST(payload);
        http.end();
        
        if (postCode == 200 || postCode == 201) {
            oledShowProgressBar(4, 4, tr(STR_SPOOL_TAG), "Neu angelegt!");
            oledSetPriority(DISPLAY_PRIORITY_ACTION, 3000);
            vTaskDelay(pdMS_TO_TICKS(3000));
            oledClearPriority();
            return true;
        }
    }

    // Fehlerfall
    oledShowProgressBar(1, 1, tr(STR_FAILURE), tr(STR_API_ERROR));
    oledSetPriority(DISPLAY_PRIORITY_WARNING, 2000);
    vTaskDelay(pdMS_TO_TICKS(2000));
    oledClearPriority();
    return false;
}

void filamanApiTask(void* pvParameters) {
    for (;;) {
        ApiRequest req;
        bool hasReq = false;

        if (xSemaphoreTake(queueMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            for(int i=0; i<MAX_API_QUEUE; i++) {
                if(apiQueue[i].active) {
                    req = apiQueue[i];
                    apiQueue[i].active = false;
                    hasReq = true;
                    break;
                }
            }
            xSemaphoreGive(queueMutex);
        }

        if (hasReq) {
            filamanApiState = API_TRANSMITTING;
            switch (req.type) {
                case API_REQUEST_HEARTBEAT: sendHeartbeatWithRetry(2); break;
                case API_REQUEST_SYNC_BAMBUDDY: syncBambuddySpool(req.str1, req.val); break;
                default: break;
            }
            filamanApiState = API_IDLE;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void sendHeartbeatAsync() {
    if (!checkFilamanRegistration()) return;
    if (xSemaphoreTake(queueMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        // Prevent duplicate heartbeats
        for(int i=0; i<MAX_API_QUEUE; i++) if(apiQueue[i].active && apiQueue[i].type == API_REQUEST_HEARTBEAT) {
            xSemaphoreGive(queueMutex);
            return;
        }
        for(int i=0; i<MAX_API_QUEUE; i++) if(!apiQueue[i].active) {
            apiQueue[i].type = API_REQUEST_HEARTBEAT;
            apiQueue[i].id1 = 0;
            apiQueue[i].id2 = 0;
            apiQueue[i].str1 = "";
            apiQueue[i].str2 = "";
            apiQueue[i].val = 0.0f;
            apiQueue[i].active = true;
            break;
        }
        xSemaphoreGive(queueMutex);
    }
}

void syncBambuddySpoolAsync(String tagUuid, float weight) {
    // Bambuddy: Asynchroner Aufruf zum Aktualisieren oder Anlegen einer Spule
    Serial.printf("syncBambuddySpoolAsync: tagUuid=%s, weight=%.1f\n", tagUuid.c_str(), weight);
    if (!checkFilamanRegistration()) {
        Serial.println("ERROR: URL nicht konfiguriert");
        return;
    }
    if (weight <= 0) {
        Serial.println("ERROR: Gewicht ist 0 oder negativ");
        return;
    }
    if (xSemaphoreTake(queueMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        for(int i=0; i<MAX_API_QUEUE; i++) if(!apiQueue[i].active) {
            apiQueue[i].type = API_REQUEST_SYNC_BAMBUDDY;
            apiQueue[i].id1 = 0; // Nicht mehr benötigt
            apiQueue[i].id2 = 0;
            apiQueue[i].str1 = tagUuid;
            apiQueue[i].str2 = "";
            apiQueue[i].val = weight;
            apiQueue[i].active = true;
            Serial.printf("Weight queued for Bambuddy API (slot %d)\n", i);
            break;
        }
        xSemaphoreGive(queueMutex);
    }
}

bool initFilaman() {
    oledShowProgressBar(3, NUM_SETUP_STEPS, DISPLAY_BOOT_TEXT, tr(STR_API_INIT));
    loadFilamanConfig();
    queueMutex = xSemaphoreCreateMutex();
    // Move to Core 1 (Hardware Core) to free up Core 0 for WiFi/Webserver
    // Set priority to 1 (same as Scale/NFC) to ensure fair scheduling
    xTaskCreatePinnedToCore(filamanApiTask, "FilaManApi", 6144, NULL, 1, NULL, 1);
    if (checkFilamanRegistration()) sendHeartbeatAsync();
    return true;
}
