#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h> 

// --- CẤU HÌNH UART ---
#define RXD2 16
#define TXD2 17
#define UART_BAUD 115200

// --- CẤU HÌNH LOGIC TRANSISTOR ---
#define RELAY_ACTIVE_LOW  false 
#if defined(RELAY_ACTIVE_LOW) && RELAY_ACTIVE_LOW
  #define DEVICE_ON   LOW
  #define DEVICE_OFF  HIGH
#else
  #define DEVICE_ON   HIGH
  #define DEVICE_OFF  LOW
#endif

// --- CẤU HÌNH AUTOMATION ---
int light_on_threshold  = 40;   
int light_off_threshold = 60;   
unsigned long pump_min_time = 1000; 
unsigned long pump_start_time = 0;

// Cờ chặn điều khiển
bool auto_light_enabled = false; 
bool auto_irrigation_enabled = false;

// --- KHAI BÁO CHÂN ---
#define PIN_LIGHT 32  
#define PIN_PUMP  33  

// --- WIFI ---
const char* ssid = "iPhone";      
const char* password = "123456789";     

IPAddress local_ip(172, 20, 10, 5);     
IPAddress gateway(172, 20, 10, 1);        
IPAddress subnet(255, 255, 255, 240);
IPAddress primaryDNS(8, 8, 8, 8);

WebServer server(80);

// Biến trạng thái
float temp = 0.0;
float humid = 0.0;
int light = 0;
int soil = 0;
bool irrigation_on = false;
bool light_on = false;

// --- HÀM LỌC NHIỄU VÀ ĐỌC DỮ LIỆU ---
void readSTM32Data() {
  while (Serial2.available()) {
    // Đọc từng dòng, loại bỏ rác
    String input = Serial2.readStringUntil('\n');
    input.trim();
    
    // Tìm vị trí dấu ngoặc nhọn đầu và cuối để lọc sạch JSON
    int firstBrace = input.indexOf('{');
    int lastBrace = input.lastIndexOf('}');

    if (firstBrace != -1 && lastBrace != -1 && lastBrace > firstBrace) {
      // Cắt lấy đúng đoạn JSON sạch
      String cleanJson = input.substring(firstBrace, lastBrace + 1);
      
      Serial.print("[STM32 CLEAN]: "); Serial.println(cleanJson);

      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, cleanJson);

      if (!error) {
        // Chỉ cập nhật nếu dữ liệu hợp lý (tránh số 0 do nhiễu)
        if (doc.containsKey("temp")) temp = doc["temp"];
        if (doc.containsKey("humid")) humid = doc["humid"];
        
        // Kiểm tra light hợp lệ (0-100) mới cập nhật
        if (doc.containsKey("light")) {
            int new_light = doc["light"];
            if (new_light >= 0 && new_light <= 100) light = new_light;
        }
        
        if (doc.containsKey("soil")) soil = doc["soil"];
      } else {
        Serial.println("[ERR] JSON Lỗi");
      }
    }
  }
}

// --- HÀM CHẠY TỰ ĐỘNG ---
void runAutomationControl() {
  
  // 1. AUTO LIGHT
  if (auto_light_enabled) {
    if (light < light_on_threshold && !light_on) {
        light_on = true;
        digitalWrite(PIN_LIGHT, DEVICE_ON);
        Serial.printf("[AUTO] Bật đèn (Light %d < %d)\n", light, light_on_threshold);
    }
    else if (light > light_off_threshold && light_on) {
        light_on = false;
        digitalWrite(PIN_LIGHT, DEVICE_OFF);
        Serial.printf("[AUTO] Tắt đèn (Light %d > %d)\n", light, light_off_threshold);
    }
  }

  // 2. AUTO IRRIGATION
  if (auto_irrigation_enabled) {
    if (!irrigation_on && light < 50) {
        irrigation_on = true;
        pump_start_time = millis();
        digitalWrite(PIN_PUMP, DEVICE_ON);
        Serial.printf("[AUTO] Bật bơm (Light %d < 50)\n", light);
    }

    // Tắt bơm: Chỉ tắt khi ĐÃ chạy đủ thời gian tối thiểu
    if (irrigation_on) {
        if (millis() - pump_start_time > pump_min_time) {
            irrigation_on = false;
            digitalWrite(PIN_PUMP, DEVICE_OFF);
            Serial.println("[AUTO] Tắt bơm (Xong chu trình)");
        }
    }
  }
}

