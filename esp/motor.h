#ifndef MOTOR_H
#define MOTOR_H

#include <Arduino.h>
#include <Adafruit_PWMServoDriver.h>
#include <Wire.h>

// Каналы моторов
#define LEFT_LEG    4,5
#define RIGHT_LEG   6,7
#define LEFT_HAND   8,9
#define RIGHT_HAND  10,11
#define LEFT_WEEL   12,13
#define RIGHT_WEEL  14,15

#define PWM_FREQ         1000
#define MOTOR_MAX_SPEED  4095

void motBegin(Adafruit_PWMServoDriver* pwm);

void motStop(Adafruit_PWMServoDriver* pwm, uint8_t pin_a, uint8_t pin_b);
void motStopAll(Adafruit_PWMServoDriver* pwm);   // все 6 моторов, включая колёса
void motForward(Adafruit_PWMServoDriver* pwm, uint8_t pin_a, uint8_t pin_b, uint16_t speed);
void motBackward(Adafruit_PWMServoDriver* pwm, uint8_t pin_a, uint8_t pin_b, uint16_t speed);
void motBrake(Adafruit_PWMServoDriver* pwm, uint8_t pin_a, uint8_t pin_b);

// Единая точка «отработать ошибку»: стоп / медленно / обычно
void motDriveCorrection(Adafruit_PWMServoDriver* pwm,
  uint8_t pin_a, uint8_t pin_b,
  int16_t err, int16_t deadband, int16_t slow_zone,
  uint16_t speed, uint16_t slow_speed);

#endif