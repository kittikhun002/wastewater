#include <Arduino.h>
#include <ESP8266WiFi.h>

// ====== Library สำหรับ OTA ของ ESP8266 ======
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <WiFiClientSecure.h>

// ====== ตั้งค่า WiFi ของคุณ ======
const char* ssid = "trueGigatexFiber_ef4_2G"; 
const char* password = "XP6b7E2M";

// ====== ลิงก์อมตะ (ดึง Latest Release เสมอ) ======
const char* update_url = "https://github.com/kittikhun002/wastewater/releases/latest/download/firmware.bin";

void doUpdate() {
  WiFiClientSecure otaClient;
  otaClient.setInsecure(); // ข้ามการตรวจ SSL

  Serial.println("\n====================================");
  Serial.println("[OTA] เริ่มต้นตรวจสอบอัปเดตจาก GitHub...");
  Serial.println("====================================");
  
  // ของ ESP8266 จะใช้คำสั่ง ESPhttpUpdate
  t_httpUpdate_return ret = ESPhttpUpdate.update(otaClient, update_url);

  switch (ret) {
    case HTTP_UPDATE_FAILED:
      Serial.printf("[OTA] อัปเดตล้มเหลว Error (%d): %s\n", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
      break;

    case HTTP_UPDATE_NO_UPDATES:
      Serial.println("[OTA] ไม่มีเวอร์ชันใหม่");
      break;

    case HTTP_UPDATE_OK:
      Serial.println("[OTA] อัปเดตสำเร็จ! บอร์ดกำลังจะ Restart...");
      // ESP8266 จะรีสตาร์ทตัวเองอัตโนมัติเมื่ออัปเดตเสร็จ
      break;
  }
}

void setup() {
  Serial.begin(115200);

  // *** ข้อความแสดงเวอร์ชันปัจจุบัน ***
  Serial.println("\n=================================");
  Serial.println(">>> ESP8266 Firmware Version: 1.0 <<<");
  Serial.println("=================================");

  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  
  // รอ 3 วินาที แล้วเริ่มดึงไฟล์อัปเดต
  delay(3000); 
  doUpdate(); 
}

void loop() {
  Serial.println("System is running Version 1.0... Waiting for updates.");
  delay(3000);
}