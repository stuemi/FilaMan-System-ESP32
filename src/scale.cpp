#include "nfc.h"
#include "scale.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include "config.h"
#include "HX711.h"
#include "display.h"
#include "esp_task_wdt.h"
#include <Preferences.h>
#include "lang.h"

HX711 scale;

TaskHandle_t ScaleTask;

int16_t weight = 0;

// Weight stabilization variables
#define MOVING_AVERAGE_SIZE 12          // Erhöht: Glättet mehr, bleibt aber reaktionsschnell.
#define LOW_PASS_ALPHA 0.2f             // Reduziert: Macht den Filter stabiler gegen Rauschen.
#define DISPLAY_THRESHOLD 1.0f          // Erhöht: Verhindert das "Flackern" der Anzeige bei kleinen Schwankungen.
#define API_THRESHOLD 1.5f              // Beibehalten: Löst API-Aktionen weiterhin schnell aus.
#define MEASUREMENT_INTERVAL_MS 30      // Beibehalten: Schnelle Messungen für genügend Datenpunkte.

float weightBuffer[MOVING_AVERAGE_SIZE];
uint8_t bufferIndex = 0;
bool bufferFilled = false;
float filteredWeight = 0.0f;
int16_t lastDisplayedWeight = 0;
int16_t lastStableWeight = 0;        // For API/action triggering
unsigned long lastMeasurementTime = 0;

uint8_t weightCounterToApi = 0;
uint8_t scale_tare_counter = 0;
bool scaleTareRequest = false;
uint8_t pauseMainTask = 0;
bool scaleCalibrated;
bool scaleConnected;
bool autoTare = true;
bool scaleCalibrationActive = false;
volatile bool scaleCalibrationRequest = false;

/**
 * Non blocking wait function
 */
void friendlyWait(uint16_t ticks) {
    for (uint16_t i = 0; i < ticks; i++) {
    yield();
    vTaskDelay(pdMS_TO_TICKS(1));
    esp_task_wdt_reset();
  }
}

// ##### Weight stabilization functions #####

/**
 * Reset weight filter buffer - call after tare or calibration
 */
void resetWeightFilter() {
  bufferIndex = 0;
  bufferFilled = false;
  filteredWeight = 0.0f;
  lastDisplayedWeight = 0;
  lastStableWeight = 0;            // Reset stable weight for API actions

  // Initialize buffer with zeros
  for (int i = 0; i < MOVING_AVERAGE_SIZE; i++) {
    weightBuffer[i] = 0.0f;
  }
}

/**
 * Calculate moving average from weight buffer
 */
float calculateMovingAverage() {
  float sum = 0.0f;
  int count = bufferFilled ? MOVING_AVERAGE_SIZE : bufferIndex;

  for (int i = 0; i < count; i++) {
    sum += weightBuffer[i];
  }

  return (count > 0) ? sum / count : 0.0f;
}

/**
 * Apply low-pass filter to smooth weight readings
 * Uses exponential smoothing: y_new = alpha * x_new + (1-alpha) * y_old
 */
float applyLowPassFilter(float newValue) {
  filteredWeight = LOW_PASS_ALPHA * newValue + (1.0f - LOW_PASS_ALPHA) * filteredWeight;
  return filteredWeight;
}

/**
 * Process new weight reading with stabilization
 * Returns stabilized weight value
 */
int16_t processWeightReading(float rawWeight) {
  // Add to moving average buffer
  weightBuffer[bufferIndex] = rawWeight;
  bufferIndex = (bufferIndex + 1) % MOVING_AVERAGE_SIZE;

  if (bufferIndex == 0) {
    bufferFilled = true;
  }

  // Calculate moving average
  float avgWeight = calculateMovingAverage();

  // Apply low-pass filter
  float smoothedWeight = applyLowPassFilter(avgWeight);

  // Round to nearest gram
  int16_t newWeight = round(smoothedWeight);

  // Update displayed weight if display threshold is reached
  if (abs(newWeight - lastDisplayedWeight) >= DISPLAY_THRESHOLD) {
    lastDisplayedWeight = newWeight;
  }

  // Update global weight for API actions only if stable threshold is reached
  int16_t weightToReturn = weight; // Default: keep current weight

  if (abs(newWeight - lastStableWeight) >= API_THRESHOLD) {
    lastStableWeight = newWeight;
    weightToReturn = newWeight;
  }

  return weightToReturn;
}

