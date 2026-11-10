typedef struct {
  uint16_t pin;
  bool invert;
  char *type;
  int32_t dist;
  uint8_t oldSens;
} my_sensor_t;

#include <WiFi.h>
#include <WiFiUdp.h>
#include <ESP32Encoder.h>
#include "motor.h"


const char *ssid = "Kvant";
const char *password = "KvantKvant";

const int PC_UDP_PORT = 8888;
const int MY_RX_UDP_PORT = 7777;
const int MY_TX_UDP_PORT = 9999;

// Настройки статического IP-адреса
IPAddress local_IP(192, 168, 1, 134);  // Желаемый IP-адрес для ESP32
IPAddress gateway(192, 168, 1, 1);     // Шлюз
IPAddress subnet(255, 255, 255, 0);    // Мас ка подсети
IPAddress bcast_IP(255, 255, 255, 255);


WiFiUDP udpRx;
Adafruit_PWMServoDriver motorPwm = Adafruit_PWMServoDriver();
ESP32Encoder encoder;

typedef struct {
  int run = 0;         // -1 назад 0 стоп 1 вперед
  bool checkD = 0;     // ищем дефекты илил нет
  bool isLimit = 0;    // до лимита
  uint32_t limit = 0;  // лимит
} flags_t;

typedef enum { ST_WAIT,
               ST_RAZGON,
               ST_BRAKE } state_t;

state_t curSt = ST_WAIT;
flags_t flags;

int32_t enc = 0;
int32_t dist;

void parseUdpCmd() {
  int sz = udpRx.parsePacket();
  if (sz <= 0) return;  // пакетов нет

  char buf[64];
  int len = udpRx.read(buf, sizeof(buf) - 1);
  if (len <= 0) return;
  buf[len] = '\0';  // для atoi / логов

  Serial.printf("[UDP] %s:%u -> \"%s\" (%d)\n",
                udpRx.remoteIP().toString().c_str(),
                udpRx.remotePort(), buf, len);

  if (len < 3) return;              // минимум |h;
  if (buf[0] != '|') return;        // строго с '|'
  if (buf[len - 1] != ';') return;  // строго с ';'

  char cmd = buf[1];

  if ((cmd == 'f' || cmd == 'b') && len < 4) return;

  uint32_t num = (cmd == 'f' || cmd == 'b')
                   ? (uint32_t)atoi(buf + 2)
                   : 0;

  switch (cmd) {
    case 'f':
      flags.run = 1;
      flags.checkD = 1;
      flags.isLimit = 1;
      flags.limit = num;
      break;

    case 'b':
      flags.run = -1;
      flags.checkD = 1;
      flags.isLimit = 1;
      flags.limit = num;
      break;

    case 'h':
      flags.run = 1;
      flags.checkD = 0;
      flags.isLimit = 0;
      flags.limit = 0;
      break;

    case 't':
      flags.run = -1;
      flags.checkD = 0;
      flags.isLimit = 0;
      flags.limit = 0;
      break;

    case 'p':
      flags.run = 0;
      break;

    case 'u':
      encoder.setCount(0);
      break;

    default:
      break;  // неизвестный код
  }
}


// todo поменять пины
my_sensor_t my_sens[4] = {
  { 1, false, "HOLE,R, ", 0, 255 },
  { 2, false, "HOLE,L, ", 0, 255 },
  { 4, false, "BOLT,R, ", 0, 255 },
  { 5, false, "BOLT,L, ", 0, 255 },
};

const char *udpAddress = "255.255.255.255";
const int udpPort = 3333;

void initSens2(my_sensor_t *ss) {
  pinMode(ss->pin, INPUT);
  uint8_t t = digitalRead(ss->pin);
  if (ss->invert) {
    t = !t;
  }
  ss->oldSens = t;
}
bool readSens(my_sensor_t *ss) {
  uint8_t t = digitalRead(ss->pin);
  if (ss->invert) {
    t = !t;
  }
  if (t == 1 && ss->oldSens == 0) {
    ss->oldSens = 1;
    Serial.print(ss->pin);
    Serial.println(" RISING");
    ss->dist = dist;
  } else if (t == 0 && ss->oldSens == 1) {
    ss->oldSens = 0;
    Serial.print(ss->pin);
    Serial.println(" FOLING");

    udpRx.beginPacket(udpAddress, udpPort);

    udpRx.print(ss->type);
    udpRx.print(ss->dist);
    udpRx.print(" ,");
    udpRx.print(abs(dist - ss->dist));
    udpRx.endPacket();
    return true;
  }
  return false;
}


