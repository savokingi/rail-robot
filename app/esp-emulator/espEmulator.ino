/*
  Эмулятор ESP для проверки сервера Rail Robot.

  Подключение: USB Serial, 115200 бод.
  Протокол: один JSON-объект на строку.
  Поддерживает мотор, GPS, энкодер, твердомер, сервопривод краски и watchdog.
*/

#include <Arduino.h>
#include <math.h>

static const uint32_t BAUD_RATE = 115200;
static const float SPEED_MPS = 1.25f;
static const float TICKS_PER_METER = 20.0f;
static const uint32_t WATCHDOG_MS = 1200;
static const uint32_t TELEMETRY_MS = 400;

static const uint8_t ROUTE_POINTS = 23;
static const double ROUTE_LAT[ROUTE_POINTS] = {
  56.3251594, 56.3252689, 56.3257282, 56.3260389, 56.3264601,
  56.3267151, 56.3269862, 56.3272419, 56.3272917, 56.3274963,
  56.3277089, 56.3278687, 56.3280750, 56.3283429, 56.3285871,
  56.3288352, 56.3290552, 56.3292306, 56.3293951, 56.3295254,
  56.3296475, 56.3298686, 56.3301172
};
static const double ROUTE_LON[ROUTE_POINTS] = {
  43.9496451, 43.9496886, 43.9497931, 43.9498452, 43.9498555,
  43.9498223, 43.9497624, 43.9496673, 43.9496488, 43.9495440,
  43.9493976, 43.9492740, 43.9490849, 43.9487924, 43.9484576,
  43.9480520, 43.9476112, 43.9471454, 43.9466046, 43.9461158,
  43.9455775, 43.9443083, 43.9427896
};

double routeOffsets[ROUTE_POINTS];
double routeLength = 0.0;
double distanceMeters = 0.0;
int8_t motorDirection = 0;
uint32_t lastHeartbeatAt = 0;
uint32_t lastTickAt = 0;
uint32_t lastTelemetryAt = 0;
bool measurementPending = false;
uint32_t measurementReadyAt = 0;
String measurementId;
String inputLine;

double distanceBetween(double lat1, double lon1, double lat2, double lon2) {
  const double metersPerLat = 111320.0;
  const double avgLat = (lat1 + lat2) * 0.5 * PI / 180.0;
  const double dx = (lon2 - lon1) * metersPerLat * cos(avgLat);
  const double dy = (lat2 - lat1) * metersPerLat;
  return sqrt(dx * dx + dy * dy);
}

long readLongField(const String &json, const String &field, long fallback = 0) {
  const String key = "\"" + field + "\":";
  int start = json.indexOf(key);
  if (start < 0) return fallback;
  start += key.length();
  return json.substring(start).toInt();
}

String readStringField(const String &json, const String &field) {
  const String key = "\"" + field + "\":\"";
  int start = json.indexOf(key);
  if (start < 0) return "";
  start += key.length();
  int end = json.indexOf('"', start);
  return end < 0 ? "" : json.substring(start, end);
}

void sendAck(long id, bool ok = true) {
  Serial.print("{\"type\":\"ack\",\"id\":");
  Serial.print(id);
  Serial.print(",\"ok\":");
  Serial.print(ok ? "true" : "false");
  Serial.println("}");
}

void routePosition(double target, double &lat, double &lon) {
  target = constrain(target, 0.0, routeLength);
  uint8_t segment = 0;
  while (segment < ROUTE_POINTS - 2 && target > routeOffsets[segment + 1]) segment++;
  const double segmentStart = routeOffsets[segment];
  const double segmentLength = routeOffsets[segment + 1] - segmentStart;
  const double ratio = segmentLength > 0 ? (target - segmentStart) / segmentLength : 0;
  lat = ROUTE_LAT[segment] + (ROUTE_LAT[segment + 1] - ROUTE_LAT[segment]) * ratio;
  lon = ROUTE_LON[segment] + (ROUTE_LON[segment + 1] - ROUTE_LON[segment]) * ratio;
}

