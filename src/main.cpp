#include <Arduino.h>
#include <NimBLEDevice.h>
#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>

// Піни для ESP32-C3 Mini
#define LED_PIN 4        // Пін для WS2812
#define LED_COUNT 25     // Кількість світлодіодів
#define SCAN_DURATION 2  // Тривалість сканування в секундах

// BLE налаштування
#define TARGET_DEVICE_NAME "Bezkrovnyi"  // Назва BLE маячка для пошуку
#define TARGET_MAC_ADDRESS "0c:4e:a0:5f:fd:3c"  // MAC адреса цільового маячка
#define SCAN_INTERVAL 3000             // Інтервал між скануваннями в мс

// WiFi налаштування
#define WIFI_SSID "Bezkrovnyi"
#define WIFI_PASSWORD "H12775UA69x69/qW"

// Веб-сервер
WebServer server(80);

// Налаштування світла
struct LightSettings {
  uint8_t brightness = 0;          // Поточна яскравість
  uint8_t targetBrightness = 0;    // Цільова яскравість
  uint8_t awayBrightness = 70;     // Яскравість при відході
  uint8_t red = 255;
  uint8_t green = 255;
  uint8_t blue = 255;
  uint8_t white = 0;
  uint16_t colorTemp = 4000;
  bool isAway = true;              // За замовчуванням вважаємо, що користувача нема
  bool smoothTransition = true;
  unsigned long lastUpdate = 0;
  uint16_t fadeOutTime = 3000;     // Затухання
  uint16_t fadeInTime = 3000;      // Відновлення
  bool lightsOn = false;
} lightSettings;

// Створюємо об'єкт для керування світлодіодами
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

// Змінні для BLE
NimBLEScan* pBLEScan;
bool beaconFound = false;
unsigned long lastScanTime = 0;
bool isScanning = false;
unsigned long scanStartTime = 0;

// Функція для встановлення кольору з температурою
void setColorWithTemp(uint8_t r, uint8_t g, uint8_t b, uint8_t w, uint16_t temp) {
  float tempK = temp / 100.0;
  float red, green, blue;
  
  if (tempK <= 66) {
    red = 255;
    green = max(0.0, min(255.0, 99.4708025861 * log(tempK) - 161.1195681661));
    blue = max(0.0, min(255.0, 138.5177312231 * log(tempK - 10) - 305.0447927307));
  } else {
    red = max(0.0, min(255.0, 329.698727446 * pow(tempK - 60, -0.1332047592)));
    green = max(0.0, min(255.0, 288.1221695283 * pow(tempK - 60, -0.0755148492)));
    blue = 255;
  }
  
  r = (uint8_t)(r * red / 255);
  g = (uint8_t)(g * green / 255);
  b = (uint8_t)(b * blue / 255);
  
  for (int i = 0; i < LED_COUNT; i++) {
    strip.setPixelColor(i, strip.Color(r, g, b, w));
  }
  strip.show();
}

// Плавний перехід яскравості
void smoothBrightnessTransition() {
  if (!lightSettings.smoothTransition) return;

  unsigned long now = millis();
  if (now - lightSettings.lastUpdate < 20) return;

  lightSettings.lastUpdate = now;

  int brightnessDiff = lightSettings.targetBrightness - lightSettings.brightness;
  
  if (abs(brightnessDiff) > 1) {
    uint16_t transitionTime = (brightnessDiff > 0) ? lightSettings.fadeInTime : lightSettings.fadeOutTime;

    int totalSteps = transitionTime / 20;
    int step = brightnessDiff / totalSteps;
    if (step == 0) step = (brightnessDiff > 0) ? 1 : -1;

    lightSettings.brightness += step;

    if (lightSettings.brightness > 255) lightSettings.brightness = 255;
    if (lightSettings.brightness < 0) lightSettings.brightness = 0;

    strip.setBrightness(lightSettings.brightness);
    setColorWithTemp(lightSettings.red, lightSettings.green, lightSettings.blue, lightSettings.white, lightSettings.colorTemp);
  }
}

// При відновленні BLE
void turnOnLights() {
  if (lightSettings.isAway) {
    lightSettings.isAway = false;
    lightSettings.lightsOn = true;
    lightSettings.targetBrightness = 255; 
    Serial.println("Light ON - BLE beacon found!");
  }
}

// При втраті BLE
void turnOffLights() {
  if (!lightSettings.isAway) {
    lightSettings.isAway = true;
    lightSettings.lightsOn = true; // Лише зменшуємо яскравість
    lightSettings.targetBrightness = lightSettings.awayBrightness; 
    Serial.println("Light dimmed - BLE beacon not found. Brightness will decrease to awayBrightness");
  }
}

// Callback BLE
class MyAdvertisedDeviceCallbacks: public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice* advertisedDevice) {
        String deviceMac = String(advertisedDevice->getAddress().toString().c_str());
        deviceMac.toLowerCase();
        
        if (deviceMac.equals(TARGET_MAC_ADDRESS)) {
            Serial.println("*** TARGET BEACON FOUND BY MAC! ***");
            beaconFound = true;
            return;
        }
        if (advertisedDevice->haveName()) {
            String deviceName = String(advertisedDevice->getName().c_str());
            if (deviceName.indexOf(TARGET_DEVICE_NAME) >= 0) {
                Serial.println("*** TARGET BEACON FOUND BY NAME! ***");
                beaconFound = true;
                return;
            }
        }
    }
};

