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

// ##### Bambu Tag Helper Functions #####
// Simplified: only read UID, no decryption needed (UID is always visible)
bool detectBambuTag(const uint8_t* uid, uint8_t uidLength) {
    Serial.println("Detected Bambu Lab tag (Mifare Classic) - reading UID only");

    // Create UID string with separators (same format as NTAG)
    String uidString = "";
    for (uint8_t i = 0; i < uidLength; i++) {
        if (uid[i] < 0x10) uidString += "0";
        uidString += String(uid[i], HEX);
        if (i < uidLength - 1) {
            uidString += ":";
        }
    }
    uidString.toUpperCase();

    Serial.print("  UID: ");
    Serial.println(uidString);

    isBambuTag = true;
    activeTagUuid = uidString;
    activeSpoolId = ""; // Will be resolved by FilaMan API based on tag UID

    // Create minimal JSON for compatibility
    JsonDocument doc;
    doc["vendor"] = "Bambu Lab";
    doc["type"] = "Bambu";
    nfcJsonData = "";
    serializeJson(doc, nfcJsonData);

    // Set normal read success state - isBambuTag flag is set for later use
    nfcReaderState = NFC_READ_SUCCESS;

    return true;
}

// ##### Funktionen für RFID #####
void payloadToJson(uint8_t *data) {
    const char* startJson = strchr((char*)data, '{');
    const char* endJson = strrchr((char*)data, '}');

    if (startJson && endJson && endJson > startJson) {
      String jsonString = String(startJson, endJson - startJson + 1);
      //Serial.print("Bereinigter JSON-String: ");
      //Serial.println(jsonString);

      // JSON-Dokument verarbeiten
      JsonDocument doc;  // Passen Sie die Größe an den JSON-Inhalt an
      DeserializationError error = deserializeJson(doc, jsonString);

      if (!error) {
        const char* color_hex = doc["color_hex"];
        const char* type = doc["type"];
        int min_temp = doc["min_temp"];
        int max_temp = doc["max_temp"];
        const char* brand = doc["brand"];

        Serial.println();
        Serial.println("-----------------");
        Serial.println("JSON-Parsed Data:");
        Serial.println(color_hex);
        Serial.println(type);
        Serial.println(min_temp);
        Serial.println(max_temp);
        Serial.println(brand);
        Serial.println("-----------------");
        Serial.println();
      } else {
        Serial.print("deserializeJson() failed: ");
        Serial.println(error.f_str());
      }

      doc.clear();
    } else {
        Serial.println("Kein gültiger JSON-Inhalt gefunden oder fehlerhafte Formatierung.");
    }
  }

uint16_t readTagSize()
{
  uint8_t buffer[4];
  memset(buffer, 0, 4);
  nfc.ntag2xx_ReadPage(3, buffer);
  return buffer[2]*8;
}

// Robust page reading with error recovery
bool robustPageRead(uint8_t page, uint8_t* buffer) {
    const int MAX_READ_ATTEMPTS = 3;

    for (int attempt = 0; attempt < MAX_READ_ATTEMPTS; attempt++) {
        esp_task_wdt_reset();
        yield();

        if (nfc.ntag2xx_ReadPage(page, buffer)) {
            return true;
        }

        Serial.printf("Page %d read failed, attempt %d/%d\n", page, attempt + 1, MAX_READ_ATTEMPTS);

        // Try to stabilize connection between attempts
        if (attempt < MAX_READ_ATTEMPTS - 1) {
            vTaskDelay(pdMS_TO_TICKS(25));

            // Re-verify tag presence with quick check
            uint8_t uid[7];
            uint8_t uidLength;
            if (!nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 100)) {
                Serial.println("Tag lost during read operation");
                return false;
            }
        }
    }

    return false;
}

