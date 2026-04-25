#include <Arduino.h>
#include <Wire.h>
#include <IOXESP32_4-20mA_Receiver.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// ====== Library สำหรับ OTA ของ ESP32 ======
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>

Receiver4_20 sensor(&Wire, 0x44);

// ====== ตั้งค่า WiFi ======
const char* ssid = "trueGigatexFiber_ef4_2G";          
const char* password = "XP6b7E2M";   

// ====== ตั้งค่า MQTT (Thingsboard) ======
const char* mqtt_server = "tboard.ngrok.app";  
const int mqtt_port = 1883;
const char* mqtt_access_token = "JrbWRmMjLTxrI4jtdxjk";  
const char* mqtt_client_id = "WaterSensor_001";

WiFiClient espClient;
PubSubClient client(espClient);

// ====== ตั้งค่าช่วง pH ======
#define PH_MIN 0.0
#define PH_MAX 14.0

// ====== ตัวแปรสำหรับจับเวลา ======
unsigned long lastMqttTime = 0;
const unsigned long mqttInterval = 5000;  // ส่งข้อมูลเซนเซอร์ทุก 5 วินาที

unsigned long lastReconnectAttempt = 0;   // [เพิ่มใหม่] จับเวลาต่อ ThingsBoard

unsigned long lastOtaCheckTime = 0;
const unsigned long otaCheckInterval = 86400000; // เช็กอัปเดตวันละ 1 ครั้ง (24 ชั่วโมง)

// ====== ตัวแปรสำหรับ OTA ======
String current_version = "1.1"; // อัปเดตเป็น V1.1 (Non-Blocking)
const char* update_url = "https://github.com/kittikhun002/wastewater/releases/latest/download/firmware.bin";
const char* version_url = "https://raw.githubusercontent.com/kittikhun002/wastewater/main/version.txt";

// ------------------------------------------------------------------
// ฟังก์ชัน OTA
// ------------------------------------------------------------------
String checkGitHubVersion() {
  WiFiClientSecure client;
  client.setInsecure(); 
  HTTPClient http;
  
  http.begin(client, version_url);
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
  WiFiClientSecure otaClient;
  otaClient.setInsecure();
  httpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  
  Serial.println("\n[OTA] เริ่มดาวน์โหลดไฟล์ Firmware...");
  t_httpUpdate_return ret = httpUpdate.update(otaClient, update_url);

  if (ret == HTTP_UPDATE_FAILED) {
    Serial.printf("[OTA] ล้มเหลว Error (%d): %s\n", httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
  }
}

// ------------------------------------------------------------------
// ฟังก์ชัน WiFi
// ------------------------------------------------------------------
void setup_wifi() {
  Serial.print("\nConnecting to WiFi: ");
  Serial.println(ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500); Serial.print("."); attempts++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected! IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nFailed to connect to WiFi");
  }
}

void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  // รับข้อความจาก ThingsBoard
}

// ------------------------------------------------------------------
// SETUP
// ------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  Wire.begin();

  Serial.println("\n=================================");
  Serial.println(">>> Wastewater System V: " + current_version + " <<<");
  Serial.println("=================================");

  while (!sensor.begin()) {
    Serial.println("Not found hardware :(");
    delay(2000);
  }
  Serial.println("4-20mA pH Reader Ready");
  
  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(mqtt_callback);

  // เช็กอัปเดต 1 รอบตอนเพิ่งเสียบปลั๊กไฟ
  if (WiFi.status() == WL_CONNECTED) {
    String available_version = checkGitHubVersion();
    if (available_version != "" && available_version != current_version) {
      Serial.println(">>> เจออัปเดตใหม่ตอนเปิดเครื่อง! กำลังโหลด...");
      doUpdate(); 
    }
  }
}

// ------------------------------------------------------------------
// MAIN LOOP
// ------------------------------------------------------------------
void loop() {
  delay(200);

  // ---------------------------------------------------------
  // 1. ระบบรักษาการเชื่อมต่อ MQTT (Non-Blocking)
  // ---------------------------------------------------------
  if (!client.connected()) {
    if (WiFi.status() == WL_CONNECTED) {
      long now = millis();
      // ลองต่อใหม่ทุกๆ 5 วินาทีเท่านั้น จะไม่วนลูปค้าง (ไม่มี while แล้ว)
      if (now - lastReconnectAttempt > 5000) {
        lastReconnectAttempt = now;
        Serial.print("Attempting MQTT connection to Thingsboard...");
        if (client.connect(mqtt_client_id, mqtt_access_token, "")) {
          Serial.println("connected!");
          lastReconnectAttempt = 0; // รีเซ็ตเวลาเมื่อต่อติด
        } else {
          Serial.print("failed, rc=");
          Serial.print(client.state());
          Serial.println(" try again in 5 seconds");
        }
      }
    }
  } else {
    client.loop(); // ให้ทำงานเฉพาะตอนที่ต่อติดแล้ว
  }

  // ---------------------------------------------------------
  // 2. งานหลัก: อ่านค่าเซนเซอร์และส่งขึ้น ThingsBoard
  // ---------------------------------------------------------
  if (sensor.measure()) {
    float current = sensor.current(); 
    if (current < 4.0) current = 4.0;
    if (current > 20.0) current = 20.0;

    float ph = ((current - 4.0) / 16.0) * (PH_MAX - PH_MIN) + PH_MIN;

    // ส่งข้อมูลทุกๆ 5 วินาที (และต้องต่อ ThingsBoard ติดอยู่ถึงจะส่ง)
    if (millis() - lastMqttTime >= mqttInterval) {
      lastMqttTime = millis();
      
      Serial.print("Current: "); Serial.print(current, 2);
      Serial.print(" mA  |  pH: "); Serial.println(ph, 2);

      if (client.connected()) {
        StaticJsonDocument<200> doc;
        doc["ph"] = ph;
        char payload[200];
        serializeJson(doc, payload);
        client.publish("v1/devices/me/telemetry", payload);
      }
    }
  } else {
    Serial.println("Hardware error !");
  }

  // ---------------------------------------------------------
  // 3. งานรอง: ระบบ OTA แอบเช็กวันละ 1 ครั้ง
  // ---------------------------------------------------------
  if (millis() - lastOtaCheckTime >= otaCheckInterval) {
    lastOtaCheckTime = millis(); 
    
    Serial.println("\n[Auto-Check] ได้เวลาเช็กอัปเดตประจำวัน...");
    String available_version = checkGitHubVersion();
    
    if (available_version != "" && available_version != current_version) {
      Serial.println(">>> เจอเวอร์ชันใหม่ (" + available_version + ")! สั่งดาวน์โหลดเดี๋ยวนี้!");
      doUpdate();
    } else {
      Serial.println(">>> ระบบเป็นเวอร์ชันล่าสุดแล้ว ลุยงานต่อ!");
    }
  }
}