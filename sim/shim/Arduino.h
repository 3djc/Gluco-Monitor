// Host-side stand-in for the Arduino-ESP32 core, just enough to build the Gluco-Monitor
// firmware sources on a desktop machine. String/Print come from the real core.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdarg.h>
#include <algorithm>
#include <cmath>
#include <pgmspace.h>
#include "stdlib_noniso.h"
#include "WString.h"
#include "Print.h"
#include "esp32-hal-log.h"

using std::abs;
using std::max;
using std::min;

#define EXT_RAM_BSS_ATTR
#define IRAM_ATTR
#define DRAM_ATTR

typedef uint8_t byte;
typedef bool boolean;

#define LOW 0
#define HIGH 1
#define INPUT 1
#define OUTPUT 3
#define INPUT_PULLUP 5

#define ADC_11db 3
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))

unsigned long millis();
unsigned long micros();
void delay(uint32_t ms);
void delayMicroseconds(uint32_t us);
int64_t esp_timer_get_time();
long map(long x, long in_min, long in_max, long out_min, long out_max);

inline void pinMode(uint8_t, uint8_t) {}
inline void digitalWrite(uint8_t, uint8_t) {}
inline int digitalRead(uint8_t) { return HIGH; }
inline void analogReadResolution(uint8_t) {}
inline void analogSetPinAttenuation(uint8_t, int) {}
uint32_t analogReadMilliVolts(uint8_t pin);

// Serial maps to the terminal: output goes to stdout, input comes from stdin (non-blocking).
class SimSerial : public Print
{
public:
    void begin(unsigned long) {}
    size_t write(uint8_t c) override;
    size_t write(const uint8_t *buf, size_t n) override;
    int available();
    int read();
    void flush() {}
    operator bool() const { return true; }
};
extern SimSerial Serial;

class EspClass
{
public:
    void restart();
    uint32_t getFreeHeap() { return 200000; }
    uint32_t getFreePsram() { return 8000000; }
    uint64_t getEfuseMac() { return 0x0000112233445566ULL; }
};
extern EspClass ESP;

inline bool psramInit() { return true; }
inline bool psramFound() { return true; }
inline int ledcAttach(uint8_t, uint32_t, uint8_t) { return 1; }
void ledcWrite(uint8_t pin, uint32_t duty);
