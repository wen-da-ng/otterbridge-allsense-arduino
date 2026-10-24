// AllSenseBLE: receive sensor data from an AllSense phone over Bluetooth Low Energy (ESP32).
// The board advertises the AllSense service; in the app (Home → Connect → Microcontroller) tap Connect once and the
// phone reconnects to it by itself whenever it's in range. Same protocol and API as
// AllSenseSerial (it *is* an AllSenseSerial running over a BLE "serial port").
//
//   AllSenseBLE phone;
//   void setup() {
//     phone.begin("my robot");               // the name shown in the app
//     phone.subscribe("accelerometer", 50);
//   }
//   void loop() {
//     phone.loop();
//     if (const AllSenseSample* a = phone.latest("accelerometer")) { float z = (*a)[2]; }
//   }
#pragma once

#if !defined(ESP32)
#error "AllSenseBLE needs an ESP32 (with Bluetooth LE). On other boards use AllSenseSerial over USB."
#endif

#include <Arduino.h>

#include "AllSenseSerial.h"

class BLEServer;
class BLECharacteristic;

/** The BLE link as an Arduino Stream: bytes written by the phone come out of read(). */
class AllSenseBleStream : public Stream {
public:
    void received(const uint8_t* data, size_t length);  // from the BLE task
    void setConnected(bool connected);
    bool isConnected() const { return _connected; }
    void setTx(BLECharacteristic* tx) { _tx = tx; }

    int available() override;
    int read() override;
    int peek() override;
    size_t write(uint8_t b) override { return write(&b, 1); }
    size_t write(const uint8_t* data, size_t length) override;
    void flush() override {}

private:
    static const size_t kSize = 1024;
    uint8_t _buffer[kSize];
    size_t _head = 0, _tail = 0;
    portMUX_TYPE _lock = portMUX_INITIALIZER_UNLOCKED;
    volatile bool _connected = false;
    BLECharacteristic* _tx = nullptr;
};

class AllSenseBLE : public AllSenseSerial {
public:
    /** Starts advertising as `boardName` (shown in the app). Call once in setup(). */
    void begin(const char* boardName = "AllSense board");
    /** True while the phone is connected over BLE (data may still take a moment to start). */
    bool bleConnected() const { return _stream.isConnected(); }

private:
    AllSenseBleStream _stream;
    BLEServer* _server = nullptr;
};
