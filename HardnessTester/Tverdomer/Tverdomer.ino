// ============================================================
//  Твердомер на основе двух пар оптических датчиков
//  Arduino Nano, неблокирующий код, аппаратные прерывания
//  D2 -> INT0 (верхний датчик, ВД)
//  D3 -> INT1 (нижний датчик,  НД)
//  D10 -> кнопка (INPUT_PULLUP: покой=HIGH, нажатие=LOW)
//  Датчики: в покое LOW, при перекрытии шариком -> HIGH
// ============================================================

// ---------- ВЫБОР РЕЖИМА ----------
// 1 - длинный боёк / большой шарик (перекрывает оба или только нижний)
// 2 - маленький шарик (не закрывает нижний датчик в покое)
#define MEASURE_MODE 1

// ---------- ПИНЫ ----------
const uint8_t PIN_TOP    = 2;   // ВД  (INT0)
const uint8_t PIN_BOTTOM = 3;   // НД  (INT1)
const uint8_t PIN_BUTTON = 10;  // кнопка, подтяжка к +

// ---------- ЛОГИКА УРОВНЯ ДАТЧИКА ----------
// true, если датчик перекрыт (шарик в луче).
// У наших датчиков при срабатывании HIGH.
#define SENSOR_BLOCKED(pin)  (digitalRead(pin) == HIGH)

// ---------- КОНСТАНТЫ ----------
const float S_DIST      = 0.01f;    // расстояние между датчиками, м
const float TWO_AS      = 0.1813f;  // 2*a*s для коррекции
const unsigned long DEBOUNCE_MS        = 200;
const unsigned long MEASURE_TIMEOUT_MS = 3000;

// ---------- СОСТОЯНИЕ ----------
volatile uint32_t tEdge[4];      // метки времени фронтов
volatile uint8_t  edgeCount = 0; // сколько фронтов поймано
volatile bool     measuring = false;

// предыдущее состояние датчиков (перекрыт/нет) — для различения фронтов
volatile bool topWasBlocked = false;
volatile bool botWasBlocked = false;

uint32_t lastButtonMs   = 0;
uint32_t measureStartMs = 0;

// ------------------------------------------------------------
//  ISR верхнего датчика (D2 / INT0)
// ------------------------------------------------------------
void topISR() {
  if (!measuring || edgeCount >= 4) return;

  bool blocked    = SENSOR_BLOCKED(PIN_TOP);
  bool onBlock    = ( blocked && !topWasBlocked);  // появление перекрытия
  bool onUnblock  = (!blocked &&  topWasBlocked);  // снятие перекрытия
  topWasBlocked   = blocked;

#if MEASURE_MODE == 1
  // Режим 1:
  //   появление перекрытия ВД -> edge0 (старт 1, вниз)
  //   снятие перекрытия ВД    -> edge3 (стоп 2,  вверх)
  if (onBlock && edgeCount == 0) {
    tEdge[0] = micros();
    edgeCount = 1;
  } else if (onUnblock && edgeCount == 3) {
    tEdge[3] = micros();
    edgeCount = 4;
  }
#else
  // Режим 2: на ВД нужны только появления перекрытия
  //   -> edge0 (вниз, старт 1)  или  edge3 (вверх, стоп 2)
  if (onBlock) {
    if (edgeCount == 0) {
      tEdge[0] = micros();
      edgeCount = 1;
    } else if (edgeCount == 3) {
      tEdge[3] = micros();
      edgeCount = 4;
    }
  }
#endif
}

// ------------------------------------------------------------
//  ISR нижнего датчика (D3 / INT1)
// ------------------------------------------------------------
void bottomISR() {
  if (!measuring || edgeCount >= 4) return;

  bool blocked    = SENSOR_BLOCKED(PIN_BOTTOM);
  bool onBlock    = ( blocked && !botWasBlocked);
  bool onUnblock  = (!blocked &&  botWasBlocked);
  botWasBlocked   = blocked;

#if MEASURE_MODE == 1
  // Режим 1:
  //   появление перекрытия НД -> edge1 (стоп 1, вниз)
  //   снятие перекрытия НД    -> edge2 (старт 2, вверх)
  if (onBlock && edgeCount == 1) {
    tEdge[1] = micros();
    edgeCount = 2;
  } else if (onUnblock && edgeCount == 2) {
    tEdge[2] = micros();
    edgeCount = 3;
  }
#else
  // Режим 2: на НД нужны только появления перекрытия
  //   -> edge1 (вниз, стоп 1)  и  edge2 (вверх, старт 2)
  if (onBlock) {
    if (edgeCount == 1) {
      tEdge[1] = micros();
      edgeCount = 2;
    } else if (edgeCount == 2) {
      tEdge[2] = micros();
      edgeCount = 3;
    }
  }
#endif
}

