// Host tests for the compact protocol and AllSenseSerial (run: bash extras/test/run.sh).
// The expected bytes are the same as in the app's Kotlin tests (CompactTest.kt) and
// the protocol (vectors/compact_vectors.json in otterbridge-allsense-protocol), so all implementations must agree byte for byte.
#include <cmath>
#include <cstdio>
#include <deque>
#include <string>
#include <vector>

#include "../../src/AllSenseCompact.h"
#include "../../src/AllSenseSerial.h"

namespace ac = allsense_compact;
unsigned long fake_now_ms = 1000;
static int failures = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);          \
            failures++;                                                          \
        }                                                                        \
    } while (0)

static std::string hex(const uint8_t* p, size_t n) {
    std::string s;
    char b[3];
    for (size_t i = 0; i < n; i++) {
        std::snprintf(b, sizeof b, "%02x", p[i]);
        s += b;
    }
    return s;
}

static std::vector<uint8_t> unhex(const std::string& s) {
    std::vector<uint8_t> v;
    for (size_t i = 0; i < s.size(); i += 2) v.push_back((uint8_t)std::stoi(s.substr(i, 2), nullptr, 16));
    return v;
}

static void testCodec() {
    CHECK(ac::crc16((const uint8_t*)"123456789", 9) == 0x29B1);

    const char* cases[][2] = {{"00", "0101"},         {"0000", "010101"},         {"001100", "01021101"},
                              {"11220033", "0311220233"}, {"11223344", "0511223344"}, {"11000000", "0211010101"}};
    for (auto& c : cases) {
        auto raw = unhex(c[0]);
        uint8_t out[16];
        size_t n = ac::cobsEncode(raw.data(), raw.size(), out);
        CHECK(hex(out, n) == c[1]);
        size_t m = ac::cobsDecode(out, n);
        CHECK(hex(out, m) == c[0]);
    }
    uint8_t run[255], enc[260];
    for (int i = 0; i < 255; i++) run[i] = (uint8_t)(i + 1);
    size_t n = ac::cobsEncode(run, 254, enc);
    CHECK(n == 255 && enc[0] == 0xFF);  // canonical: no trailing empty block
    n = ac::cobsEncode(run, 255, enc);
    CHECK(n == 257 && enc[255] == 0x02 && enc[256] == 0xFF);

    // Reference frames (as in vectors/compact_vectors.json and the spec).
    uint8_t p[80], f[100];
    size_t len = ac::subscribe(p, 1, 50.0f, "accelerometer");
    CHECK(hex(f, ac::frame(p, len, f)) == "03020101124842616363656c65726f6d65746572b12100");

    auto data = unhex("0b1003010215cd5b070303010101010109c03fc3f51cc18f6f");  // DATA frame, no 0x00
    size_t pl = ac::unframe(data.data(), data.size());
    CHECK(pl == 22);
    ac::Data d;
    float v[8];
    CHECK(ac::parseData(data.data(), pl, d, v, 8));
    CHECK(d.streamId == 3 && d.seq == 513 && d.tUs == 123456789u && d.accuracy == 3 && d.count == 3);
    CHECK(v[0] == 0.0f && v[1] == 1.5f && std::fabs(v[2] + 9.81f) < 1e-6f);

    auto bad = unhex("0b1003010215cd5b070303010101010109c03fc3f51cc18f6e");  // CRC off by one
    CHECK(ac::unframe(bad.data(), bad.size()) == 0);
}

// A fake phone on the other end of the cable.
class FakePort : public Stream {
public:
    std::deque<uint8_t> toBoard;
    std::vector<uint8_t> fromBoard;

    int available() override { return (int)toBoard.size(); }
    int read() override {
        if (toBoard.empty()) return -1;
        int c = toBoard.front();
        toBoard.pop_front();
        return c;
    }
    size_t write(const uint8_t* data, size_t n) override {
        fromBoard.insert(fromBoard.end(), data, data + n);
        return n;
    }

    void phoneSends(const uint8_t* payload, size_t n) {
        uint8_t f[300];
        size_t m = ac::frame(payload, n, f);
        toBoard.insert(toBoard.end(), f, f + m);
    }

    // Payloads of the frames the board sent since the last call.
    std::vector<std::vector<uint8_t>> boardFrames() {
        std::vector<std::vector<uint8_t>> out;
        std::vector<uint8_t> cur;
        for (uint8_t b : fromBoard) {
            if (b) {
                cur.push_back(b);
                continue;
            }
            size_t n = ac::unframe(cur.data(), cur.size());
            if (n) out.emplace_back(cur.begin(), cur.begin() + n);
            cur.clear();
        }
        fromBoard.clear();
        return out;
    }
};

