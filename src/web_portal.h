/**
 * web_portal.h — SoftAP Captive Setup Portal for Crypto TKey S3
 * ==============================================================================
 * Broadcasts "T-Key-Setup" at 192.168.4.1 for initial onboarding, custom PIN,
 * duress PIN, multi-network Wi-Fi setup, 24-coin selection, and seed generation.
 * Follows embedded-setup-portal-dev skill standards:
 *  - Zero mnemonic seed transmission over HTTP (Air-gapped on-screen only)
 *  - Asynchronous Wi-Fi network scanner (/scan)
 *  - Captive OS probe redirection (iOS hotspot-detect, Android generate_204)
 *  - Full thermal radio shutdown upon saving
 */

#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include "crypto_coins.h"

class CryptoWallet;
class WifiManager;

class WebPortal {
public:
    void begin(CryptoWallet* wallet, WifiManager* wifi);
    void update();
    void stop();
    bool isRunning() const { return _isRunning; }
    bool isSetupDone() const { return _setupDone; }

    const char* getNewPin() const { return _newPin; }
    const char* getNewDuressPin() const { return _newDuressPin; }

private:
    void handleRoot();
    void handleScan();
    void handleSave();
    void handleNotFound();

    WebServer*   _server = nullptr;
    DNSServer*   _dns = nullptr;
    CryptoWallet* _wallet = nullptr;
    WifiManager*  _wifi = nullptr;

    bool _isRunning = false;
    bool _setupDone = false;
    char _newPin[16] = "1234";
    char _newDuressPin[16] = "8888";
};
