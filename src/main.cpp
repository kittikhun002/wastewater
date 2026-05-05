#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <WiFiManager.h>
#include <IOXESP32_4-20mA_Receiver.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>

// ===== Global Variables =====
Preferences  preferences;
char         tb_token[40]    = "";
char         device_type[10] = "pre"; // "pre" หรือ "post" — ตั้งค่าผ่าน WiFiManager
String       current_version = "1.6.2"; // อัปเดตเป็น 1.6.2 (เพิ่มระบบ Fail-safe)

Receiver4_20 sensor_ph(&Wire, 0x44);
Receiver4_20 sensor_do(&Wire, 0x45);
bool  ph_ready = false, do_ready = false;
float phValue  = 0,     doValue  = 0;

const char*  mqtt_server = "thingsboard.lesyslab.com";
WiFiClient   espClient;
PubSubClient client(espClient);

String version_url = "";
String update_url  = "";

// ---------------------------------------------------------
// ค่า Calibration แยกตามบ่อ
// สูตร: Error = (m * current) - c
// แก้ตรงนี้เมื่อรู้ค่าจากการวัดจริง
// ---------------------------------------------------------
float pre_m  = 0.008125, pre_c  = 0.0325;
float post_m = 0.008125, post_c = 0.0325;

// ===== Calibration =====
float calculateError(float current, float m, float c) {
  return (m * current) - c;
}

// ===== OTA =====
String checkGitHubVersion() {
  WiFiClientSecure sc; sc.setInsecure();
  HTTPClient http; http.begin(sc, version_url);
  String ver = "";
  if (http.GET() == HTTP_CODE_OK) { ver = http.getString(); ver.trim(); }
  http.end();
  return ver;
}

void doUpdate() {
  WiFiClientSecure sc; sc.setInsecure();
  httpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  Serial.println("🚀 Starting OTA Update...");
  httpUpdate.update(sc, update_url);
}

// ===== MQTT Callback =====
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg = "";
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  if (msg.indexOf("OTA") >= 0) {
    if (checkGitHubVersion() != current_version) doUpdate();
  }
}

// ===== MQTT Reconnect (มี throttle 5 วินาที) =====
void reconnect() {
  if (WiFi.status() != WL_CONNECTED || strlen(tb_token) < 5) return;

  // throttle ป้องกันวิ่งทุก loop
  static unsigned long lastReconnect = 0;
  if (millis() - lastReconnect < 5000) return;
  lastReconnect = millis();

  if (client.connected()) return;

  Serial.print("🔌 Connecting MQTT...");
  if (client.connect("ESP32_Wastewater", tb_token, NULL)) {
    Serial.println(" ✅ Connected");
    client.subscribe("v1/devices/me/rpc/request/+");
    String attr = "{\"firmware_version\":\"" + current_version + "\",\"device_type\":\"" + String(device_type) + "\"}";
    client.publish("v1/devices/me/attributes", attr.c_str());
  } else {
    Serial.println(" ❌ failed rc=" + String(client.state()));
  }
}

// ===== Read Sensors (ทั้งสองบ่ออ่านทั้ง pH และ DO) =====
void readSensors() {
  // เลือกค่า calibration ตามบ่อ
  float m = (strcmp(device_type, "post") == 0) ? post_m : pre_m;
  float c = (strcmp(device_type, "post") == 0) ? post_c : pre_c;

  // อ่าน pH
  if (!ph_ready) ph_ready = sensor_ph.begin();
  if (ph_ready && sensor_ph.measure()) {
    float raw  = constrain(sensor_ph.current(), 4.0, 20.0);
    float corr = raw + calculateError(raw, m, c);
    phValue    = ((corr - 4.0) / 16.0) * 14.0;
    Serial.printf("pH: %.2f (I=%.3f mA)\n", phValue, corr);
  } else {
    Serial.println("❌ pH read error");
  }

  // อ่าน DO
  if (!do_ready) do_ready = sensor_do.begin();
  if (do_ready && sensor_do.measure()) {
    float raw  = constrain(sensor_do.current(), 4.0, 20.0);
    float corr = raw + calculateError(raw, m, c);
    doValue    = ((corr - 4.0) / 16.0) * 10.0;
    Serial.printf("DO: %.2f mg/L (I=%.3f mA)\n", doValue, corr);
  } else {
    Serial.println("❌ DO read error");
  }
}

