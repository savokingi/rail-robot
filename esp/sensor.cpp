#include "sensor.h"
#include "regulator.h"
uint32_t butCheckTmr;
uint32_t distCheckTmr;

float getBatteryVoltage() {
    uint32_t total_mV = 0;
    for (int i = 0; i < ADC_SAMPLES; i++) {
        total_mV += analogReadMilliVolts(BAT_CHECK_PIN);
        delayMicroseconds(200);
    }
    float pin_mV = (float)total_mV / ADC_SAMPLES;    // мВ на пине
    return (pin_mV * DIVIDER_RATIO) / 1000.0;        // В на батарее
}

void checkBat(float *bV){
  if(millis() - butCheckTmr >= BAT_CHECK_PERIOD){
    *bV = getBatteryVoltage();
    butCheckTmr = millis();
    // Serial.print("[checkBat]: ");
    // Serial.println(*bV);
  }
}

void initDist(){
  pinMode(ECHO_PIN, INPUT);
  pinMode(TRIG_PIN, OUTPUT);
}
uint16_t getDistcm() {
  // Стабилизируем пин перед отправкой импульса
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);

  // Импульс 10 мкс для активации датчика
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  uint32_t us = pulseIn(ECHO_PIN, HIGH, 24000);
  
  uint16_t distanceCm = (us + 29) / 58;
  return distanceCm;
}

bool flag;
bool checkDist(uint16_t *d){
  if(millis() -  distCheckTmr >= 100){
    distCheckTmr = millis();
    *d = getDistcm();
    flag = *d<50 & *d !=0;
    // Serial.print("[checkDist]: ");
    // Serial.println(*d);
    updateCur();
    updateCur();

  }
  return flag;
}
