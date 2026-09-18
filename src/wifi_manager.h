/**
 * wifi_manager.h — Persistent Multi-Network Wi-Fi Engine for LilyGo T-Dongle S3
 * ==============================================================================
 * Automatically stores, scans, and connects to known Wi-Fi networks in range.
 */

#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>

#define MAX_WIFI_NETWORKS 10

struct StoredNetwork {
    char ssid[33];
    char pass[65];
};

class WifiManager {
public:
    void begin();
    void update(); // Non-blocking periodic check and auto-reconnect

    bool addNetwork(const char* ssid, const char* pass);
    bool removeNetwork(const char* ssid);
    void clearAll();
    
    int  scanNetworks();
    bool connectBest();
    bool connectTo(const char* ssid, const char* pass);
    void disconnect();

    bool isConnected() const { return WiFi.status() == WL_CONNECTED; }
    String getIp() const { return isConnected() ? WiFi.localIP().toString() : "0.0.0.0"; }
    String getSsid() const { return isConnected() ? WiFi.SSID() : "Disconnected"; }
    int8_t getRssi() const { return isConnected() ? WiFi.RSSI() : -100; }
    
    int getSavedCount() const { return _savedCount; }
    const char* getSavedSsid(int index) const;

private:
    void loadFromNvs();
    void saveToNvs();

    Preferences   _prefs;
    StoredNetwork _networks[MAX_WIFI_NETWORKS];
    int           _savedCount = 0;
    uint32_t      _lastScanAttempt = 0;
    uint32_t      _scanInterval = 25000; // Auto-scan every 25 seconds if disconnected
    bool          _isConnecting = false;
    uint32_t      _connectStartTime = 0;
};
