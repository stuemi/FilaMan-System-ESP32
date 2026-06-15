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

//Adafruit_PN532 nfc(PN532_SCK, PN532_MISO, PN532_MOSI, PN532_SS);
Adafruit_PN532 nfc(PN532_IRQ, PN532_RESET);

TaskHandle_t RfidReaderTask;
// AsyncWebServerRequest* volatile activeNfcWriteRequest = nullptr; // Removed
SemaphoreHandle_t nfcRequestMutex = NULL;

String activeTagUuid = "";
String nfcJsonData = "";
bool tagProcessed = false;
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

        // Bambuddy: Wir lesen NUR noch die Hardware UID aus!
        // Keine NDEF JSON Dekodierung mehr nötig. Das macht den Scan extrem schnell!
        String uidString = "";
        for (uint8_t i = 0; i < uidLength; i++) {
          if (uid[i] < 0x10) uidString += "0";
          uidString += String(uid[i], HEX);
        }
        uidString.toUpperCase();

        activeTagUuid = uidString;
        nfcReaderState = NFC_READ_SUCCESS;
        
        // Feedback ans Display - Geht nun sofort!
        oledShowProgressBar(1, 4, tr(STR_SPOOL_TAG), "UID gelesen!");
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
        pauseMainTask = 0;
        oledClearPriority();
        oledShowWeight(weight);
      }

      // Status nach erfolgreichem Lesen zurücksetzen, wenn der Tag entfernt wurde
      else if (!success && nfcReaderState == NFC_READ_SUCCESS)
      {
        nfcReaderState = NFC_IDLE;
        tagProcessed = false;
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
