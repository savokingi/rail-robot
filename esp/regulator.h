#ifndef REGULATOR_H
#define REGULATOR_H

#include <Arduino.h>
#include <Adafruit_PWMServoDriver.h>
#include "motor.h"

#define POS_DEADBAND         40
#define POS_ERROR_MARGIN_PCT 10.0


// Крайние положения (сырые значения АЦП 0-4095)

#define LEFT_HAND_POT   7
#define LEFT_HAND_OUT 3933
#define LEFT_HAND_IN 1803

#define LEFT_LEG_POT    4
#define LEFT_LEG_OUT 1691
#define LEFT_LEG_IN 1003


#define RIGHT_HAND_POT  6
#define RIGHT_HAND_OUT 2000
#define RIGHT_HAND_IN 600

#define RIGHT_LEG_POT   5
#define RIGHT_LEG_IN 2972
#define RIGHT_LEG_OUT 3971


#define LEG_SPEED   4090
#define HAND_SPEED  4090
#define HAND_SLOW_SPEED   2000
#define LEG_SLOW_SPEED    4090

#define POS_ERR_SW_SPEED 200

typedef enum {
  MOT_LEFT_LEG = 0,
  MOT_RIGHT_LEG,
  MOT_LEFT_HAND,
  MOT_RIGHT_HAND,
  MOT_COUNT
} motor_index_t;


void updateCur();
void regBegin(Adafruit_PWMServoDriver* pwm);
void regTick();
void regStop();

bool isRegDone();   // true — сценарий не выполняется

void setTransport();
void setOnRail();
void setFold();
void softInitMotor();
#endif