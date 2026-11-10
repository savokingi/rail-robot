#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>
#include "helper.h"

const char *ssid     = "Kvant";
const char *password = "KvantKvant";

#define LEFT_HAND_POT   7
#define RIGHT_HAND_POT  6
#define LEFT_LEG_POT    4
#define RIGHT_LEG_POT   5

#define LEG_SPEED    2000
#define HAND_SPEED   2000
#define WHEEL_SPEED  2000

#define PWM_FREQ        1000
#define MOTOR_MAX_SPEED 4095


Adafruit_PWMServoDriver motorPwm = Adafruit_PWMServoDriver();
WebServer server(80);

enum { MOTOR_LEG = 0, MOTOR_HAND = 1, MOTOR_WHEEL = 2 };

struct Motor {
  const char* name;
  uint8_t  pinA;
  uint8_t  pinB;
  uint8_t  potPin;
  uint16_t posIn;
  uint16_t posOut;
  uint8_t  type;
};

Motor motors[] = {
  {"left_leg", 6,  7, LEFT_LEG_POT,   0, 4095, MOTOR_LEG},
  {"right_leg", 4,  5,  RIGHT_LEG_POT,  0, 4095, MOTOR_LEG},
  {"left_hand",  11, 10,  LEFT_HAND_POT,  0, 4095, MOTOR_HAND},
  {"right_hand", 8,  9, RIGHT_HAND_POT, 0, 4095, MOTOR_HAND},
  {"left_weel",  13, 12, 0xFF, 0, 0, MOTOR_WHEEL},
  {"right_weel", 14, 15, 0xFF, 0, 0, MOTOR_WHEEL},
};
const int MOTOR_COUNT = sizeof(motors) / sizeof(motors[0]);

uint16_t legSpeed   = LEG_SPEED;
uint16_t handSpeed  = HAND_SPEED;
uint16_t wheelSpeed = WHEEL_SPEED;

uint16_t speedFor(const Motor& m) {
  switch (m.type) {
    case MOTOR_LEG:   return legSpeed;
    case MOTOR_HAND:  return handSpeed;
    default:          return wheelSpeed;
  }
}

void motorStop(Motor& m) {
  motorPwm.setPWM(m.pinA, 0, 0);
  motorPwm.setPWM(m.pinB, 0, 0);
}
void motorForward(Motor& m) {
  uint16_t sp = speedFor(m);
  motorPwm.setPWM(m.pinA, 0, sp);
  motorPwm.setPWM(m.pinB, 0, 0);
}
void motorBackward(Motor& m) {
  uint16_t sp = speedFor(m);
  motorPwm.setPWM(m.pinA, 0, 0);
  motorPwm.setPWM(m.pinB, 0, sp);
}

/* Оставляем только сырое чтение АЦП */
uint16_t readPotRaw(uint8_t pin) {
  uint32_t s = 0;
  for (int i = 0; i < 4; i++) { s += analogRead(pin); }
  return s / 4;
}

int findMotor(const String& name) {
  for (int i = 0; i < MOTOR_COUNT; i++)
    if (name == motors[i].name) return i;
  return -1;
}

void sendStatus() {
  char buf[2048];
  int len = 0;
  len += snprintf(buf + len, sizeof(buf) - len, "{\"motors\":[");
  
  for (int i = 0; i < MOTOR_COUNT; i++) {
    Motor& m = motors[i];
    len += snprintf(buf + len, sizeof(buf) - len, 
      "%s{\"name\":\"%s\",\"pinA\":%d,\"pinB\":%d,\"hasPot\":%s,\"in\":%d,\"out\":%d",
      i ? "," : "", m.name, m.pinA, m.pinB, (m.potPin != 0xFF) ? "true" : "false", m.posIn, m.posOut);
      
    if (m.potPin != 0xFF) {
      uint16_t pot = readPotRaw(m.potPin); /* Используем сырые данные 0-4095 */
      len += snprintf(buf + len, sizeof(buf) - len, ",\"pot\":%d", pot);
    }
    len += snprintf(buf + len, sizeof(buf) - len, "}");
  }
  len += snprintf(buf + len, sizeof(buf) - len, 
    "],\"speeds\":{\"leg\":%d,\"hand\":%d,\"wheel\":%d}}", legSpeed, handSpeed, wheelSpeed);
    
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "0");
  server.send(200, "application/json", buf);
}

