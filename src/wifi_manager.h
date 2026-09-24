/**
 * wifi_manager.h — Persistent Multi-Network Wi-Fi Engine for Crypto TKey S3
 * ==============================================================================
 * Stores up to 10 Wi-Fi networks in flash NVS (wifi_cfg). Two operating modes:
 *
 *  - USB host attached (plugged into a computer): radio OFF. Prices and balances
 *    arrive over USB CDC from the host companion daemon (zero RF, zero RF heat).
 *  - Wall power (no USB host): duty-cycled burst every WIFI_BURST_PERIOD_MS —
 *    async scan, join the strongest saved network, fetch prices (and balances every
 *    WIFI_BALANCE_PERIOD_MS) over verified HTTPS in a background task, radio OFF.
 *
 * Nothing here blocks the main loop (the old synchronous scan froze the UI and
 * FIDO for ~3 s at boot and every 30 s whenever no saved network was in range).
 */

#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <esp_wifi.h>

#define MAX_WIFI_NETWORKS      10
#define WIFI_BURST_PERIOD_MS   60000UL    // price refresh cadence on wall power
#define WIFI_BALANCE_PERIOD_MS 600000UL   // on-chain balance refresh cadence
#define WIFI_HOST_GRACE_MS     6000UL     // wait this long after boot for USB enumeration

struct WifiCreds {
    char ssid[33];
    char pass[65];
};

class WifiManager {
public:
    void begin();
    void update();                     // non-blocking state machine; call every loop
    void setPortalActive(bool active); // the setup portal owns the radio while active
    void forceWallMode(bool force) { _forceWall = force; }  // test builds: burst while on USB

    bool isConnected();
    bool isWallMode() const { return _wallMode; }
    const char* getConnectedSsid();
    int8_t getRssi();

    // Multi-Network Management (saving never triggers a blocking connect)
    bool addNetwork(const char* ssid, const char* pass);
    bool removeNetwork(const char* ssid);
    int  getSavedCount() const { return _savedCount; }
    const WifiCreds* getNetwork(int index) const;
    void saveToNvs();
    void loadFromNvs();

private:
    enum Phase { PHASE_OFF, PHASE_SCANNING, PHASE_CONNECTING, PHASE_FETCHING };
    void radioOff();
    void startScan();
    void connectNextCandidate();

    Preferences _prefs;
    WifiCreds   _networks[MAX_WIFI_NETWORKS];
    int         _savedCount = 0;

    Phase    _phase = PHASE_OFF;
    bool     _wallMode = false;
    bool     _forceWall = false;
    bool     _portalActive = false;
    uint32_t _bootMs = 0;
    uint32_t _phaseStart = 0;
    uint32_t _lastBurst = 0;
    uint32_t _lastBalance = 0;
    bool     _balanceDue = true;
    int      _candidates[MAX_WIFI_NETWORKS];
    int      _candCount = 0;
    int      _candIdx = 0;
};
