// Wi-Fi client: ESP32 only (other boards use AllSenseSerial).
#if defined(ESP32)

#include "AllSense.h"

#include <ArduinoJson.h>
#include <WiFi.h>

namespace {
// Copies at most size-1 chars and always terminates.
void copy(char* dst, size_t size, const char* src, size_t len) {
    size_t n = len < size - 1 ? len : size - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}
}  // namespace

bool AllSense::begin(const char* link) {
    // Accepts "http://host:port/?token=abc", "host:port/?token=abc" or "ws://host:port/ws?token=abc".
    const char* p = strstr(link, "://");
    p = p ? p + 3 : link;
    const char* hostEnd = p;
    while (*hostEnd && *hostEnd != ':' && *hostEnd != '/' && *hostEnd != '?') hostEnd++;
    char host[64];
    copy(host, sizeof(host), p, hostEnd - p);
    uint16_t port = 8765;
    if (*hostEnd == ':') port = (uint16_t)atoi(hostEnd + 1);
    const char* t = strstr(link, "token=");
    if (!host[0] || !t) return false;
    t += 6;
    const char* tEnd = t;
    while (*tEnd && *tEnd != '&' && *tEnd != ' ' && *tEnd != '\r' && *tEnd != '\n') tEnd++;
    char token[48];
    copy(token, sizeof(token), t, tEnd - t);
    begin(host, port, token);
    return true;
}

void AllSense::setPowerSave(bool on) { WiFi.setSleep(on); }

void AllSense::begin(const char* host, uint16_t port, const char* token) {
    setPowerSave(false);
    // catalog=none: the phone's catalog (~20 KB) exceeds what the ESP32 WebSocket library accepts
    // in one message (15 KB), and topics can be built from the device name instead.
    String path = String("/ws?token=") + token + "&catalog=none";
    _ws.begin(host, port, path.c_str());
    _ws.onEvent([this](WStype_t type, uint8_t* payload, size_t length) { onEvent(type, payload, length); });
    _ws.setReconnectInterval(2000);
    // Ping every 5 s; two missed pongs (3 s each) mean the link is dead (e.g. Wi-Fi gone) -> reconnect.
    _ws.enableHeartbeat(5000, 3000, 2);
}

bool AllSense::subscribe(const char* name, float rateHz) {
    Stream* s = find(name);
    if (!s) {
        for (auto& candidate : _streams) {
            if (!candidate.used) { s = &candidate; break; }
        }
        if (!s) return false;  // raise ALLSENSE_MAX_STREAMS
        *s = Stream();
        s->used = true;
        copy(s->name, sizeof(s->name), name, strlen(name));
        s->sample.name = s->name;
    }
    s->rateHz = rateHz;
    if (_ready) sendSubscribe(*s);  // otherwise sent once the phone says hello
    return true;
}

void AllSense::unsubscribe(const char* name) {
    Stream* s = find(name);
    if (!s) return;
    if (_ready) {
        JsonDocument doc;
        doc["op"] = "unsubscribe";
        doc["topics"].add(s->topic);
        String out;
        serializeJson(doc, out);
        sendText(out.c_str());
    }
    s->used = false;
}

void AllSense::loop() { _ws.loop(); }

const AllSenseSample* AllSense::latest(const char* name) const {
    const Stream* s = find(name);
    return s && s->has ? &s->sample : nullptr;
}

void AllSense::onEvent(WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case WStype_CONNECTED:
            sendText("{\"op\":\"hello\",\"client\":\"esp32\"}");
            break;
        case WStype_DISCONNECTED:
            _ready = false;
            break;
        case WStype_TEXT:
            handleText((const char*)payload, length);
            break;
        default:
            break;
    }
}

void AllSense::handleText(const char* text, size_t length) {
    // Not requested (catalog=none), but skip it cheaply if an older app sends it anyway.
    if (strncmp(text, "{\"op\":\"catalog\"", 15) == 0) return;
    // Link test padding: skip without parsing.
    if (strncmp(text, "{\"op\":\"fill\"", 12) == 0) return;

    JsonDocument doc;
    if (deserializeJson(doc, text, length)) return;
    const char* op = doc["op"] | "";

    if (strcmp(op, "data") == 0) {
        Stream* s = findTopic(doc["topic"] | "");
        if (!s) return;
        AllSenseSample& sample = s->sample;
        JsonArrayConst v = doc["v"];
        sample.count = 0;
        for (JsonVariantConst x : v) {
            if (sample.count >= ALLSENSE_MAX_VALUES) break;
            sample.values[sample.count++] = x.isNull() ? NAN : x.as<float>();
        }
        sample.seq = doc["seq"] | 0;
        sample.phoneTimeNs = doc["t"].as<uint64_t>();
        sample.accuracy = doc["acc"] | 0;
        sample.receivedMs = millis();
        s->has = true;
        _received++;
        if (_callback) _callback(sample);
    } else if (strcmp(op, "hello") == 0) {
        const char* device = doc["device"] | "";
        copy(_device, sizeof(_device), device, strlen(device));
        _ready = true;
        for (auto& s : _streams) {
            if (s.used) sendSubscribe(s);  // first connect, or resume after a reconnect
        }
    } else if (strcmp(op, "ping") == 0) {
        // Link test: answer at once with the same id.
        char pong[48];
        snprintf(pong, sizeof pong, "{\"op\":\"pong\",\"id\":%ld}", (long)(doc["id"] | 0L));
        sendText(pong);
    } else if (strcmp(op, "status") == 0 || strcmp(op, "error") == 0) {
        const char* msg = doc["reason"] | (doc["message"] | "");
        copy(_error, sizeof(_error), msg, strlen(msg));
    }
}

void AllSense::sendSubscribe(Stream& s) {
    // Topics are allsense.<device>.sensors.<name> (https://docs.allsense.otterbridge.org/PROTOCOL/).
    snprintf(s.topic, sizeof(s.topic), "allsense.%s.sensors.%s", _device, s.name);
    JsonDocument doc;
    doc["op"] = "subscribe";
    doc["topics"].add(s.topic);
    doc["rate_hz"] = s.rateHz;
    String out;
    serializeJson(doc, out);
    sendText(out.c_str());
}

void AllSense::sendText(const char* json) { _ws.sendTXT(json); }

AllSense::Stream* AllSense::find(const char* name) {
    for (auto& s : _streams) {
        if (s.used && strcmp(s.name, name) == 0) return &s;
    }
    return nullptr;
}

const AllSense::Stream* AllSense::find(const char* name) const {
    for (const auto& s : _streams) {
        if (s.used && strcmp(s.name, name) == 0) return &s;
    }
    return nullptr;
}

AllSense::Stream* AllSense::findTopic(const char* topic) {
    for (auto& s : _streams) {
        if (s.used && strcmp(s.topic, topic) == 0) return &s;
    }
    return nullptr;
}

#endif  // ESP32
