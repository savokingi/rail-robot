#include "regulator.h"

// ---- Статическая таблица моторов ----
typedef struct {
  uint8_t  pin_a;
  uint8_t  pin_b;
  uint8_t  pot_pin;
  uint16_t in_pos;
  uint16_t out_pos;
  uint16_t speed;
  uint16_t slow_speed;
} motor_data_t;

static const motor_data_t motor_data[MOT_COUNT] = {
  { LEFT_LEG,   LEFT_LEG_POT,   LEFT_LEG_IN,   LEFT_LEG_OUT,
    LEG_SPEED,  LEG_SLOW_SPEED  },   // MOT_LEFT_LEG
  { RIGHT_LEG,  RIGHT_LEG_POT,  RIGHT_LEG_IN,  RIGHT_LEG_OUT,
    LEG_SPEED,  LEG_SLOW_SPEED  },   // MOT_RIGHT_LEG
  { LEFT_HAND,  LEFT_HAND_POT,  LEFT_HAND_IN,  LEFT_HAND_OUT,
    HAND_SPEED, HAND_SLOW_SPEED },   // MOT_LEFT_HAND
  { RIGHT_HAND, RIGHT_HAND_POT, RIGHT_HAND_IN, RIGHT_HAND_OUT,
    HAND_SPEED, HAND_SLOW_SPEED },   // MOT_RIGHT_HAND
};

// ---- Один шаг сценария: какие моторы крутим одновременно ----
// Шаги сценария лежат подряд; count == 0 — терминатор (конец сценария).
typedef struct {
  motor_index_t motors[MOT_COUNT];
  uint8_t count;
} step_t;

// Transport: все 4 за один шаг
static const step_t seq_transport[] = {
  {{MOT_LEFT_LEG, MOT_RIGHT_LEG, MOT_LEFT_HAND, MOT_RIGHT_HAND}, 4},
  {{MOT_LEFT_LEG}, 0},  // терминатор
};

// OnRail: сначала руки (шаг 1), потом ноги (шаг 2)
static const step_t seq_onrail[] = {
  {{MOT_LEFT_HAND, MOT_RIGHT_HAND}, 2},
  {{MOT_LEFT_LEG,  MOT_RIGHT_LEG},  2},
  {{MOT_LEFT_LEG}, 0},
};

// Fold: сначала ноги (шаг 1), потом руки (шаг 2)
static const step_t seq_fold[] = {
  {{MOT_LEFT_LEG,  MOT_RIGHT_LEG},  2},
  {{MOT_LEFT_HAND, MOT_RIGHT_HAND}, 2},
  {{MOT_LEFT_LEG}, 0},
};

// ---- Состояние: одна переменная ----
static const step_t* cur_step = NULL;   // NULL = ничего не выполняется
static Adafruit_PWMServoDriver* pwm_ptr = NULL;
static uint16_t cur_val[MOT_COUNT];
static uint16_t target_val[MOT_COUNT];

// ================= внутренние =================

static void updateCur(){
  for (int i = 0; i < MOT_COUNT; i++)
    cur_val[i] = analogRead(motor_data[i].pot_pin);
}

static bool isAtTarget(int i){
  int16_t e = (int16_t)target_val[i] - (int16_t)cur_val[i];
  if (e < 0) e = -e;
  return e <= POS_DEADBAND;
}

// true — вышли за допустимый коридор IN/OUT (±POS_ERROR_MARGIN_PCT)
static bool isBreakLimits(int i){
  const motor_data_t* m = &motor_data[i];
  uint16_t lo = (m->in_pos < m->out_pos) ? m->in_pos : m->out_pos;
  uint16_t hi = (m->in_pos < m->out_pos) ? m->out_pos : m->in_pos;

  uint16_t range  = hi - lo;
  uint16_t margin = (uint32_t)range * POS_ERROR_MARGIN_PCT / 100;

  int32_t lo_lim = (int32_t)lo - margin; if (lo_lim < 0)    lo_lim = 0;
  int32_t hi_lim = (int32_t)hi + margin; if (hi_lim > 4095) hi_lim = 4095;

  int32_t c = cur_val[i];
  return (c < lo_lim || c > hi_lim);
}

static void checkLimits(){
  for (int i = 0; i < MOT_COUNT; i++){
    if (isBreakLimits(i)){
      motStopAll(pwm_ptr);
      Serial.printf("[ОШИБКА РЕГУЛЯТОРА] мотор %d вышел за лимит: cur=%u in=%u out=%u\n",
                    i, cur_val[i], motor_data[i].in_pos, motor_data[i].out_pos);
      while (true){
        Serial.println("[ОЖИДАНИЕ ПЕРЕЗАПУСКА] риск повреждения механики");
        delay(2000);
      }
    }
  }
}

// ================= публичное =================

void regBegin(Adafruit_PWMServoDriver* pwm){
  pwm_ptr = pwm;
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  for (int i = 0; i < MOT_COUNT; i++) analogRead(motor_data[i].pot_pin);
  updateCur();
  for (int i = 0; i < MOT_COUNT; i++) target_val[i] = cur_val[i];
  motStopAll(pwm_ptr);
}

void regStop(){
  motStopAll(pwm_ptr);
  updateCur();
  for (int i = 0; i < MOT_COUNT; i++) target_val[i] = cur_val[i];
  cur_step = NULL;
}

bool isRegDone(){ return cur_step == NULL; }

void regTick(){
  updateCur();
  checkLimits();
  if (cur_step == NULL) return;

  bool all_done = true;
  for (uint8_t k = 0; k < cur_step->count; k++){
    int m = (int)cur_step->motors[k];
    if (isAtTarget(m)){
      motStop(pwm_ptr, motor_data[m].pin_a, motor_data[m].pin_b);
    } else {
      int16_t err = (int16_t)target_val[m] - (int16_t)cur_val[m];
      motDriveCorrection(pwm_ptr,
        motor_data[m].pin_a, motor_data[m].pin_b,
        err, POS_DEADBAND, POS_SLOW_SPEED,
        motor_data[m].speed, motor_data[m].slow_speed);

      all_done = false;
    }
  }

  if (all_done){
    cur_step++;                          // следующий шаг (или терминатор)
    if (cur_step->count == 0){           // конец сценария
      cur_step = NULL;
      Serial.println("[REG] done");
    }
  }
}

// ---- Старт сценариев ----

void setTransport(){
  target_val[MOT_LEFT_LEG]   = LEFT_LEG_IN;
  target_val[MOT_RIGHT_LEG]  = RIGHT_LEG_IN;
  target_val[MOT_LEFT_HAND]  = LEFT_HAND_IN;
  target_val[MOT_RIGHT_HAND] = RIGHT_HAND_IN;
  cur_step = seq_transport;
  Serial.println("[REG] transport");
}

void setOnRail(){
  target_val[MOT_LEFT_LEG]   = LEFT_LEG_IN;
  target_val[MOT_RIGHT_LEG]  = RIGHT_LEG_IN;
  target_val[MOT_LEFT_HAND]  = LEFT_HAND_OUT;
  target_val[MOT_RIGHT_HAND] = RIGHT_HAND_OUT;
  cur_step = seq_onrail;
  Serial.println("[REG] onrail");
}

void setFold(){
  target_val[MOT_LEFT_LEG]   = LEFT_LEG_OUT;
  target_val[MOT_RIGHT_LEG]  = RIGHT_LEG_OUT;
  target_val[MOT_LEFT_HAND]  = LEFT_HAND_IN;
  target_val[MOT_RIGHT_HAND] = RIGHT_HAND_IN;
  cur_step = seq_fold;
  Serial.println("[REG] fold");
}