#define ECHO_PIN 9
#define TRIG_PIN 14
void initDist() {
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

float getAngle(){
  //TODO ЛУКА напиши тут функциювозвлащяющую угол наклона
  return 0.1;
}

bool ismark= false;
bool oldAngle=false;
void updateSens() {
  if(!flags.checkD)
    return;
  for (int i = 0; i < 4; i++) {
    ismark = ismark || readSens(&my_sens[i]);
  }
  if(getAngle()>0.9){
    if(oldAngle==false){
      oldAngle=true;
      ismark |=true;
      udpRx.beginPacket(udpAddress, udpPort);
      udpRx.print("ANGLE,");
      udpRx.print(dist);
      udpRx.endPacket();
    }
  }else{
    if(oldAngle==true){
      oldAngle=false;
    }
  }

}

void initAllSensor(){
    for (int i = 0; i < 4; i++) {
    initSens2(&my_sens[i]);
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println();

  initAllSensor();
  initDist();
  motBegin(&motorPwm);
  encoder.attachHalfQuad(11, 12);  // CLK, DT
  encoder.setCount(0);             // Сброс в 0

  if (!WiFi.config(local_IP, gateway, subnet)) {
    Serial.println("Ошибка: Не удалось настроить статический IP");
  }

  Serial.print("Подключение к ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected.");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  if (!udpRx.begin(MY_RX_UDP_PORT)) {  // 7777
    Serial.printf("udpRx: не удалось открыть %u\n", MY_RX_UDP_PORT);
    while (true) delay(1000);  // или другая обработка
  }
  Serial.printf("udpRx слушает %u\n", MY_RX_UDP_PORT);
}
#define MAX_SPEED 4000
int32_t currentSpeed = 0;
uint32_t timer = 0;


bool oldDist = false;

void loop() {
  parseUdpCmd();
  updateSens();
  enc = encoder.getCount();

  dist = (enc >= 0) ? (int32_t)enc : (int32_t)(-enc);

  if (timer < millis()) {
    timer = millis() + 10;

    switch (curSt) {

      case ST_WAIT:
        currentSpeed = 0;

        if (flags.run != 0) {
          curSt = ST_RAZGON;
          initAllSensor();
        }
        break;

      case ST_RAZGON:
        if (flags.run == 0) {
          curSt = ST_BRAKE;
          break;
        }
        if (flags.run > 0 && currentSpeed < MAX_SPEED - 50) {
          currentSpeed += 50;
        } else if (flags.run < 0 && currentSpeed > -(MAX_SPEED - 50)) {
          currentSpeed -= 50;
        }



        if(getDistcm()<50 ){
          if (oldDist==false){
            oldDist=true;
            udpRx.beginPacket(udpAddress, udpPort);
            udpRx.print("WALL,");
            udpRx.print(dist);
            udpRx.endPacket();
          }
          currentSpeed = 0;
        }else{
          if (oldDist==true){
            oldDist=false;
          }
        }

        if(ismark){
          ismark=false;
          //TODO В блокирующем режиме маркируй деффект
        }

        if (flags.isLimit && dist >= flags.limit) {
          curSt = ST_BRAKE;
          flags.run = 0;
        }
        // TODO: проверка на дефект бдлокирующее торможение и маркировка

        break;

      case ST_BRAKE:
        if (currentSpeed == 0) {  // снова поехали
          curSt = ST_WAIT;
          break;
        }

        if (currentSpeed > 0) {
          currentSpeed -= 50;
        } else if (currentSpeed < 0) {
          currentSpeed += 50;
        }

        break;
    }

    if (currentSpeed > 0) {
      motForward(&motorPwm, currentSpeed);
    } else if (currentSpeed < 0) {
      motBackward(&motorPwm, -currentSpeed);
    } else {
      motStop(&motorPwm);
    }
  }
}