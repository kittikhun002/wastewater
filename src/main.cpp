#include <Arduino.h>
#include <ESP8266WiFi.h>

// ====== Library สำหรับ OTA ของ ESP8266 ======
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <WiFiClientSecure.h>

// ====== ตั้งค่า WiFi ของคุณ ======
const char* ssid = "ใส่ชื่อ_WIFI_ของคุณ"; 
const char* password = "ใส่รหัสผ่าน_WIFI_ของคุณ";

// ====== ลิงก์อมตะ (ดึง Latest Release เสมอ) ======
const char* update_url = "https://github.com/kittikhun002/wastewater/releases/latest/download/firmware.bin";

void doUpdate() {
  WiFiClientSecure otaClient;
  otaClient.setInsecure(); // ข้ามการตรวจ SSL

  Serial.println("\n====================================");
  Serial.println("[OTA] เริ่มต้นตรวจสอบอัปเดตจาก GitHub...");
  Serial.println("====================================");
  
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
      break;
  }
}

void setup() {
  Serial.begin(115200);

  // *** จุดไคลแมกซ์: เปลี่ยนตรงนี้เป็น 2.0 ***
  Serial.println("\n=================================");
  Serial.println(">>> ESP8266 Firmware Version: 2.0 (OTA SUCCESS!) <<<");
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
  
  // ใน V2 จะใส่ doUpdate() ไว้เผื่ออัปเดตเป็น V3 ต่อในอนาคตครับ
  delay(3000); 
  doUpdate(); 
}

void loop() {
  Serial.println("System is running Version 2.0...");
  delay(3000);
}