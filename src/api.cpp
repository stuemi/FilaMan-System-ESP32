#include "api.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
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

// State for the new assignment workflow
bool isAssignmentPending = false;
String pendingTagForAssignment = "";
float pendingWeightForAssignment = 0.0f;

void clearPendingAssignment() {
    isAssignmentPending = false;
    pendingTagForAssignment = "";
}

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
    if (!checkFilamanRegistration() || WiFi.status() != WL_CONNECTED) {
        filamanConnected = false;
        return false;
    }
    // Dummy-Heartbeat hält das UI "online", solange WLAN da ist und die Waage konfiguriert wurde.
    // Echte API-Fehler werden unten beim Wiegen (syncBambuddySpool) abgefangen.
    filamanConnected = true;
    return true;
}

// Bambuddy: Zentrale Logik-Weiche für Workflow 1 & 2
bool syncBambuddySpool(String tagUuid, float measuredWeight) {
    Serial.printf("syncBambuddy: starte API-Abfrage - tagUuid=%s, weight=%.1f\n", tagUuid.c_str(), measuredWeight);
    if (!checkFilamanRegistration() || WiFi.status() != WL_CONNECTED) {
        Serial.println("ERROR: Keine URL konfiguriert oder WiFi nicht verbunden");
        filamanConnected = false;
        return false;
    }

    HTTPClient http;
    http.setTimeout(10000);
    
    // =========================================================
    // 1. GET Request: Prüfen, ob Spule in Bambuddy existiert
    // =========================================================
    // Wir rufen ALLE Spulen ab, da die API kein Filtern nach tag_uid unterstützt
    String getUrl = filamanUrl + "/api/v1/inventory/spools";
    http.begin(getUrl);
    
    http.addHeader("Authorization", "Bearer " + filamanToken);
    
    int httpCode = http.GET();
    String response = http.getString();
    http.end();

    if (httpCode != 200) {
        Serial.printf("GET-Request fehlgeschlagen. HTTP-Code: %d\n", httpCode);
        Serial.println("Server-Antwort:");
        Serial.println(response);
    }

    int foundSpoolId = -1;

    if (httpCode == 200) {
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, response);
        if (!error) {
            // Die Antwort ist ein Array von Spulen. Wir müssen es durchsuchen.
            if (doc.is<JsonArray>()) {
                JsonArray spools = doc.as<JsonArray>();
                Serial.printf("Durchsuche %d Spulen nach tag_uid: %s\n", spools.size(), tagUuid.c_str());
                for (JsonObject spool : spools) {
                    String serverTag = spool["tag_uid"] | "";
                    // Wir prüfen, ob die UID vom Server mit der (kürzeren) gescannten UID beginnt.
                    // tagUuid ist bereits in Großbuchstaben.
                    if (serverTag.startsWith(tagUuid)) {
                        foundSpoolId = spool["id"] | -1;
                        Serial.printf("Treffer! Spule '%s' gefunden mit ID: %d\n", serverTag.c_str(), foundSpoolId);
                        break; // Suche beenden, wenn Treffer gefunden
                    }
                }
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

        JsonDocument postDoc;
        postDoc["spool_id"] = foundSpoolId;
        postDoc["weight_grams"] = measuredWeight;
        String payload;
        serializeJson(postDoc, payload);

        Serial.println("Sende Payload für Update:");
        Serial.println(payload);

        int postCode = http.POST(payload);
        String responseBody = http.getString();
        Serial.printf("Update-Request beendet. HTTP-Code: %d\n", postCode);
        Serial.println("Server-Antwort:");
        Serial.println(responseBody);

        http.end();
        
        if (postCode == 200 || postCode == 201 || postCode == 204) {
            oledShowRemainingWeight((int)measuredWeight);
            oledSetPriority(DISPLAY_PRIORITY_ACTION, 3000);
            vTaskDelay(pdMS_TO_TICKS(3000));
            oledClearPriority();
            filamanConnected = true;
            return true;
        }
    } else {
        // WORKFLOW 2: Unbekannte Spule
        // Prüfen, ob es eine Bambu-Spule ist. Wenn ja, nicht zuweisen, sondern Meldung anzeigen.
        if (tagUuid.length() == 8) {
            Serial.println("Unbekannte Bambu Lab Spule erkannt. Bitte ins AMS legen.");
            oledDisplayText(tr(STR_BAMBU_SPOOL_DETECTED));
            oledSetPriority(DISPLAY_PRIORITY_INFO, 5000); // Meldung für 5 Sekunden anzeigen
            vTaskDelay(pdMS_TO_TICKS(5000));
            oledClearPriority();
            filamanConnected = true; // Verbindung ist ja ok
            return true; // Workflow "erfolgreich" beendet (kein Fehler)
        }

        // WORKFLOW 1: Bekannte Spule -> Gewicht updaten
        // Für alle anderen unbekannten Spulen (NTAG etc.) den Zuweisungs-Workflow starten.
        isAssignmentPending = true;
        pendingTagForAssignment = tagUuid;
        pendingWeightForAssignment = measuredWeight;

        Serial.println("Unbekannter Tag. Starte Zuweisungs-Workflow.");
        oledDisplayText(tr(STR_ASSIGN_SPOOL));
        oledSetPriority(DISPLAY_PRIORITY_ACTION, 15000); // Längerer Timeout, damit Nutzer Zeit hat
        ws.textAll("{\"type\":\"assignment_pending\"}"); // Web-Clients informieren

        filamanConnected = true;
        return true; // Workflow erfolgreich gestartet
    }

    // Fehlerfall
    filamanConnected = false;
    oledShowProgressBar(1, 1, tr(STR_FAILURE), tr(STR_API_ERROR));
    oledSetPriority(DISPLAY_PRIORITY_WARNING, 2000);
    vTaskDelay(pdMS_TO_TICKS(2000));
    oledClearPriority();
    return false;
}

String getUntaggedSpools() {
    if (!checkFilamanRegistration() || WiFi.status() != WL_CONNECTED) {
        return "[]";
    }

    HTTPClient http;
    http.setTimeout(10000);
    String getUrl = filamanUrl + "/api/v1/inventory/spools";
    http.begin(getUrl);
    http.addHeader("Authorization", "Bearer " + filamanToken);
    
    int httpCode = http.GET();
    String response = http.getString();
    http.end();

    if (httpCode != 200) {
        Serial.printf("getUntaggedSpools: Fehler beim Abrufen der Spulen. HTTP-Code: %d\n", httpCode);
        return "[]";
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, response);
    if (error || !doc.is<JsonArray>()) {
        Serial.println("getUntaggedSpools: Fehler beim Parsen der JSON-Antwort.");
        return "[]";
    }

    JsonDocument filteredDoc;
    JsonArray filteredArray = filteredDoc.to<JsonArray>();

    for (JsonObject spool : doc.as<JsonArray>()) {
        if (spool["tag_uid"].isNull() || (spool["tag_uid"].is<String>() && spool["tag_uid"].as<String>().isEmpty())) {
            JsonObject newSpool = filteredArray.add<JsonObject>();
            // Alle gewünschten Felder für die Auswahl hinzufügen
            newSpool["id"] = spool["id"];
            newSpool["brand"] = spool["brand"];
            newSpool["material"] = spool["material"];
            newSpool["subtype"] = spool["subtype"];
            newSpool["color_name"] = spool["color_name"];
            newSpool["created_at"] = spool["created_at"];
            newSpool["label_weight"] = spool["label_weight"];
            newSpool["last_scale_weight"] = spool["last_scale_weight"];
        }
    }

    String filteredResponse;
    serializeJson(filteredDoc, filteredResponse);
    return filteredResponse;
}

bool linkTag(int spoolId, const String& tagUid) {
    if (!checkFilamanRegistration() || WiFi.status() != WL_CONNECTED) {
        return false;
    }

    HTTPClient http;
    http.setTimeout(10000);
    String postUrl = filamanUrl + "/api/v1/inventory/spools/" + String(spoolId) + "/link-tag";
    http.begin(postUrl);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", "Bearer " + filamanToken);

    JsonDocument postDoc;
    postDoc["tag_uid"] = tagUid;
    postDoc["data_origin"] = "nfc_link";

    String payload;
    serializeJson(postDoc, payload);

    Serial.printf("Verlinke Tag %s mit Spule %d\n", tagUid.c_str(), spoolId);
    Serial.println("Sende Payload: " + payload);

    // Laut API-Dokumentation wird PATCH für das Verlinken eines Tags erwartet.
    int postCode = http.PATCH(payload);
    String responseBody = http.getString();
    http.end();

    Serial.printf("Link-Request beendet. HTTP-Code: %d\n", postCode);
    Serial.println("Server-Antwort: " + responseBody);

    return (postCode == 200 || postCode == 201 || postCode == 204);
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
        // Doppelte Heartbeats verhindern
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
    // Auf Core 1 (Hardware Core) verschieben, um Core 0 für WiFi/Webserver freizuhalten
    // Priorität auf 1 setzen (wie Waage/NFC), um faires Scheduling zu gewährleisten
    xTaskCreatePinnedToCore(filamanApiTask, "FilaManApi", 6144, NULL, 1, NULL, 1);
    if (checkFilamanRegistration()) sendHeartbeatAsync();
    return true;
}
