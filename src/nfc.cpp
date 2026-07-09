#include "nfc.h"
#include <Arduino.h>
#include <Adafruit_PN532.h>
#include <ArduinoJson.h>
#include "config.h"
#include "website.h"
#include "api.h"
#include "esp_task_wdt.h"
#include "scale.h"
#include "main.h"
#include "lang.h"
#include "mbedtls/sha256.h"
#include "mbedtls/md.h"
#include "hkdf.h"

//Adafruit_PN532 nfc(PN532_SCK, PN532_MISO, PN532_MOSI, PN532_SS);
Adafruit_PN532 nfc(PN532_IRQ, PN532_RESET);

TaskHandle_t RfidReaderTask;
// AsyncWebServerRequest* volatile activeNfcWriteRequest = nullptr; // Removed
SemaphoreHandle_t nfcRequestMutex = NULL;

String activeTagUuid = "";
String nfcJsonData = "";
bool tagProcessed = false;
bool isBambuTag = false; // NEU: Flag zur Unterscheidung
volatile bool nfcReadingTaskSuspendRequest = false;
volatile bool nfcReadingTaskSuspendState = false;

volatile nfcReaderStateType nfcReaderState = NFC_IDLE;
// 0 = nicht gelesen
// 1 = erfolgreich gelesen
// 2 = fehler beim Lesen
// 3 = schreiben
// 4 = fehler beim Schreiben
// 5 = erfolgreich geschrieben
// 6 = reading
// ***** PN532

// Bambu Lab MIFARE Classic key derivation constants
const byte BAMBU_MASTER_KEY[] = {
    0x9A, 0x75, 0x9C, 0xF2, 0xC4, 0xF7, 0xCA, 0xFF,
    0x22, 0x2C, 0xB9, 0x76, 0x9B, 0x41, 0xBC, 0x96
};
const byte BAMBU_CONTEXT[] = "RFID-A"; // 7 bytes, inklusive des impliziten Null-Terminators

/**
 * @brief Leitet den MIFARE-Schlüssel für einen bestimmten Sektor aus der UID ab.
 * 
 * @param uid Die 4-Byte-UID des Tags.
 * @param sector Der Sektor (0-15), für den der Schlüssel benötigt wird.
 * @param keyA Der Puffer, in den der 6-Byte-Schlüssel geschrieben wird.
 * @return true bei Erfolg, false bei Fehler.
 */
bool deriveBambuKeyForSector(const uint8_t* uid, uint8_t sector, uint8_t* keyA) {
    if (sector > 15) return false;

    const mbedtls_md_info_t* md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (md_info == NULL) {
        Serial.println("Fehler: SHA256 nicht verfügbar.");
        return false;
    }

    // Wir brauchen nur 6 Bytes pro Sektor, also leiten wir nur die benötigten ab.
    uint8_t okm[96]; // Puffer für alle 16 Sektorschlüssel
    bool ok = hkdf_sha256(
        BAMBU_MASTER_KEY,
        sizeof(BAMBU_MASTER_KEY),
        uid,
        4,
        BAMBU_CONTEXT,
        7, // Explizit 7 Bytes, wie im Python-Skript (b"RFID-A\x00")
        okm,
        sizeof(okm));

    if (!ok)
        return false;

    memcpy(keyA, okm + sector * 6, 6);
    return true;
}