// Початок BLE сканування
void startBLEScan() {
  if (!isScanning) {
    isScanning = true;
    scanStartTime = millis();
    beaconFound = false;
    Serial.println("Starting BLE scan...");
    pBLEScan->start(SCAN_DURATION, false);
  }
}

// Перевірка завершення BLE
void checkBLEScan() {
  if (isScanning) {
    unsigned long now = millis();
    if (now - scanStartTime >= (SCAN_DURATION * 1000)) {
      isScanning = false;
      pBLEScan->clearResults();
      
      Serial.println("BLE scan completed. Beacon found: " + String(beaconFound));
      
      if (beaconFound) {
        turnOnLights();
      } else {
        turnOffLights();
      }
    }
  }
}

// API
void handleRoot() {
  File file = SPIFFS.open("/index.html", "r");
  if (!file) {
    server.send(404, "text/plain", "File not found");
    return;
  }
  server.streamFile(file, "text/html");
  file.close();
}

void handleStatus() {
  StaticJsonDocument<200> doc;
  doc["connected"] = WiFi.status() == WL_CONNECTED;
  doc["beaconFound"] = beaconFound;
  doc["brightness"] = lightSettings.brightness;
  doc["isAway"] = lightSettings.isAway;
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleUpdate() {
  if (server.hasArg("plain")) {
    StaticJsonDocument<200> doc;
    deserializeJson(doc, server.arg("plain"));
    
    bool changed = false;
    if (doc.containsKey("red")) { lightSettings.red = doc["red"]; changed = true; }
    if (doc.containsKey("green")) { lightSettings.green = doc["green"]; changed = true; }
    if (doc.containsKey("blue")) { lightSettings.blue = doc["blue"]; changed = true; }
    if (doc.containsKey("white")) { lightSettings.white = doc["white"]; changed = true; }
    if (doc.containsKey("brightness")) { lightSettings.targetBrightness = doc["brightness"]; changed = true; }
    if (doc.containsKey("awayBrightness")) { lightSettings.awayBrightness = doc["awayBrightness"]; changed = true; }
    if (doc.containsKey("colorTemp")) { lightSettings.colorTemp = doc["colorTemp"]; changed = true; }
    if (doc.containsKey("fadeOutTime")) { lightSettings.fadeOutTime = doc["fadeOutTime"]; changed = true; }
    if (doc.containsKey("fadeInTime")) { lightSettings.fadeInTime = doc["fadeInTime"]; changed = true; }
    
    if (changed) {
      Serial.println("Settings updated via web interface");
      setColorWithTemp(lightSettings.red, lightSettings.green, lightSettings.blue, lightSettings.white, lightSettings.colorTemp);
    }
    
    server.send(200, "application/json", "{\"status\":\"ok\"}");
  }
}

void handleToggle() {
  if (lightSettings.isAway) {
    turnOnLights();
  } else {
    turnOffLights();
  }
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

void handleSave() {
  server.send(200, "application/json", "{\"status\":\"saved\"}");
}

void handleLoad() {
  StaticJsonDocument<200> doc;
  doc["red"] = lightSettings.red;
  doc["green"] = lightSettings.green;
  doc["blue"] = lightSettings.blue;
  doc["white"] = lightSettings.white;
  doc["brightness"] = lightSettings.brightness;
  doc["awayBrightness"] = lightSettings.awayBrightness;
  doc["colorTemp"] = lightSettings.colorTemp;
  doc["fadeOutTime"] = lightSettings.fadeOutTime;
  doc["fadeInTime"] = lightSettings.fadeInTime;
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void setupWebServer() {
  server.on("/", handleRoot);
  server.on("/api/status", handleStatus);
  server.on("/api/update", HTTP_POST, handleUpdate);
  server.on("/api/toggle", HTTP_POST, handleToggle);
  server.on("/api/save", HTTP_POST, handleSave);
  server.on("/api/load", handleLoad);
  server.begin();
  Serial.println("Web server started");
}

void setup() {
  Serial.begin(115200);
  Serial.println("ESP32-C3 Mini BLE WS2812 Controller");
  Serial.println("Searching for BLE beacon: " + String(TARGET_DEVICE_NAME));
  Serial.println("MAC address: " + String(TARGET_MAC_ADDRESS));
  
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS Mount Failed");
    return;
  }
  
  strip.begin();
  strip.setBrightness(lightSettings.brightness);
  setColorWithTemp(lightSettings.red, lightSettings.green, lightSettings.blue, lightSettings.white, lightSettings.colorTemp);
  
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.println("WiFi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  
  setupWebServer();
  
  NimBLEDevice::init("");
  pBLEScan = NimBLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);
  
  Serial.println("Starting...");
}

void loop() {
  server.handleClient();
  
  smoothBrightnessTransition();  // завжди оновлюємо
  
  checkBLEScan();
  
  if (!isScanning && (millis() - lastScanTime >= SCAN_INTERVAL)) {
    lastScanTime = millis();
    startBLEScan();
  }
  
  delay(5);
}