static int callbackCount = 0;
static void onSample(const AllSenseSample&) { callbackCount++; }

static void testSession() {
    FakePort port;
    AllSenseSerial phone;
    phone.begin(port, "esp32");
    phone.subscribe("accelerometer", 50);
    phone.onSample(onSample);

    phone.loop();  // the board says hello
    auto frames = port.boardFrames();
    CHECK(frames.size() == 1 && frames[0][0] == ac::kHello);
    CHECK(frames.size() == 1 && std::string(frames[0].begin() + 2, frames[0].end()) == "esp32");
    CHECK(!phone.connected());

    // The phone answers -> the board subscribes with stream id 0.
    uint8_t p[80];
    port.phoneSends(p, ac::hello(p, "rover"));
    phone.loop();
    CHECK(phone.connected());
    CHECK(std::string(phone.device()) == "rover");
    frames = port.boardFrames();
    CHECK(frames.size() == 1 && frames[0][0] == ac::kSubscribe && frames[0][1] == 0);
    CHECK(frames.size() == 1 && std::string(frames[0].begin() + 6, frames[0].end()) == "accelerometer");

    // Line noise, then data for stream 0: seq 7, t = 1000000 µs, values (0, 1.5, -9.81).
    const uint8_t noise[] = {0x41, 0x42, 0x00};
    port.toBoard.insert(port.toBoard.end(), noise, noise + 3);
    const uint8_t dp[] = {ac::kData, 0, 7, 0, 0x40, 0x42, 0x0F, 0x00, 3, 3, 0, 0, 0, 0, 0, 0, 0xC0, 0x3F, 0xC3, 0xF5, 0x1C, 0xC1};
    port.phoneSends(dp, sizeof dp);
    phone.loop();
    const AllSenseSample* a = phone.latest("accelerometer");
    CHECK(a != nullptr);
    if (a) {
        CHECK(a->count == 3 && (*a)[1] == 1.5f && a->seq == 7 && a->phoneTimeNs == 1000000ull * 1000);
        CHECK(std::isnan((*a)[5]));
    }
    CHECK(callbackCount == 1 && phone.samplesReceived() == 1);

    // Unknown stream name: the phone's STATUS text is kept.
    const uint8_t st[] = {ac::kStatus, 0, ac::kStatusUnknownName, 'N', 'o', 'p', 'e'};
    port.phoneSends(st, sizeof st);
    phone.loop();
    CHECK(std::string(phone.lastError()) == "Nope");

    // A stream the user hasn't shared over this link: the reason fits the error buffer whole.
    const char reason[] = "Not shared over Bluetooth (Microcontroller)";
    uint8_t ns[3 + sizeof reason] = {ac::kStatus, 0, ac::kStatusNotShared};
    memcpy(ns + 3, reason, sizeof reason - 1);
    port.phoneSends(ns, 3 + sizeof reason - 1);
    phone.loop();
    CHECK(std::string(phone.lastError()) == reason);

    // Link test: PING is answered with PONG and the same token.
    const uint8_t ping[] = {ac::kPing, 4, 3, 2, 1};
    port.phoneSends(ping, sizeof ping);
    phone.loop();
    frames = port.boardFrames();
    CHECK(frames.size() == 1 && frames[0].size() == 5 && frames[0][0] == ac::kPong && frames[0][1] == 4 && frames[0][4] == 1);
    // FILL (link test padding) is ignored.
    uint8_t fill[240];
    memset(fill, 0x55, sizeof fill);
    fill[0] = ac::kFill;
    port.phoneSends(fill, sizeof fill);
    phone.loop();
    CHECK(port.boardFrames().empty());

    // After 1 s without sending, the board sends a heartbeat.
    fake_now_ms += 1100;
    port.phoneSends(p, ac::heartbeat(p));
    phone.loop();
    frames = port.boardFrames();
    CHECK(frames.size() == 1 && frames[0][0] == ac::kHeartbeat);

    // The phone goes silent for > 3 s (cable unplugged): link lost, the board says hello again.
    fake_now_ms += 3500;
    phone.loop();
    CHECK(!phone.connected());
    frames = port.boardFrames();
    CHECK(!frames.empty() && frames.back()[0] == ac::kHello);

    // The phone comes back: the board resubscribes by itself.
    port.phoneSends(p, ac::hello(p, "rover"));
    phone.loop();
    frames = port.boardFrames();
    CHECK(phone.connected() && !frames.empty() && frames.back()[0] == ac::kSubscribe);
}

int main() {
    testCodec();
    testSession();
    if (failures == 0) std::printf("All compact protocol tests passed.\n");
    return failures == 0 ? 0 : 1;
}
