#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <ThingsCloudMQTT.h>
#include <time.h>

#include <Wire.h>
#include <Adafruit_PN532.h>

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <SPI.h>

// —— 后端基础地址 ——  
const String backendBase = "http://ip:端口号/api";

// —— WiFi & MQTT 设置 ——  
const char* ssid = "";
const char* password = "";
#define THINGSCLOUD_MQTT_HOST           "gz-3-mqtt.iot-api.com"
#define THINGSCLOUD_DEVICE_ACCESS_TOKEN ""
#define THINGSCLOUD_PROJECT_KEY         ""

ThingsCloudMQTT client(
  THINGSCLOUD_MQTT_HOST,
  THINGSCLOUD_DEVICE_ACCESS_TOKEN,
  THINGSCLOUD_PROJECT_KEY);

// —— 硬件引脚 ——  
#define RELAY_PIN    5
#define QR_RX_PIN    16
#define QR_TX_PIN    17

// —— PN532 I2C ——  
#define PN532_IRQ    2
#define PN532_RESET  3
TwoWire nfcWire = TwoWire(0);
Adafruit_PN532 nfc(PN532_IRQ, PN532_RESET, &nfcWire);

// —— OLED I2C ——  
#define OLED_SDA     4
#define OLED_SCL     15
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
TwoWire oledWire = TwoWire(1);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &oledWire);

// —— FreeRTOS 任务句柄 ——  
TaskHandle_t QRTaskHandle;
TaskHandle_t NFCTaskHandle;
TaskHandle_t DisplayTaskHandle;

// —— 全局状态 ——  
String globalWifiStatus    = "WiFi: --";
String globalEventStatus   = "Event: --";
bool   wifiWasConnected    = false;
int    scrollPos           = 0;
const int scrollDelay      = 20;   // 静止显示周期（帧数）
int    scrollCounter      = 0;

// —— 日志辅助 ——  
#define LOG_I(tag, msg) Serial.println(String("[") + tag + "] " + msg)

// —— 处理设备属性 ——
void handleAttributes(const JsonObject &obj) {
  if (obj.containsKey("relay")) {
    bool on = obj["relay"];
    digitalWrite(RELAY_PIN, on ? LOW : HIGH);
    LOG_I("MQTT", String("relay -> ") + (on ? "ON" : "OFF"));
  }
}

// —— MQTT 连接回调 ——
void onMQTTConnect() {
  client.onAttributesGetResponse([](const String &topic, const JsonObject &obj) {
    if (obj["result"] == 1) handleAttributes(obj["attributes"]);
  });
  client.onAttributesPush([](const JsonObject &obj) {
    handleAttributes(obj);
  });
  client.getAttributes();
}

// —— 根据用户 ID 获取用户名 ——
String getUsernameById(const String &userId) {
  HTTPClient http;
  String url = backendBase + "/user/id/" + userId;
  http.begin(url);
  int code = http.GET();
  String name = "";
  if (code == 200) {
    String payload = http.getString();
    StaticJsonDocument<256> doc;
    if (!deserializeJson(doc, payload) && doc.containsKey("username"))
      name = doc["username"].as<String>();
  } else {
    LOG_I("API", "getUsername failed " + String(code));
  }
  http.end();
  return name;
}

// —— 获取二维码内容 ——
String fetchQRCodeForUser(const String &username) {
  HTTPClient http;
  String url = backendBase + "/qrcode/db/" + username;
  http.begin(url);
  int code = http.GET();
  String qr = "";
  if (code == 404) {
    qr = "user_id=0&uuid=0000-...&user_type=0";
  } else if (code == 200) {
    String payload = http.getString();
    StaticJsonDocument<512> doc;
    if (!deserializeJson(doc, payload) && doc.containsKey("qr_content"))
      qr = doc["qr_content"].as<String>();
  } else {
    LOG_I("API", "fetchQR failed " + String(code));
  }
  http.end();
  return qr;
}

// —— 记录开锁事件 ——
void recordUnlockEvent(const String &username, const String &type) {
  HTTPClient http;
  http.begin(backendBase + "/unlock/record");
  http.addHeader("Content-Type", "application/json");
  StaticJsonDocument<256> doc;
  doc["username"]    = username;
  doc["unlock_time"] = time(nullptr);
  doc["message"]     = username + type;
  String body; serializeJson(doc, body);
  int code = http.POST(body);
  LOG_I("API", "recordUnlock status " + String(code));
  http.end();
}

// —— 验证 UID ——
bool verifyUidWithBackend(const String &uid) {
  HTTPClient http;
  http.begin(backendBase + "/verify_uid");
  http.addHeader("Content-Type", "application/json");
  StaticJsonDocument<200> doc; doc["uid"] = uid;
  String body; serializeJson(doc, body);
  int code = http.POST(body);
  String resp = http.getString();
  LOG_I("API", "verifyUid rsp: " + resp);
  http.end();
  StaticJsonDocument<200> r;
  return (!deserializeJson(r, resp) &&
          r.containsKey("message") &&
          r["message"] == "UID found in database");
}

