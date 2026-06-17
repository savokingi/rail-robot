/*

  ОПИСАНИЕ ПРОТОКОЛА

| - начало команды
: - разделитель перед значением
; - окончание команды

T - Датчик твердости рельса (Значения: 0 - 1023)
P - Датчик подуклона (наклон рельс) (Значения в 0 - 1000) градусы * 100
G - Датчик газа (Значения: 0 - 1023)
D - Датчик звука (Трещины и полости в рельсах) (Значения: 0 - 1023)
0 или 1 (левый и правый датчики рельсов) для датчика газа неважно С НИМИ РАБОТАТЬ ПО ОЧЕРЕДИ, НЕЛЬЗЯ ПАРАЛЛЕЛЬНО!

Пример работы с датчиком:
|T0:M;   Команда на измерение (Measure)
|T0:M;   Ответ от датчика если данные не готовы или после запроса на измерение
|T0:R;   Запрос на готовность данных (Ready)
|T0:1; Ответ датчика если данные готовы (данные число после : может быть только целым числом)

*/

#define SPEED 115200

typedef enum {
  WAIT,
  SEN_TYPE,
  SEN_NUM,
  COLON,
  COMMAND,
  END
} protocol_t;

typedef enum {
  SEN_T,
  SEN_P,
  SEN_G,
  SEN_D,
  SEN_MAX
} sen_t;

typedef struct {
  char tChar;
  bool num;
  bool startMis;
  uint32_t minVal;
  uint32_t maxVal;
  uint32_t val;
  uint32_t tmout;
  uint32_t tmr;

} sen_obj;

sen_obj sensor[SEN_MAX] = {

  [SEN_T] = {'T', 0, 0, 0, 1024, 0, 10000, 0},
  [SEN_P] = {'P', 0, 0, 0, 1001, 0, 5000, 0},
  [SEN_G] = {'G', 0, 0, 0, 1024, 0, 1000, 0},
  [SEN_D] = {'D', 0, 0, 0, 1024, 0, 10000, 0},
};

protocol_t st = WAIT;
void inMisure(sen_t sen) {
  Serial.print("|");
  Serial.print(sensor[sen].tChar);
  Serial.print(sensor[sen].num);
  Serial.print(":");
  Serial.print("M");
  Serial.println(";");
}
void startMisure(sen_t sen) {
  sensor[sen].tmr = millis();
  sensor[sen].startMis = 1;
  inMisure(sen);
}

void readSensor(sen_t sen) {
  if (sensor[sen].startMis) {
    if (millis() - sensor[sen].tmr >= sensor[sen].tmout) {
      sensor[sen].val = (uint32_t)((sensor[sen].val * 0.75) + (random(sensor[sen].minVal, sensor[sen].maxVal) * 0.25));
      sensor[sen].startMis = 0;
      Serial.print("|");
      Serial.print(sensor[sen].tChar);
      Serial.print(sensor[sen].num);
      Serial.print(":");
      Serial.print(sensor[sen].val);
      Serial.println(";");

    } else {
      inMisure(sen);
    }
  }
}
void parser (char c) {
  static sen_t curSenT;
  static bool start = 0;
  static bool Read = 0;

  switch (st) {
    case WAIT:
      if (c == '|') st = SEN_TYPE;
      break;

    case SEN_TYPE:
      curSenT = SEN_MAX;

      for (uint8_t i = 0; i < SEN_MAX; i++) {
        if (sensor[i].tChar == c) curSenT = (sen_t)i;
      }
      if (curSenT != SEN_MAX) st = SEN_NUM;
      else st = WAIT;
      break;

    case SEN_NUM:
      if (c == '0' || c == '1') {
        sensor[curSenT].num = c - '0';
        st = COLON;
      }
      else {
        st = WAIT;
      }
      break;

    case COLON:
      if (c == ':') st = COMMAND;
      else st = WAIT;
      break;

    case COMMAND:
      if (c == 'M') {
        start = 1;
        st = END;
      } else if (c == 'R') {
        Read = 1;
        st = END;
      } else {
        st = WAIT;
      }
      break;

    case END:
      if (c == ';') {
        if (start) {
          start = 0;
          startMisure(curSenT);
        }
        if (Read) {
          Read = 0;
          readSensor(curSenT);
        }
      }
      st = WAIT;
      break;
  }
}
void setup() {
  Serial.begin(SPEED);
  randomSeed(analogRead(0));
}

void loop() {
  if (Serial.available()) {
    char c = (char) Serial.read();
    parser(c);
  }

}
