#ifndef REGULATOR_H
#define REGULATOR_H

#include <Arduino.h>
#include <Adafruit_PWMServoDriver.h>
#include "motor.h"

#define POS_DEADBAND         20
#define POS_SLOW_SPEED       100
#define POS_ERROR_MARGIN_PCT 2

// Крайние положения (сырые значения АЦП 0-4095)
#define LEFT_HAND_OUT 260
#define RIGHT_HAND_OUT 467
#define LEFT_LEG_OUT 904
#define RIGHT_LEG_OUT 1692

#define LEFT_HAND_IN 2586
#define RIGHT_HAND_IN 105
#define LEFT_LEG_IN 908
#define RIGHT_LEG_IN 1692

#define LEG_SPEED   4090
#define HAND_SPEED  4090
#define HAND_SLOW_SPEED   3500
#define LEG_SLOW_SPEED    4090

#define LEFT_HAND_POT   4
#define RIGHT_HAND_POT  5
#define LEFT_LEG_POT    6
#define RIGHT_LEG_POT   7

typedef enum {
  MOT_LEFT_LEG = 0,
  MOT_RIGHT_LEG,
  MOT_LEFT_HAND,
  MOT_RIGHT_HAND,
  MOT_COUNT
} motor_index_t;

void regBegin(Adafruit_PWMServoDriver* pwm);
void regTick();
void regStop();

bool isRegDone();   // true — сценарий не выполняется

void setTransport();
void setOnRail();
void setFold();

#endif