String detectNtagType()
{
  // Read capability container from page 3 to determine exact NTAG type
  uint8_t ccBuffer[4];
  memset(ccBuffer, 0, 4);

  if (!nfc.ntag2xx_ReadPage(3, ccBuffer)) {
    Serial.println("Failed to read capability container");
    return "UNKNOWN";
  }

  // Also read configuration pages to get more info
  uint8_t configBuffer[4];
  memset(configBuffer, 0, 4);

  Serial.print("Capability Container: ");
  for (int i = 0; i < 4; i++) {
    if (ccBuffer[i] < 0x10) Serial.print("0");
    Serial.print(ccBuffer[i], HEX);
    Serial.print(" ");
  }
  Serial.println();

  // NTAG type detection based on capability container
  // CC[2] contains the data area size in bytes / 8
  uint16_t dataAreaSize = ccBuffer[2] * 8;

  Serial.print("Data area size from CC: ");
  Serial.println(dataAreaSize);

  // Try to read different configuration pages to determine exact type
  String tagType = "UNKNOWN";

  // Try to read page 41 (NTAG213 ends at page 39, so this should fail)
  uint8_t testBuffer[4];
  bool canReadPage41 = nfc.ntag2xx_ReadPage(41, testBuffer);

  // Try to read page 130 (NTAG215 ends at page 129, so this should fail for NTAG213/215)
  bool canReadPage130 = nfc.ntag2xx_ReadPage(130, testBuffer);

  if (dataAreaSize <= 180 && !canReadPage41) {
    tagType = "NTAG213";
    Serial.println("Detected: NTAG213 (cannot read beyond page 39)");
  } else if (dataAreaSize <= 540 && canReadPage41 && !canReadPage130) {
    tagType = "NTAG215";
    Serial.println("Detected: NTAG215 (can read page 41, cannot read page 130)");
  } else if (dataAreaSize <= 928 && canReadPage130) {
    tagType = "NTAG216";
    Serial.println("Detected: NTAG216 (can read page 130)");
  } else {
    // Fallback: use data area size from capability container
    if (dataAreaSize <= 180) {
      tagType = "NTAG213";
      Serial.println("Fallback detection: NTAG213 based on data area size");
    } else if (dataAreaSize <= 540) {
      tagType = "NTAG215";
      Serial.println("Fallback detection: NTAG215 based on data area size");
    } else {
      tagType = "NTAG216";
      Serial.println("Fallback detection: NTAG216 based on data area size");
    }
  }

  return tagType;
}

uint16_t getAvailableUserDataSize()
{
  String tagType = detectNtagType();
  uint16_t userDataSize = 0;

  if (tagType == "NTAG213") {
    // NTAG213: User data from page 4-39 (36 pages * 4 bytes = 144 bytes)
    userDataSize = 144;
    Serial.println("NTAG213 confirmed - 144 bytes user data available");
  } else if (tagType == "NTAG215") {
    // NTAG215: User data from page 4-129 (126 pages * 4 bytes = 504 bytes)
    userDataSize = 504;
    Serial.println("NTAG215 confirmed - 504 bytes user data available");
  } else if (tagType == "NTAG216") {
    // NTAG216: User data from page 4-225 (222 pages * 4 bytes = 888 bytes)
    userDataSize = 888;
    Serial.println("NTAG216 confirmed - 888 bytes user data available");
  } else {
    // Unknown tag type, use conservative estimate
    uint16_t tagSize = readTagSize();
    userDataSize = tagSize - 60; // Reserve 60 bytes for headers/config
    Serial.print("Unknown NTAG type, using conservative estimate: ");
    Serial.println(userDataSize);
  }

  return userDataSize;
}

uint16_t getMaxUserDataPages()
{
  String tagType = detectNtagType();
  uint16_t maxPages = 0;

  if (tagType == "NTAG213") {
    maxPages = 39; // Pages 4-39 are user data
  } else if (tagType == "NTAG215") {
    maxPages = 129; // Pages 4-129 are user data
  } else if (tagType == "NTAG216") {
    maxPages = 225; // Pages 4-225 are user data
  } else {
    // Conservative fallback
    maxPages = 39;
    Serial.println("Unknown tag type, using NTAG213 page limit as fallback");
  }

  Serial.print("Maximum writable page: ");
  Serial.println(maxPages);
  return maxPages;
}

