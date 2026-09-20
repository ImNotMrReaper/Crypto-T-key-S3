/**
 * wifi_manager.h — Persistent Multi-Network Wi-Fi Engine for Crypto TKey S3
 * ==============================================================================
 * Auto-stores up to 10 Wi-Fi networks in flash NVS (wifi_cfg), auto-scans and
 * connects to best known signal, and enforces modem power-saving to stay cool.
 */

#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <esp_wifi.h>

#define MAX_WIFI_NETWORKS 10

struct WifiCreds {
    char ssid[33];
    char pass[65];
};

class WifiManager {
public:
    void begin();
    void update();
    bool connectBest();
    void disconnect();
    bool isConnected();
    String getIp();
    String getSsid();
    int8_t getRssi();

    // Multi-Network Management
    bool addNetwork(const char* ssid, const char* pass);
    bool removeNetwork(const char* ssid);
    int  getSavedCount() const { return _savedCount; }
    const WifiCreds* getNetwork(int index) const;

    void saveToNvs();
    void loadFromNvs();

private:
    Preferences _prefs;
    WifiCreds   _networks[MAX_WIFI_NETWORKS];
    int         _savedCount = 0;
    uint32_t    _lastCheck = 0;
    bool        _connecting = false;
};
