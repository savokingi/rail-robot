#include "motor.h"

void motStop(Adafruit_PWMServoDriver* pwm, uint8_t pin_a, uint8_t pin_b){
  pwm->setPWM(pin_a, 0, 0);
  pwm->setPWM(pin_b, 0, 0);
}

void motStopAll(Adafruit_PWMServoDriver* pwm){
  motBrake(pwm, LEFT_LEG);
  motBrake(pwm, RIGHT_LEG);
  motBrake(pwm, LEFT_HAND);
  motBrake(pwm, RIGHT_HAND);
  motBrake(pwm, LEFT_WEEL);
  motBrake(pwm, RIGHT_WEEL);
}

void motForward(Adafruit_PWMServoDriver* pwm, uint8_t pin_a, uint8_t pin_b, uint16_t speed){
  pwm->setPWM(pin_a, 0, speed);
  pwm->setPWM(pin_b, 0, 0);
}

void motBackward(Adafruit_PWMServoDriver* pwm, uint8_t pin_a, uint8_t pin_b, uint16_t speed){
  pwm->setPWM(pin_a, 0, 0);
  pwm->setPWM(pin_b, 0, speed);
}

void motBrake(Adafruit_PWMServoDriver* pwm, uint8_t pin_a, uint8_t pin_b){
  pwm->setPWM(pin_a, 0, MOTOR_MAX_SPEED);
  pwm->setPWM(pin_b, 0, MOTOR_MAX_SPEED);
}

void motDriveCorrection(Adafruit_PWMServoDriver* pwm,
  uint8_t pin_a, uint8_t pin_b,
  int16_t err, int16_t deadband, int16_t slow_zone,
  uint16_t speed, uint16_t slow_speed){
    
  int16_t aerr = (err < 0) ? -err : err;
  if (aerr <= deadband){ motStop(pwm, pin_a, pin_b); return; }

  uint16_t s = (aerr <= slow_zone) ? slow_speed : speed;
  if (err > 0) motForward (pwm, pin_a, pin_b, s);
  else         motBackward(pwm, pin_a, pin_b, s);
}

void motBegin(Adafruit_PWMServoDriver* pwm){
  pwm->begin();
  Wire.begin();
  pwm->setPWMFreq(PWM_FREQ);
  motStopAll(pwm);
}