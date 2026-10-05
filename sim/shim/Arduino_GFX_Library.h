// Replaces the library's umbrella header: the real Arduino_GFX core + Arduino_Canvas are used,
// but the QSPI bus and AXS15231B panel are replaced by an SDL window (see sim_runtime.cpp).
#pragma once
#include <Arduino.h>
#include "Arduino_DataBus.h"
#include "Arduino_G.h"
#include "Arduino_GFX.h"
#include "canvas/Arduino_Canvas.h"

// The firmware only passes this to the panel constructor.
class Arduino_ESP32QSPI
{
public:
    Arduino_ESP32QSPI(int8_t, int8_t, int8_t, int8_t, int8_t, int8_t) {}
};

// Physical panel: 320x480 portrait RGB565 frame buffer, shown in an SDL window.
class Arduino_AXS15231B : public Arduino_G
{
public:
    Arduino_AXS15231B(Arduino_ESP32QSPI *, int8_t, uint8_t, bool, int16_t w, int16_t h) : Arduino_G(w, h) {}
    bool begin(int32_t speed = GFX_NOT_DEFINED) override;
    void drawBitmap(int16_t, int16_t, uint8_t *, int16_t, int16_t, uint16_t, uint16_t) override {}
    void drawIndexedBitmap(int16_t, int16_t, uint8_t *, uint16_t *, int16_t, int16_t, int16_t = 0) override {}
    void draw3bitRGBBitmap(int16_t, int16_t, uint8_t *, int16_t, int16_t) override {}
    void draw16bitRGBBitmap(int16_t x, int16_t y, uint16_t *bitmap, int16_t w, int16_t h) override;
    void draw24bitRGBBitmap(int16_t, int16_t, uint8_t *, int16_t, int16_t) override {}
};
