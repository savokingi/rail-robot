#ifndef SENSOR_H
#define SENSOR_H

#include <Arduino.h>

// --- Параметры делителя ---
const float R1 = 100000.0;          // 100 кОм (верхний)
const float R2 = 21000.0;           // 21 кОм (нижний)
const float DIVIDER_RATIO = (R1 + R2) / R2;   // ≈ 5.762

// Пин измерения напряжения питания
#define BAT_CHECK_PIN 8
#define ADC_SAMPLES 32         // усреднение — чем больше, тем стабильнее
#define BAT_CHECK_PERIOD 10000    // в милисекундах

#define ECHO_PIN 9
#define TRIG_PIN 14
#define DIST_CHECK_PERIOD 5000

void checkBat(float *bV);
void initDist();
bool checkDist(uint16_t *d);
#endif