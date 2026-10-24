// AllSense Wi-Fi quickstart: your phone becomes this board's motion sensor.
//
// 1. In the AllSense app: Start streaming, then Connect -> Browser shows the link.
// 2. Upload this sketch, open the Serial Monitor at 115200 baud (line ending: Newline) and type:
//      ssid Your WiFi Name
//      pass your-wifi-password
//      link http://192.168.1.20:8765/?token=abcd      <- the link shown in the app
//    They're saved on the board, so you only do this once. Type "show" to see them, "reset" to clear.
// 3. Tilt the phone: values print below, and the board's RGB LED changes colour with the tilt.
//
// The board and the phone must be on the same Wi-Fi (or the board on the phone's hotspot).

#include <AllSense.h>
#include <Preferences.h>
#include <WiFi.h>

AllSense phone;
Preferences settings;
String ssid, pass, phoneLink;
bool started = false;

void loadSettings() {
    settings.begin("allsense", true);
    ssid = settings.getString("ssid", "");
    pass = settings.getString("pass", "");
    phoneLink = settings.getString("link", "");
    settings.end();
}

void save(const char* key, const String& value) {
    settings.begin("allsense", false);
    settings.putString(key, value);
    settings.end();
}

void printHelp() {
    Serial.println();
    Serial.println("AllSense quickstart. Type in the Serial Monitor (Newline line ending):");
    Serial.println("  ssid <Wi-Fi name>    pass <Wi-Fi password>    link <link from the app>");
    Serial.println("  show    reset");
}

// Commands typed into the Serial Monitor.
void handleSerial() {
    if (!Serial.available()) return;
    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line.startsWith("ssid ")) { ssid = line.substring(5); save("ssid", ssid); Serial.println("Saved Wi-Fi name. Restarting..."); delay(300); ESP.restart(); }
    else if (line.startsWith("pass ")) { pass = line.substring(5); save("pass", pass); Serial.println("Saved password. Restarting..."); delay(300); ESP.restart(); }
    else if (line.startsWith("link ")) { phoneLink = line.substring(5); save("link", phoneLink); Serial.println("Saved link. Restarting..."); delay(300); ESP.restart(); }
    else if (line == "show") {
        Serial.printf("ssid: %s\npass: %s\nlink: %s\n", ssid.c_str(), pass.length() ? "(set)" : "(empty)", phoneLink.c_str());
        Serial.printf("Wi-Fi: %s  IP: %s\n", WiFi.isConnected() ? "connected" : "not connected", WiFi.localIP().toString().c_str());
        Serial.printf("phone: %s  device: %s  samples: %lu\n", phone.connected() ? "connected" : "not connected", phone.device(),
                      (unsigned long)phone.samplesReceived());
    } else if (line == "reset") {
        settings.begin("allsense", false); settings.clear(); settings.end();
        Serial.println("Cleared. Restarting..."); delay(300); ESP.restart();
    } else if (line.length()) {
        printHelp();
    }
}

void setup() {
    Serial.begin(115200);
    delay(1500);  // give the Serial Monitor time to attach
    loadSettings();
    printHelp();
    if (ssid.isEmpty() || phoneLink.isEmpty()) {
        Serial.println("\nNot set up yet: type ssid, pass and link (see above).");
        return;
    }
    Serial.printf("Connecting to Wi-Fi \"%s\"", ssid.c_str());
    WiFi.begin(ssid.c_str(), pass.c_str());
    for (int i = 0; i < 40 && !WiFi.isConnected(); i++) { delay(250); Serial.print('.'); handleSerial(); }
    Serial.println(WiFi.isConnected() ? " connected, IP " + WiFi.localIP().toString() : " failed (check ssid/pass; will keep trying)");

    if (!phone.begin(phoneLink.c_str())) {
        Serial.println("That link has no token. Copy the full link from the app (Connect -> Browser).");
        return;
    }
    phone.subscribe("accelerometer", 50);  // names as shown (and renamable) in the app
    phone.subscribe("game_rotation_vector", 20);
    started = true;
}

void loop() {
    handleSerial();
    if (!started) return;
    phone.loop();

    static uint32_t lastPrint = 0;
    static uint32_t lastCount = 0;
    if (millis() - lastPrint < 200) return;  // print 5 times a second
    uint32_t rate = (phone.samplesReceived() - lastCount) * 1000 / max<uint32_t>(1, millis() - lastPrint);
    lastPrint = millis();
    lastCount = phone.samplesReceived();

    const AllSenseSample* a = phone.latest("accelerometer");
    if (!phone.connected() || !a) {
        Serial.printf("Waiting for the phone (%s)%s%s\n", WiFi.isConnected() ? "Wi-Fi ok" : "no Wi-Fi",
                      *phone.lastError() ? ": " : "", phone.lastError());
        return;
    }
    Serial.printf("accel  x=%6.2f  y=%6.2f  z=%6.2f m/s2   age %3lu ms   %lu samples/s\n", (*a)[0], (*a)[1], (*a)[2],
                  (unsigned long)a->ageMs(), (unsigned long)rate);

#ifdef RGB_BUILTIN
    // Tilt -> colour: red follows x, green follows y, blue is "flat".
    auto level = [](float v) { return (uint8_t)constrain(fabsf(v) * 26.0f, 0.0f, 255.0f); };  // 9.8 m/s2 -> ~255
    rgbLedWrite(RGB_BUILTIN, level((*a)[0]) / 4, level((*a)[1]) / 4, level((*a)[2]) / 16);  // /4: not blinding
#endif
}
