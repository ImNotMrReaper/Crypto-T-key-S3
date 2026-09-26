// Host-test stand-in for the Arduino core: only what the unit-tested modules use.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <string>

#include <cmath>
#include <algorithm>

#define HIGH 1
#define LOW 0
#define INPUT 0x00
#define OUTPUT 0x01
#define INPUT_PULLUP 0x05
// Arduino defines these as macros: keep them here so a name clash fails on the host too
#define HEX 16
#define DEC 10
#define OCT 8
#define BIN 2

#ifndef PI
#define PI 3.14159265358979323846f
#endif

using std::min;
using std::max;

namespace hoststub {
extern uint32_t now_ms;          // tests advance time explicitly
extern int pin_level[64];        // tests set the button level
}
inline uint32_t millis() { return hoststub::now_ms; }
inline uint32_t micros() { return hoststub::now_ms * 1000u; }
inline void delay(uint32_t ms) { hoststub::now_ms += ms; }
inline void pinMode(uint8_t, uint8_t) {}
inline void digitalWrite(uint8_t, uint8_t) {}
inline int digitalRead(uint8_t pin) { return hoststub::pin_level[pin & 63]; }

// Just enough of Arduino's String for the modules under test.
class String {
public:
    String(const char* s = "") : _s(s ? s : "") {}
    String(const std::string& s) : _s(s) {}
    unsigned int length() const { return (unsigned int)_s.size(); }
    const char* c_str() const { return _s.c_str(); }
    String& operator=(const char* s) { _s = s ? s : ""; return *this; }
    bool operator==(const char* s) const { return _s == s; }
private:
    std::string _s;
};

// Minimal Serial sink so modules that log still link.
struct HostSerial {
    template <typename... A> int printf(const char* f, A... a) { return 0; }
    void println(const char* = "") {}
    void print(const char*) {}
    void flush() {}
};
extern HostSerial Serial;