// --- API STATUS ---
void handleApiStatus() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  JsonDocument doc;
  doc["temperature_C"] = temp;
  doc["humidity_percent"] = humid;
  doc["light_lux"] = light;
  doc["soil_moisture_percent"] = soil;
  doc["irrigation_on"] = irrigation_on;
  doc["light_on"] = light_on;
  doc["irrigation_auto_mode"] = auto_irrigation_enabled;
  doc["light_auto_mode"] = auto_light_enabled;
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// --- API CONTROL ---
void handleApiControl() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  if (server.hasArg("plain") == false) {
    server.send(400, "application/json", "{}");
    return;
  }
  String body = server.arg("plain");
  JsonDocument doc;
  deserializeJson(doc, body);
  
  const char* actuator = doc["actuator"]; 
  const char* state = doc["state"];       
  bool target_state = (strcmp(state, "ON") == 0);

  Serial.printf("[WEB] %s -> %s\n", actuator, state);

  // Xử lý bật tắt chế độ Auto
  if (strcmp(actuator, "irrigation_auto") == 0) {
    auto_irrigation_enabled = target_state;
    if (!auto_irrigation_enabled) { irrigation_on = false; digitalWrite(PIN_PUMP, DEVICE_OFF); }
    server.send(200, "application/json", "{\"status\":\"ok\"}");
    return;
  }

  if (strcmp(actuator, "light_auto") == 0) {
    auto_light_enabled = target_state;
    if (!auto_light_enabled) { light_on = false; digitalWrite(PIN_LIGHT, DEVICE_OFF); }
    server.send(200, "application/json", "{\"status\":\"ok\"}");
    return;
  }

  // Xử lý thủ công (Chặn nếu đang Auto)
  if (strcmp(actuator, "irrigation") == 0) {
    if (auto_irrigation_enabled) {
        server.send(409, "application/json", "{\"status\":\"error\"}");
        return;
    }
    irrigation_on = target_state;
    digitalWrite(PIN_PUMP, irrigation_on ? DEVICE_ON : DEVICE_OFF);
  } 
  else if (strcmp(actuator, "light") == 0) {
    if (auto_light_enabled) {
        server.send(409, "application/json", "{\"status\":\"error\"}");
        return;
    }
    light_on = target_state;
    digitalWrite(PIN_LIGHT, light_on ? DEVICE_ON : DEVICE_OFF);
  }

  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

void handleRoot() {
  File file = LittleFS.open("/index.html", "r");
  if (file) {
    server.streamFile(file, "text/html");
    file.close();
  } else {
    server.send(404, "text/plain", "File not found");
  }
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(UART_BAUD, SERIAL_8N1, RXD2, TXD2);

  pinMode(PIN_LIGHT, OUTPUT);
  pinMode(PIN_PUMP, OUTPUT);
  digitalWrite(PIN_LIGHT, DEVICE_OFF);
  digitalWrite(PIN_PUMP, DEVICE_OFF);

  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS Error");
    return;
  }

  WiFi.mode(WIFI_STA); 
  WiFi.config(local_ip, gateway, subnet, primaryDNS);
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nIP: " + WiFi.localIP().toString());

  server.on("/", HTTP_GET, handleRoot);
  server.on("/index.html", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleApiStatus);
  server.on("/api/control", HTTP_POST, handleApiControl);
  server.serveStatic("/", LittleFS, "/"); 

  server.begin();
}

void loop() {
  server.handleClient();
  readSTM32Data();
  runAutomationControl();
  delay(5);
}
