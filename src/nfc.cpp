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

JsonDocument rfidData;
String activeSpoolId = "";
String activeTagUuid = "";
String lastSpoolId = "";
String nfcJsonData = "";
bool tagProcessed = false;
bool isBambuTag = false;
volatile bool nfcReadingTaskSuspendRequest = false;
volatile bool nfcReadingTaskSuspendState = false;
volatile bool nfcWriteInProgress = false; // Prevent any tag operations during write

volatile nfcReaderStateType nfcReaderState = NFC_IDLE;
// 0 = nicht gelesen
// 1 = erfolgreich gelesen
// 2 = fehler beim Lesen
// 3 = schreiben
// 4 = fehler beim Schreiben
// 5 = erfolgreich geschrieben
// 6 = reading
// ***** PN532

// Safe tag detection with manual retry logic and short timeouts
bool safeTagDetection(uint8_t* uid, uint8_t* uidLength) {
    const int MAX_ATTEMPTS = 3;
    const int SHORT_TIMEOUT = 100; // Very short timeout to prevent hanging

    for (int attempt = 0; attempt < MAX_ATTEMPTS; attempt++) {
        // Watchdog reset on each attempt
        esp_task_wdt_reset();
        yield();

        // Use short timeout to avoid blocking
        bool success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, uidLength, SHORT_TIMEOUT);

        if (success) {
            Serial.printf("✓ Tag detected on attempt %d with %dms timeout\n", attempt + 1, SHORT_TIMEOUT);
            return true;
        }

        // Short pause between attempts
        vTaskDelay(pdMS_TO_TICKS(25));

        // Refresh RF field after failed attempt (but not on last attempt)
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
    // Regular watchdog reset
    esp_task_wdt_reset();
    yield();

    // Skip scanning during write operations, but keep NFC interface active
    if (nfcReaderState != NFC_WRITING && !nfcWriteInProgress && !nfcReadingTaskSuspendRequest && !booting)
    {
      nfcReadingTaskSuspendState = false;
      yield();

      uint8_t success;
      uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };  // Buffer to store the returned UID
      uint8_t uidLength;

      // Use safe tag detection instead of blocking readPassiveTargetID
      success = safeTagDetection(uid, &uidLength);

      foundNfcTag(nullptr, success);

      // As long as there is still a tag on the reader, do not try to read it again
      if (success && nfcReaderState == NFC_IDLE)
      {
        // Set the current tag as not processed
        tagProcessed = false;

        // Wake display when a tag is detected
        oledResetActivityTimer();

        // Display some basic information about the card
        Serial.println("Found an ISO14443A card");

        nfcReaderState = NFC_READING;
        pauseMainTask = 1;

        oledShowProgressBar(0, 4, tr(STR_READING), tr(STR_DETECTING_TAG));
        oledSetPriority(DISPLAY_PRIORITY_ACTION, 1500);

        // Stabilization time for reliable tag communication
        Serial.println("Tag detected, stabilizing...");
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
        activeSpoolId = "";
        activeTagUuid = "";
        tagProcessed = false;
        pauseMainTask = 0;
        oledClearPriority();
        oledShowWeight(weight);
      }

      // Reset state after successful read when tag is removed
      else if (!success && nfcReaderState == NFC_READ_SUCCESS)
      {
        nfcReaderState = NFC_IDLE;
        tagProcessed = false;
        isBambuTag = false;
        Serial.println("Tag nach erfolgreichem Lesen entfernt - bereit für nächsten Tag");
      }

      // Add a pause after successful reading to prevent immediate re-reading
      if (nfcReaderState == NFC_READ_SUCCESS) {
        // After tag is processed, slow down scanning to give API time
        Serial.println("Tag processed - slowing scan to 2 seconds");
        vTaskDelay(pdMS_TO_TICKS(2000));
      } else {
        // Faster scanning when no tag or idle state
        vTaskDelay(pdMS_TO_TICKS(500));
      }

      // aktualisieren der Website wenn sich der Status ändert
      sendNfcData();
    }
    else
    {
      nfcReadingTaskSuspendState = true;

      // Different behavior for write protection vs. full suspension
      if (nfcWriteInProgress) {
        // During write: Just pause scanning, don't disable NFC interface
        // Serial.println("NFC Scanning paused during write operation");
        vTaskDelay(pdMS_TO_TICKS(100)); // Shorter delay during write
      } else {
        // Full suspension requested
        Serial.println("NFC Reading disabled");
        vTaskDelay(pdMS_TO_TICKS(1000));
      }
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
