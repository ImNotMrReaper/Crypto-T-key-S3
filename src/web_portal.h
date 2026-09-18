/**
 * web_portal.h — Web-Based SoftAP Captive Setup Portal for LilyGo T-Dongle S3
 * ===========================================================================
 * Broadcasts "T-Key-Setup" Wi-Fi Access Point with responsive dark-mode web UI
 * allowing full customization of PIN, Wi-Fi networks, and crypto addresses.
 */

#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>

class CryptoWallet;
class WifiManager;

class WebPortal {
public:
    void begin(CryptoWallet* wallet, WifiManager* wifi);
    void update();
    void stop();

    bool isRunning() const { return _isRunning; }
    bool isSetupComplete() const { return _setupDone; }
    const char* getCustomPin() const { return _newPin; }

private:
    void handleRoot();
    void handleSave();
    void handleNotFound();

    WebServer* _server = nullptr;
    DNSServer* _dns = nullptr;
    CryptoWallet* _wallet = nullptr;
    WifiManager*  _wifi = nullptr;

    bool _isRunning = false;
    bool _setupDone = false;
    char _newPin[16] = "1234";
};
