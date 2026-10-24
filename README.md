# AllSense for Arduino

Use an Android phone running **OtterBridge AllSense** as your project's IMU, compass, barometer, light sensor and more. Two ways to connect:

- **USB cable** (`AllSenseSerial`, any board): plug the board into the phone, e.g. USB-C to USB-C. No Wi-Fi, and the phone powers the board. Tested on ESP32-S3; compiles for Arduino Uno (5 KB flash, 772 B RAM).
- **Bluetooth LE** (`AllSenseBLE`, ESP32): wireless with no router. Tap **Connect** once in the app.
- **Wi-Fi** (`AllSense`, ESP32 family): see below.

## USB (any board)

```cpp
#include <AllSenseSerial.h>

AllSenseSerial phone;

void setup() {
  Serial.begin(115200);
  phone.begin(Serial);                    // the port plugged into the phone
  phone.subscribe("accelerometer", 50);   // name as shown in the app, rate in Hz
}

void loop() {
  phone.loop();
  if (const AllSenseSample* a = phone.latest("accelerometer")) {
    float z = (*a)[2];                    // use it; don't print to Serial (it carries the protocol)
  }
}
```

1. Upload, then plug the board into the phone. Android asks whether to open AllSense: tick **Always** so it reconnects by itself.
2. In the app: **Home → Start streaming**. **Connect → Microcontroller** shows the board.

Example: **File → Examples → AllSense → USBQuickstart**. The RGB LED blinks blue while waiting, then follows the phone's tilt.

Over USB the link uses the compact binary protocol (checksummed frames, about 23 bytes per 3-axis sample). Either side can unplug, reset or restart; the board reconnects and resubscribes by itself. It shares `AllSenseSample`, `latest()` and `onSample()` with the Wi-Fi class. Supported boards: ESP32-S2/S3/C3/C6 native USB, Arduino (Uno, Nano, Mega, R4…), Raspberry Pi Pico, and boards with CH340, CP210x or FTDI USB chips.

## Bluetooth LE (ESP32)

```cpp
#include <AllSenseBLE.h>

AllSenseBLE phone;

void setup() {
  Serial.begin(115200);                  // free for your own debug output
  phone.begin("my robot");               // name shown in the app
  phone.subscribe("accelerometer", 50);
}

void loop() {
  phone.loop();
  if (const AllSenseSample* a = phone.latest("accelerometer")) { float z = (*a)[2]; }
}
```

In the app: **Home → Start streaming**, then **Connect → Microcontroller → By Bluetooth**: tap **Connect** next to your board. That's needed only once: the phone reconnects whenever the board is in range. Example: **BLEQuickstart**. `AllSenseBLE` is an `AllSenseSerial` running over a BLE serial-style service, so the API is identical, and `bleConnected()` tells you if the phone is connected.

## Wi-Fi (ESP32)

```cpp
#include <AllSense.h>
#include <WiFi.h>

AllSense phone;

void setup() {
  Serial.begin(115200);
  WiFi.begin("my-wifi", "my-password");
  phone.begin("http://192.168.1.20:8765/?token=abcd");  // link from the app (Home → Connect)
  phone.subscribe("accelerometer", 50);                  // name as shown in the app, rate in Hz
}

void loop() {
  phone.loop();
  if (const AllSenseSample* a = phone.latest("accelerometer")) {
    Serial.printf("x=%.2f y=%.2f z=%.2f\n", (*a)[0], (*a)[1], (*a)[2]);
  }
  delay(20);
}
```

## Install

1. Arduino IDE → **Library Manager** → install **WebSockets** (by Markus Sattler) and **ArduinoJson** (by Benoit Blanchon). ESP32 boards need them for every sketch; other boards don't.
2. Install this library: on this repository's GitHub page, **Code → Download ZIP**, then in the IDE **Sketch → Include Library → Add .ZIP Library** and pick the ZIP. Or clone the repository into `Documents/Arduino/libraries/AllSense`. (In the Library Manager as **AllSense** soon.)

## Quickstart example

**File → Examples → AllSense → WiFiQuickstart** needs no passwords in code. Upload it, open the Serial Monitor (115200 baud, line ending **Newline**) and type:

```
ssid Your WiFi Name
pass your-wifi-password
link http://192.168.1.20:8765/?token=abcd
```

They're saved on the board. Tilt the phone: values print, and boards with an RGB LED (like the ESP32-S3 DevKit) change colour with the tilt.

## API

| | |
|---|---|
| `begin(link)` | Connect using the app's link (contains the token). Returns false if the link has no token. |
| `subscribe(name, rateHz)` | Start a stream; names as shown and renamed in the app. Up to 12 (`ALLSENSE_MAX_STREAMS`). |
| `unsubscribe(name)` | Stop a stream. |
| `loop()` | Call often. Receives data and reconnects automatically. |
| `latest(name)` | Newest `AllSenseSample*`, or `nullptr` until one arrives. |
| `onSample(callback)` | Called for every sample (from `loop()`). |
| `connected()`, `device()`, `samplesReceived()`, `lastError()` | Status. |

`AllSenseSample`: `s[0]`, `s[1]`… values (`NAN` if missing), `count`, `seq`, `phoneTimeNs` (when the phone measured it), `receivedMs` / `ageMs()`, `accuracy`.

Values use the app's units: m/s² (accelerometer), rad/s (gyroscope), µT (magnetometer), hPa (pressure), lx (light).

## Latency

`begin()` turns off the ESP32's Wi-Fi power saving. With it on (the ESP32 default), the radio dozes between router beacons and samples arrive in bursts 100–300 ms late. To save battery with slow sensors, call `phone.setPowerSave(true)` after `begin()`. Each sample's `ageMs()` tells you how old it is.

## Reliability

If Wi-Fi drops, the library reconnects and resubscribes by itself. A link that goes silent (e.g. the phone left Wi-Fi) is detected with WebSocket pings within about 11 seconds.

## Board settings (ESP32-S3 N16R8)

Board **ESP32S3 Dev Module**, Flash Size **16MB**, PSRAM **OPI PSRAM**, USB CDC On Boot **Enabled** (if you use the port labelled **USB**; use **Disabled** with the **COM/UART** port).

## License

MIT. This library and the protocol are MIT, so you can use them in any project. The AllSense app is a free download from [otterbridge-allsense](https://github.com/wen-da-ng/otterbridge-allsense).
