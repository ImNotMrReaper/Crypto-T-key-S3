/**
 * web_portal.h — SoftAP Captive Setup Portal for Crypto TKey S3
 * ==============================================================================
 * Broadcasts "T-Key-Setup" at 192.168.4.1 for initial onboarding, custom PIN,
 * duress PIN, multi-network Wi-Fi setup, 24-coin selection, and seed generation.
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
    void handleSave();
    void handleNotFound();

    WebServer*   _server = nullptr;
    DNSServer*   _dns = nullptr;
    CryptoWallet* _wallet = nullptr;
    WifiManager*  _wifi = nullptr;

    bool _isRunning = false;
    bool _setupDone = false;
    char _newPin[8] = "1234";
    char _newDuressPin[8] = "9999";
};
