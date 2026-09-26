#pragma once
#include <stdint.h>
#include <string>
#include "Arduino.h"

enum wl_status_t {
    WL_NO_SHIELD = 255,
    WL_IDLE_STATUS = 0,
    WL_NO_SSID_AVAIL = 1,
    WL_SCAN_COMPLETED = 2,
    WL_CONNECTED = 3,
    WL_CONNECT_FAILED = 4,
    WL_CONNECTION_LOST = 5,
    WL_DISCONNECTED = 6
};

#define WIFI_OFF 0
#define WIFI_STA 1
#define WIFI_AP  2
#define WIFI_SCAN_RUNNING -1

typedef enum {
    WIFI_POWER_19_5dBm = 78,
    WIFI_POWER_8_5dBm = 34
} wifi_power_t;

class WiFiSTAClass {
public:
    bool started() { return _started; }
    bool begin(bool = false) { _started = true; return true; }
    bool _started = false;
};

class WiFiClass {
public:
    WiFiSTAClass STA;
    void persistent(bool) {}
    void disconnect(bool = false) { _mode = WIFI_OFF; }
    void mode(int m) { _mode = m; if (m == WIFI_STA) STA._started = true; }
    int getMode() { return _mode; }
    int status() { return WL_DISCONNECTED; }
    void scanDelete() {}
    int scanComplete() { return 0; }
    int scanNetworks(bool = false, bool = false, bool = false, uint32_t = 300) { return 0; }
    String SSID(int = 0) { return ""; }
    int32_t RSSI(int = 0) { return -70; }
    bool begin(const char* = nullptr, const char* = nullptr) { STA._started = true; return true; }
    bool setTxPower(wifi_power_t) { return true; }
private:
    int _mode = WIFI_OFF;
};

extern WiFiClass WiFi;
