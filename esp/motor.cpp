#include "motor.h"

void motStop(Adafruit_PWMServoDriver* pwm){
  pwm->setPWM(LEFT_WEEL_A, 0, 0);
  pwm->setPWM(LEFT_WEEL_B, 0, 0);
  pwm->setPWM(RIGHT_WEEL_A, 0, 0);
  pwm->setPWM(RIGHT_WEEL_B, 0, 0);
}

void motForward(Adafruit_PWMServoDriver* pwm, uint16_t speed){
  pwm->setPWM(LEFT_WEEL_A, 0, speed);
  pwm->setPWM(LEFT_WEEL_B, 0, 0);
  pwm->setPWM(RIGHT_WEEL_A, 0, speed);
  pwm->setPWM(RIGHT_WEEL_B, 0, 0);
}

void motBackward(Adafruit_PWMServoDriver* pwm, uint16_t speed){
  pwm->setPWM(LEFT_WEEL_A, 0, 0);
  pwm->setPWM(LEFT_WEEL_B, 0, speed);
  pwm->setPWM(RIGHT_WEEL_A, 0, 0);
  pwm->setPWM(RIGHT_WEEL_B, 0, speed);
}


void motBegin(Adafruit_PWMServoDriver* pwm){
  Wire.begin(41, 42);
  pwm->begin(); 
  pwm->setPWMFreq(PWM_FREQ);
  motStop(pwm);
}