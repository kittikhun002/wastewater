#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <WiFiManager.h>
#include <IOXESP32_4-20mA_Receiver.h>
#include <ArduinoJson.h>
 
// ====== Library สำหรับ OTA ======
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>
 
// ===== Sensor =====
Receiver4_20 sensor_ph(&Wire, 0x44); 
Receiver4_20 sensor_do(&Wire, 0x45);
 
// ===== MQTT (ThingsBoard) =====
const char* mqtt_server = "0.tcp.ap.ngrok.io";
const int mqtt_port = 14158;
const char* token = "JrbWRmMjLTxrI4jtdxjk";
 
WiFiClient espClient;
PubSubClient client(espClient);
 
// ===== OTA Settings =====
String current_version = "1.1"; 
const char* version_url = "https://raw.githubusercontent.com/kittikhun002/wastewater/main/version.txt";
const char* update_url = "https://github.com/kittikhun002/wastewater/releases/latest/download/firmware.bin";
 
// ===== Variables =====
float phValue = 0;
float doValue = 0;
unsigned long lastSend = 0;
const long interval = 600000; // 10 นาที
unsigned long lastReconnectAttempt = 0;
unsigned long lastOtaCheck = 0;
const unsigned long otaInterval = 86400000; // 24 ชม.
 
// ===== Error Correction =====
float calculateError(float current) {
  return 0.008125 * current - 0.0325;
}
 
// ===== OTA Functions =====
String checkGitHubVersion() {
  WiFiClientSecure secureClient;
  secureClient.setInsecure();
  HTTPClient http;
  http.begin(secureClient, version_url);
  int httpCode = http.GET();
  String new_version = "";
  if (httpCode == HTTP_CODE_OK) {
    new_version = http.getString();
    new_version.trim();
  }
  http.end();
  return new_version;
}
 
void doUpdate() {
  WiFiClientSecure secureClient;
  secureClient.setInsecure();
  httpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  Serial.println("🚀 Starting OTA Update...");
  t_httpUpdate_return ret = httpUpdate.update(secureClient, update_url);
}
 
// ===== MQTT Reconnect (Non-Blocking) =====
void reconnect() {
  if (WiFi.status() != WL_CONNECTED) return;
  long now = millis();
  if (now - lastReconnectAttempt > 5000) { // พยายามต่อทุก 5 วินาที
    lastReconnectAttempt = now;
    Serial.print("Connecting to ThingsBoard...");
    if (client.connect("ESP32_Wastewater", token, NULL)) {
      Serial.println("✅ Connected");
    } else {
      Serial.print("❌ Failed, rc=");
      Serial.println(client.state());
    }
  }
}
 
// ===== อ่านค่า Sensor =====
void readSensors() {
  // pH
  if (sensor_ph.measure()) {
    float current = constrain(sensor_ph.current(), 4.0, 20.0);
    float corr = current + calculateError(current);
    phValue = ((corr - 4.0) / 16.0) * 14.0;
    Serial.print("pH: "); Serial.println(phValue, 2);
  }
 
  // DO
  if (sensor_do.measure()) {
    float current = constrain(sensor_do.current(), 4.0, 20.0);
    float corr = current + calculateError(current);
    doValue = ((corr - 4.0) / 16.0) * 10.0;
    Serial.print("DO: "); Serial.println(doValue, 2);
  }
}
 
// ===== Setup =====
void setup() {
  Serial.begin(115200);
  Wire.begin();
  while (!sensor_ph.begin() || !sensor_do.begin()) {
    Serial.println("❌ Sensors not ready, retrying...");
    delay(2000);
  }
  Serial.println("✅ Sensors Ready");
 
  WiFiManager wm;
  wm.autoConnect("ESP32-Wastewater-Setup");
 
  client.setServer(mqtt_server, mqtt_port);
 
  // เช็ก OTA ทันทีที่เปิดเครื่อง
  if (WiFi.status() == WL_CONNECTED) {
    String net_v = checkGitHubVersion();
    if (net_v != "" && net_v != current_version) {
      doUpdate();
    }
  }
}
 
// ===== Loop =====
void loop() {
  // 1. จัดการการเชื่อมต่อ MQTT
  if (!client.connected()) {
    reconnect();
  } else {
    client.loop();
  }
 
  // 2. อ่านและส่งข้อมูลตามรอบเวลา (10 นาที)
  if (millis() - lastSend > interval) {
    lastSend = millis();
    readSensors();
    if (client.connected()) {
      StaticJsonDocument<200> doc;
      doc["ph"] = phValue;
      doc["do"] = doValue;
      char payload[200];
      serializeJson(doc, payload);
      client.publish("v1/devices/me/telemetry", payload);
      Serial.println("📤 Data Sent to ThingsBoard");
    }
  }
 
  // 3. เช็ก OTA ประจำวัน
  if (millis() - lastOtaCheck > otaInterval) {
    lastOtaCheck = millis();
    String net_v = checkGitHubVersion();
    if (net_v != "" && net_v != current_version) {
      doUpdate();
    }
  }
}