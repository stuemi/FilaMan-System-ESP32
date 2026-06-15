# FilaMan & Bambuddy Integration Fahrplan (Roadmap)

## Ziel
Die FilaMan ESP32 Firmware (C++) so anpassen, dass Spulengewichte in Bambuddy automatisch aktualisiert oder neue Spulen als "Auto-gen" angelegt werden. Es wird kein separates Kiosk-Display verwendet.
Es soll also die Hardware von Filaman genutzt werden und die gemessenen Daten entsprechend an Bambuddy per Rest API weitergeben werden

## Globale Regeln
- **Code-Kommentare:** Alle neuen Kommentare im C++ Code müssen auf Deutsch sein.
- **Git Commits:** Alle Commit-Nachrichten müssen auf Deutsch sein.
- API_Schlüssel von Bambuddy lautet: bb_Kvg_PtfDk0UAZnYx68z8B5PrMdJ4m2XxAD8DmbDRN8g
- **NFC Write-Logik:** Die originale FilaMan "Tag-Schreib-Logik" wird in diesem Projekt absichtlich entfernt, da Bambuddy als "Read-Only" System bezüglich der NFC-Tags fungiert (es wird nur die Hardware-UID gelesen).

## Workflow 1: Bekannte Spule Gewicht aktualisieren
1. Spule auf Waage auflegen -> NFC-Tag scannen -> `tag_uid` auslesen.
2. API GET: `/api/v1/inventory/spools?tag_uid=<UID>`
3. Treffer gefunden: `spool_id` aus der Antwort extrahieren.
4. API POST: `/api/v1/spoolbuddy/scale/update-spool-weight`
   - Payload: `{"spool_id": <ID>, "weight": <Aktuelles_Gewicht>}`

## Workflow 2: Unbekannte Spule
1. NFC-Tag scannen -> `tag_uid` auslesen.
2. API GET: `/api/v1/inventory/spools?tag_uid=<UID>` -> Kein Treffer.
3. API POST (Neu anlegen): `/api/v1/inventory/spools`
   - Payload: `{"tag_uid": "<UID>", "material": "Auto-gen"}`
4. Das tatsächliche Material und die Farbe werden später manuell in der Bambuddy PC-Oberfläche angepasst.

## Aktueller Status
- [x] Repository geforkt und in VS Code geklont.
- [x] Projekt-Fahrplan und Kontext-Datei erstellt.
- [x] Die relevanten C++ Dateien finden, die NFC-Scans und API-Anfragen verarbeiten.
- [x] Den GET-Request (die Logik-Weiche) implementieren.
- [x] Den POST-Request für Workflow 1 implementieren.
- [x] Den POST-Request für Workflow 2 implementieren.
- [x] Code für `api.h` und `api.cpp` schreiben und bereinigen.
- [x] Code für `main.cpp` anpassen (Weiche nutzen, Schreib-Logik entfernen).
- [ ] Auf den ESP32 flashen und testen.

## Geplante Datei-Änderungen (Architektur-Umbau)

### 1. `src/api.h`
- **Was geändert wird:** Entfernen alter FilaMan-Endpunkte (`sendLocation`, `sendRfidResult`). `sendWeight` wird umbenannt zu `syncBambuddySpool(String tagUuid, float weight)`.
- **Warum:** Anpassung an die verschlankte Bambuddy-Architektur.

### 2. `src/api.cpp`
- **Was geändert wird:** Bambuddy API-Key fest einbinden. Dummy-Funktionen für FilaMan-Kompatibilität (Registrierung, Heartbeat) erstellen. Die "Logik-Weiche" (`syncBambuddySpool`) als Kernstück implementieren.
- **Warum:** Hier findet die eigentliche Übersetzung von FilaMan zu Bambuddy statt. Das asynchrone Queue-System bleibt erhalten.

### 3. `src/main.cpp`
- **Was geändert wird:** NFC-Lese-Logik bereinigen (Unterscheidung zwischen Bambu/NTAG mit spool_id entfernen, da nur noch `tag_uid` relevant ist). Aufruf der API-Queue auf die neuen Parameter anpassen. Komplettes Entfernen der "Tag-Schreiben"-Logikblöcke.
- **Warum:** Da Bambuddy die Tags nur als "Nummernschild" erkennt, wird der Code massiv verschlankt und robuster gegen Fehler gemacht.