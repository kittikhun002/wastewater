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

bool ph_ready = false;
bool do_ready = false;

// ===== MQTT (ThingsBoard) =====
const char* mqtt_server = "thingsboard.lesyslab.com";
const int mqtt_port = 1883;
const char* token = "JrbWRmMjLTxrI4jtdxjk";

WiFiClient espClient;
PubSubClient client(espClient);

// ===== OTA Settings =====
String current_version = "1.3"; 
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

// 🚨 ตัวแปรสำหรับ Panic Mode (โหมดหนีตาย)
unsigned long offlineStartTime = 0;
bool isOffline = false;

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

// 🎧 ===== ฟังก์ชันรับคำสั่งด่วน (หูทิพย์ MQTT) =====
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  
  Serial.print("📩 ได้รับคำสั่งจากศูนย์: ");
  Serial.println(message);

  // ถ้าในข้อความมีคำว่า "OTA" ให้เริ่มดึงโค้ดทันที!
  if (message.indexOf("OTA") >= 0) {
    Serial.println("🔥 ได้รับคำสั่งบังคับอัปเดต! กำลังวิ่งไปเช็ก GitHub...");
    String net_v = checkGitHubVersion();
    if (net_v != "" && net_v != current_version) {
      doUpdate();
    } else {
      Serial.println("✅ โค้ดในเครื่องเป็นเวอร์ชันล่าสุดอยู่แล้ว ไม่ต้องอัปเดต");
    }
  }
}

// ===== MQTT Reconnect (Non-Blocking) =====
void reconnect() {
  if (WiFi.status() != WL_CONNECTED) return;
  long now = millis();
  if (now - lastReconnectAttempt > 5000) { 
    lastReconnectAttempt = now;
    Serial.print("Connecting to ThingsBoard...");
    if (client.connect("ESP32_Wastewater", token, NULL)) {
      Serial.println("✅ Connected");
      
      // 📡 สมัครรับคำสั่งจาก ThingsBoard
      client.subscribe("v1/devices/me/rpc/request/+"); 
      client.subscribe("v1/devices/me/attributes");
      
    } else {
      Serial.print("❌ Failed, rc=");
      Serial.println(client.state());
    }
  }
}

// ===== อ่านค่า Sensor (ระบบฟื้นฟูตัวเอง) =====
void readSensors() {
  // --- ระบบของ pH ---
  if (!ph_ready) ph_ready = sensor_ph.begin();
  if (ph_ready) {
    if (sensor_ph.measure()) {
      float current = constrain(sensor_ph.current(), 4.0, 20.0);
      float corr = current + calculateError(current);
      phValue = ((corr - 4.0) / 16.0) * 14.0;
      Serial.print("pH: "); Serial.println(phValue, 2);
    } else {
      Serial.println("⚠️ pH Sensor lost connection!");
      ph_ready = false;
    }
  } else {
    Serial.println("❌ pH Sensor not connected!");
  }

  // --- ระบบของ DO ---
  if (!do_ready) do_ready = sensor_do.begin();
  if (do_ready) {
    if (sensor_do.measure()) {
      float current = constrain(sensor_do.current(), 4.0, 20.0);
      float corr = current + calculateError(current);
      doValue = ((corr - 4.0) / 16.0) * 10.0;
      Serial.print("DO: "); Serial.println(doValue, 2);
    } else {
      Serial.println("⚠️ DO Sensor lost connection!");
      do_ready = false;
    }
  } else {
    Serial.println("❌ DO Sensor not connected!");
  }
}

// ===== Setup =====
void setup() {
  Serial.begin(115200);
  Wire.begin();
  
  Serial.println("\n===========================");
  Serial.println("🚀 ยินดีต้อนรับสู่ V1.3 (Ultimate Edition!)");
  Serial.println("===========================");

  ph_ready = sensor_ph.begin();
  do_ready = sensor_do.begin();
  if (ph_ready && do_ready) {
    Serial.println("✅ All Sensors Ready");
  } else {
    Serial.println("⚠️ Some sensors not found, will retry automatically...");
  }

  WiFiManager wm;
  wm.autoConnect("ESP32-Wastewater-Setup");

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(mqttCallback); // 🎧 เปิดระบบหูทิพย์

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
  // 1. จัดการการเชื่อมต่อ MQTT & 🚨 Panic Mode
  if (!client.connected()) {
    reconnect();
    
    // --- เริ่มจับเวลา Panic Mode ---
    if (!isOffline) {
      isOffline = true;
      offlineStartTime = millis(); 
    } else {
      // ถ้าหลุดต่อเนื่องเกิน 10 นาที (600000 มิลลิวินาที)
      if (millis() - offlineStartTime > 600000) {
        Serial.println("🚨 Panic Mode: ThingsBoard ล่มเกิน 10 นาที! วิ่งไปเช็ก GitHub แป๊บ...");
        String net_v = checkGitHubVersion();
        
        if (net_v != "" && net_v != current_version) {
          doUpdate(); // เจอโค้ดใหม่! อัปเดตเพื่อหนีตายทันที
        } else {
          Serial.println("✅ ไม่มีโค้ดใหม่บน GitHub, กลับไปพยายามต่อ ThingsBoard ต่อ...");
          offlineStartTime = millis(); // รีเซ็ตเวลานับ 10 นาทีใหม่
        }
      }
    }
  } else {
    client.loop(); // ทำงานปกติ
    if (isOffline) {
      isOffline = false; // ถ้ากลับมาต่อติดแล้ว ให้ยกเลิกสถานะ Panic Mode
    }
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

  // 3. เช็ก OTA ประจำวัน (ร่มชูชีพสำรอง)
  if (millis() - lastOtaCheck > otaInterval) {
    lastOtaCheck = millis();
    String net_v = checkGitHubVersion();
    if (net_v != "" && net_v != current_version) {
      doUpdate();
    }
  }
}