// AllSenseSerial: receive sensor data from an AllSense phone over a serial link, typically the
// board's USB port plugged into the phone (USB-C to USB-C). Uses the compact protocol
// (https://docs.allsense.otterbridge.org/PROTOCOL/). Works on any Arduino board; no Wi-Fi needed.
//
//   AllSenseSerial phone;
//   void setup() {
//     Serial.begin(115200);
//     phone.begin(Serial);
//     phone.subscribe("accelerometer", 50);
//   }
//   void loop() {
//     phone.loop();
//     if (const AllSenseSample* a = phone.latest("accelerometer")) { float z = (*a)[2]; }
//   }
//
// The port carries the protocol, so don't print debug text to it: use another serial port or LEDs.
#pragma once

#include <Arduino.h>

#include "AllSenseCompact.h"
#include "AllSenseSample.h"

#ifndef ALLSENSE_SERIAL_RX_BUFFER
#if defined(__AVR__)
#define ALLSENSE_SERIAL_RX_BUFFER 80  // fits up to 16 values per sample
#else
#define ALLSENSE_SERIAL_RX_BUFFER 160
#endif
#endif

class AllSenseSerial {
public:
    using SampleCallback = void (*)(const AllSenseSample&);

    /** Starts talking to the phone on `port` (e.g. Serial). Call port.begin() first. */
    void begin(Stream& port, const char* clientName = "arduino");

    /** Starts a stream by the name set in the app (e.g. "accelerometer"). Rate in Hz. */
    bool subscribe(const char* name, float rateHz = 50);
    void unsubscribe(const char* name);

    /** Called for every sample, from loop(). Keep it quick. */
    void onSample(SampleCallback callback) { _callback = callback; }

    /** Call often: reads the port, (re)connects and resubscribes when needed. */
    void loop();

    /** The newest sample of a stream, or nullptr until one has arrived. */
    const AllSenseSample* latest(const char* name) const;

    /** True while the phone is answering (it sends at least a heartbeat every second). */
    bool connected() const { return _ready; }
    const char* device() const { return _device; }
    uint32_t samplesReceived() const { return _received; }
    /** Last problem the phone reported about a stream, e.g. "No stream called acelerometer". */
    const char* lastError() const { return _error; }

private:
    struct Stream_ {
        char name[ALLSENSE_MAX_NAME] = "";
        float rateHz = 0;
        bool used = false;
        bool has = false;
        AllSenseSample sample;
    };

    void handleFrame(uint8_t* payload, size_t length);
    void sendPayload(const uint8_t* payload, size_t length);
    void sendSubscribe(uint8_t id);
    int find(const char* name) const;

    Stream* _port = nullptr;
    Stream_ _streams[ALLSENSE_MAX_STREAMS];
    SampleCallback _callback = nullptr;
    char _client[ALLSENSE_MAX_NAME] = "arduino";
    char _device[ALLSENSE_MAX_NAME] = "";
    char _error[48] = "";
    uint8_t _rx[ALLSENSE_SERIAL_RX_BUFFER];
    size_t _rxLength = 0;
    bool _rxOverflow = false;
    bool _ready = false;
    uint32_t _lastRxMs = 0;
    uint32_t _lastHelloMs = 0;
    uint32_t _lastTxMs = 0;
    uint32_t _received = 0;
};
