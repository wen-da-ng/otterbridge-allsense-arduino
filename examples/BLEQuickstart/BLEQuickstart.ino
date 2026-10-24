// AllSense BLE quickstart (ESP32): the phone becomes this board's motion sensor over Bluetooth LE.
// No Wi-Fi, no cable to the phone, no passwords.
//
// 1. Upload this sketch. Open the Serial Monitor (115200) to watch it.
// 2. In the app: Start streaming, then Connect -> Microcontroller -> By Bluetooth: tap Connect next to "AllSense S3".
//    (Once is enough: the phone reconnects by itself whenever the board is in range.)
// 3. Tilt the phone: values print below and the RGB LED follows the tilt.
//    The LED blinks blue while it waits for the phone.

#include <AllSenseBLE.h>

AllSenseBLE phone;

void setup() {
    Serial.begin(115200);  // free for debugging: the phone talks over BLE
    phone.begin("AllSense S3");  // the name shown in the app
    phone.subscribe("accelerometer", 50);
    Serial.println("Advertising as \"AllSense S3\". In the app: Start streaming, then Microcontroller -> By Bluetooth -> Connect.");
}

void loop() {
    phone.loop();

    static uint32_t lastPrint = 0, lastCount = 0;
    const AllSenseSample* a = phone.latest("accelerometer");
    bool live = phone.connected() && a && a->ageMs() < 1000;

#ifdef RGB_BUILTIN
    if (!live) {
        rgbLedWrite(RGB_BUILTIN, 0, 0, (millis() / 500) % 2 ? 24 : 0);  // blinking blue: waiting
    } else {
        auto level = [](float v) { return (uint8_t)constrain(fabsf(v) * 6.5f, 0.0f, 64.0f); };
        rgbLedWrite(RGB_BUILTIN, level((*a)[0]), level((*a)[1]), level((*a)[2]) / 4);
    }
#endif

    if (millis() - lastPrint >= 1000) {
        if (!phone.bleConnected()) {
            Serial.println("Waiting for the phone to connect...");
        } else if (!live) {
            Serial.printf("Connected over BLE, waiting for data%s%s\n", *phone.lastError() ? ": " : "", phone.lastError());
        } else {
            Serial.printf("%lu samples/s   x=%6.2f y=%6.2f z=%6.2f   from %s\n",
                          (unsigned long)(phone.samplesReceived() - lastCount), (*a)[0], (*a)[1], (*a)[2], phone.device());
        }
        lastCount = phone.samplesReceived();
        lastPrint = millis();
    }
}