void handleMove() {
  int idx = findMotor(server.arg("motor"));
  if (idx < 0) { server.send(400, "text/plain", "bad motor"); return; }
  String dir = server.arg("dir");
  Motor& m = motors[idx];
  if      (dir == "forward")  motorForward(m);
  else if (dir == "backward") motorBackward(m);
  else                        motorStop(m);
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleStop() {
  String name = server.arg("motor");
  if (name == "all" || name.length() == 0) {
    for (int i = 0; i < MOTOR_COUNT; i++) motorStop(motors[i]);
  } else {
    int idx = findMotor(name);
    if (idx >= 0) motorStop(motors[idx]);
  }
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleReverse() {
  int idx = findMotor(server.arg("motor"));
  if (idx < 0) { server.send(400, "text/plain", "bad motor"); return; }
  Motor& m = motors[idx];
  uint8_t t = m.pinA; m.pinA = m.pinB; m.pinB = t;
  sendStatus();
}

void handleFix() {
  int idx = findMotor(server.arg("motor"));
  if (idx < 0) { server.send(400, "text/plain", "bad motor"); return; }
  Motor& m = motors[idx];
  if (m.potPin == 0xFF) { server.send(400, "text/plain", "no pot"); return; }
  String which = server.arg("which");
  uint16_t v = readPotRaw(m.potPin); /* Сохраняем сырые данные 0-4095 */
  if      (which == "in")  m.posIn  = v;
  else if (which == "out") m.posOut = v;
  else { server.send(400, "text/plain", "bad which"); return; }
  sendStatus();
}

void handleSpeed() {
  String type = server.arg("type");
  long val = server.arg("value").toInt();
  if (val < 0) val = 0;
  if (val > MOTOR_MAX_SPEED) val = MOTOR_MAX_SPEED;

  if      (type == "leg")   legSpeed   = (uint16_t)val;
  else if (type == "hand")  handSpeed  = (uint16_t)val;
  else if (type == "wheel") wheelSpeed = (uint16_t)val;
  else { server.send(400, "text/plain", "bad type"); return; }

  server.send(200, "application/json", "{\"ok\":true}");
}

String buildConfig() {
  String s;
  s.reserve(1024);
  s += "// Скорости\n";
  s += "#define LEG_SPEED   " + String(legSpeed)   + "\n";
  s += "#define HAND_SPEED  " + String(handSpeed)  + "\n";
  s += "#define WHEEL_SPEED " + String(wheelSpeed) + "\n\n";
  s += "// Каналы моторов\n";
  s += "#define LEFT_LEG "   + String(motors[0].pinA) + "," + String(motors[0].pinB) + "\n";
  s += "#define RIGHT_LEG "  + String(motors[1].pinA) + "," + String(motors[1].pinB) + "\n\n";
  s += "#define LEFT_HAND "  + String(motors[2].pinA) + "," + String(motors[2].pinB) + "\n";
  s += "#define RIGHT_HAND " + String(motors[3].pinA) + "," + String(motors[3].pinB) + "\n\n";
  s += "#define LEFT_WEEL "  + String(motors[4].pinA) + "," + String(motors[4].pinB) + "\n";
  s += "#define RIGHT_WEEL " + String(motors[5].pinA) + "," + String(motors[5].pinB) + "\n\n";
  s += "// Крайние положения (сырые значения АЦП 0-4095)\n";
  s += "#define LEFT_HAND_OUT "  + String(motors[2].posOut) + "\n";
  s += "#define RIGHT_HAND_OUT " + String(motors[3].posOut) + "\n";
  s += "#define LEFT_LEG_OUT "   + String(motors[0].posOut) + "\n";
  s += "#define RIGHT_LEG_OUT "  + String(motors[1].posOut) + "\n\n";
  s += "#define LEFT_HAND_IN "   + String(motors[2].posIn) + "\n";
  s += "#define RIGHT_HAND_IN "  + String(motors[3].posIn) + "\n";
  s += "#define LEFT_LEG_IN "    + String(motors[0].posIn) + "\n";
  s += "#define RIGHT_LEG_IN "   + String(motors[1].posIn) + "\n";
  return s;
}

void handleConfig() {
  String cfg = buildConfig();
  Serial.println("=== CONFIG START ===");
  Serial.print(cfg);
  Serial.println("=== CONFIG END ===");
  
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.send(200, "text/plain; charset=utf-8", cfg);
}

void handleRoot() {
  server.sendHeader("Content-Encoding", "gzip");
  server.send_P(200, "text/html", (const char*)html_gz, html_gz_len);
}

void handleNotFound() {
  server.send(404, "text/plain", "404");
}

void setup() {
  Serial.begin(115200);
  delay(200);

  // разрядность 0-4095, диапозон до 3.1в
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  uint8_t potPins[] = { LEFT_HAND_POT, RIGHT_HAND_POT, LEFT_LEG_POT, RIGHT_LEG_POT };
  for (uint8_t p : potPins) pinMode(p, INPUT);

  Wire.begin(41, 42);
  motorPwm.begin();
  motorPwm.setPWMFreq(PWM_FREQ);
  for (int i = 0; i < MOTOR_COUNT; i++) motorStop(motors[i]);

  Serial.print("Connecting to "); Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println();
  
  WiFi.setSleep(false);
  
  Serial.print("IP: "); Serial.println(WiFi.localIP());

  server.on("/",             handleRoot);
  server.on("/api/status",   sendStatus);
  server.on("/api/move",     handleMove);
  server.on("/api/stop",     handleStop);
  server.on("/api/reverse",  handleReverse);
  server.on("/api/fix",      handleFix);
  server.on("/api/speed",    handleSpeed);
  server.on("/api/config",   handleConfig);
  server.onNotFound(handleNotFound);
  server.begin();
}

void loop() {
  server.handleClient();
}