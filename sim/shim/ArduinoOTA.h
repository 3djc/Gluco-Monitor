#pragma once
#include <Arduino.h>
class ArduinoOTAClass
{
public:
    void setHostname(const char *) {}
    void begin() {}
    void handle() {}
};
extern ArduinoOTAClass ArduinoOTA;