/**
 * Get current filtered weight for display purposes
 * This returns the smoothed weight even if it hasn't triggered API actions
 */
int16_t getFilteredDisplayWeight() {
  return lastDisplayedWeight;
}

// ##### Funktionen für Waage #####
uint8_t setAutoTare(bool autoTareValue) {
  Serial.print("Set AutoTare to ");
  Serial.println(autoTareValue);
  autoTare = autoTareValue;

  // Speichern mit NVS
  Preferences preferences;
  preferences.begin(NVS_NAMESPACE_SCALE, false); // false = readwrite
  preferences.putBool(NVS_KEY_AUTOTARE, autoTare);
  preferences.end();

  return 1;
}

uint8_t tareScale() {
  Serial.println("Tare scale");
  scale.tare();
  resetWeightFilter();

  return 1;
}

void scale_loop(void * parameter) {
  Serial.println("++++++++++++++++++++++++++++++");
  Serial.println("Scale Loop started");
  Serial.println("++++++++++++++++++++++++++++++");

  //scaleTareRequest == true;
  // Initialize weight filter
  resetWeightFilter();
  lastMeasurementTime = millis();

  for(;;) {
    unsigned long currentTime = millis();

    // Only measure at defined intervals to reduce noise
    if (currentTime - lastMeasurementTime >= MEASUREMENT_INTERVAL_MS) {
      if (scale.is_ready())
      {
        // Waage manuell Taren
        if (scaleTareRequest == true || (autoTare && scale_tare_counter >= 20))
        {
          Serial.println("Re-Tare scale");
          oledDisplayText(tr(STR_TARE_SCALE));
          vTaskDelay(pdMS_TO_TICKS(1000));
          scale.tare();
          resetWeightFilter(); // Reset filter after manual tare
          vTaskDelay(pdMS_TO_TICKS(1000));
          oledShowWeight(0);
          scaleTareRequest = false;
          scale_tare_counter = 0;
          weight = 0; // Reset global weight variable after tare
        }

        // Check for calibration request
        if (scaleCalibrationRequest) {
            scaleCalibrationRequest = false;
            calibrate_scale();
        }

        // Get raw weight reading
        float rawWeight = scale.get_units();

        // Process weight with stabilization
        int16_t stabilizedWeight = processWeightReading(rawWeight);

        // Update global weight variable only if it changed significantly (for API actions)
        if (stabilizedWeight != weight) {
          weight = stabilizedWeight;
          oledResetActivityTimer(); // Wake display on weight change
        }

        // Prüfen ob die Waage korrekt genullt ist
        // Abweichung von 2g ignorieren
        if (autoTare && (rawWeight > 2 && rawWeight < 7) || rawWeight < -2)
        {
          scale_tare_counter++;
        }
        else
        {
          scale_tare_counter = 0;
        }

        // Debug output for monitoring (can be removed in production)
        static unsigned long lastDebugTime = 0;
        if (currentTime - lastDebugTime > 2000) { // Print every 2 seconds
          lastDebugTime = currentTime;
        }

        lastMeasurementTime = currentTime;
      }
    }

    vTaskDelay(pdMS_TO_TICKS(10)); // Shorter delay for more responsive loop
  }
}

long readHX711Raw() {
  long value = 0;

  for (int i = 0; i < 24; i++) {
    digitalWrite(LOADCELL_SCK_PIN, HIGH);
    delayMicroseconds(1);

    value = (value << 1) | digitalRead(LOADCELL_DOUT_PIN);

    digitalWrite(LOADCELL_SCK_PIN, LOW);
    delayMicroseconds(1);
  }

  // Gain = 128 (1 extra pulse)
  digitalWrite(LOADCELL_SCK_PIN, HIGH);
  delayMicroseconds(1);
  digitalWrite(LOADCELL_SCK_PIN, LOW);

  return value;
}

