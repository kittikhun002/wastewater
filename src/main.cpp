#include <Arduino.h>
#include <ESP8266WiFi.h>

// ====== Library สำหรับ OTA ======
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <WiFiClientSecure.h>

// ====== ตั้งค่า WiFi ======
const char* ssid = "trueGigatexFiber_ef4_2G"; 
const char* password = "XP6b7E2M";

// ====== ตั้งค่าเวอร์ชัน ======
String current_version = "5.0"; // โค้ดเวอร์ชันปัจจุบัน

const char* update_url = "https://github.com/kittikhun002/wastewater/releases/latest/download/firmware.bin";
const char* version_url = "https://raw.githubusercontent.com/kittikhun002/wastewater/main/version.txt";

// ====== ตัวแปรสำหรับจับเวลา (Timer) ======
unsigned long lastCheckTime = 0;
// สำหรับตอนเทส: ให้เช็กทุก 1 นาที (60,000 มิลลิวินาที)
// สำหรับของจริง: แนะนำให้แก้เป็น 1 ชั่วโมง (3,600,000 มิลลิวินาที)
const unsigned long checkInterval = 60000; 

// ฟังก์ชันวิ่งไปแอบดูเลขบน GitHub
String checkGitHubVersion() {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  
  http.begin(client, version_url);
  int httpCode = http.GET(); 
  
  String new_version = "";
  if (httpCode == HTTP_CODE_OK) {
    new_version = http.getString();
    new_version.trim(); // ตัดช่องว่าง
  } 
  http.end();
  return new_version;
}

// ฟังก์ชันโหลดไฟล์อัปเดต
void doUpdate() {
  WiFiClientSecure otaClient;
  otaClient.setInsecure();
  ESPhttpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  
  Serial.println("\n[OTA] เริ่มดาวน์โหลดไฟล์ Firmware...");
  t_httpUpdate_return ret = ESPhttpUpdate.update(otaClient, update_url);

  if (ret == HTTP_UPDATE_FAILED) {
    Serial.printf("[OTA] ล้มเหลว Error (%d): %s\n", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n=================================");
  Serial.println(">>> ESP8266 Firmware V: " + current_version + " <<<");
  Serial.println("=================================");

  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\nWiFi connected!");
  
  // ให้เช็ก 1 รอบตอนเพิ่งเสียบปลั๊ก เพื่อความชัวร์
  String available_version = checkGitHubVersion();
  if (available_version != "" && available_version != current_version) {
    doUpdate(); 
  }
}

void loop() {
  // ----------------------------------------------------
  // 1. งานหลัก: จำลองการทำงานของโปรเจกต์คุณ (อ่านเซนเซอร์/ส่งข้อมูล)
  // (อนาคตเอาโค้ดอ่านค่า DO/pH มาแทรกตรงนี้ได้เลย)
  // ----------------------------------------------------
  Serial.println("กำลังอ่านค่าเซนเซอร์น้ำเสีย... (สมมุติ)");
  delay(2000); // สมมุติว่าใช้เวลาอ่านค่า 2 วินาที


  // ----------------------------------------------------
  // 2. งานรอง: ระบบแอบเช็ก OTA แบบอัตโนมัติ 
  // (มันจะไม่ทำงานจนกว่าจะครบ 1 นาทีตามที่ตั้งไว้)
  // ----------------------------------------------------
  if (millis() - lastCheckTime >= checkInterval) {
    lastCheckTime = millis(); // รีเซ็ตนาฬิกา
    
    Serial.println("\n[Auto-Check] ได้เวลาเช็กอัปเดตประจำรอบ...");
    String available_version = checkGitHubVersion();
    
    if (available_version != "" && available_version != current_version) {
      Serial.println(">>> เจอเวอร์ชันใหม่ (" + available_version + ")! สั่งดาวน์โหลดเดี๋ยวนี้!");
      doUpdate();
    } else {
      Serial.println(">>> ยังเป็นเวอร์ชัน " + current_version + " ลุยงานหลักต่อได้เลย!");
    }
  }
}