bool decodeNdefAndReturnJson(const byte* encodedMessage, String uidString) {
  oledShowProgressBar(1, 4, tr(STR_READING), tr(STR_DECODING_DATA));
  oledSetPriority(DISPLAY_PRIORITY_ACTION, 1500);

  // Debug: Print first 32 bytes of the raw data
  Serial.println("Raw NDEF data (first 32 bytes):");
  for (int i = 0; i < 32; i++) {
    if (encodedMessage[i] < 0x10) Serial.print("0");
    Serial.print(encodedMessage[i], HEX);
    Serial.print(" ");
    if ((i + 1) % 16 == 0) Serial.println();
  }
  Serial.println();

  // Look for the NDEF TLV structure starting from the beginning
  int tlvOffset = 0;
  bool foundNdefTlv = false;

  // Search for NDEF TLV (0x03) in the first few bytes
  for (int i = 0; i < 16; i++) {
    if (encodedMessage[i] == 0x03) {
      tlvOffset = i;
      foundNdefTlv = true;
      Serial.print("Found NDEF TLV at offset: ");
      Serial.println(tlvOffset);
      break;
    }
  }

  if (!foundNdefTlv) {
    Serial.println("No NDEF TLV found in tag data");
    return false;
  }

  // Get the NDEF message length from TLV
  uint16_t ndefMessageLength = 0;
  int ndefRecordOffset = 0;

  if (encodedMessage[tlvOffset + 1] == 0xFF) {
    // Extended length format: next 2 bytes contain the actual length
    ndefMessageLength = (encodedMessage[tlvOffset + 2] << 8) | encodedMessage[tlvOffset + 3];
    ndefRecordOffset = tlvOffset + 4; // Skip TLV tag + 0xFF + 2 length bytes
    Serial.print("NDEF Message Length (extended): ");
  } else {
    // Standard length format: single byte contains the length
    ndefMessageLength = encodedMessage[tlvOffset + 1];
    ndefRecordOffset = tlvOffset + 2; // Skip TLV tag + 1 length byte
    Serial.print("NDEF Message Length (standard): ");
  }
  Serial.println(ndefMessageLength);

  // Get pointer to NDEF record
  const byte* ndefRecord = &encodedMessage[ndefRecordOffset];

  // Parse NDEF record header
  byte recordHeader = ndefRecord[0];
  byte typeLength = ndefRecord[1];

  Serial.print("NDEF Record Header: 0x");
  Serial.println(recordHeader, HEX);
  Serial.print("Type Length: ");
  Serial.println(typeLength);

  // Determine payload length (can be 1 or 4 bytes depending on SR flag)
  uint32_t payloadLength = 0;
  byte payloadLengthBytes = 1;
  byte payloadLengthOffset = 2;

  // Check if Short Record (SR) flag is set (bit 4)
  if (recordHeader & 0x10) { // SR flag
    payloadLength = ndefRecord[2];
    payloadLengthBytes = 1;
    payloadLengthOffset = 2;
  } else {
    // Long record format (4 bytes for payload length)
    payloadLength = (ndefRecord[2] << 24) | (ndefRecord[3] << 16) |
                   (ndefRecord[4] << 8) | ndefRecord[5];
    payloadLengthBytes = 4;
    payloadLengthOffset = 2;
  }

  Serial.print("Payload Length: ");
  Serial.println(payloadLength);
  Serial.print("Payload Length Bytes: ");
  Serial.println(payloadLengthBytes);

  // Check for ID field (if IL flag is set)
  byte idLength = 0;
  if (recordHeader & 0x08) { // IL flag
    idLength = ndefRecord[payloadLengthOffset + payloadLengthBytes];
    Serial.print("ID Length: ");
    Serial.println(idLength);
  }

  // Calculate offset to payload
  byte payloadOffset = 1 + 1 + payloadLengthBytes + typeLength + idLength;

  Serial.print("Calculated payload offset: ");
  Serial.println(payloadOffset);

  // Verify we have enough data
  if (payloadOffset + payloadLength > ndefMessageLength) {
    Serial.println("Invalid NDEF structure - payload extends beyond message");
    Serial.print("Payload offset + length: ");
    Serial.print(payloadOffset + payloadLength);
    Serial.print(", NDEF message length: ");
    Serial.println(ndefMessageLength);
    return false;
  }

  // Print the record type for debugging
  Serial.print("Record Type: ");
  for (int i = 0; i < typeLength; i++) {
    Serial.print((char)ndefRecord[1 + 1 + payloadLengthBytes + i]);
  }
  Serial.println();

  nfcJsonData = "";

  // Extract JSON payload with validation
  uint32_t actualJsonLength = 0;
  for (uint32_t i = 0; i < payloadLength; i++) {
    byte currentByte = ndefRecord[payloadOffset + i];

    // Stop at null terminator or if we find the end of JSON
    if (currentByte == 0x00) {
      Serial.print("Found null terminator at position: ");
      Serial.println(i);
      break;
    }

    // Only add printable characters and common JSON characters
    if (currentByte >= 32 && currentByte <= 126) {
      nfcJsonData += (char)currentByte;
      actualJsonLength++;
    } else {
      Serial.print("Skipping non-printable byte at position ");
      Serial.print(i);
      Serial.print(": 0x");
      Serial.println(currentByte, HEX);
    }

    // Check if we've reached the end of a JSON object
    if (currentByte == '}') {
      // Count opening and closing braces to detect complete JSON
      int braceCount = 0;
      for (uint32_t j = 0; j <= i; j++) {
        if (ndefRecord[payloadOffset + j] == '{') braceCount++;
        else if (ndefRecord[payloadOffset + j] == '}') braceCount--;
      }

      if (braceCount == 0) {
        Serial.print("Found complete JSON object at position: ");
        Serial.println(i);
        actualJsonLength = i + 1;
        break;
      }
    }
  }

  Serial.print("Actual JSON length extracted: ");
  Serial.println(actualJsonLength);
  Serial.print("Total nfcJsonData length: ");
  Serial.println(nfcJsonData.length());
  Serial.println("=== DECODED JSON DATA START ===");
  Serial.println(nfcJsonData);
  Serial.println("=== DECODED JSON DATA END ===");

  // Check if JSON was truncated
  if (nfcJsonData.length() < payloadLength && !nfcJsonData.endsWith("}")) {
    Serial.println("WARNING: JSON payload appears to be truncated!");
    Serial.print("Expected payload length: ");
    Serial.println(payloadLength);
    Serial.print("Actual extracted length: ");
    Serial.println(nfcJsonData.length());
  }

  // Trim any trailing whitespace or invalid characters
  nfcJsonData.trim();

  // JSON-Dokument verarbeiten
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, nfcJsonData);
  if (error)
  {
    nfcJsonData = "";
    Serial.println("Fehler beim Verarbeiten des JSON-Dokuments");
    return false;
  }
  else
  {
    if(filamanConnected){
      Serial.println("JSON-Dokument erfolgreich verarbeitet");
      if (doc["sm_id"].is<String>() && doc["sm_id"] != "" && doc["sm_id"] != "0")
      {
        oledShowProgressBar(2, 4, tr(STR_SPOOL_TAG), tr(STR_WEIGHING));
        oledSetPriority(DISPLAY_PRIORITY_ACTION, 2000);
        activeSpoolId = doc["sm_id"].as<String>();
        lastSpoolId = activeSpoolId;
      }

      else if(doc["location_id"].is<int>())
      {
        Serial.println("Location Tag found!");
        int locId = doc["location_id"].as<int>();

        // Check if a spool was scanned before
        if (lastSpoolId.length() == 0 || lastSpoolId == "0") {
          Serial.println("No spool scanned before location tag - showing warning");
          oledShowProgressBar(1, 1, tr(STR_LOCATION), tr(STR_SCAN_SPOOL_FIRST));
          oledSetPriority(DISPLAY_PRIORITY_WARNING, 3000);
        } else {
          int sId = lastSpoolId.toInt();
          sendLocationAsync(sId, "", locId, "");

          // Display feedback - location was set
          oledShowProgressBar(1, 1, tr(STR_LOCATION), tr(STR_LOCATION_SET));
          oledSetPriority(DISPLAY_PRIORITY_ACTION, 3000);

          // Clear lastSpoolId to prevent accidental re-assignment
          lastSpoolId = "";
          Serial.println("Location set - lastSpoolId cleared to prevent re-assignment");
        }

        // Mark as processed so main.cpp doesn't try to send weight
        tagProcessed = true;
        activeSpoolId = "";
      }
      else
      {
        Serial.println("Unbekannter Tag-Inhalt.");
        activeSpoolId = "";
        oledShowProgressBar(1, 1, tr(STR_FAILURE), tr(STR_UNKNOWN_TAG));
        oledSetPriority(DISPLAY_PRIORITY_WARNING, 2000);
      }
    } else {
      oledShowProgressBar(4, 4, tr(STR_FAILURE_EXCL), tr(STR_API_OFFLINE));
      oledSetPriority(DISPLAY_PRIORITY_WARNING, 2000);
    }
  }

  doc.clear();

  return true;
}

