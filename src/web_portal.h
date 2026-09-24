/**
 * web_portal.h — Setup portal for Crypto T-Key S3
 * ==============================================================================
 * WPA2 SoftAP "T-Key-XXXX" whose random per-session password is shown only on the
 * device screen (with a Wi-Fi join QR), so reaching the page proves you can see the
 * device. The page (src/portal_page.h) talks to a small JSON API:
 *
 *   GET  /api/state   login state; full settings once authenticated
 *   POST /api/login   setup password → session cookie (5 failures → lockout)
 *   POST /api/save    PINs, setup password, coin selection, home theme, Wi-Fi
 *   GET  /api/scan    nearby networks
 *   POST /api/exit    leave without saving (already provisioned devices)
 *
 * Sessions: one random HttpOnly SameSite=Strict cookie, 10-minute idle expiry; every
 * POST also needs the per-session CSRF token in the X-TKey-CSRF header. The setup
 * password is stored as salted PBKDF2-HMAC-SHA256 (legacy SHA-256 hashes are upgraded
 * on the next successful login). No PIN or password is ever sent back to the browser,
 * and the recovery seed never touches the portal.
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
    void begin(CryptoWallet* wallet, WifiManager* wifi, bool isProvisioned);
    void update();
    void stop();
    bool isRunning() const { return _isRunning; }
    bool isSetupDone() const { return _setupDone; }     // settings saved and applied
    bool isExitRequested() const { return _exitRequested; }

    const char* apSsid() const { return _apSsid; }
    const char* apPass() const { return _apPass; }
    const char* wifiQr() const { return _wifiQr; }

    static bool hasSetupPassword();

private:
    void handlePage();
    void handleState();
    void handleLogin();
    void handleSave();
    void handleScan();
    void handleExit();
    void handleNotFound();

    bool isAuthenticated();
    bool checkCsrf();
    void startSession();
    void sendJson(int code, const String& body);
    void sendError(int code, const char* msg);

    WebServer*    _server = nullptr;
    DNSServer*    _dns = nullptr;
    CryptoWallet* _wallet = nullptr;
    WifiManager*  _wifi = nullptr;

    bool _isRunning = false;
    bool _setupDone = false;
    bool _exitRequested = false;
    bool _isProvisioned = false;

    char _apSsid[16] = "";
    char _apPass[12] = "";
    char _wifiQr[64] = "";

    char     _session[33] = "";
    char     _csrf[33] = "";
    uint32_t _sessionSeenMs = 0;

    uint8_t  _loginFails = 0;
    uint32_t _lockUntilMs = 0;
    uint32_t _lockMinutes = 5;
};
