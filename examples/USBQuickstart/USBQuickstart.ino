// AllSense USB quickstart: plug the board into the phone and it becomes your motion sensor.
// No Wi-Fi, no passwords. The phone powers the board through the cable.
//
// 1. Upload this sketch from your computer.
//    ESP32-S3: Tools -> USB CDC On Boot -> Enabled, and use the board's port labelled "USB".
// 2. Plug the board into the phone with a USB-C to USB-C data cable (or an OTG adapter).
//    Android asks whether to open AllSense: tick "Always" so it connects by itself next time.
// 3. In the app: Start streaming.
//
// The USB port now carries the AllSense protocol, so this sketch shows what's happening on the
// RGB LED instead of printing:
//   blinking blue      waiting for the phone (is streaming started?)
//   colour = tilt      live data: red follows x, green follows y
//   red flashes        the phone reported a problem (e.g. a misspelt stream name)

#include <AllSenseSerial.h>

AllSenseSerial phone;

#ifdef RGB_BUILTIN
void led(uint8_t r, uint8_t g, uint8_t b) { rgbLedWrite(RGB_BUILTIN, r, g, b); }
#elif defined(LED_BUILTIN)
void led(uint8_t r, uint8_t g, uint8_t b) { digitalWrite(LED_BUILTIN, (r | g | b) ? HIGH : LOW); }
#else
void led(uint8_t, uint8_t, uint8_t) {}
#endif

void setup() {
    Serial.begin(115200);  // on native-USB boards the speed doesn't matter
#if !defined(RGB_BUILTIN) && defined(LED_BUILTIN)
    pinMode(LED_BUILTIN, OUTPUT);
#endif
    phone.begin(Serial, "usb-quickstart");
    phone.subscribe("accelerometer", 50);  // name as shown (and renamable) in the app
}

void loop() {
    phone.loop();

    if (!phone.connected()) {
        led(0, 0, (millis() / 500) % 2 ? 24 : 0);  // blinking blue
        return;
    }
    if (phone.lastError()[0] && (millis() / 150) % 2) {
        led(40, 0, 0);  // red flash
        return;
    }
    const AllSenseSample* a = phone.latest("accelerometer");
    if (!a) return;
    // Tilt -> colour; 9.8 m/s² (1 g) on an axis is full brightness (kept low: these LEDs are bright).
    auto level = [](float v) { return (uint8_t)constrain(fabsf(v) * 6.5f, 0.0f, 64.0f); };
    led(level((*a)[0]), level((*a)[1]), level((*a)[2]) / 4);
}
