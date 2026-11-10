#ifndef MOTOR_H
#define MOTOR_H

#include <Arduino.h>
#include <Adafruit_PWMServoDriver.h>
#include <Wire.h>

// Каналы моторов

#define LEFT_WEEL_A 13
#define LEFT_WEEL_B 12
#define RIGHT_WEEL_A 14
#define RIGHT_WEEL_B 15

#define PWM_FREQ         1000

void motBegin(Adafruit_PWMServoDriver* pwm);
void motStop(Adafruit_PWMServoDriver* pwm);
void motForward(Adafruit_PWMServoDriver* pwm, uint16_t speed);
void motBackward(Adafruit_PWMServoDriver* pwm, uint16_t speed);



#endif