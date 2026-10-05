// I2C stub that impersonates the AXS15231B touch controller (address 0x3B).
// A mouse click in the simulator window is reported as a single touch point.
#pragma once
#include <Arduino.h>

class TwoWire
{
public:
    bool begin(int, int) { return true; }
    void setClock(uint32_t) {}
    void beginTransmission(uint8_t) {}
    size_t write(const uint8_t *, size_t n) { return n; }
    uint8_t endTransmission() { return 0; }
    size_t requestFrom(uint8_t addr, size_t len);
    int read();
private:
    uint8_t buf[16];
    size_t pos = 0, n = 0;
};
extern TwoWire Wire;