// ------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  while (!Serial) {}

  pinMode(PIN_TOP,    INPUT);         // внешняя подтяжка
  pinMode(PIN_BOTTOM, INPUT);
  pinMode(PIN_BUTTON, INPUT_PULLUP);  // покой=HIGH, нажатие=LOW

  attachInterrupt(digitalPinToInterrupt(PIN_TOP),    topISR,    CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_BOTTOM), bottomISR, CHANGE);

  Serial.println(F("=== Hardness Tester ==="));
#if MEASURE_MODE == 1
  Serial.println(F("Mode 1: big ball / long striker"));
#else
  Serial.println(F("Mode 2: small ball"));
#endif
  Serial.println(F("Press button (D10) to start measurement..."));
}

// ------------------------------------------------------------
void startMeasurement() {
  noInterrupts();
  edgeCount = 0;

  // Защёлкиваем текущее состояние, чтобы не поймать ложный фронт
  // при первом же прерывании после старта.
  topWasBlocked = SENSOR_BLOCKED(PIN_TOP);
  botWasBlocked = SENSOR_BLOCKED(PIN_BOTTOM);

  measuring      = true;
  measureStartMs = millis();
  interrupts();

  Serial.println(F("Measuring..."));
}

// ------------------------------------------------------------
void processResult() {
  uint32_t t0, t1, t2, t3;
  noInterrupts();
  t0 = tEdge[0]; t1 = tEdge[1]; t2 = tEdge[2]; t3 = tEdge[3];
  interrupts();

  // Защита от неверного порядка фронтов (переполнение uint32_t)
  if (t1 < t0 || t3 < t2) {
    Serial.println(F("ERROR: bad edge order"));
    return;
  }

  uint32_t dt1 = t1 - t0;   // падение
  uint32_t dt2 = t3 - t2;   // отскок

  if (dt1 == 0 || dt2 == 0) {
    Serial.println(F("ERROR: zero dt"));
    return;
  }

  // Скорости на базе S (м/с)
  float v1 = (1000000.0f * S_DIST) / (float)dt1;
  float v2 = (1000000.0f * S_DIST) / (float)dt2;

  // Коррекция по 2*a*S
  v1 = sqrtf(v1 * v1 + TWO_AS);
  v2 = sqrtf(v2 * v2 + TWO_AS);

  float leebF = (1000.0f * v2) / v1;

  // Пересчёты шкал (эмпирические формулы)
  float RcF   = -0.000112211f * leebF * leebF + 0.2808769f * leebF - 93.83636f;
  float BrinF =  0.00237066f  * leebF * leebF - 1.646908f  * leebF + 455.7088f;
  float VickF =  0.00264169f  * leebF * leebF - 1.900271f  * leebF + 519.8346f;

  Serial.print(F("dt1(us)=")); Serial.print(dt1);
  Serial.print(F("  dt2(us)=")); Serial.print(dt2);
  Serial.print(F("  v1(m/s)=")); Serial.print(v1, 4);
  Serial.print(F("  v2(m/s)=")); Serial.print(v2, 4);
  Serial.println();

  if (leebF >= 400.0f && leebF < 1000.0f) {
    Serial.print(F("Leeb HL = ")); Serial.println((uint16_t)leebF);
    Serial.print(F("HRC     = ")); Serial.println(RcF,   1);
    Serial.print(F("HB      = ")); Serial.println(BrinF, 1);
    Serial.print(F("HV      = ")); Serial.println(VickF, 1);
  } else if (leebF >= 950.0f) {
    Serial.print(F("Leeb HL = ")); Serial.println((uint16_t)leebF);
    Serial.println(F("** High value **"));
  } else {
    Serial.print(F("Leeb HL = ")); Serial.println(leebF, 1);
    Serial.println(F("** WARNING: out of range **"));
  }
  Serial.println(F("-------------------------"));
}

// ------------------------------------------------------------
void loop() {
  // --- кнопка старта (неблокирующий антидребезг) ---
  // INPUT_PULLUP: в покое HIGH, при нажатии LOW
  if (digitalRead(PIN_BUTTON) == LOW && !measuring &&
      (millis() - lastButtonMs > DEBOUNCE_MS)) {
    lastButtonMs = millis();
    startMeasurement();
    return;
  }

  // --- завершение измерения ---
  if (measuring) {
    bool done = false;
    noInterrupts();
    if (edgeCount >= 4) done = true;
    interrupts();

    if (done) {
      measuring = false;
      processResult();
    } else if (millis() - measureStartMs > MEASURE_TIMEOUT_MS) {
      noInterrupts();
      uint8_t ec = edgeCount;
      measuring = false;
      interrupts();
      Serial.print(F("TIMEOUT, edges caught = "));
      Serial.println(ec);
    }
  }
}