// Sichere Tag-Erkennung mit manuellem Retry und kurzen Timeouts
bool safeTagDetection(uint8_t* uid, uint8_t* uidLength) {
    const int MAX_ATTEMPTS = 3;
    const int SHORT_TIMEOUT = 100; // Sehr kurzer Timeout, um Aufhängen zu vermeiden

    for (int attempt = 0; attempt < MAX_ATTEMPTS; attempt++) {
        // Watchdog bei jedem Versuch zurücksetzen
        esp_task_wdt_reset();
        yield();

        // Kurzen Timeout verwenden, um Blockieren zu vermeiden
        bool success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, uidLength, SHORT_TIMEOUT);

        if (success) {
            Serial.printf("✓ Tag detected on attempt %d with %dms timeout\n", attempt + 1, SHORT_TIMEOUT);
            return true;
        }

        // Kurze Pause zwischen den Versuchen
        vTaskDelay(pdMS_TO_TICKS(25));

        // RF-Feld nach fehlgeschlagenem Versuch aktualisieren (außer beim letzten Versuch)
        if (attempt < MAX_ATTEMPTS - 1) {
            nfc.SAMConfig();
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    return false;
}

void scanRfidTask(void * parameter) {
  Serial.println("RFID Task gestartet");
  for(;;) {
    // Regelmäßiger Watchdog-Reset
    esp_task_wdt_reset();
    yield();

    if (!nfcReadingTaskSuspendRequest && !booting)
    {
      nfcReadingTaskSuspendState = false;
      yield();

      uint8_t success;
      uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };  // Puffer zum Speichern der UID
      uint8_t uidLength;

      // Sichere Tag-Erkennung anstelle des blockierenden readPassiveTargetID nutzen
      success = safeTagDetection(uid, &uidLength);

      foundNfcTag(nullptr, success);

      // Solange noch ein Tag auf dem Leser liegt, nicht erneut versuchen ihn zu lesen
      if (success && nfcReaderState == NFC_IDLE)
      {
        // Aktuellen Tag als noch nicht verarbeitet markieren
        tagProcessed = false;
        isBambuTag = false; // Zurücksetzen für jeden neuen Scan

        // Display aufwecken, wenn ein Tag erkannt wird
        oledResetActivityTimer();

        // Basisinformationen der Karte im Serial Monitor ausgeben
        Serial.println("Found an ISO14443A card");

        nfcReaderState = NFC_READING;
        pauseMainTask = 1;

        oledShowProgressBar(0, 4, tr(STR_READING), tr(STR_DETECTING_TAG));
        oledSetPriority(DISPLAY_PRIORITY_ACTION, 1500);

        // Stabilisierungszeit für zuverlässige Tag-Kommunikation
        Serial.println("Tag erkannt, stabilisiere...");
        vTaskDelay(pdMS_TO_TICKS(500)); // Increased from 200ms for reliable reads

        // =================================================================
        // NEU: Detaillierter Daten-Dump des gesamten Tags für die Fehlersuche
        // =================================================================
        /*
        Serial.println("--- Start Tag-Daten-Dump ---");
        String fullUidString = "";
        for (uint8_t i = 0; i < uidLength; i++) {
          if (uid[i] < 0x10) fullUidString += "0";
          fullUidString += String(uid[i], HEX);
        }
        fullUidString.toUpperCase();
        Serial.println("Vollständige UID: " + fullUidString);
        Serial.printf("Tag-Länge: %d bytes\n", uidLength);

        // Wenn es ein 4-Byte-Tag ist, versuchen wir, ihn als MIFARE Classic zu behandeln
        if (uidLength == 4) {
            Serial.println("4-Byte-Tag erkannt, versuche MIFARE Classic Authentifizierung...");
            uint8_t keyA[6];
            uint8_t blockBuffer[16];
            
            // Wir versuchen, alle 16 Sektoren (Blöcke 0-63) zu lesen
            for (uint8_t sector = 0; sector < 16; sector++) {
                Serial.printf("\n--- Sektor %d ---\n", sector);
                if (deriveBambuKeyForSector(uid, sector, keyA)) {
                    Serial.print("Abgeleiteter Schlüssel für Sektor "); Serial.print(sector); Serial.print(": ");
                    for(int k=0; k<6; k++) { Serial.printf("%02X ", keyA[k]); }
                    Serial.println();

                    // Authentifiziere den ersten Block des Sektors (Trailer-Block wäre sicherer, aber zum Lesen reicht das)
                    if (nfc.mifareclassic_AuthenticateBlock(uid, uidLength, sector * 4, 0, keyA)) {
                        Serial.printf("Authentifizierung für Sektor %d erfolgreich!\n", sector);
                        // Lese die 4 Blöcke dieses Sektors
                        for (uint8_t block = 0; block < 4; block++) {
                            uint8_t currentBlock = (sector * 4) + block;
                            if (nfc.mifareclassic_ReadDataBlock(currentBlock, blockBuffer)) {
                                Serial.printf("  Block %02d: ", currentBlock);
                                for(int j=0; j<16; j++) { Serial.printf("%02X ", blockBuffer[j]); }
                                Serial.println();
                            } else {
                                Serial.printf("  Block %02d: Lesefehler nach Authentifizierung.\n", currentBlock);
                            }
                        }
                    } else {
                        Serial.printf("Authentifizierung für Sektor %d fehlgeschlagen.\n", sector);
                    }
                }
            }
        }
        Serial.println("--- Ende Tag-Daten-Dump ---"); */

        // =================================================================
        // NEU: Intelligente Erkennung von Bambu-Tags vs. generischen Tags
        // =================================================================
        String uidString = ""; // Wird entweder die Hardware-UID oder die Tray-UUID

        if (uidLength == 4) {
            // Potenzieller Bambu-Tag (MIFARE Classic)
            Serial.println("4-Byte-Tag erkannt, versuche Bambu-Authentifizierung...");
            uint8_t keyA[6];
            uint8_t blockBuffer[16];
            uint8_t sector = 2; // Tray-UUID ist in Sektor 2

            if (deriveBambuKeyForSector(uid, sector, keyA) && nfc.mifareclassic_AuthenticateBlock(uid, uidLength, sector * 4, 0, keyA)) {
                Serial.println("Bambu-Authentifizierung erfolgreich! Lese Tray-UUID aus Block 9.");
                isBambuTag = true;
                if (nfc.mifareclassic_ReadDataBlock(9, blockBuffer)) {
                    for (int j = 0; j < 16; j++) {
                        if (blockBuffer[j] < 0x10) uidString += "0";
                        uidString += String(blockBuffer[j], HEX);
                    }
                    uidString.toUpperCase();
                    Serial.println("Gelesene Tray-UUID: " + uidString);
                } else {
                    Serial.println("Fehler: Konnte Block 9 nach Authentifizierung nicht lesen.");
                    isBambuTag = false; // Fallback
                }
            } else {
                Serial.println("Bambu-Authentifizierung fehlgeschlagen. Behandle als generischen MIFARE-Tag.");
            }
        }

        // Fallback oder generischer Tag: Hardware-UID verwenden
        if (uidString.isEmpty()) {
            isBambuTag = false; // Sicherstellen, dass das Flag korrekt ist
            for (uint8_t i = 0; i < uidLength; i++) {
                if (uid[i] < 0x10) uidString += "0";
                uidString += String(uid[i], HEX);
            }
            uidString.toUpperCase();
            Serial.println("Verwende Hardware-UID: " + uidString);
        }

        activeTagUuid = uidString; // Setze die globale Tag-ID
        nfcReaderState = NFC_READ_SUCCESS;
        
        // Feedback ans Display - Geht nun sofort!
        const char* statusMsg = isBambuTag ? "Bambu Tag" : "NFC Tag";
        oledShowProgressBar(1, 4, tr(STR_SPOOL_TAG), statusMsg);
        oledSetPriority(DISPLAY_PRIORITY_ACTION, 1000);
        Serial.println("Tag UID gelesen: " + uidString);
      }

      if (!success && nfcReaderState != NFC_IDLE && !nfcReadingTaskSuspendRequest)
      {
        Serial.printf("NFC: Tag removed (Previous state: %d)\n", nfcReaderState);
        nfcReaderState = NFC_IDLE;
        nfcJsonData = "";
        activeTagUuid = "";
        tagProcessed = false;
        isBambuTag = false;
        pauseMainTask = 0;
        oledClearPriority();
        oledShowWeight(weight);
      }

      // Status nach erfolgreichem Lesen zurücksetzen, wenn der Tag entfernt wurde
      else if (!success && nfcReaderState == NFC_READ_SUCCESS)
      {
        nfcReaderState = NFC_IDLE;
        tagProcessed = false;
        isBambuTag = false;
        Serial.println("Tag nach erfolgreichem Lesen entfernt - bereit für nächsten Tag");
      }

      // Pause nach erfolgreichem Lesen hinzufügen, um sofortiges erneutes Scannen zu verhindern
      if (nfcReaderState == NFC_READ_SUCCESS) {
        // Nach der Tag-Verarbeitung das Scannen verlangsamen, um der API Zeit zu geben
        Serial.println("Tag processed - slowing scan to 2 seconds");
        vTaskDelay(pdMS_TO_TICKS(2000));
      } else {
        // Schnelleres Scannen, wenn kein Tag anliegt
        vTaskDelay(pdMS_TO_TICKS(500));
      }

      // aktualisieren der Website wenn sich der Status ändert
      sendNfcData();
    }
    else
    {
      nfcReadingTaskSuspendState = true;

      // Full suspension requested
      Serial.println("NFC Reading disabled");
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
    yield();
  }
}

void startNfc() {
  nfcRequestMutex = xSemaphoreCreateMutex();
  oledShowProgressBar(4, NUM_SETUP_STEPS, DISPLAY_BOOT_TEXT, tr(STR_NFC_INIT));
  nfc.begin();                                           // Beginne Kommunikation mit RFID Leser

  delay(1000);
  unsigned long versiondata = nfc.getFirmwareVersion();  // Lese Versionsnummer der Firmware aus
  if (! versiondata) {                                   // Wenn keine Antwort kommt
    Serial.println("Kann kein RFID Board finden !");            // Sende Text "Kann kein..." an seriellen Monitor
    oledDisplayText(tr(STR_NO_RFID_BOARD));
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
  else {
    Serial.print("Chip PN5 gefunden"); Serial.println((versiondata >> 24) & 0xFF, HEX); // Sende Text und Versionsinfos an seriellen
    Serial.print("Firmware ver. "); Serial.print((versiondata >> 16) & 0xFF, DEC);      // Monitor, wenn Antwort vom Board kommt
    Serial.print('.'); Serial.println((versiondata >> 8) & 0xFF, DEC);                  //

    nfc.SAMConfig();
    // Set the max number of retry attempts to read from a card
    // This prevents us from waiting forever for a card, which is
    // the default behaviour of the PN532.
    //nfc.setPassiveActivationRetries(0x7F);
    //nfc.setPassiveActivationRetries(0xFF);

    BaseType_t result = xTaskCreatePinnedToCore(
      scanRfidTask, /* Function to implement the task */
      "RfidReader", /* Name of the task */
      5115,  /* Stack size in words */
      NULL,  /* Task input parameter */
      rfidTaskPrio,  /* Priority of the task */
      &RfidReaderTask,  /* Task handle. */
      rfidTaskCore); /* Core where the task should run */

      if (result != pdPASS) {
        Serial.println("Fehler beim Erstellen des RFID Tasks");
    } else {
        Serial.println("RFID Task erfolgreich erstellt");
    }
  }
}
