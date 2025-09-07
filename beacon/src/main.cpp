#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEServer.h>
#include <NimBLEUtils.h>
#include <NimBLEAdvertising.h>

// BLE налаштування
#define DEVICE_NAME "Bezkrovnyi"
#define SERVICE_UUID "12345678-1234-1234-1234-123456789abc"
#define CHARACTERISTIC_UUID "87654321-4321-4321-4321-cba987654321"

// Змінні для BLE
NimBLEServer* pServer;
NimBLECharacteristic* pCharacteristic;
bool deviceConnected = false;
bool oldDeviceConnected = false;

// Callback для підключення/відключення
class MyServerCallbacks: public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer) {
        deviceConnected = true;
        Serial.println("Клієнт підключився");
    };

    void onDisconnect(NimBLEServer* pServer) {
        deviceConnected = false;
        Serial.println("Клієнт відключився");
    }
};

// Callback для характеристик
class MyCallbacks: public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *pCharacteristic) {
        std::string rxValue = pCharacteristic->getValue();
        if (rxValue.length() > 0) {
            Serial.println("Отримано дані: " + String(rxValue.c_str()));
        }
    }
};

void setup() {
    Serial.begin(115200);
    Serial.println("ESP32-C3 Mini BLE Beacon - Bezkrovnyi");
    
    // Ініціалізуємо BLE
    NimBLEDevice::init(DEVICE_NAME);
    NimBLEDevice::setPower(ESP_PWR_LVL_P9); // Максимальна потужність
    
    // Створюємо BLE сервер
    pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());
    
    // Створюємо BLE сервіс
    NimBLEService* pService = pServer->createService(SERVICE_UUID);
    
    // Створюємо характеристику
    pCharacteristic = pService->createCharacteristic(
        CHARACTERISTIC_UUID,
        NIMBLE_PROPERTY::READ |
        NIMBLE_PROPERTY::WRITE |
        NIMBLE_PROPERTY::NOTIFY
    );
    
    pCharacteristic->setCallbacks(new MyCallbacks());
    pCharacteristic->setValue("Beacon Active");
    
    // Запускаємо сервіс
    pService->start();
    
    // Налаштовуємо рекламу
    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);
    pAdvertising->setMaxPreferred(0x12);
    
    // Запускаємо рекламу
    pAdvertising->start();
    
    Serial.println("BLE Beacon запущено!");
    Serial.println("Назва пристрою: " + String(DEVICE_NAME));
    Serial.println("MAC адреса: " + String(NimBLEDevice::getAddress().toString().c_str()));
    Serial.println("Очікування підключення...");
}

void loop() {
    // Перевіряємо зміни підключення
    if (!deviceConnected && oldDeviceConnected) {
        delay(500); // даємо BLE стек час для завершення
        pServer->startAdvertising(); // перезапускаємо рекламу
        Serial.println("Реклама перезапущена");
        oldDeviceConnected = deviceConnected;
    }
    
    if (deviceConnected && !oldDeviceConnected) {
        oldDeviceConnected = deviceConnected;
    }
    
    // Відправляємо дані кожні 2 секунди
    if (deviceConnected) {
        String timeString = "Time: " + String(millis() / 1000);
        pCharacteristic->setValue(timeString.c_str());
        pCharacteristic->notify();
        Serial.println("Відправлено: " + timeString);
        delay(2000);
    }
    
    delay(100);
}
