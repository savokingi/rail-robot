#include <WiFi.h>
#include <WebServer.h>
#include "helper.h"
#include "motor.h"
#include "regulator.h"

const char *ssid = "Kvant";
const char *password = "KvantKvant";  

WebServer server(80);
Adafruit_PWMServoDriver motorPwm = Adafruit_PWMServoDriver();


typedef enum{
  ST_SET_ON_RAIL,
  ST_ON_RAIL,
  ST_SET_FOLD,
  ST_FOLD,
  ST_DRIVE
}states_t;

const char* stateList[] = {
  "ST_SET_ON_RAIL",
  "ST_ON_RAIL",
  "ST_SET_FOLD",
  "ST_FOLD",
  "ST_DRIVE"
};

typedef struct{
  bool auto_mode = false;
  bool alarm = false;
  bool obstacleDetected = false;
  bool trainRunning = false;
  uint8_t batteryPercent;
  float batteryVoltage;
  float distance;
  int sensorValue;
}info_t;

states_t machineState;
info_t info;

void handleRoot(){
  server.sendHeader("Content-Encoding", "gzip");
  server.send_P(200, "text/html", (const char*)html_gz, html_gz_len);
}

void sendStatus() {
  String j = "{";
  j += "\"batteryPercent\":"   + String(info.batteryPercent) + ",";
  j += "\"batteryVoltage\":"   + String(info.batteryVoltage, 2) + ",";
  j += "\"distanceMeters\":"   + String(info.distance) + ",";
  j += "\"obstacleDetected\":" + String(info.obstacleDetected ? "true" : "false") + ",";
  j += "\"sensorValue\":"      + String(info.sensorValue) + ",";
  j += "\"autoMode\":"         + String(info.auto_mode ? "true" : "false") + ",";
  j += "\"alarm\":"            + String(info.alarm ? "true" : "false") + ",";
  j += "\"trainRunning\":"     + String(info.trainRunning ? "true" : "false") + ",";
  j += "\"machineState\":\""   + String(stateList[machineState]) + "\"";
  j += "}";
  server.send(200, "application/json", j);
}

void handleMode() {
if (!server.hasArg("auto")) {
    server.send(400, "text/plain", "no auto");
    return;
  }
  bool newMode = (server.arg("auto") == "1");
  if (newMode != info.auto_mode) {
    info.auto_mode = newMode;
    info.trainRunning = false;
  }
  Serial.printf("[deviceMode] %s\n", info.auto_mode ? "AUTO_CONTROL" : "HAND_CONTROL");
  sendStatus();
}

void handleCommand() {
  if (!server.hasArg("cmd")) {
    server.send(400, "text/plain", "no cmd");
    return;
  }
  String cmd = server.arg("cmd");
  Serial.printf("[CMD] %s\n", cmd.c_str());

  // Авария работает всегда
  if (cmd == "alarm") {
    info.alarm = !info.alarm;
    if (info.alarm) {
      info.trainRunning = false;
      // TODO: остановить моторы
    }
    sendStatus();
    return;
  }

  // В аварии прочие команды игнорируем
  if (info.alarm) {
    sendStatus();
    return;
  }

  // ----------------- РУЧНОЙ РЕЖИМ -----------------
  if (!info.auto_mode) {
    if (cmd == "transport") {
      // ...
    }
    else if (cmd == "fold") {
      // ...
    }
    else if (cmd == "unfold") {
      // ...
    }
    else if (cmd == "forward") {
      // TODO: Запустить моторы вперед
    }
    else if (cmd == "backward") {
      // TODO: Запустить моторы назад
    }
    else if (cmd == "stop") {
      // TODO: ОСТАНОВИТЬ моторы
    }
  }
  // ----------------- АВТОНОМНЫЙ РЕЖИМ -----------------
  else {
    if (cmd == "train") {
      info.trainRunning = !info.trainRunning;
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

  motBegin(&motorPwm);
  regBegin(&motorPwm);

  Serial.print("Connecting to ");
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

}

void loop() {
  server.handleClient();
  regTick();

  // ----------------- РУЧНОЙ РЕЖИМ -----------------
  if(!info.auto_mode){

  }
  // ----------------- АВТОНОМНЫЙ РЕЖИМ -----------------
  else{

  }

}
