// The shortest AllSense sketch: read the phone's accelerometer over a USB cable.
// Upload, plug the board into the phone, then in the app: Start streaming.

#include <AllSenseSerial.h>

AllSenseSerial phone;

void setup() {
    Serial.begin(115200);
    phone.begin(Serial, "my-robot");
    phone.subscribe("accelerometer", 50);  // name as shown in the app, rate in Hz
}

void loop() {
    phone.loop();
    if (const AllSenseSample* a = phone.latest("accelerometer")) {
        float z = (*a)[2];  // use it; don't print: Serial carries the protocol
        (void)z;
    }
}
