#include <Arduino.h>
#include <Wire.h>
#include <IOXESP32_4-20mA_Receiver.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

Receiver4_20 sensor(&Wire, 0x44);

// ====== ตั้งค่า WiFi ======
const char* ssid = "trueGigatexFiber_ef4_2G";          // แก้ไข WiFi name
const char* password = "XP6b7E2M";   // แก้ไข WiFi password

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

// ====== ตัวแปรสำหรับส่ง MQTT ======
unsigned long lastMqttTime = 0;
const unsigned long mqttInterval = 5000;  // ส่งทุก 5 วินาที

void setup_wifi() {
  Serial.print("\nConnecting to WiFi: ");
  Serial.println(ssid);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nFailed to connect to WiFi");
  }
}

void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived on topic: ");
  Serial.println(topic);
  Serial.print("Message: ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
}

void reconnect_mqtt() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection to Thingsboard...");
    
    // เชื่อมต่อกับ Thingsboard โดยใช้ Access Token โดยไม่ต้องรหัสผ่าน
    if (client.connect(mqtt_client_id, mqtt_access_token, "")) {
      Serial.println("connected!");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  Wire.begin();

  while (!sensor.begin()) {
    Serial.println("Not found hardware :(");
    delay(2000);
  }

  Serial.println("\n4-20mA pH Reader Start");
  
  // เชื่อมต่อ WiFi
  setup_wifi();
  
  // ตั้งค่า MQTT
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(mqtt_callback);
}

void loop() {
  delay(200);

  // รักษาการเชื่อมต่อ MQTT
  if (!client.connected()) {
    if (WiFi.status() == WL_CONNECTED) {
      reconnect_mqtt();
    }
  }
  client.loop();

  // อ่านข้อมูลจากเซ็นเซอร์
  if (sensor.measure()) {
    float current = sensor.current();  // อ่านกระแส
    
    // ====== จำกัดช่วง 4-20mA ======
    if (current < 4.0) current = 4.0;
    if (current > 20.0) current = 20.0;

    // ====== แมพ 4-20mA → pH ======
    float ph = ((current - 4.0) / 16.0) * (PH_MAX - PH_MIN) + PH_MIN;

    Serial.print("Current: ");
    Serial.print(current, 2);
    Serial.print(" mA  |  pH: ");
    Serial.println(ph, 2);

    // ส่งข้อมูลไป Thingsboard ทุก mqttInterval
    if (millis() - lastMqttTime >= mqttInterval) {
      lastMqttTime = millis();
      
      if (client.connected()) {
        // สร้าง JSON payload (แค่ pH)
        StaticJsonDocument<200> doc;
        doc["ph"] = ph;
        
        // แปลง JSON เป็น string
        char payload[200];
        serializeJson(doc, payload);
        
        // ส่งไป Thingsboard โดยใช้ topic v1/devices/me/telemetry
        client.publish("v1/devices/me/telemetry", payload);
        
        Serial.print("Data sent to Thingsboard: ");
        Serial.println(payload);
      }
    }
  } else {
    Serial.println("Hardware error !");
  }
}