#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <WiFiManager.h>
#include <IOXESP32_4-20mA_Receiver.h>
#include <ArduinoJson.h>
#include <Preferences.h> 

// ====== Library สำหรับ OTA ======
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>

// ===== Sensor =====
Receiver4_20 sensor_ph(&Wire, 0x44); 
Receiver4_20 sensor_do(&Wire, 0x45);

bool ph_ready = false;
bool do_ready = false;

// ===== MQTT (ThingsBoard) =====
const char* mqtt_server = "thingsboard.lesyslab.com";
const int mqtt_port = 1883;
char tb_token[40] = ""; 

WiFiClient espClient;
PubSubClient client(espClient);
Preferences preferences; 

// ===== OTA Settings =====
// 🔥 แก้ที่นี่ที่เดียวจบ! ทั้งในระบบ OTA, Serial Monitor และ Dashboard จะเปลี่ยนตามหมด
String current_version = "1.4"; 
const char* version_url = "https://raw.githubusercontent.com/kittikhun002/wastewater/main/version.txt";
const char* update_url = "https://github.com/kittikhun002/wastewater/releases/latest/download/firmware.bin";

// ===== Variables =====
float phValue = 0, doValue = 0;
unsigned long lastSend = 0;
const long interval = 600000; // 10 นาที
unsigned long lastReconnectAttempt = 0;
unsigned long lastOtaCheck = 0;
const unsigned long otaInterval = 86400000; // 24 ชม.
unsigned long offlineStartTime = 0;
bool isOffline = false;

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
  httpUpdate.update(secureClient, update_url);
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (unsigned int i = 0; i < length; i++) message += (char)payload[i];
  if (message.indexOf("OTA") >= 0) {
    String net_v = checkGitHubVersion();
    if (net_v != "" && net_v != current_version) doUpdate();
  }
}

void reconnect() {
  if (WiFi.status() != WL_CONNECTED || strlen(tb_token) < 5) return;
  long now = millis();
  if (now - lastReconnectAttempt > 5000) { 
    lastReconnectAttempt = now;
    if (client.connect("ESP32_Wastewater", tb_token, NULL)) {
      Serial.println("✅ Connected to ThingsBoard");
      client.subscribe("v1/devices/me/rpc/request/+"); 
      
      // ส่งเวอร์ชันขึ้นเป็น Attribute ให้เช็กที่หน้า Device ได้เลย
      String attr = "{\"firmware_version\":\"" + current_version + "\"}";
      client.publish("v1/devices/me/attributes", attr.c_str());
    }
  }
}

void readSensors() {
  if (!ph_ready) ph_ready = sensor_ph.begin();
  if (ph_ready && sensor_ph.measure()) {
    float c = constrain(sensor_ph.current(), 4.0, 20.0);
    phValue = ((c + calculateError(c) - 4.0) / 16.0) * 14.0;
  }
  if (!do_ready) do_ready = sensor_do.begin();
  if (do_ready && sensor_do.measure()) {
    float c = constrain(sensor_do.current(), 4.0, 20.0);
    doValue = ((c + calculateError(c) - 4.0) / 16.0) * 10.0;
  }
}

// ===== Setup =====
void setup() {
  Serial.begin(115200);
  Wire.begin();
  
  // 🚀 ส่วนแสดงเวอร์ชันแบบ Auto
  Serial.println("\n===========================");
  Serial.print("🚀 ยินดีต้อนรับสู่ V"); 
  Serial.print(current_version); 
  Serial.println(" (Ultimate Edition!)");
  Serial.println("===========================");

  preferences.begin("wastewater", false);
  String saved_token = preferences.getString("token", "");
  saved_token.toCharArray(tb_token, 40);

  WiFiManager wm;
  WiFiManagerParameter custom_token("token", "ThingsBoard Token", tb_token, 40);
  wm.addParameter(&custom_token);

  if (!wm.autoConnect("ESP32-Wastewater-Setup-After")) {
    delay(3000); ESP.restart();
  }

  String input_token = String(custom_token.getValue());
  if (input_token != saved_token) {
    preferences.putString("token", input_token);
    input_token.toCharArray(tb_token, 40);
  }

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(mqttCallback);

  if (WiFi.status() == WL_CONNECTED) {
    if (checkGitHubVersion() != current_version) doUpdate();
  }
}

// ===== Loop =====
void loop() {
  if (!client.connected()) {
    reconnect();
    if (!isOffline) { isOffline = true; offlineStartTime = millis(); }
    else if (millis() - offlineStartTime > 600000) {
      if (checkGitHubVersion() != current_version) doUpdate();
      offlineStartTime = millis();
    }
  } else {
    client.loop();
    isOffline = false;
  }

  if (millis() - lastSend > interval) {
    lastSend = millis();
    readSensors();
    
    if (client.connected()) {
      StaticJsonDocument<256> doc;
      if (ph_ready) doc["ph"] = phValue;
      if (do_ready) doc["do"] = doValue;
      
      // ส่งเวอร์ชันขึ้นไปโชว์ในกราฟ/แดชบอร์ดด้วย
      doc["version"] = current_version; 
      
      char payload[256];
      serializeJson(doc, payload);
      client.publish("v1/devices/me/telemetry", payload);
      Serial.print("📤 Sent Data V"); Serial.println(current_version);
    }
  }

  if (millis() - lastOtaCheck > otaInterval) {
    lastOtaCheck = millis();
    if (checkGitHubVersion() != current_version) doUpdate();
  }
}