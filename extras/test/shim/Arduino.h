// Minimal Arduino stand-in so the library's serial client can be unit-tested on a PC.
#pragma once
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

extern unsigned long fake_now_ms;
inline unsigned long millis() { return fake_now_ms; }

class Stream {
public:
    virtual ~Stream() = default;
    virtual int available() = 0;
    virtual int read() = 0;
    virtual size_t write(const uint8_t* data, size_t n) = 0;
};