bool deepSearchScale() {
  // without pull-up resistor (10k) on LOADCELL_DOUT_PIN the hx711 lib functions
  // are not reliable in detecting a missing chip
  // these tests work even without pull-up resistor
  const uint32_t timeout = 1000;
  const int samples = 10;

  pinMode(LOADCELL_DOUT_PIN, INPUT);
  pinMode(LOADCELL_SCK_PIN, OUTPUT);
  digitalWrite(LOADCELL_SCK_PIN, LOW);

  //delay(500);
  friendlyWait(500);

  // 1. HX711 vorhanden?
  uint32_t start = millis();
  while (digitalRead(LOADCELL_DOUT_PIN) == HIGH) {
    if (millis() - start > timeout) {
      Serial.println("HX711 not found (DOUT stays HIGH).");
      return false;
    }
  }

  Serial.println("HX711 seems to answer...");

  // 2. Mehrere Werte lesen
  long values[samples];

  for (int i = 0; i < samples; i++) {
    // warten bis bereit
    uint32_t t = millis();
    while (digitalRead(LOADCELL_DOUT_PIN) == HIGH) {
      if (millis() - t > timeout) {
        Serial.println("HX711 not found (Timeout during read).");
        return false;
      }
    }

    values[i] = readHX711Raw();
    delay(50);
  }

  // 3. Analyse
  int invalidCount = 0;

  for (int i = 0; i < samples; i++) {
    if (values[i] == 0 || values[i] == 0xFFFFFF) {
      invalidCount++;
    }
  }

  // 4. Entscheidungen

  // Kein Sensor
  if (invalidCount > samples / 2) {
    Serial.println("HX711 not ready (Loadcell missing).");
    return false;
  }

  Serial.println("HX711 found.");
  return true;
}

bool scaleDetected(){
  // Step 1: use hx711 library functions (need pull-up resistor to recognize missing hx711)
  if ( !(scale.wait_ready_timeout(1000))) {
    Serial.println("HX711 not found (pull-up resistor installed).");
    return false;
  }

  //Step 2: when pull-up resistor is missing, then deep search...
  return deepSearchScale();

}

void start_scale(bool touchSensorConnected) {
  Serial.println("Prüfe Calibration Value");
  float calibrationValue;

  // NVS lesen
  Preferences preferences;
  preferences.begin(NVS_NAMESPACE_SCALE, true); // true = readonly
  if(preferences.isKey(NVS_KEY_CALIBRATION)){
    calibrationValue = preferences.getFloat(NVS_KEY_CALIBRATION);
    scaleCalibrated = true;
  }else{
    calibrationValue = SCALE_DEFAULT_CALIBRATION_VALUE;
    scaleCalibrated = false;
  }

  // auto Tare
  // Wenn Touch Sensor verbunden, dann autoTare auf false setzen
  // Danach prüfen was in NVS gespeichert ist
  autoTare = (touchSensorConnected) ? false : true;
  autoTare = preferences.getBool(NVS_KEY_AUTOTARE, autoTare);

  preferences.end();

  Serial.print("Read Scale Calibration Value ");
  Serial.println(calibrationValue);

  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);

  oledShowProgressBar(5, NUM_SETUP_STEPS, DISPLAY_BOOT_TEXT, tr(STR_SEARCHING_SCALE));
  friendlyWait(3000);

  scaleConnected = scaleDetected();
  if ( scaleConnected ) {
    scale.set_scale(calibrationValue);
    //vTaskDelay(pdMS_TO_TICKS(5000));

    // Initialize weight stabilization filter
    resetWeightFilter();

    // Display Gewicht
    oledShowWeight(0);

    Serial.println("starte Scale Task");
    BaseType_t result = xTaskCreatePinnedToCore(
      scale_loop, /* Function to implement the task */
      "ScaleLoop", /* Name of the task */
      2048,  /* Stack size in words */
      NULL,  /* Task input parameter */
      scaleTaskPrio,  /* Priority of the task */
      &ScaleTask,  /* Task handle. */
      scaleTaskCore); /* Core where the task should run */

    if (result != pdPASS) {
        Serial.println("Fehler beim Erstellen des ScaleLoop-Tasks");
    } else {
        Serial.println("ScaleLoop-Task erfolgreich erstellt");
    }
  }
  else {
    // No HX711 found - scale mode is required for Bambuddy
    Serial.println("Kann kein HX711 Board finden !");            // Sende Text "Kann kein..." an seriellen Monitor
    oledDisplayText(tr(STR_HX711_NOT_FOUND));
    vTaskDelay(pdMS_TO_TICKS(1500));
  }
}

