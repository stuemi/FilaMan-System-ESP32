#include "commonFS.h"
#include <LittleFS.h>

String readFile(const char* filename) {
    File file = LittleFS.open(filename, "r");
    if (!file) {
        Serial.print("Fehler beim Öffnen der Datei: ");
        Serial.println(filename);
        return "";
    }
    
    // Effizient: Speicher vorab allokieren und in Blöcken lesen
    // Alte Implementierung war O(n²) wegen Byte-für-Byte String-Konkatenation
    size_t fileSize = file.size();
    String content;
    content.reserve(fileSize + 1);  // Speicher vorab allokieren
    
    // Lese in 512-Byte Blöcken statt Byte für Byte
    char buffer[512];
    while (file.available()) {
        size_t bytesRead = file.readBytes(buffer, sizeof(buffer));
        content.concat(buffer, bytesRead);
    }
    
    file.close();
    return content;
}

void initializeFileSystem() {
    if (!LittleFS.begin(true)) {
        Serial.println("LittleFS Mount Failed");
        return;
    }
    Serial.printf("LittleFS Total: %u bytes\n", LittleFS.totalBytes());
    Serial.printf("LittleFS Used: %u bytes\n", LittleFS.usedBytes());
    Serial.printf("LittleFS Free: %u bytes\n", LittleFS.totalBytes() - LittleFS.usedBytes());
}