// —— 二维码任务 ——
void QRTask(void* pvParameters) {
  const TickType_t interval = pdMS_TO_TICKS(500);
  const String fallback = "user_id=0&uuid=0000-...&user_type=0";
  for (;;) {
    if (Serial2.available()) {
      String scan = Serial2.readStringUntil('\n'); scan.trim();
      if (scan.length()) {
        LOG_I("QR", "scan: " + scan);
        int s = scan.indexOf("user_id=") + 8;
        int e = scan.indexOf("&", s);
        String uid = scan.substring(s, e);
        String user = getUsernameById(uid);
        String qr   = user.length() ? fetchQRCodeForUser(user) : fallback;
        bool ok     = (scan == qr && qr != fallback);

        if (ok) {
          globalEventStatus = "QR OK, unlocking";
          LOG_I("QR", "match, unlocking");
          digitalWrite(RELAY_PIN, HIGH);
          client.publish("attributes","{\"relay\":false}");
          recordUnlockEvent(user, "二维码开锁");
          vTaskDelay(pdMS_TO_TICKS(5000));
          digitalWrite(RELAY_PIN, LOW);
          client.publish("attributes","{\"relay\":true}");
          globalEventStatus = "QRcode unlock OK";
        } else {
          globalEventStatus = "QRcode not match";
          LOG_I("QR", "no match");
        }
      }
    }
    vTaskDelay(interval);
  }
}

// —— NFC 任务 ——
void NFCTask(void* pv) {
  const TickType_t interval = pdMS_TO_TICKS(200);
  for (;;) {
    uint8_t uid[7], len;
    if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &len)) {
      String s;
      for (int i = 0; i < len; i++) {
        if (uid[i] < 0x10) s += "0";
        s += String(uid[i], HEX) + (i < len-1 ? ":" : "");
      }
      LOG_I("NFC", "UID: " + s);
      bool ok = verifyUidWithBackend(s);
      if (ok) {
        globalEventStatus = "IC OK, unlocking";
        LOG_I("NFC", "verified, unlocking");
        digitalWrite(RELAY_PIN, HIGH);
        String username = "18106982017";
        StaticJsonDocument<200> relayState;
        relayState["relay"] = false;
        String relayJson;
        serializeJson(relayState, relayJson);
        client.publish("attributes", relayJson);
        recordUnlockEvent(username, "IC卡开锁");
        vTaskDelay(pdMS_TO_TICKS(5000));
        digitalWrite(RELAY_PIN, LOW);
        relayJson = "";
        relayState["relay"] = true;
        serializeJson(relayState, relayJson);
        client.publish("attributes", relayJson);
        globalEventStatus = "IC card unlock OK";
      } else {
        globalEventStatus = "IC card verify failed";
        LOG_I("NFC", "verify failed");
      }
    }
    vTaskDelay(interval);
  }
}

// —— OLED 显示任务 ——
void DisplayTask(void* pv) {
  const TickType_t interval = pdMS_TO_TICKS(100);
  scrollCounter = scrollDelay;  // 初始静止
  for (;;) {
    // 更新 WiFi 状态
    if (WiFi.status() != WL_CONNECTED) {
      if (wifiWasConnected) {
        globalWifiStatus = "WiFi disconnected";
        wifiWasConnected = false;
      }
    } else {
      if (!wifiWasConnected) {
        globalWifiStatus = String("WiFi connected: ") + WiFi.localIP().toString();
        wifiWasConnected = true;
        scrollPos = 0;
        scrollCounter = scrollDelay;
      }
    }
    int textWidth = globalWifiStatus.length() * 6;
    int maxOffset = max(0, textWidth - SCREEN_WIDTH);

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setTextWrap(false); // 禁止自动换行，避免跑到第二行

    // 计算横向偏移
    int x = (scrollCounter > 0 || maxOffset == 0) ? 0 : -scrollPos;
    display.setCursor(x, 0);
    display.print(globalWifiStatus);

    // 滚动逻辑
    if (maxOffset > 0) {
      if (scrollCounter > 0) {
        scrollCounter--;
      } else {
        scrollPos++;
        if (scrollPos > maxOffset) {
          scrollPos = 0;
          scrollCounter = scrollDelay;
        }
      }
    }

    // 第二行事件状态，不受影响
    display.setCursor(0, 16);
    display.print(globalEventStatus);
    display.display();

    vTaskDelay(interval);
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(RELAY_PIN, OUTPUT); 
  digitalWrite(RELAY_PIN, LOW);
  Serial2.begin(9600, SERIAL_8N1, QR_RX_PIN, QR_TX_PIN);

  // OLED 初始化
  oledWire.begin(OLED_SDA, OLED_SCL, 100000);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) 
  { 
    LOG_I("OLED", "init failed"); while (1); 
  }
  display.clearDisplay(); display.display();
  
  // WiFi 连接
  LOG_I("WiFi", "Connecting to " + String(ssid));
  client.enableDebuggingMessages(); 
  client.setWifiCredentials(ssid, password);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) 
  { 
    delay(500); Serial.print("."); 
  }
  globalWifiStatus  = String("WiFi connected: ") + WiFi.localIP().toString();
  wifiWasConnected  = true;
  scrollCounter     = scrollDelay;
  LOG_I("WiFi", "Connected, IP: " + WiFi.localIP().toString());

  // NTP 同步
  configTime(8*3600, 0, "ntp1.aliyun.com", "ntp2.aliyun.com");
  while (time(nullptr) < 100000) 
  { 
    delay(500); Serial.print("."); 
  }
  LOG_I("NTP", "Time synced");

  // MQTT & PN532
  onMQTTConnect();
  nfcWire.begin(21, 22, 100000);
  nfc.begin(); 
  if (!nfc.getFirmwareVersion()) 
  { 
    LOG_I("NFC","PN532 not found"); 
    while(1);
  } 
  nfc.SAMConfig();
  LOG_I("NFC","PN532 ready");

  // 创建任务
  xTaskCreatePinnedToCore(QRTask,      "QRTask",      4096, NULL, 1, &QRTaskHandle, 1);
  xTaskCreatePinnedToCore(NFCTask,     "NFCTask",     4096, NULL, 1, &NFCTaskHandle,1);
  xTaskCreatePinnedToCore(DisplayTask, "DisplayTask", 4096, NULL, 1, &DisplayTaskHandle,1);
}

void loop() {
  client.loop();
  delay(10);
}
