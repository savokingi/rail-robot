#include <WiFi.h>
#include <WebServer.h>
#include <WiFiUdp.h>

#include "helper.h"
#include "motor.h"
#include "regulator.h"
#include "sensor.h"

const char *ssid = "Kvant";
const char *password = "KvantKvant";  
const int UDP_PORT = 8888;

// Настройки статического IP-адреса
IPAddress local_IP(192, 168, 1, 134);  // Желаемый IP-адрес для ESP32
IPAddress gateway(192, 168, 1, 1);     // Шлюз
IPAddress subnet(255, 255, 255, 0);    // Мас ка подсети
IPAddress bcast_IP(255,255,255, 255);

WebServer server(80);
WiFiUDP udp;
Adafruit_PWMServoDriver motorPwm = Adafruit_PWMServoDriver();

typedef enum{
  ST_SET_H_CONTROL,
  ST_H_CONTROL,
  ST_SET_AUTO,
  ST_SET_ON_RAIL,
  ST_ON_RAIL,
  ST_SET_FOLD,
  ST_FOLD,
  ST_DRIVE,
  ST_OBST_DETECTED
}states_t;

const char* stateList[] = {
  "ST_SET_H_CONTROL",
  "ST_H_CONTROL",
  "ST_SET_AUTO",
  "ST_SET_ON_RAIL",
  "ST_ON_RAIL",
  "ST_SET_FOLD",
  "ST_FOLD",
  "ST_DRIVE",
  "ST_OBST_DETECTED"
};

typedef struct {
  bool autoM = false;
  bool sirenOn = false;
  bool markOn = false;  // промаркировать
  bool alarm = false;                     
  bool trRun = false;

  states_t mSt = ST_SET_H_CONTROL;     // Состояние машины
  uint32_t enc;                           // Количество импульсов с энкодера
  float bV;                               // Напряжение батареи
  bool obst = false;                      // Препятствие
  uint16_t dist;                           // Дистанция с датчика расстояния
  uint16_t sL[2][8];                      // x и y с левого датчика
  uint16_t sR[2][8];                      // x и y с правого датчика

} info_t;
info_t info;

void handleRoot(){
  server.sendHeader("Content-Encoding", "gzip");
  server.send_P(200, "text/html", (const char*)html_gz, html_gz_len);
}

void sendStatus() {
  String j = "{";
  j += "\"auto\":"    + String(info.autoM  ? "true" : "false") + ",";
  j += "\"alarm\":"   + String(info.alarm  ? "true" : "false") + ",";
  j += "\"trRun\":"   + String(info.trRun  ? "true" : "false") + ",";
  j += "\"sirenOn\":" + String(info.sirenOn ? "true" : "false");
  j += "}";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", j);
}

void handleMode() {
  if (!server.hasArg("auto")) {
      server.send(400, "text/plain", "no auto");
      return;
    }
    bool newMode = (server.arg("auto") == "1");
    if (newMode != info.autoM) {
      info.autoM = newMode;
      info.trRun = false;
      info.mSt = (info.autoM ? ST_SET_AUTO : ST_SET_H_CONTROL);
    }
    Serial.printf("[autoM] %s\n", info.autoM ? "true" : "false");
    Serial.printf("[mSt] %s\n", stateList[info.mSt]);
    sendStatus();
}

void handleCommand() {
  if (!server.hasArg("cmd")) {
    server.send(400, "text/plain", "no cmd");
    return;
  }

  String cmd = server.arg("cmd");
  Serial.printf("[CMD] %s\n", cmd.c_str());

  // Авария — переключатель, работает всегда
  if (cmd == "alarm") {
    info.alarm = !info.alarm;

    if (info.alarm) {
      info.trRun = false;
      // Останавливаем моторы при аварии
      regStop();
    }

    sendStatus();
    return;
  }

  // Сирена — кнопка в HTML видна всегда, поэтому обрабатываем до проверки alarm
  if (cmd == "siren_on") {
    info.sirenOn = true;
    sendStatus();
    return;
  }

  if (cmd == "siren_off") {
    info.sirenOn = false;
    sendStatus();
    return;
  }

  // В аварии остальные команды игнорируем
  if (info.alarm) {
    sendStatus();
    return;
  }

  // Маркировка дефекта — доступна в обоих режимах
  if (cmd == "mark_defect") {
    info.markOn = true;
    sendStatus();
    return;
  }

  // ----------------- РУЧНОЙ РЕЖИМ -----------------
  if (!info.autoM) {
    if (cmd == "transport") {
      setTransport();
    }
    else if (cmd == "fold") {
      setFold();
    }
    else if (cmd == "unfold") {
      setOnRail();
    }
    else if (cmd == "forward") {
      Serial.println("writefor");
      motForward(&motorPwm, LEFT_WEEL, WEEL_SPEED);
      motForward(&motorPwm, RIGHT_WEEL, WEEL_SPEED);
    }
    else if (cmd == "backward") {
      Serial.println("writeback");
      motBackward(&motorPwm, LEFT_WEEL, WEEL_SPEED);
      motBackward(&motorPwm, RIGHT_WEEL, WEEL_SPEED);
    }
    else if (cmd == "stop") {
      motStop(&motorPwm, LEFT_WEEL);
      motStop(&motorPwm, RIGHT_WEEL);
    }
  }
  // ----------------- АВТОНОМНЫЙ РЕЖИМ -----------------
  else {
    if (cmd == "train") {
      info.trRun = !info.trRun;
    }
  }

  sendStatus();
}
void handleNotFound(){
  server.send(404, "text/plain", "404: Страница не найдена");
}



void setup() {
  Serial.begin(115200);
  Serial.println();

  // разрядность 0-4095, диапозон до 3.1в
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  motBegin(&motorPwm);
  regBegin(&motorPwm);
  initDist();

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

  server.on("/", handleRoot);
  server.on("/api/status", sendStatus);
  server.on("/api/mode", handleMode);
  server.on("/api/command", handleCommand);
  server.onNotFound(handleNotFound);

  // Запуск сервера
  server.begin();

  softInitMotor();

}

void loop() {
  server.handleClient();
  regTick();
  checkBat(&info.bV);
  checkDist(&info.dist);

  if(info.markOn){
    motStopAll(&motorPwm); // стопим моторы
    info.markOn = 0;
    // TODO блокирующя функция маркировки

  }
  switch(info.mSt){
    case ST_SET_H_CONTROL:
    motStopAll(&motorPwm);
    info.mSt = ST_H_CONTROL;
    break;

    case ST_H_CONTROL:
    // ожидаем команд с вэба
    break;

    case ST_SET_AUTO:
      motStopAll(&motorPwm);
      info.mSt = ST_SET_ON_RAIL;
    break;

    case ST_SET_ON_RAIL:
    break;

    case ST_ON_RAIL:
    break;

    case ST_SET_FOLD:
    break;

    case ST_FOLD:
    break;

    case ST_DRIVE:
    break;

    case ST_OBST_DETECTED:
    break;
  }

}
