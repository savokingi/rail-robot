#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include "helper.h"

#define PWM_POWER 140
#define PWM_FREQ 1000

#define PWM_A D5
#define PWM_B D6

#define OUT_A1 D1
#define OUT_A2 D2

#define OUT_B1 D7
#define OUT_B2 D8

const char* ssid = "RZD_PROJECT";
const char* password = "123456789";

ESP8266WebServer server(80);

typedef enum{
  WAIT,
  FORWARD,
  BACKWARD,
  STOP
} st_t;

st_t st = WAIT;

void handleRoot() {
  server.sendHeader("Content-Encoding", "gzip");
  server.send_P(200, "text/html", (const char*)html_gz, html_gz_len);
}

void handleNotFound() {
    server.send(404, "text/plain", "404: Страница не найдена");
}

void handleF(){
  st = FORWARD;
  server.send(200, "text/plain", "Forward");
}
void handleB(){
  st = BACKWARD;
  server.send(200, "text/plain", "Backward");

}
void handleStop(){
  st = STOP;
  server.send(200, "text/plain", "Stop");

}

void setup() {
  digitalWrite(OUT_A1, 0);
  digitalWrite(OUT_A2, 0);  
  digitalWrite(OUT_B1, 0);
  digitalWrite(OUT_B2, 0);

  pinMode(OUT_A1, OUTPUT);
  pinMode(OUT_A2, OUTPUT);
  pinMode(OUT_B1, OUTPUT);
  pinMode(OUT_B2, OUTPUT);

  pinMode(PWM_A, OUTPUT);
  pinMode(PWM_B, OUTPUT);

  analogWriteFreq(PWM_FREQ); 

  analogWrite(PWM_A, PWM_POWER);
  analogWrite(PWM_B, PWM_POWER);

  Serial.begin(115200);


  Serial.println();
  Serial.print("Настройка точки доступа...");

  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password);
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));

  IPAddress myIP = WiFi.softAPIP();
  Serial.print("Имя сети (SSID): ");
  Serial.println(ssid);
  Serial.print("IP-адрес веб-сервера: ");
  Serial.println(myIP);

    // Настраиваем сервер
  server.on("/", handleRoot);
  server.on("/forward", HTTP_GET, handleF);
  server.on("/backward", HTTP_GET, handleB);
  server.on("/stop", HTTP_GET, handleStop);
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("HTTP server started");

}

void loop() {
  server.handleClient();

  switch(st){
    case WAIT:
      break;
    
    case STOP:
      analogWrite(PWM_A, 0);
      analogWrite(PWM_B, 0);

      digitalWrite(OUT_A1, 0);
      digitalWrite(OUT_A2, 0);
      digitalWrite(OUT_B1, 0);
      digitalWrite(OUT_B2, 0);

      st = WAIT;
      break;

    case FORWARD:
      analogWrite(PWM_A, PWM_POWER);
      analogWrite(PWM_B, PWM_POWER);

      digitalWrite(OUT_A1, 1);
      digitalWrite(OUT_A2, 0);
      digitalWrite(OUT_B1, 1);
      digitalWrite(OUT_B2, 0);

      st = WAIT;
      break;

    case BACKWARD:
      analogWrite(PWM_A, PWM_POWER);
      analogWrite(PWM_B, PWM_POWER);

      digitalWrite(OUT_A1, 0);
      digitalWrite(OUT_A2, 1);
      digitalWrite(OUT_B1, 0);
      digitalWrite(OUT_B2, 1);

      st = WAIT;
      break;  
  }
}
