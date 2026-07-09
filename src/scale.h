#ifndef SCALE_H
#define SCALE_H

#include <Arduino.h>
#include "HX711.h"
#include "config.h"

uint8_t setAutoTare(bool autoTareValue);
void start_scale(bool touchSensorConnected);
uint8_t calibrate_scale();
uint8_t tareScale();

// Weight stabilization functions
void resetWeightFilter();
float calculateMovingAverage();
float applyLowPassFilter(float newValue);
int16_t processWeightReading(float rawWeight);
int16_t getFilteredDisplayWeight();

#define SCALE_LEVEL_WEIGHT_1 500.0f
#define SCALE_LEVEL_WEIGHT_2 1000.0f

extern HX711 scale;
extern int16_t weight;
extern uint8_t weightCounterToApi;
extern uint8_t scale_tare_counter;
extern bool scaleTareRequest;
extern uint8_t pauseMainTask;
extern bool scaleCalibrated;
extern bool scaleConnected;
extern bool autoTare;
extern bool scaleCalibrationActive;
extern volatile bool scaleCalibrationRequest;

extern TaskHandle_t ScaleTask;

#endif