void sendTelemetry() {
  double lat, lon;
  routePosition(distanceMeters, lat, lon);
  Serial.print("{\"type\":\"telemetry\",\"lat\":");
  Serial.print(lat, 7);
  Serial.print(",\"lon\":");
  Serial.print(lon, 7);
  Serial.print(",\"speed\":");
  Serial.print(motorDirection == 0 ? 0.0 : SPEED_MPS, 2);
  Serial.print(",\"distanceMeters\":");
  Serial.print(distanceMeters, 3);
  Serial.print(",\"wheelTicks\":");
  Serial.print((uint32_t)(distanceMeters * TICKS_PER_METER));
  Serial.print(",\"motor\":\"");
  Serial.print(motorDirection > 0 ? "forward" : motorDirection < 0 ? "back" : "stop");
  Serial.println("\"}");
}

void processCommand(const String &json) {
  const String command = readStringField(json, "command");
  const long id = readLongField(json, "id", -1);

  if (command == "heartbeat") {
    lastHeartbeatAt = millis();
    const String direction = readStringField(json, "direction");
    motorDirection = direction == "back" ? -1 : 1;
    return;
  }
  if (command == "reset") {
    distanceMeters = 0;
    motorDirection = 0;
    sendAck(id);
  } else if (command == "move") {
    motorDirection = readStringField(json, "direction") == "back" ? -1 : 1;
    lastHeartbeatAt = millis();
    sendAck(id);
  } else if (command == "stop") {
    motorDirection = 0;
    sendAck(id);
  } else if (command == "measure") {
    measurementId = readStringField(json, "measurementId");
    measurementPending = true;
    measurementReadyAt = millis() + 900;
    sendAck(id);
  } else if (command == "mark") {
    sendAck(id);
    Serial.print("{\"type\":\"marked\",\"measurementId\":\"");
    Serial.print(readStringField(json, "measurementId"));
    Serial.println("\"}");
  } else {
    sendAck(id, false);
  }
}

void setup() {
  Serial.begin(BAUD_RATE);
  randomSeed(micros());
  routeOffsets[0] = 0;
  for (uint8_t i = 1; i < ROUTE_POINTS; i++) {
    routeLength += distanceBetween(ROUTE_LAT[i - 1], ROUTE_LON[i - 1], ROUTE_LAT[i], ROUTE_LON[i]);
    routeOffsets[i] = routeLength;
  }
  lastTickAt = millis();
  lastHeartbeatAt = millis();
}

void loop() {
  while (Serial.available()) {
    char value = (char)Serial.read();
    if (value == '\n') {
      inputLine.trim();
      if (inputLine.length()) processCommand(inputLine);
      inputLine = "";
    } else if (value != '\r' && inputLine.length() < 500) {
      inputLine += value;
    }
  }

  const uint32_t now = millis();
  const double elapsed = min((uint32_t)500, now - lastTickAt) / 1000.0;
  lastTickAt = now;

  if (motorDirection != 0 && now - lastHeartbeatAt > WATCHDOG_MS) motorDirection = 0;
  distanceMeters += motorDirection * SPEED_MPS * elapsed;
  distanceMeters = constrain(distanceMeters, 0.0, routeLength);

  if (measurementPending && (int32_t)(now - measurementReadyAt) >= 0) {
    measurementPending = false;
    const bool defect = random(0, 100) < 20;
    const int hardness = defect ? random(790, 861) : random(590, 691);
    Serial.print("{\"type\":\"measurement\",\"measurementId\":\"");
    Serial.print(measurementId);
    Serial.print("\",\"hardness\":");
    Serial.print(hardness);
    Serial.print(",\"status\":\"");
    Serial.print(defect ? "defect" : "ok");
    Serial.println("\"}");
  }

  if (now - lastTelemetryAt >= TELEMETRY_MS) {
    lastTelemetryAt = now;
    sendTelemetry();
  }
}
