# Bambuddy Scale Firmware (Based on FilaMan)

⚠️ **Important: This fork of the FilaMan project has been exclusively modified to communicate directly with a local [Bambuddy](https://github.com/bambuddy) instance.**

This firmware uses ESP32 hardware for weight measurement and NFC tag identification.
When a spool is scanned, the device automatically sends the weight to the Bambuddy API or creates unknown spools as "Auto-gen" in the database.

![Scale](./img/scale_trans.png)


More Images can be found in the [img Folder](/img/)
or my website: [FilaMan Website](https://www.filaman.app)
german explanatory video: [Youtube](https://youtu.be/uNDe2wh9SS8?si=b-jYx4I1w62zaOHU)
Discord Server: [https://discord.gg/my7Gvaxj2v](https://discord.gg/my7Gvaxj2v)

### ESP32 Hardware Features
- **Weight Measurement:** Using a load cell with HX711 amplifier for precise weight tracking.
- **NFC Tag Reading:** PN532 module for reading the hardware UID of NFC tags. (No writing required!)
- **OLED Display:** Shows current weight and connection status.
- **WiFi Connectivity:** WiFiManager for easy network configuration.
- **Any NFC Tags:** Any NFC tag or sticker can be used, as only the hardware UID serves as a reference for Bambuddy.

### Web Interface Features
- **Real-time Updates:** WebSocket connection for live data updates.
- **Bambuddy Integration:**
  - Synchronize spool data with the Bambuddy server.
  - Update spool weights automatically.

### If you want to support my work, i would be happy to get a coffe

<a href="https://www.buymeacoffee.com/manuelw" target="_blank"><img src="https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png" alt="Buy Me A Coffee" style="height: 30px !important;width: 108px !important;" ></a>

## Detailed Functionality

### ESP32 Functionality
- **User Interactions:** The OLED display provides immediate feedback on the system status, including weight measurements and connection status.

### Web Interface Functionality
- **User Interactions:** The web interface allows users to interact with the system, configure the device, and monitor status.
- **UI Elements:** Includes forms for registration, buttons for scale actions, and real-time status indicators.

## Hardware Requirements

### Components (Affiliate Links)
- **ESP32 Development Board:** Any ESP32 variant.
[Amazon Link](https://amzn.to/3FHea6D)
- **HX711 5kg Load Cell Amplifier:** For weight measurement.
[Amazon Link](https://amzn.to/4ja1KTe)
- **OLED 0.96 Zoll I2C white/yellow Display:** 128x64 SSD1306.
[Amazon Link](https://amzn.to/445aaa9)
- **PN532 NFC NXP RFID-Modul V3:** For NFC tag operations.
[Amazon Link](https://amzn.eu/d/gy9vaBX)
- **NFC Tags NTAG213 NTAG215:** RFID Tag
[Amazon Link](https://amzn.to/3E071xO)
- **TTP223 Touch Sensor (optional):** For reTARE per Button/Touch
[Amazon Link](https://amzn.to/4hTChMK)


### Pin Configuration
| Component          | ESP32 Pin |
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

**!! Make sure that the DIP switches on the PN532 are set to I2C**
**Use the 3V pin from the ESP for the touch sensor**

![Wiring](./img/Schaltplan.png)

![myWiring](./img/IMG_2589.jpeg)
![myWiring](./img/IMG_2590.jpeg)

*The load cell is connected to most HX711 modules as follows:
E+ red
E- black
A- white
A+ green*

## Software Dependencies

### ESP32 Libraries
- `WiFiManager`: Network configuration
- `ESPAsyncWebServer`: Web server functionality
- `ArduinoJson`: JSON parsing and creation
- `Adafruit_PN532`: NFC functionality
- `Adafruit_SSD1306`: OLED display control
- `HX711`: Load cell communication

### Installation

## Prerequisites
- **Software:**
  - [PlatformIO](https://platformio.org/) in VS Code
  - Local Bambuddy instance
- **Hardware:**
  - ESP32 Development Board
  - HX711 Load Cell Amplifier
  - Load Cell (weight sensor)
  - OLED Display (128x64 SSD1306)
  - PN532 NFC Module
  - Connecting wires


### Step-by-Step Installation
### Compile and Flash
1. **Clone the Repository:**
    ```bash
    git clone https://github.com/<YOUR_GITHUB_NAME>/Filaman-System-esp32.git
    cd Filaman-System-esp32
    ```
2. **Install Dependencies:**
    ```bash
    pio lib install
    ```
3. **Flash the ESP32 firmware and filesystem:**
    ```bash
    pio run --target upload
    pio run --target uploadfs
    ```
4. **Initial Setup:**
    - Connect to the "BambuddyScale" WiFi access point.
    - Configure WiFi settings through the captive portal.
    - Access the web interface at `http://BambuddyScale.local` or the IP address.

## Documentation

### Relevant Links
- [Bambuddy](https://github.com/bambuddy)
- [PlatformIO Documentation](https://docs.platformio.org/)

### Tutorials and Examples
- [PlatformIO Getting Started](https://docs.platformio.org/en/latest/tutorials/espressif32/arduino_debugging_unit_testing.html)
- [ESP32 Web Server Tutorial](https://randomnerdtutorials.com/esp32-web-server-arduino-ide/)

## License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for details.

## Materials

### Useful Resources
- [ESP32 Official Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/)
- [Arduino Libraries](https://www.arduino.cc/en/Reference/Libraries)
- [NFC Tag Information](https://learn.adafruit.com/adafruit-pn532-rfid-nfc/overview)

### Community and Support
- [PlatformIO Community](https://community.platformio.org/)
- [Arduino Forum](https://forum.arduino.cc/)
- [ESP32 Forum](https://www.esp32.com/)

## Availability

The code can be tested and the application can be downloaded from the [GitHub repository](https://github.com/ManuelW77/Filaman-System-esp32).

### If you want to support my work, i would be happy to get a coffe
<a href="https://www.buymeacoffee.com/manuelw" target="_blank"><img src="https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png" alt="Buy Me A Coffee" style="height: 30px !important;width: 108px !important;" ></a>