// Read complete JSON data for fast-path to enable web interface display
bool readCompleteJsonForFastPath() {
    Serial.println("=== FAST-PATH: Reading complete JSON for web interface ===");

    // Read tag size first
    uint16_t tagSize = readTagSize();
    if (tagSize == 0) {
        Serial.println("FAST-PATH: Could not determine tag size");
        return false;
    }

    // Create buffer for complete data
    uint8_t* data = (uint8_t*)malloc(tagSize);
    if (!data) {
        Serial.println("FAST-PATH: Could not allocate memory for complete read");
        return false;
    }
    memset(data, 0, tagSize);

    // Read all pages
    uint8_t numPages = tagSize / 4;
    for (uint8_t i = 4; i < 4 + numPages; i++) {
        if (!robustPageRead(i, data + (i - 4) * 4)) {
            Serial.printf("FAST-PATH: Failed to read page %d\n", i);
            free(data);
            return false;
        }

        // Check for NDEF message end
        if (data[(i - 4) * 4] == 0xFE) {
            Serial.println("FAST-PATH: Found NDEF message end marker");
            break;
        }

        yield();
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(2));
    }

    // Decode NDEF and extract JSON
    bool success = decodeNdefAndReturnJson(data, ""); // Empty UID string for fast-path

    free(data);

    if (success) {
        Serial.println("✓ FAST-PATH: Complete JSON data successfully loaded");
        Serial.print("nfcJsonData length: ");
        Serial.println(nfcJsonData.length());
    } else {
        Serial.println("✗ FAST-PATH: Failed to decode complete JSON data");
    }

    return success;
}

