#include "AllSenseSerial.h"

namespace ac = allsense_compact;

namespace {
const uint32_t kHelloEveryMs = 1000;    // until the phone answers
const uint32_t kLinkLostAfterMs = 3000; // the phone sends something at least every second
const uint32_t kHeartbeatEveryMs = 1000;

void copyName(char* dst, size_t size, const char* src) {
    strncpy(dst, src, size - 1);
    dst[size - 1] = '\0';
}
}  // namespace

void AllSenseSerial::begin(Stream& port, const char* clientName) {
    _port = &port;
    copyName(_client, sizeof(_client), clientName);
    _ready = false;
    _lastHelloMs = millis() - kHelloEveryMs;  // say hello on the first loop()
}

bool AllSenseSerial::subscribe(const char* name, float rateHz) {
    int i = find(name);
    if (i < 0) {
        for (int j = 0; j < ALLSENSE_MAX_STREAMS; j++) {
            if (!_streams[j].used) { i = j; break; }
        }
        if (i < 0) return false;  // raise ALLSENSE_MAX_STREAMS
        _streams[i] = Stream_();
        _streams[i].used = true;
        copyName(_streams[i].name, sizeof(_streams[i].name), name);
        _streams[i].sample.name = _streams[i].name;
    }
    _streams[i].rateHz = rateHz;
    if (_ready) sendSubscribe((uint8_t)i);  // otherwise sent once the phone says hello
    return true;
}

void AllSenseSerial::unsubscribe(const char* name) {
    int i = find(name);
    if (i < 0) return;
    if (_ready) {
        uint8_t p[2];
        sendPayload(p, ac::unsubscribe(p, (uint8_t)i));
    }
    _streams[i].used = false;
}

const AllSenseSample* AllSenseSerial::latest(const char* name) const {
    int i = find(name);
    return i >= 0 && _streams[i].has ? &_streams[i].sample : nullptr;
}

void AllSenseSerial::loop() {
    if (!_port) return;
    // Split the byte stream at 0x00 into frames.
    while (_port->available() > 0) {
        int c = _port->read();
        if (c < 0) break;
        if (c != 0) {
            if (_rxLength < sizeof(_rx)) _rx[_rxLength++] = (uint8_t)c; else _rxOverflow = true;
            continue;
        }
        if (_rxLength > 0 && !_rxOverflow) {
            size_t n = ac::unframe(_rx, _rxLength);
            if (n > 0) handleFrame(_rx, n);
        }
        _rxLength = 0;
        _rxOverflow = false;
    }

    uint32_t now = millis();
    if (_ready && now - _lastRxMs > kLinkLostAfterMs) _ready = false;  // unplugged, or streaming stopped
    if (!_ready && now - _lastHelloMs >= kHelloEveryMs) {
        _lastHelloMs = now;
        uint8_t p[2 + 64];
        sendPayload(p, ac::hello(p, _client));
    }
    if (_ready && now - _lastTxMs >= kHeartbeatEveryMs) {
        uint8_t p[1];
        sendPayload(p, ac::heartbeat(p));
    }
}

void AllSenseSerial::handleFrame(uint8_t* p, size_t n) {
    _lastRxMs = millis();
    switch (p[0]) {
        case ac::kData: {
            ac::Data d;
            float values[ALLSENSE_MAX_VALUES];
            if (!ac::parseData(p, n, d, values, ALLSENSE_MAX_VALUES)) return;
            if (d.streamId >= ALLSENSE_MAX_STREAMS || !_streams[d.streamId].used) return;
            AllSenseSample& s = _streams[d.streamId].sample;
            memcpy(s.values, values, sizeof(float) * d.count);
            s.count = d.count;
            s.seq = d.seq;
            s.phoneTimeNs = (uint64_t)d.tUs * 1000;
            s.accuracy = d.accuracy;
            s.receivedMs = _lastRxMs;
            _streams[d.streamId].has = true;
            _received++;
            if (_callback) _callback(s);
            break;
        }
        case ac::kHello: {
            // The phone (re)started the session and dropped our subscriptions: send them again.
            size_t len = n > 2 ? n - 2 : 0;
            if (len >= sizeof(_device)) len = sizeof(_device) - 1;
            memcpy(_device, p + 2, len);
            _device[len] = '\0';
            _ready = true;
            for (int i = 0; i < ALLSENSE_MAX_STREAMS; i++) {
                if (_streams[i].used) sendSubscribe((uint8_t)i);
            }
            break;
        }
        case ac::kPing: {
            // Link test: echo it back as PONG (same token) so the phone can time the round trip.
            if (n >= 5) {
                uint8_t pong[5] = {ac::kPong, p[1], p[2], p[3], p[4]};
                sendPayload(pong, sizeof pong);
            }
            break;
        }
        case ac::kStatus: {
            if (n >= 3 && p[2] != ac::kStatusOk && p[2] != ac::kStatusArmed) {
                size_t len = n - 3;
                if (len >= sizeof(_error)) len = sizeof(_error) - 1;
                memcpy(_error, p + 3, len);
                _error[len] = '\0';
            }
            break;
        }
        default:
            break;  // heartbeat or unknown: just proves the link is alive
    }
}

void AllSenseSerial::sendSubscribe(uint8_t id) {
    uint8_t p[6 + 64];
    sendPayload(p, ac::subscribe(p, id, _streams[id].rateHz, _streams[id].name));
}

void AllSenseSerial::sendPayload(const uint8_t* payload, size_t length) {
    uint8_t out[6 + 64 + 2 + 4];  // largest message we send: SUBSCRIBE with a 64-char name
    size_t n = ac::frame(payload, length, out);
    if (n == 0) return;
    _port->write(out, n);
    _lastTxMs = millis();
}

int AllSenseSerial::find(const char* name) const {
    for (int i = 0; i < ALLSENSE_MAX_STREAMS; i++) {
        if (_streams[i].used && strcmp(_streams[i].name, name) == 0) return i;
    }
    return -1;
}

unsigned long allsenseMillis() { return millis(); }
