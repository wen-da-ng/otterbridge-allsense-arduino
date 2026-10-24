// AllSense: receive sensor data from an Android phone running OtterBridge AllSense.
// Protocol: https://docs.allsense.otterbridge.org/PROTOCOL/ (https://github.com/wen-da-ng/otterbridge-allsense-protocol, MIT).
//
//   AllSense phone;
//   phone.begin("http://192.168.1.20:8765/?token=abcd");   // link from the app (Home → Connect)
//   phone.subscribe("accelerometer", 50);
//   ...
//   void loop() {
//     phone.loop();
//     if (const AllSenseSample* a = phone.latest("accelerometer")) Serial.println((*a)[2]);
//   }
#pragma once

#if !defined(ESP32)
#error "AllSense over Wi-Fi needs an ESP32. On other boards use AllSenseSerial (USB/serial link)."
#endif

#include <Arduino.h>
#include <WebSocketsClient.h>

#include <functional>

#include "AllSenseSample.h"

class AllSense {
public:
    using SampleCallback = std::function<void(const AllSenseSample&)>;

    /** Connects using the link from the app (Home → Connect). Returns false if the link can't be read. */
    bool begin(const char* link);
    void begin(const char* host, uint16_t port, const char* token);

    /** By default begin() turns off Wi-Fi power saving: with it on, the radio dozes between
     *  beacons and samples arrive in bursts 100-300 ms late. Call with true *after* begin()
     *  to save power instead (e.g. for slow sensors on batteries). */
    void setPowerSave(bool on);

    /** Starts a stream by the name set in the app (e.g. "accelerometer"). Rate in Hz for continuous sensors. */
    bool subscribe(const char* name, float rateHz = 50);
    void unsubscribe(const char* name);

    /** Called for every sample, from loop(). Keep it quick. */
    void onSample(SampleCallback callback) { _callback = callback; }

    /** Call often: receives data, reconnects when needed. */
    void loop();

    /** The newest sample of a stream, or nullptr until one has arrived. */
    const AllSenseSample* latest(const char* name) const;

    bool connected() const { return _ready; }
    const char* device() const { return _device; }
    uint32_t samplesReceived() const { return _received; }
    /** Message from the phone about a stream, e.g. "Android won't deliver this sensor to apps". */
    const char* lastError() const { return _error; }

private:
    struct Stream {
        char name[ALLSENSE_MAX_NAME] = "";
        char topic[96] = "";
        float rateHz = 0;
        bool used = false;
        bool has = false;
        AllSenseSample sample;
    };

    void onEvent(WStype_t type, uint8_t* payload, size_t length);
    void handleText(const char* text, size_t length);
    void sendSubscribe(Stream& s);
    void sendText(const char* json);
    Stream* find(const char* name);
    const Stream* find(const char* name) const;
    Stream* findTopic(const char* topic);

    WebSocketsClient _ws;
    Stream _streams[ALLSENSE_MAX_STREAMS];
    SampleCallback _callback;
    char _device[ALLSENSE_MAX_NAME] = "";
    char _error[96] = "";
    bool _ready = false;  // connected and the phone said hello
    uint32_t _received = 0;
};