bool quickSpoolIdCheck(String uidString) {
    // Fast-path: Read NDEF structure to quickly locate and check JSON payload
    // This dramatically speeds up known spool recognition

    // CRITICAL: Do not execute during write operations!
    if (nfcWriteInProgress) {
        Serial.println("FAST-PATH: Skipped during write operation");
        return false;
    }

    Serial.println("=== FAST-PATH: Quick sm_id Check ===");

    // Read enough pages to cover NDEF header + beginning of payload (pages 4-8 = 20 bytes)
    uint8_t ndefData[20];
    memset(ndefData, 0, 20);

    for (uint8_t page = 4; page < 9; page++) {
        if (!robustPageRead(page, ndefData + (page - 4) * 4)) {
            Serial.print("FAST-PATH: Failed to read page ");
            Serial.print(page);
            Serial.println(" - falling back to full read");
            return false; // Fall back to full read if any page read fails
        }
    }

    // Parse NDEF structure to find JSON payload start
    Serial.print("Raw NDEF data (first 20 bytes): ");
    for (int i = 0; i < 20; i++) {
        if (ndefData[i] < 0x10) Serial.print("0");
        Serial.print(ndefData[i], HEX);
        Serial.print(" ");
    }
    Serial.println();

    // Look for NDEF TLV (0x03) at the beginning
    int tlvOffset = -1;
    for (int i = 0; i < 8; i++) {
        if (ndefData[i] == 0x03) {
            tlvOffset = i;
            Serial.print("Found NDEF TLV at offset: ");
            Serial.println(tlvOffset);
            break;
        }
    }

    if (tlvOffset == -1) {
        Serial.println("✗ FAST-PATH: No NDEF TLV found");
        return false;
    }

    // Parse NDEF record to find JSON payload
    int ndefRecordStart;
    if (ndefData[tlvOffset + 1] == 0xFF) {
        // Extended length format
        ndefRecordStart = tlvOffset + 4;
    } else {
        // Standard length format
        ndefRecordStart = tlvOffset + 2;
    }

    if (ndefRecordStart >= 20) {
        Serial.println("✗ FAST-PATH: NDEF record starts beyond read data");
        return false;
    }

    // Parse NDEF record header
    uint8_t recordHeader = ndefData[ndefRecordStart];
    uint8_t typeLength = ndefData[ndefRecordStart + 1];

    // Calculate payload offset
    uint8_t payloadLengthBytes = (recordHeader & 0x10) ? 1 : 4; // SR flag check
    uint8_t idLength = (recordHeader & 0x08) ? ndefData[ndefRecordStart + 2 + payloadLengthBytes + typeLength] : 0; // IL flag check

    int payloadOffset = ndefRecordStart + 1 + 1 + payloadLengthBytes + typeLength + idLength;

    Serial.print("NDEF Record Header: 0x");
    Serial.print(recordHeader, HEX);
    Serial.print(", Type Length: ");
    Serial.print(typeLength);
    Serial.print(", Payload offset: ");
    Serial.println(payloadOffset);

    // Check if payload starts within our read data
    if (payloadOffset >= 20) {
        Serial.println("✗ FAST-PATH: JSON payload starts beyond quick read data - need more pages");

        // Read additional pages to get to JSON payload
        uint8_t extraData[16]; // Read 4 more pages
        memset(extraData, 0, 16);

        for (uint8_t page = 9; page < 13; page++) {
            if (!robustPageRead(page, extraData + (page - 9) * 4)) {
                Serial.print("FAST-PATH: Failed to read additional page ");
                Serial.print(page);
                Serial.println(" - falling back to full read");
                return false; // Fall back to full read if extended read fails
            }
        }

        // Combine data
        uint8_t combinedData[36];
        memcpy(combinedData, ndefData, 20);
        memcpy(combinedData + 20, extraData, 16);

        // Extract JSON from combined data
        String jsonStart = "";
        int jsonStartPos = payloadOffset;
        for (int i = 0; i < 36 - payloadOffset && i < 30; i++) {
            uint8_t currentByte = combinedData[payloadOffset + i];
            if (currentByte >= 32 && currentByte <= 126) {
                jsonStart += (char)currentByte;
            }
            // Stop at first brace to get just the beginning
            if (currentByte == '{' && i > 0) break;
        }

        Serial.print("JSON start from extended read: ");
        Serial.println(jsonStart);

        // Check for sm_id pattern - look for non-zero sm_id values
        if (jsonStart.indexOf("\"sm_id\":\"") >= 0) {
            int smIdStart = jsonStart.indexOf("\"sm_id\":\"") + 9;
            int smIdEnd = jsonStart.indexOf("\"", smIdStart);

            if (smIdEnd > smIdStart && smIdEnd < jsonStart.length()) {
                String quickSpoolId = jsonStart.substring(smIdStart, smIdEnd);
                Serial.print("Found sm_id in extended read: ");
                Serial.println(quickSpoolId);

                // Only process if sm_id is not "0" (known spool)
                if (quickSpoolId != "0" && quickSpoolId.length() > 0) {
                    Serial.println("✓ FAST-PATH: Known spool detected!");

                    // Set as active spool immediately
                    activeSpoolId = quickSpoolId;
                    lastSpoolId = activeSpoolId;

                    // Read complete JSON data for web interface display
                    Serial.println("FAST-PATH: Reading complete JSON data for web interface...");
                    if (readCompleteJsonForFastPath()) {
                        Serial.println("✓ FAST-PATH: Complete JSON data loaded for web interface");
                    } else {
                        Serial.println("⚠ FAST-PATH: Could not read complete JSON, web interface may show limited data");
                    }

                    oledShowProgressBar(2, 4, tr(STR_KNOWN_SPOOL), tr(STR_QUICK_MODE));
                    oledSetPriority(DISPLAY_PRIORITY_ACTION, 1500);
                    Serial.println("✓ FAST-PATH SUCCESS: Known spool processed quickly");
                    return true;
                } else {
                    Serial.println("✗ FAST-PATH: sm_id is 0 - new brand filament, need full read");
                    return false;
                }
            }
        }

        Serial.println("✗ FAST-PATH: No sm_id pattern in extended read");
        return false;
    }

    // Extract JSON payload from the available data
    String quickJson = "";
    for (int i = payloadOffset; i < 20 && i < payloadOffset + 15; i++) {
        uint8_t currentByte = ndefData[i];
        if (currentByte >= 32 && currentByte <= 126) {
            quickJson += (char)currentByte;
        }
    }

    Serial.print("Quick JSON data: ");
    Serial.println(quickJson);

    // Look for sm_id pattern in the beginning of JSON - check for known vs new spools
    if (quickJson.indexOf("\"sm_id\":\"") >= 0) {
        Serial.println("✓ FAST-PATH: sm_id field found");

        // Extract sm_id from quick data
        int smIdStart = quickJson.indexOf("\"sm_id\":\"") + 9;
        int smIdEnd = quickJson.indexOf("\"", smIdStart);

        if (smIdEnd > smIdStart && smIdEnd < quickJson.length()) {
            String quickSpoolId = quickJson.substring(smIdStart, smIdEnd);
            Serial.print("✓ Quick extracted sm_id: ");
            Serial.println(quickSpoolId);

            // Only process known spools (sm_id != "0") via fast path
            if (quickSpoolId != "0" && quickSpoolId.length() > 0) {
                Serial.println("✓ FAST-PATH: Known spool detected!");

                // Set as active spool immediately
                activeSpoolId = quickSpoolId;
                lastSpoolId = activeSpoolId;

                // Read complete JSON data for web interface display
                Serial.println("FAST-PATH: Reading complete JSON data for web interface...");
                if (readCompleteJsonForFastPath()) {
                    Serial.println("✓ FAST-PATH: Complete JSON data loaded for web interface");
                } else {
                    Serial.println("⚠ FAST-PATH: Could not read complete JSON, web interface may show limited data");
                }

                oledShowProgressBar(2, 4, tr(STR_KNOWN_SPOOL), tr(STR_QUICK_MODE));
                oledSetPriority(DISPLAY_PRIORITY_ACTION, 1500);
                Serial.println("✓ FAST-PATH SUCCESS: Known spool processed quickly");
                return true;
            } else {
                Serial.println("✗ FAST-PATH: sm_id is 0 - new brand filament, need full read");
                return false; // sm_id="0" means new brand filament, needs full processing
            }
        } else {
            Serial.println("✗ FAST-PATH: Could not extract complete sm_id value");
            return false; // Need full read to get complete sm_id
        }
    }

    // Check for other patterns that require full read
    if (quickJson.indexOf("\"location\":\"") >= 0) {
        Serial.println("✓ FAST-PATH: Location tag detected");
        return false; // Need full read for location processing
    }

    if (quickJson.indexOf("\"brand\":\"") >= 0) {
        Serial.println("✓ FAST-PATH: Brand filament detected - may need full processing");
        return false; // Need full read for brand filament creation
    }

    Serial.println("✗ FAST-PATH: No recognizable pattern - falling back to full read");
    return false; // Fall back to full tag reading
}

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

        // create Tag UID string
        String uidString = "";
        for (uint8_t i = 0; i < uidLength; i++) {
          //TBD: Rework to remove all the string operations
          if (uid[i] < 0x10) uidString += "0";
          uidString += String(uid[i], HEX);
          if (i < uidLength - 1) {
              uidString += ":"; // Optional: Trennzeichen hinzufügen
          }
        }

        if (uidLength == 7)
        {
          activeTagUuid = uidString;
          // Try fast-path detection first for known spools
          if (quickSpoolIdCheck(uidString)) {
              Serial.println("✓ FAST-PATH: Tag processed quickly, skipping full read");
              // Set reader back to idle for next scan
              nfcReaderState = NFC_READ_SUCCESS;
              vTaskDelay(pdMS_TO_TICKS(500)); // Small delay before next scan
              continue; // Skip full tag reading and continue scan loop
          }

          Serial.println("Continuing with full tag read after fast-path check");

          uint16_t tagSize = readTagSize();
          if(tagSize > 0)
          {
            // Create a buffer depending on the size of the tag
            uint8_t* data = (uint8_t*)malloc(tagSize);
            memset(data, 0, tagSize);

            // We probably have an NTAG2xx card (though it could be Ultralight as well)
            Serial.println("Seems to be an NTAG2xx tag (7 byte UID)");
            Serial.print("Tag size: ");
            Serial.print(tagSize);
            Serial.println(" bytes");

            uint8_t numPages = readTagSize()/4;

            for (uint8_t i = 4; i < 4+numPages; i++) {

              if (!robustPageRead(i, data+(i-4) * 4))
              {
                Serial.printf("Failed to read page %d after retries, stopping\n", i);
                break; // Stop if reading fails after retries
              }

              // Check for NDEF message end
              if (data[(i - 4) * 4] == 0xFE)
              {
                Serial.println("Found NDEF message end marker");
                break; // End of NDEF message
              }

              yield();
              esp_task_wdt_reset();
              // Reduced delay for faster reading
              vTaskDelay(pdMS_TO_TICKS(2)); // Reduced from 5ms to 2ms
            }

            Serial.println("Tag reading completed, starting NDEF decode...");

            if (!decodeNdefAndReturnJson(data, uidString))
            {
              oledShowProgressBar(1, 1, tr(STR_FAILURE), tr(STR_UNKNOWN_TAG));
              oledSetPriority(DISPLAY_PRIORITY_WARNING, 2000);
              nfcReaderState = NFC_READ_ERROR;
            }
            else
            {
              nfcReaderState = NFC_READ_SUCCESS;
            }

            free(data);
          }
          else
          {
            // NTAG reading failed, try reading as Bambu Lab tag
            Serial.println("NTAG read failed, trying Bambu Lab tag...");
            if (!detectBambuTag(uid, uidLength)) {
                oledShowProgressBar(1, 1, tr(STR_FAILURE), tr(STR_TAG_READ_ERROR));
                oledSetPriority(DISPLAY_PRIORITY_WARNING, 2000);
                nfcReaderState = NFC_READ_ERROR;
                activeSpoolId = "";
                Serial.println("Tag read failed - activeSpoolId reset to prevent autoSet");
            }
          }
        }
        else
        {
          // UID length != 7, might be a Mifare Classic (Bambu tags)
          Serial.println("Not a standard NTAG (UID length != 7), trying Bambu Lab tag...");
          if (!detectBambuTag(uid, uidLength)) {
            //TBD: Show error here?!
            oledShowProgressBar(1, 1, tr(STR_FAILURE), tr(STR_UNKNOWN_TAG_TYPE));
            oledSetPriority(DISPLAY_PRIORITY_WARNING, 2000);
            Serial.println("This doesn't seem to be an NTAG2xx tag (UUID length != 7 bytes)!");
            // Reset activeSpoolId when tag type is unknown to prevent autoSet
            activeSpoolId = "";
            Serial.println("Unknown tag type - activeSpoolId reset to prevent autoSet");
          }
        }
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
