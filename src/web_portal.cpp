/**
 * web_portal.cpp — Implementation of Web-Based SoftAP Captive Setup Portal
 */

#include "web_portal.h"
#include "crypto_wallet.h"
#include "wifi_manager.h"

void WebPortal::begin(CryptoWallet* wallet, WifiManager* wifi) {
    _wallet = wallet;
    _wifi = wifi;
    _setupDone = false;

    WiFi.mode(WIFI_AP);
    WiFi.setTxPower(WIFI_POWER_8_5dBm); // Low TX power to prevent power supply dips
    WiFi.softAP("T-Key-Setup");
    delay(100);

    IPAddress apIP(192, 168, 4, 1);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));

    _dns = new DNSServer();
    _dns->start(53, "*", apIP); // Captive portal redirect

    _server = new WebServer(80);
    _server->on("/", HTTP_GET, [this]() { handleRoot(); });
    _server->on("/save", HTTP_POST, [this]() { handleSave(); });
    _server->onNotFound([this]() { handleNotFound(); });
    _server->begin();

    _isRunning = true;
    Serial.println("[PORTAL] Captive Setup Portal running at http://192.168.4.1 (SSID: 'T-Key-Setup')");
}

void WebPortal::update() {
    if (!_isRunning) return;
    if (_dns) _dns->processNextRequest();
    if (_server) _server->handleClient();
}

void WebPortal::stop() {
    if (_server) {
        _server->stop();
        delete _server;
        _server = nullptr;
    }
    if (_dns) {
        _dns->stop();
        delete _dns;
        _dns = nullptr;
    }
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    _isRunning = false;
    Serial.println("[PORTAL] Web Setup Portal stopped.");
}

void WebPortal::handleNotFound() {
    // Redirect any captive portal query to root
    _server->sendHeader("Location", "http://192.168.4.1/", true);
    _server->send(302, "text/plain", "");
}

void WebPortal::handleRoot() {
    String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<title>T-Key S3 Vault Setup</title>";
    html += "<style>";
    html += "body{background:#0b0c10;color:#c5c6c7;font-family:system-ui,-apple-system,sans-serif;margin:0;padding:20px;}";
    html += ".card{background:#1f2833;border:1px solid #45a29e;border-radius:12px;padding:20px;max-width:480px;margin:0 auto 20px auto;box-shadow:0 8px 24px rgba(0,0,0,0.5);}";
    html += "h1{color:#66fcf1;font-size:20px;margin-top:0;display:flex;align-items:center;gap:8px;}";
    html += "h2{color:#45a29e;font-size:16px;border-bottom:1px solid #333;padding-bottom:6px;margin-top:16px;}";
    html += "label{display:block;font-size:13px;color:#8f9ba8;margin:10px 0 4px 0;}";
    html += "input[type='text'],input[type='password']{width:100%;box-sizing:border-box;padding:10px;background:#0b0c10;border:1px solid #333;border-radius:6px;color:#fff;font-size:15px;}";
    html += "input[type='submit'],button{background:#45a29e;color:#0b0c10;border:none;border-radius:6px;padding:12px 20px;font-size:15px;font-weight:bold;cursor:pointer;width:100%;margin-top:16px;}";
    html += "input[type='submit']:hover{background:#66fcf1;}";
    html += ".badge{background:#142c33;color:#66fcf1;padding:6px 10px;border-radius:6px;font-family:monospace;font-size:12px;word-break:break-all;margin-bottom:8px;}";
    html += ".seed{background:#101318;border:1px dashed #45a29e;padding:12px;border-radius:6px;font-family:monospace;color:#f0a500;font-size:13px;line-height:1.6;}";
    html += "</style></head><body><div class='card'>";
    html += "<h1>⚡ T-Key S3 Vault Setup</h1>";
    html += "<p style='font-size:14px;'>Hardware Security Key & Multi-Currency Crypto Signer Configuration</p>";

    html += "<form action='/save' method='POST'>";

    html += "<h2>1. Security & Master PIN</h2>";
    html += "<label>Set 4-Digit Master PIN:</label>";
    html += "<input type='password' name='pin' maxlength='4' value='1234' required pattern='[0-9]{4}'>";

    html += "<h2>2. Wi-Fi Network Credentials</h2>";
    html += "<label>Network SSID (2.4GHz):</label>";
    html += "<input type='text' name='ssid' placeholder='Your Home/Office Wi-Fi'>";
    html += "<label>Wi-Fi Password:</label>";
    html += "<input type='password' name='wifipass' placeholder='Wi-Fi Password'>";

    html += "<h2>3. Multi-Currency Accounts</h2>";
    if (_wallet) {
        html += "<label>Bitcoin (SegWit Bech32):</label>";
        html += "<div class='badge'>" + String(_wallet->getAddress(COIN_BTC)) + "</div>";
        html += "<label>Ethereum (EVM 0x):</label>";
        html += "<div class='badge'>" + String(_wallet->getAddress(COIN_ETH)) + "</div>";
        html += "<label>Solana (Ed25519 Base58):</label>";
        html += "<div class='badge'>" + String(_wallet->getAddress(COIN_SOL)) + "</div>";
        html += "<label>BIP-39 Recovery Mnemonic Seed:</label>";
        html += "<div class='seed'>" + String(_wallet->getMnemonicPhrase()) + "</div>";
    }

    html += "<input type='submit' value='Save Settings & Launch Vault'>";
    html += "</form></div></body></html>";

    _server->send(200, "text/html", html);
}

void WebPortal::handleSave() {
    if (_server->hasArg("pin")) {
        String p = _server->arg("pin");
        if (p.length() == 4) {
            strncpy(_newPin, p.c_str(), sizeof(_newPin) - 1);
        }
    }

    if (_server->hasArg("ssid") && _server->hasArg("wifipass")) {
        String s = _server->arg("ssid");
        String pass = _server->arg("wifipass");
        if (s.length() > 0 && _wifi) {
            _wifi->addNetwork(s.c_str(), pass.c_str());
        }
    }

    _setupDone = true;

    String successHtml = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>";
    successHtml += "<style>body{background:#0b0c10;color:#66fcf1;font-family:sans-serif;text-align:center;padding:50px;}</style></head>";
    successHtml += "<body><h2>✅ Configuration Saved!</h2><p>T-Key S3 has been provisioned. The Access Point is shutting down and launching your secure vault.</p></body></html>";

    _server->send(200, "text/html", successHtml);
}
