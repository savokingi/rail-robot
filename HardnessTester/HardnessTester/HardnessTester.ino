/*
Данная программа отправляет данные измерений от твердометра (по шкале Либа)
через UART по протоколу общения с датчиками 

ОПИСАНИЕ ПРОТОКОЛА

| - начало команды
: - разделитель перед значением
; - окончание команды

T - Датчик твердости рельса
0 или 1 (левый и правый датчики рельсов)

Пример работы с датчиком:
|T0:M;   Команда на измерение (Measure)
|T0:M;   Ответ от датчика если данные не готовы или после запроса на измерение
|T0:R;   Запрос на готовность данных (Ready)
|T0:1; Ответ датчика если данные готовы (данные число после : может быть только целым числом)
*/

#define SPEED 115200 // Скорость Serial
#define SEN_T 'T'
#define SEN_INDX '0'

// Расстояние между датчиками в метрах.
float s = 0.0049;

// Константа 2as используется для коррекции скорости с учетом конечного снижения на 9,25 мм после усреднения результатов измерений.
float twoas = 0.1813;

typedef enum {
  WAIT,
  SEN_TYPE,
  SEN_NUM,
  COLON,
  COMMAND,
  END
} protocol_t;

bool startMis = 0;
bool isRead = 0;
bool dataReady = 0;
unsigned long t[4], dt1, dt2; 
float v1, v2 ; 
uint16_t leeb, Rc, Brin, Vick;

protocol_t st = WAIT;

void setup(){ 
Serial.begin(SPEED);

pinMode(2, INPUT_PULLUP); 
pinMode(3, INPUT_PULLUP); 
} 

void inMisure() {
  if(isRead){
    Serial.print("|");
    Serial.print(SEN_T);
    Serial.print(SEN_INDX);
    Serial.print(":");
    Serial.print("M");
    Serial.println(";");
  }
}

void parser (char c) {

  switch (st) {
    case WAIT:
      if (c == '|') st = SEN_TYPE;
      break;

    case SEN_TYPE:
      if (c == SEN_T) st = SEN_NUM;
      else st = WAIT;
      break;

    case SEN_NUM:
      if (c == SEN_INDX) {
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
        startMis = 1;
        st = END;
      } else if (c == 'R') {
        isRead = 1;
        st = END;
      } else {
        st = WAIT;
      }
      break;

    case END:
      if (c != ';') {
        isRead = 0;
        startMis = 0;
      }
      st = WAIT;
      break;
  }
}
void myCalc() { 
// Get timings. This works better than using interrupts. 
while (! digitalRead(2)) {inMisure(); } 
t[0] = micros(); 
while (! digitalRead(3)) {inMisure(); } 
t[1] = micros(); 
while (digitalRead(3)) {inMisure(); } 
while (! digitalRead(3)) {inMisure(); } 
t[2] = micros(); 
while (! digitalRead(2)) {inMisure(); } 
t[3] = micros(); 
// Calculate time intervals 
dt1 = t[1] - t[0]; 
dt2 = t[3] - t[2]; 
// Calculate and adjust drop & rebound velocities 
v1 = (1000000*s)/dt1; v1 = sqrt(v1*v1 + twoas); 
v2 = (1000000*s)/dt2; v2 = sqrt(v2*v2 + twoas); 
// Calculate leeb hardness value and convert to other scales 
leeb = (1000 * v2)/v1; // Leeb value 
Rc = -0.000112211 * leeb*leeb + 0.2808769 * leeb - 93.83636; // HRC 
Brin = 0.00237066 * leeb*leeb -1.646908 * leeb + 455.7088; 
// Brinell 
Vick = 0.00264169 * leeb*leeb -1.900271 * leeb + 519.8346; 
// Vickers
} 

void loop() { 
  if (Serial.available()) {
      char c = (char) Serial.read();
      parser(c);
  }
  if(isRead && dataReady){
    Serial.print("|");
    Serial.print(SEN_T);
    Serial.print(SEN_INDX);
    Serial.print(":");
    Serial.print(leeb);
    Serial.println(";");
    isRead = 0;
    dataReady = 0;
  }
  if(startMis){
    myCalc();
    startMis = 0;
    dataReady = 1;
  }
}
