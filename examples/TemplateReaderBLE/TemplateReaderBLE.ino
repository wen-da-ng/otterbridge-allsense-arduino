// AllSense frame template reader over Bluetooth LE (ESP32): a board with its OWN protocol and
// no AllSense library. It looks like many hobby BLE boards: a Nordic UART Service ("NUS"), where
// whatever the phone writes to the RX characteristic is the serial data.
//
// The phone sends the my_robot_imu template's frames (app/src/main/assets/templates/my_robot_imu.json):
//   AA 55 | ax ay az (int16, m/s² × 100) | gx gy gz (int16, rad/s × 1000) | seq (u8) | xor (u8)
//
// 1. Upload this sketch (ESP32 / ESP32-S3).
// 2. In the app: Start streaming, then Microcontroller -> By Bluetooth: "Robot NUS" shows as "Nearby · its own
//    protocol". Tap Connect… and pick "my_robot_imu".
// 3. Tilt the phone: the RGB LED follows the tilt. Blinking blue means no valid frames.
//
// Once a second it notifies a status line on the TX characteristic and prints it on Serial. The
// phone ignores it; on a PC, allsense-fake-template --ble "Robot NUS" (pip install "allsense-testkit[ble]") uses it to check the
// sketch without a phone.

#if !defined(ESP32)
#error "This example needs an ESP32 with Bluetooth LE."
#endif

#include <BLEDevice.h>
#if defined(CONFIG_BLUEDROID_ENABLED)
#include <BLE2902.h>
#endif
#include <BLEServer.h>

const char* kName = "Robot NUS";
const char* kNus = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
const char* kNusRx = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";  // phone -> board (write)
const char* kNusTx = "6e400003-b5a3-f393-e0a9-e50e24dcca9e";  // board -> phone (notify)

const uint8_t kHeader0 = 0xAA, kHeader1 = 0x55;
const int kFrameSize = 16;

// Filled by the BLE task, read by loop().
portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
uint8_t frame[kFrameSize];
int have = 0;
volatile unsigned long goodFrames = 0, badFrames = 0, lastFrameMs = 0;
int16_t accel[3], gyro[3];
uint8_t lastSeq = 0;

BLECharacteristic* tx = nullptr;
volatile bool connected = false;

int16_t readI16(const uint8_t* p) { return (int16_t)(p[0] | (p[1] << 8)); }

void onFrame() {
    uint8_t x = 0;
    for (int i = 2; i < kFrameSize - 1; i++) x ^= frame[i];
    if (x != frame[kFrameSize - 1]) { badFrames++; return; }
    portENTER_CRITICAL(&lock);
    for (int i = 0; i < 3; i++) {
        accel[i] = readI16(frame + 2 + 2 * i);
        gyro[i] = readI16(frame + 8 + 2 * i);
    }
    lastSeq = frame[14];
    portEXIT_CRITICAL(&lock);
    goodFrames++;
    lastFrameMs = millis();
}

// BLE delivers bytes in packets; frames can be split across them or packed together.
void feed(const uint8_t* data, size_t n) {
    for (size_t i = 0; i < n; i++) {
        uint8_t b = data[i];
        if (have == 0 && b != kHeader0) continue;
        if (have == 1 && b != kHeader1) { have = (b == kHeader0) ? 1 : 0; continue; }
        frame[have++] = b;
        if (have == kFrameSize) { onFrame(); have = 0; }
    }
}

class RxCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* c) override { feed(c->getData(), c->getLength()); }
};

class ServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer*) override { connected = true; }
    void onDisconnect(BLEServer* server) override {
        connected = false;
        have = 0;
        server->getAdvertising()->start();
    }
};

void setup() {
    Serial.begin(115200);
    BLEDevice::init(kName);
    BLEDevice::setMTU(247);
    BLEServer* server = BLEDevice::createServer();
    server->setCallbacks(new ServerCallbacks());
    BLEService* service = server->createService(kNus);
    BLECharacteristic* rx = service->createCharacteristic(kNusRx, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
    rx->setCallbacks(new RxCallbacks());
    tx = service->createCharacteristic(kNusTx, BLECharacteristic::PROPERTY_NOTIFY);
#if defined(CONFIG_BLUEDROID_ENABLED)
    tx->addDescriptor(new BLE2902());
#endif
    service->start();
    BLEAdvertising* advertising = BLEDevice::getAdvertising();
    advertising->addServiceUUID(kNus);
    BLEAdvertisementData scanResponse;
    scanResponse.setName(kName);
    advertising->setScanResponseData(scanResponse);
    advertising->setScanResponse(true);
    advertising->start();
    Serial.println("Advertising as \"Robot NUS\" (Nordic UART). In the app: Bluetooth LE -> Connect… -> my_robot_imu.");
}

void loop() {
    int16_t a[3];
    portENTER_CRITICAL(&lock);
    memcpy(a, accel, sizeof a);
    portEXIT_CRITICAL(&lock);
    bool live = millis() - lastFrameMs < 500 && goodFrames > 0;
#ifdef RGB_BUILTIN
    if (!live) {
        rgbLedWrite(RGB_BUILTIN, 0, 0, (millis() / 500) % 2 ? 24 : 0);
    } else {
        auto level = [](int16_t v) { return (uint8_t)constrain(abs(v) * 64L / 1000, 0L, 64L); };  // 10 m/s² = full
        rgbLedWrite(RGB_BUILTIN, level(a[0]), level(a[1]), level(a[2]) / 4);
    }
#endif

    static unsigned long lastReport = 0;
    if (millis() - lastReport >= 1000) {
        lastReport = millis();
        char line[96];
        snprintf(line, sizeof line, "frames=%lu bad=%lu seq=%u accel_x100=%d,%d,%d\n",
                 (unsigned long)goodFrames, (unsigned long)badFrames, lastSeq, a[0], a[1], a[2]);
        Serial.print(line);
        if (connected && tx) {
            tx->setValue((uint8_t*)line, strlen(line));
            tx->notify();
        }
    }
    delay(10);
}
