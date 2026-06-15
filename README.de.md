# Bambuddy Scale Firmware (Basis: FilaMan)

⚠️ **Wichtig: Dieser Fork des FilaMan-Projekts wurde exklusiv modifiziert, um direkt mit einer lokalen [Bambuddy](https://github.com/bambuddy)-Instanz zu kommunizieren.**

Diese Firmware nutzt ESP32-Hardware zur Gewichtsmessung und Identifizierung von Filament-Spulen via NFC.
Wird eine Spule gescannt, sendet das Gerät das Gewicht automatisch an die Bambuddy-API oder legt unbekannte Spulen vollautomatisch als "Auto-gen" in der Datenbank an.

![Waage](./img/scale_trans.png)

Weitere Bilder finden Sie im [img Ordner](/img/)
oder auf meiner Website: [FilaMan Website](https://www.filaman.app)
Deutsches Erklärvideo: [Youtube](https://youtu.be/uNDe2wh9SS8?si=b-jYx4I1w62zaOHU)
Discord Server: [https://discord.gg/my7Gvaxj2v](https://discord.gg/my7Gvaxj2v)

### ESP32 Hardware-Features
- **Gewichtsmessung:** Verwendung einer Wägezelle mit HX711-Verstärker für präzises Gewichts-Tracking.
- **NFC-Tag Scannen:** PN532-Modul zum Lesen der Hardware-UID von NFC-Tags. (Kein Beschreiben notwendig!)
- **OLED-Display:** Zeigt aktuelles Gewicht und Verbindungsstatus an.
- **WiFi-Konnektivität:** WiFiManager für einfache Netzwerkkonfiguration.
- **Beliebige NFC-Tags:** Jeder NFC-Tag oder Aufkleber kann verwendet werden, da nur die Hardware-UID als Referenz für Bambuddy dient.

### Weboberflächen-Features
- **Echtzeit-Updates:** WebSocket-Verbindung für Live-Datenaktualisierungen.
- **Bambuddy Integration:**
  - Synchronisierung der Spulendaten mit dem Bambuddy-Server.
  - Automatische Aktualisierung der Spulengewichte.

## Detaillierte Funktionalität

### ESP32-Funktionalität
- **Benutzerinteraktionen:** Das OLED-Display bietet sofortiges Feedback zum Systemstatus, einschließlich Gewichtsmessungen und Verbindungsstatus.

### Weboberflächen-Funktionalität
- **Benutzerinteraktionen:** Die Weboberfläche ermöglicht es Benutzern, mit dem System zu interagieren, das Gerät zu konfigurieren und den Status zu überwachen.
- **UI-Elemente:** Enthält Formulare zur Registrierung, Schaltflächen für Waagenaktionen und Echtzeit-Statusanzeigen.

## Hardware-Anforderungen

### Komponenten (Affiliate-Links)
- **ESP32 Development Board:** Jede ESP32-Variante.
[Amazon Link](https://amzn.to/3FHea6D)
- **HX711 5kg Wägezellenverstärker:** Für die Gewichtsmessung.
[Amazon Link](https://amzn.to/4ja1KTe)
- **OLED 0,96 Zoll I2C weiß/gelbes Display:** 128x64 SSD1306.
[Amazon Link](https://amzn.to/445aaa9)
- **PN532 NFC NXP RFID-Modul V3:** Für NFC-Tag-Operationen.
[Amazon Link](https://amzn.eu/d/gy9vaBX)
- **NFC-Tags NTAG213 NTAG215:** RFID-Tag.
[Amazon Link](https://amzn.to/3E071xO)
- **TTP223 Touch-Sensor (optional):** Für TARA per Knopfdruck/Berührung.
[Amazon Link](https://amzn.to/4hTChMK)


### Pin-Konfiguration
| Komponente         | ESP32 Pin |
|-------------------|-----------|
| HX711 DOUT        | 16        |
| HX711 SCK         | 17        |
| OLED SDA          | 21        |
| OLED SCL          | 22        |
| PN532 IRQ         | 32        |
| PN532 RESET       | 33        |
| PN532 SDA         | 21        |
| PN532 SCL         | 22        |
| TTP223 I/O        | 25        |

**!! Stellen Sie sicher, dass die DIP-Schalter am PN532 auf I2C eingestellt sind.**
**Verwenden Sie den 3V-Pin des ESP für den Touch-Sensor.**

![Verkabelung](./img/Schaltplan.png)

![meineVerkabelung](./img/IMG_2589.jpeg)
![meineVerkabelung](./img/IMG_2590.jpeg)

*Die Wägezelle wird an die meisten HX711-Module wie folgt angeschlossen:
E+ rot
E- schwarz
A- weiß
A+ grün*

## Software-Abhängigkeiten

### ESP32-Bibliotheken
- `WiFiManager`: Netzwerkkonfiguration
- `ESPAsyncWebServer`: Webserver-Funktionalität
- `ArduinoJson`: JSON-Parsing und -Erstellung
- `Adafruit_PN532`: NFC-Funktionalität
- `Adafruit_SSD1306`: OLED-Display-Steuerung
- `HX711`: Kommunikation mit der Wägezelle

### Installation

## Voraussetzungen
- **Software:**
  - [PlatformIO](https://platformio.org/) in VS Code
  - Lokale Bambuddy Instanz
- **Hardware:**
  - ESP32 Development Board
  - HX711 Wägezellenverstärker
  - Wägezelle (Gewichtssensor)
  - OLED-Display (128x64 SSD1306)
  - PN532 NFC-Modul
  - Verbindungskabel


### Schritt-für-Schritt-Installation
### Kompilieren und Flashen
1. **Repository klonen:**
    ```bash
    git clone https://github.com/<DEIN_GITHUB_NAME>/Filaman-System-esp32.git
    cd Filaman-System-esp32
    ```
2. **Abhängigkeiten installieren:**
    ```bash
    pio lib install
    ```
3. **ESP32 Firmware und Dateisystem flashen:**
    ```bash
    pio run --target upload
    pio run --target uploadfs
    ```
4. **Ersteinrichtung:**
    - Verbinden Sie sich mit dem "BambuddyScale" WiFi-Access-Point.
    - Konfigurieren Sie die WiFi-Einstellungen über das Captive Portal.
    - Greifen Sie über `http://BambuddyScale.local` oder die IP-Adresse auf die Weboberfläche zu.

## Dokumentation

### Relevante Links
- [Bambuddy](https://github.com/bambuddy)
- [PlatformIO Dokumentation](https://docs.platformio.org/)

### Tutorials und Beispiele
- [PlatformIO Erste Schritte](https://docs.platformio.org/en/latest/tutorials/espressif32/arduino_debugging_unit_testing.html)
- [ESP32 Web Server Tutorial](https://randomnerdtutorials.com/esp32-web-server-arduino-ide/)

## Lizenz

Dieses Projekt steht unter der MIT-Lizenz. Weitere Informationen finden Sie in der Datei [LICENSE](LICENSE).

## Materialien

### Nützliche Ressourcen
- [Offizielle ESP32-Dokumentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/)
- [Arduino Bibliotheken](https://www.arduino.cc/en/Reference/Libraries)
- [NFC-Tag Informationen](https://learn.adafruit.com/adafruit-pn532-rfid-nfc/overview)

### Community und Support
- [PlatformIO Community](https://community.platformio.org/)
- [Arduino Forum](https://forum.arduino.cc/)
- [ESP32 Forum](https://www.esp32.com/)

## Verfügbarkeit

Der Code kann getestet und die Anwendung vom [GitHub-Repository](https://github.com/ManuelW77/Filaman-System-esp32) heruntergeladen werden.

### Wenn Sie meine Arbeit unterstützen möchten, freue ich mich über einen Kaffee
<a href="https://www.buymeacoffee.com/manuelw" target="_blank"><img src="https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png" alt="Buy Me A Coffee" style="height: 30px !important;width: 108px !important;" ></a>
