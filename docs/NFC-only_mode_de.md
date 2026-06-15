# NFC-only Modus

## Beschreibung
Es kann praktisch sein, ein kleines Gerät in der Nähe deines Desktop-Computers zu haben, wenn du neue Spulen in deine Bambuddy-Datenbank hinzufügst.
Da keine Waage erforderlich ist (das Gewicht einer neuen Spule ist in der Regel bekannt), kannst du auf die Waagen-Komponenten verzichten, um einen kleineren Formfaktor zu erreichen. Du scannst einfach den Tag, und Bambuddy legt die Spule automatisch an.

![FilaLite_no_scale](../img/FilaLite_no_scale.png)

## Bedienung

Bevor du das NFC-only Terminal verwenden kannst, musst du in der Web-Oberfläche die Bambuddy URL und deinen API-Key hinterlegen.

### Tag lesen (Auto-gen)
Das Gerät liest ausschließlich die UID des Tags. Wenn du eine neue Spule scannst, wird diese vollautomatisch über die Bambuddy-API in deinem System angelegt. Das Schreiben von Tags ist in dieser angepassten Bambuddy-Firmware nicht mehr notwendig!

## NFC-only Modus

Beim Start versucht der ESP32, den HX711 (Verstärker) zusammen mit einem angeschlossenen Loadcell-Sensor zu erkennen. Wenn das gelingt, startet er im Normalmodus mit voller Funktionalität.

Andernfalls schaltet er in den **NFC-only Modus**!

Statt einer Gewichtsanzeige zeigt das Display eine Bereitschaftsmeldung. Zusätzlich ist das Waagensymbol in der oberen Zeile durchgestrichen.

![Ready Prompt](../img/no_scale_ready.png)

## Aufbau / Installation
### Hardware
Die Verkabelung der Hardware ist gleich wie bei der originalen, voll ausgestatteten FilaMan-Waage. Es gibt nur 3 Unterschiede:

- Kein HX711 angeschlossen (und natürlich keine Loadcell)
- Kein Tastensensor (ohne Waage ist kein Tarieren nötig)
- optional: 10k Pull-up-Widerstand an Pin 16 (RX2)

Der Pull-up-Widerstand ist nicht zwingend erforderlich, erhöht aber die Zuverlässigkeit beim Erkennen eines fehlenden HX711. Ohne Pull-up-Widerstand sollte es aber ebenfalls funktionieren.

![Schaltplan mit Pull-Up](../img/Schaltplan%20NFC-only%20pullup.png)

| Komponente    | ESP32 Pin |
| ----         | ----      |
| 10k Pull-up  | 16        |
|              | 17        |
| OLED SDA     | 21        |
| OLED SCL     | 22        |
| PN532 IRQ    | 32        |
| PN532 RESET  | 33        |
| PN532 SDA    | 21        |
| PN532 SCL    | 22        |

#### PN532
- **!! Stelle sicher, dass die DIP-Schalter des PN532 auf I2C gestellt sind**
- Verbinde `VCC` mit 5V

#### OLED
- Verbinde `VCC` mit 3,3V

#### 10k Widerstand (optional)
- Verbinde den Widerstand mit PIN 16 (wo normalerweise der DOUT des HX711 angeschlossen wäre) und 5V

### Software
Folge den Anweisungen in der [Hauptdokumentation](../README.de.md#schritt-für-schritt-installation).

Du benötigst Version 3.3.1 oder neuer für eine funktionierende Hardware-Erkennung und den NFC-only Modus.