// ===== Setup =====
void setup() {
  Serial.begin(115200);
  Wire.begin();

  preferences.begin("wastewater", false);
  String saved_token = preferences.getString("token", "");
  String saved_type  = preferences.getString("device_type", "pre");
  saved_token.toCharArray(tb_token, 40);
  saved_type.toCharArray(device_type, 10);

  // WiFiManager — ใส่ token และ device_type ตอนเชื่อม WiFi ครั้งแรก
  WiFiManager wm;
  WiFiManagerParameter custom_token("token", "ThingsBoard Token", tb_token, 40);
  WiFiManagerParameter custom_type("dtype", "pre / post", device_type, 10);
  wm.addParameter(&custom_token);
  wm.addParameter(&custom_type);

  String chipId = String((uint32_t)ESP.getEfuseMac(), HEX);
  chipId.toUpperCase();
  String apName = "Wastewater-" + chipId;

  if (!wm.autoConnect(apName.c_str())) { delay(3000); ESP.restart(); }

  // บันทึกค่าถ้าเปลี่ยน แล้ว restart
  String new_token = String(custom_token.getValue());
  String new_type  = String(custom_type.getValue()); new_type.trim(); new_type.toLowerCase();
  if (new_token != saved_token || new_type != saved_type) {
    preferences.putString("token", new_token);
    preferences.putString("device_type", new_type);
    delay(1000); ESP.restart();
  }

  // สร้าง URL OTA ตาม device_type
  version_url = "https://raw.githubusercontent.com/kittikhun002/wastewater/main/" + String(device_type) + "_version.txt";
  update_url  = "https://github.com/kittikhun002/wastewater/releases/latest/download/" + String(device_type) + "_firmware.bin";

  Serial.println("\n===========================");
  Serial.println("🚀 Wastewater V" + current_version);
  Serial.println("📍 บ่อ: " + String(device_type));
  Serial.println("===========================");

  client.setServer(mqtt_server, 1883);
  client.setCallback(mqttCallback);

  // เช็ก OTA ตอนเปิดเครื่อง
  if (WiFi.status() == WL_CONNECTED && checkGitHubVersion() != current_version) doUpdate();
}

// ===== Loop =====
void loop() {
  // 🛡️ ตัวแปรเก็บเวลาตอนที่เน็ต/ThingsBoard หลุด
  static unsigned long tbOfflineStartTime = millis();

  // จัดการการเชื่อมต่อ MQTT
  if (!client.connected()) {
    reconnect();
    
    // 🛡️ [เพิ่มใหม่] ถ้า ThingsBoard หลุดติดต่อกันเกิน 10 นาที (600,000 ms)
    if (millis() - tbOfflineStartTime > 600000) {
      Serial.println("⚠️ ThingsBoard หลุดเกิน 10 นาที! กำลังเช็กอัปเดตฉุกเฉินบน Git...");
      if (checkGitHubVersion() != current_version) doUpdate();
      
      tbOfflineStartTime = millis(); // รีเซ็ตเวลาเพื่อไม่ให้มันเช็กรัวๆ จนโดนบล็อก
    }
  } else {
    client.loop();
    tbOfflineStartTime = millis(); // ถ้ายืนยันว่ายังเชื่อมต่ออยู่ ให้รีเซ็ตเวลาทิ้งไปเรื่อยๆ
  }

  // ส่งข้อมูลทุก 10 นาที
  static unsigned long lastSend = 0;
  if (millis() - lastSend > 600000) {
    lastSend = millis();
    readSensors();

    if (client.connected()) {
      StaticJsonDocument<256> doc;
      doc["version"]     = current_version;
      doc["device_type"] = device_type;

      // ใช้ Key มาตรฐาน "ph" และ "do" ตรงๆ เลย
      if (ph_ready) doc["ph"] = phValue;
      if (do_ready) doc["do"] = doValue;

      char payload[256];
      serializeJson(doc, payload);
      client.publish("v1/devices/me/telemetry", payload);
      Serial.println("📤 Sent: " + String(payload));
    }
  }

  // เช็ก OTA ปกติทุก 24 ชม. (ทำงานอิสระ ไม่สนว่า ThingsBoard จะหลุดหรือไม่)
  static unsigned long lastOtaCheck = 0;
  if (millis() - lastOtaCheck > 86400000) {
    lastOtaCheck = millis();
    if (checkGitHubVersion() != current_version) doUpdate();
  }
}