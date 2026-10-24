// AllSense frame template reader: a board with its OWN protocol, no AllSense library.
// This is what a robot that already has a serial protocol looks like. The phone learns the
// format from a template (app/src/main/assets/templates/my_robot_imu.json) and sends:
//
//   AA 55 | ax ay az (int16, m/s² × 100) | gx gy gz (int16, rad/s × 1000) | seq (u8) | xor (u8)
//   16 bytes, little-endian, 100 per second. xor covers the 13 bytes after the header.
//
// 1. Upload this sketch (ESP32-S3: Tools -> USB CDC On Boot -> Enabled; use the "USB" port).
// 2. Plug the board into the phone. In the app: Start streaming, then Microcontroller. In the USB card, under
//    "Speaks", tap Change and pick "my_robot_imu".
// 3. Tilt the phone: the RGB LED follows the tilt. Blinking blue means no valid frames.
//
// Once a second the sketch prints one status line back over the same cable. The phone ignores
// it; on a PC, allsense-fake-template (pip install "allsense-testkit[serial]") uses it to check the sketch without a phone.

const uint8_t kHeader0 = 0xAA, kHeader1 = 0x55;
const int kFrameSize = 16;

uint8_t frame[kFrameSize];
int have = 0;                  // bytes of the current frame received so far
unsigned long goodFrames = 0, badFrames = 0, lastFrameMs = 0;
int16_t accel[3], gyro[3];
uint8_t lastSeq = 0;

int16_t readI16(const uint8_t* p) { return (int16_t)(p[0] | (p[1] << 8)); }

void onFrame() {
    uint8_t x = 0;
    for (int i = 2; i < kFrameSize - 1; i++) x ^= frame[i];
    if (x != frame[kFrameSize - 1]) { badFrames++; return; }
    for (int i = 0; i < 3; i++) {
        accel[i] = readI16(frame + 2 + 2 * i);
        gyro[i] = readI16(frame + 8 + 2 * i);
    }
    lastSeq = frame[14];
    goodFrames++;
    lastFrameMs = millis();
}

void setup() {
    Serial.begin(115200);
}

void loop() {
    while (Serial.available()) {
        uint8_t b = Serial.read();
        // Wait for the two header bytes, then collect the rest of the frame.
        if (have == 0 && b != kHeader0) continue;
        if (have == 1 && b != kHeader1) { have = (b == kHeader0) ? 1 : 0; continue; }
        frame[have++] = b;
        if (have == kFrameSize) { onFrame(); have = 0; }
    }

    bool live = millis() - lastFrameMs < 500 && goodFrames > 0;
#ifdef RGB_BUILTIN
    if (!live) {
        rgbLedWrite(RGB_BUILTIN, 0, 0, (millis() / 500) % 2 ? 24 : 0);
    } else {
        auto level = [](int16_t v) { return (uint8_t)constrain(abs(v) * 64L / 1000, 0L, 64L); };  // 10 m/s² = full
        rgbLedWrite(RGB_BUILTIN, level(accel[0]), level(accel[1]), level(accel[2]) / 4);
    }
#endif

    static unsigned long lastReport = 0;
    if (millis() - lastReport >= 1000) {
        lastReport = millis();
        Serial.print("frames=");
        Serial.print(goodFrames);
        Serial.print(" bad=");
        Serial.print(badFrames);
        Serial.print(" seq=");
        Serial.print(lastSeq);
        Serial.print(" accel_x100=");
        Serial.print(accel[0]); Serial.print(','); Serial.print(accel[1]); Serial.print(','); Serial.print(accel[2]);
        Serial.print(" gyro_x1000=");
        Serial.print(gyro[0]); Serial.print(','); Serial.print(gyro[1]); Serial.print(','); Serial.println(gyro[2]);
    }
}