uint8_t calibrate_scale() {
  uint8_t returnState = 0;
  float calibrationFactor;

  scaleCalibrationActive = true;

  if (RfidReaderTask != NULL) vTaskSuspend(RfidReaderTask);
  // Do not suspend ScaleTask if we are running inside it
  if (ScaleTask != NULL && xTaskGetCurrentTaskHandle() != ScaleTask) vTaskSuspend(ScaleTask);

  pauseMainTask = 1;

  if (scale.wait_ready_timeout(1000))
  {

    // Schritt 1: Waage leeren und tarieren
    oledShowProgressBar(0, 5, tr(STR_SCALE_CAL), tr(STR_EMPTY_SCALE));
    friendlyWait(3000);
    scale.tare();
    Serial.println("Tare done...");

    // Schritt 2: Erstes bekanntes Gewicht auflegen (z.B. 500g)
    oledShowProgressBar(1, 5, tr(STR_SCALE_CAL), "1. Gewicht (500g)");
    friendlyWait(5000);
    long reading1 = scale.get_value(20);
    Serial.printf("Erster Rohwert: %ld\n", reading1);

    // Schritt 3: Zweites bekanntes Gewicht auflegen (z.B. 1000g)
    oledShowProgressBar(2, 5, tr(STR_SCALE_CAL), "2. Gewicht (1000g)");
    friendlyWait(8000); // Mehr Zeit zum Wechseln
    long reading2 = scale.get_value(20);
    Serial.printf("Zweiter Rohwert: %ld\n", reading2);

    // Schritt 4: Waage wieder leeren
    oledShowProgressBar(3, 5, tr(STR_SCALE_CAL), tr(STR_REMOVE_WEIGHT));
    friendlyWait(5000);

    // Schritt 5: Kalibrierungsfaktor berechnen
    // Wir verwenden die Zwei-Punkt-Formel: (Rohwert2 - Rohwert1) / (Gewicht2 - Gewicht1)
    // Hier: (reading2 - reading1) / (1000g - 500g)
    if (reading2 > reading1) {
      calibrationFactor = (float)(reading2 - reading1) / (SCALE_LEVEL_WEIGHT_2 - SCALE_LEVEL_WEIGHT_1);
    } else {
      calibrationFactor = 0; // Fehlerfall
    }

    if (calibrationFactor > 0)
    {
      Serial.print("Neuer Kalibrierungsfaktor: ");
      Serial.println(calibrationFactor);

      // Speichern mit NVS
      Preferences preferences;
      preferences.begin(NVS_NAMESPACE_SCALE, false); // false = readwrite
      preferences.putFloat(NVS_KEY_CALIBRATION, calibrationFactor);
      preferences.end();

      // Verifizieren
      preferences.begin(NVS_NAMESPACE_SCALE, true);
      float verifyValue = preferences.getFloat(NVS_KEY_CALIBRATION, 0);
      preferences.end();

      Serial.print("Gespeicherter Wert verifiziert: ");
      Serial.println(verifyValue);

      oledShowProgressBar(4, 5, tr(STR_SCALE_CAL), "Anwenden...");

      scale.set_scale(calibrationFactor);
      resetWeightFilter(); // Reset filter after calibration
      friendlyWait(2000);

      oledShowProgressBar(5, 5, tr(STR_SCALE_CAL), tr(STR_COMPLETED));

      // For some reason it is not possible to re-tare the scale here, it will result in a wdt timeout. Instead let the scale loop do the taring
      //scale.tare();
      scaleTareRequest = true;

      friendlyWait(2000);

      scaleCalibrated = true;
      returnState = 1;
    }
    else
    {
      Serial.println("Kalibrierungsfaktor ist ungültig. Bitte neu kalibrieren.");

      oledShowProgressBar(5, 5, tr(STR_FAILURE), tr(STR_CALIBRATION_ERROR));

      for (uint16_t i = 0; i < 50000; i++) {
        yield();
        vTaskDelay(pdMS_TO_TICKS(1));
        esp_task_wdt_reset();
      }
      returnState = 0;
    }
  }
  else
  {
    Serial.println("HX711 not found.");

    oledDisplayText(tr(STR_HX711_NOT_FOUND));

    for (uint16_t i = 0; i < 30000; i++) {
      yield();
      vTaskDelay(pdMS_TO_TICKS(1));
      esp_task_wdt_reset();
    }
    returnState = 0;
  }

  if (RfidReaderTask != NULL) vTaskResume(RfidReaderTask);
  if (ScaleTask != NULL && xTaskGetCurrentTaskHandle() != ScaleTask) vTaskResume(ScaleTask);
  pauseMainTask = 0;
  scaleCalibrationActive = false;

  return returnState;
}
