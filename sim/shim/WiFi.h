// Minimal WiFi stand-in: always "connected", with a canned list of networks.
#pragma once
#include <Arduino.h>

#define WL_CONNECTED 3
#define WL_DISCONNECTED 6
enum wifi_mode_t { WIFI_OFF, WIFI_STA, WIFI_AP, WIFI_AP_STA };
enum wifi_sort_method_t { WIFI_CONNECT_AP_BY_SIGNAL, WIFI_CONNECT_AP_BY_SECURITY };
enum wifi_scan_method_t { WIFI_FAST_SCAN, WIFI_ALL_CHANNEL_SCAN };

struct SimIP
{
    String toString() const { return String("192.168.1.42"); }
};

class WiFiClass
{
public:
    int status();
    bool hostname(const String &) { return true; }
    wifi_mode_t getMode() { return WIFI_STA; }
    String BSSIDstr(uint8_t i);
    uint8_t *BSSID(uint8_t i);
    int32_t channel(uint8_t i) { return 1 + (i * 5) % 11; }
    void begin(const char *, const char *, int32_t, const uint8_t *);
    SimIP localIP() { return SimIP(); }
    bool mode(wifi_mode_t) { return true; }
    bool disconnect(bool = false) { return true; }
    void setSortMethod(wifi_sort_method_t) {}
    void setScanMethod(wifi_scan_method_t) {}
    int16_t scanNetworks();
    String SSID(uint8_t i);
    int32_t RSSI(uint8_t i);
    void scanDelete() {}
    void begin(const char *, const char * = nullptr);
    String macAddress() { return String("AA:BB:CC:DD:EE:FF"); }
};
extern WiFiClass WiFi;
