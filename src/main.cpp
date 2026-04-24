#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>

const char* ssid = "Wokwi-GUEST"; 
const char* password = "";

// ใช้ลิงก์อมตะ (Latest)
const char* update_url = "https://github.com/kittikhun002/wastewater/releases/latest/download/firmware.bin";

void doUpdate() {
  WiFiClientSecure otaClient;
  otaClient.setInsecure();
  httpUpdate.update(otaClient, update_url);
}

void setup() {
  Serial.begin(115200);
  
  // *** จุดที่เปลี่ยนคือตรงนี้ครับ เป็น 2.0 ***
  Serial.println("\n=================================");
  Serial.println(">>> Firmware Version: 2.0 (SUCCESS!) <<<");
  Serial.println("=================================");

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  
  // ใน V2 ไม่ต้องเรียก doUpdate ใน setup แล้วก็ได้ครับ 
  // หรือจะใส่ไว้เผื่ออัปเดตเป็น V3 ต่อไปก็ได้
}

void loop() {
  Serial.println("V2 is running perfectly...");
  delay(2000);
}