// The AllSense compact protocol: framing and messages for serial links (https://docs.allsense.otterbridge.org/PROTOCOL/,
// "Compact protocol"). Plain C++ with no Arduino dependency, so it can be unit-tested on a PC.
//
// Wire format: COBS(payload + CRC16 little-endian) 0x00.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace allsense_compact {

enum : uint8_t {
    kHello = 0x01,
    kSubscribe = 0x02,
    kUnsubscribe = 0x03,
    kStatus = 0x04,
    kHeartbeat = 0x05,
    kPing = 0x06,  // phone -> board: answer with kPong and the same bytes (link test)
    kPong = 0x07,
    kFill = 0x08,  // phone -> board: link test padding, ignore it
    kData = 0x10,
};
enum : uint8_t { kStatusOk = 0, kStatusUnknownName = 1, kStatusRefused = 2, kStatusArmed = 3, kStatusNotShared = 4 };
static const uint8_t kVersion = 0;
static const size_t kMaxPayload = 250;

/** CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF). */
inline uint16_t crc16(const uint8_t* data, size_t length) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < length; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; b++) crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}

/** Canonical COBS. `out` needs length + length/254 + 1 bytes. Returns the encoded size (no 0x00). */
inline size_t cobsEncode(const uint8_t* in, size_t length, uint8_t* out) {
    size_t write = 1, codeAt = 0;
    uint8_t code = 1;
    bool endsWithFullBlock = false;
    for (size_t i = 0; i < length; i++) {
        endsWithFullBlock = false;
        if (in[i] == 0) {
            out[codeAt] = code;
            code = 1;
            codeAt = write++;
        } else {
            out[write++] = in[i];
            if (++code == 0xFF) {
                out[codeAt] = code;
                code = 1;
                codeAt = write++;
                endsWithFullBlock = true;
            }
        }
    }
    if (endsWithFullBlock) return write - 1;  // no empty block after a full 254-byte run
    out[codeAt] = code;
    return write;
}

/** Decodes in place. Returns the decoded size, or 0 if the input isn't valid COBS. */
inline size_t cobsDecode(uint8_t* buf, size_t length) {
    size_t read = 0, write = 0;
    while (read < length) {
        uint8_t code = buf[read];
        if (code == 0 || read + code > length) return 0;
        read++;
        for (uint8_t i = 1; i < code; i++) buf[write++] = buf[read++];
        if (code < 0xFF && read < length) buf[write++] = 0;
    }
    return write;
}

/**
 * Builds a frame in `out` from `payload`: COBS(payload + CRC) followed by 0x00.
 * `out` needs payloadLength + 2 + (payloadLength + 2) / 254 + 2 bytes. Returns the frame size.
 */
inline size_t frame(const uint8_t* payload, size_t payloadLength, uint8_t* out) {
    uint8_t raw[kMaxPayload + 2];
    if (payloadLength > kMaxPayload) return 0;
    memcpy(raw, payload, payloadLength);
    uint16_t crc = crc16(payload, payloadLength);
    raw[payloadLength] = (uint8_t)(crc & 0xFF);
    raw[payloadLength + 1] = (uint8_t)(crc >> 8);
    size_t n = cobsEncode(raw, payloadLength + 2, out);
    out[n++] = 0;
    return n;
}

/** Checks one received frame (bytes before the 0x00, decoded in place). Returns the payload size or 0. */
inline size_t unframe(uint8_t* buf, size_t length) {
    size_t n = cobsDecode(buf, length);
    if (n < 3) return 0;
    uint16_t crc = (uint16_t)buf[n - 2] | ((uint16_t)buf[n - 1] << 8);
    return crc16(buf, n - 2) == crc ? n - 2 : 0;
}

inline void putF32(uint8_t* p, float v) { memcpy(p, &v, 4); }  // all supported MCUs are little-endian
inline float getF32(const uint8_t* p) { float v; memcpy(&v, p, 4); return v; }
inline uint16_t getU16(const uint8_t* p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
inline uint32_t getU32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/** HELLO payload. Returns its length. */
inline size_t hello(uint8_t* out, const char* name) {
    size_t n = strlen(name);
    if (n > 64) n = 64;
    out[0] = kHello;
    out[1] = kVersion;
    memcpy(out + 2, name, n);
    return 2 + n;
}

/** SUBSCRIBE payload. Returns its length. */
inline size_t subscribe(uint8_t* out, uint8_t streamId, float rateHz, const char* name) {
    size_t n = strlen(name);
    if (n > 64) n = 64;
    out[0] = kSubscribe;
    out[1] = streamId;
    putF32(out + 2, rateHz);
    memcpy(out + 6, name, n);
    return 6 + n;
}

inline size_t unsubscribe(uint8_t* out, uint8_t streamId) {
    out[0] = kUnsubscribe;
    out[1] = streamId;
    return 2;
}

inline size_t heartbeat(uint8_t* out) {
    out[0] = kHeartbeat;
    return 1;
}

/** A parsed DATA payload. Values beyond `maxValues` are dropped. */
struct Data {
    uint8_t streamId;
    uint16_t seq;
    uint32_t tUs;
    int8_t accuracy;
    uint8_t count;  // values stored (≤ maxValues)
};

inline bool parseData(const uint8_t* p, size_t n, Data& d, float* values, uint8_t maxValues) {
    if (n < 10 || p[0] != kData) return false;
    uint8_t total = p[9];
    if (n < 10 + 4u * total) return false;
    d.streamId = p[1];
    d.seq = getU16(p + 2);
    d.tUs = getU32(p + 4);
    d.accuracy = (int8_t)p[8];
    d.count = total < maxValues ? total : maxValues;
    for (uint8_t i = 0; i < d.count; i++) values[i] = getF32(p + 10 + 4 * i);
    return true;
}

}  // namespace allsense_compact
