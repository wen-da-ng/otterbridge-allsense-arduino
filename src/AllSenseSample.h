// AllSenseSample: one reading from the phone. Shared by the Wi-Fi (AllSense) and serial
// (AllSenseSerial) clients.
#pragma once

#include <math.h>
#include <stdint.h>

#ifndef ALLSENSE_MAX_STREAMS
#if defined(__AVR__)
#define ALLSENSE_MAX_STREAMS 4  // streams you can subscribe to at once (Uno/Nano have 2 KB RAM)
#else
#define ALLSENSE_MAX_STREAMS 12
#endif
#endif
#ifndef ALLSENSE_MAX_VALUES
#define ALLSENSE_MAX_VALUES 8  // values kept per sample (an accelerometer has 3)
#endif
#ifndef ALLSENSE_MAX_NAME
#if defined(__AVR__)
#define ALLSENSE_MAX_NAME 25
#else
#define ALLSENSE_MAX_NAME 33  // stream names are at most 32 characters
#endif
#endif

unsigned long allsenseMillis();  // millis(), declared here so this header has no Arduino dependency

/** One reading from the phone. `s[0]`, `s[1]`… are the values (NAN if missing). */
struct AllSenseSample {
    const char* name = "";     // stream name, e.g. "accelerometer"
    float values[ALLSENSE_MAX_VALUES] = {};
    uint8_t count = 0;         // how many values (3 for x, y, z)
    uint32_t seq = 0;          // counts up; gaps mean samples were lost
    uint64_t phoneTimeNs = 0;  // when the phone's sensor measured it (phone boot clock, ns)
    uint32_t receivedMs = 0;   // millis() on this board when it arrived
    int8_t accuracy = 0;       // 3 high … 0 unreliable

    float operator[](int i) const { return i >= 0 && i < count ? values[i] : NAN; }
    /** Milliseconds since it arrived. */
    uint32_t ageMs() const { return (uint32_t)allsenseMillis() - receivedMs; }
};
