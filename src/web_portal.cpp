/**
 * web_portal.cpp — Implementation of SoftAP Captive Setup Portal
 */

#include "web_portal.h"
#include "crypto_wallet.h"
#include "wifi_manager.h"
#include "crypto_coins.h"
#include "seed_gen.h"

void WebPortal::begin(CryptoWallet* wallet, WifiManager* wifi) {
    _wallet = wallet;
    _wifi = wifi;
    _setupDone = false;

    WiFi.mode(WIFI_AP);
    WiFi.setTxPower(WIFI_POWER_8_5dBm); // Low TX power to prevent heat buildup
    WiFi.softAP("T-Key-Setup");
    delay(100);

    IPAddress apIP(192, 168, 4, 1);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));

    _dns = new DNSServer();
    _dns->start(53, "*", apIP); // Captive portal DNS redirect

    _server = new WebServer(80);
    _server->on("/", HTTP_GET, [this]() { handleRoot(); });
    _server->on("/save", HTTP_POST, [this]() { handleSave(); });
    _server->onNotFound([this]() { handleNotFound(); });
    _server->begin();

    _isRunning = true;
    Serial.println("[PORTAL] SoftAP Setup Portal active at http://192.168.4.1 (SSID: 'T-Key-Setup')");
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
    _isRunning = false;
    Serial.println("[PORTAL] SoftAP Setup Portal stopped.");
}

void WebPortal::handleNotFound() {
    _server->sendHeader("Location", "http://192.168.4.1/", true);
    _server->send(302, "text/plain", "");
}

void WebPortal::handleRoot() {
    String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<title>Crypto TKey S3 Setup Portal</title>";
    html += "<style>";
    html += ":root{--bg:#0a0a0c;--card:#121318;--border:#1f2330;--cyan:#00e5ff;--green:#00ff66;--gold:#ffb300;--red:#ff1744;}";
    html += "body{background:var(--bg);color:#e0e6ed;font-family:system-ui,-apple-system,sans-serif;margin:0;padding:16px;}";
    html += ".card{background:var(--card);border:1px solid var(--border);border-radius:10px;padding:20px;max-width:540px;margin:0 auto 20px auto;}";
    html += "h1{color:var(--cyan);font-size:20px;margin-top:0;display:flex;align-items:center;gap:8px;}";
    html += "h2{color:var(--cyan);font-size:15px;border-bottom:1px solid var(--border);padding-bottom:6px;margin:20px 0 10px 0;letter-spacing:1px;}";
    html += "label{display:block;font-size:12px;color:#798696;margin:8px 0 4px 0;font-weight:600;}";
    html += "input[type='text'],input[type='password'],input[type='number']{width:100%;box-sizing:border-box;padding:9px;background:#000;border:1px solid var(--border);border-radius:5px;color:#fff;font-size:14px;font-family:monospace;}";
    html += "input:focus{border-color:var(--cyan);outline:none;}";
    html += ".grid{display:grid;grid-template-columns:1fr 1fr;gap:10px;}";
    html += ".coin-item{background:#0b0c10;border:1px solid var(--border);border-radius:6px;padding:8px;display:flex;align-items:center;justify-content:space-between;}";
    html += ".coin-label{display:flex;align-items:center;gap:6px;font-weight:bold;font-size:13px;color:#fff;}";
    html += ".coin-bal{width:80px!important;padding:4px 6px!important;font-size:12px!important;}";
    html += ".btn{background:var(--cyan);color:#000;border:none;border-radius:6px;padding:12px;font-size:14px;font-weight:bold;cursor:pointer;width:100%;margin-top:20px;}";
    html += ".seed-box{background:#000;border:1px dashed var(--gold);padding:12px;border-radius:6px;font-family:monospace;color:var(--gold);font-size:12px;line-height:1.6;margin-top:6px;}";
    html += ".explainer{background:#0d141f;border-left:3px solid var(--cyan);padding:10px 14px;border-radius:4px;font-size:12px;line-height:1.5;color:#9cb3cc;margin-top:10px;}";
    html += "</style></head><body><div class='card'>";

    html += "<h1>⚡ CRYPTO TKEY S3 SETUP PORTAL</h1>";
    html += "<p style='font-size:13px; color:#798696;'>Universal Hardware Security Key & 24-Currency Vault Onboarding</p>";

    html += "<form action='/save' method='POST'>";

    // 1. PINs
    html += "<h2>1. SECURITY & MASTER PIN CODES</h2>";
    html += "<div class='grid'>";
    html += "<div><label>Master PIN (4 Digits):</label><input type='password' name='pin' maxlength='4' value='1234' required pattern='[0-9]{4}'></div>";
    html += "<div><label>Duress Wipe PIN (4 Digits):</label><input type='password' name='duress_pin' maxlength='4' value='9999' required pattern='[0-9]{4}'></div>";
    html += "</div>";
    html += "<div style='font-size:11px;color:#798696;margin-top:4px;'>* Entering Duress PIN permanently nukes all flash memory and shows a decoy crash screen.</div>";

    // 2. Wi-Fi
    html += "<h2>2. MULTI-NETWORK WI-FI REGISTRATION</h2>";
    html += "<label>Add New Network SSID (2.4GHz):</label>";
    html += "<input type='text' name='ssid' placeholder='Home / Office / Hotspot SSID'>";
    html += "<label>Network Wi-Fi Password:</label>";
    html += "<input type='password' name='wifipass' placeholder='Wi-Fi Password'>";
    if (_wifi && _wifi->getSavedCount() > 0) {
        html += "<div style='font-size:11px; color:var(--green); margin-top:8px;'>Saved Networks in Memory: ";
        for (int i = 0; i < _wifi->getSavedCount(); i++) {
            const WifiCreds* cred = _wifi->getNetwork(i);
            if (cred) html += String(cred->ssid) + (i < _wifi->getSavedCount() - 1 ? ", " : "");
        }
        html += "</div>";
    }

    // 3. Supported Coins
    html += "<h2>3. PICK CRYPTOCURRENCIES & BALANCES</h2>";
    html += "<div class='grid'>";
    for (int i = 0; i < CryptoCoinRegistry::getCoinCount(); i++) {
        CoinAsset* coin = CryptoCoinRegistry::getCoinByIndex(i);
        html += "<div class='coin-item'>";
        html += "<label class='coin-label'>";
        html += "<input type='checkbox' name='c_" + String(i) + "' value='1' " + (coin->enabled ? "checked" : "") + ">";
        html += "<span>" + String(coin->symbol) + "</span>";
        html += "</label>";
        html += "<input type='number' step='any' class='coin-bal' name='b_" + String(i) + "' value='" + String(coin->balance, 4) + "' placeholder='0.00'>";
        html += "</div>";
    }
    html += "</div>";

    // 4. Seed Backup
    html += "<h2>4. BIP-39 MASTER RECOVERY MNEMONIC</h2>";
    if (_wallet) {
        html += "<div class='seed-box'>" + String(_wallet->getMnemonicPhrase()) + "</div>";
        html += "<div style='font-size:11px;color:#798696;margin-top:4px;'>* Write these words down on paper. They never leave the physical device.</div>";
    }

    // 5. Security Key Explainer
    html += "<h2>5. HOW THE HARDWARE SECURITY KEY WORKS</h2>";
    html += "<div class='explainer'>";
    html += "<b>🛡️ Zero Setup Needed for WebAuthn / Passkeys:</b><br>";
    html += "When plugged into USB, your T-Dongle S3 functions as a native FIDO2/WebAuthn authenticator (like a YubiKey). When logging into Google, GitHub, Apple, or Binance, the site requests authentication. The dongle's screen lights up displaying the domain, pulses green, and you tap the button to sign in with un-phishable NIST P-256 ECC cryptography.";
    html += "</div>";

    html += "<input type='submit' class='btn' value='SAVE SETTINGS & LAUNCH SECURE KEY'>";
    html += "</form></div></body></html>";

    _server->send(200, "text/html", html);
}

void WebPortal::handleSave() {
    if (_server->hasArg("pin")) {
        String p = _server->arg("pin");
        if (p.length() == 4) strncpy(_newPin, p.c_str(), sizeof(_newPin) - 1);
    }
    if (_server->hasArg("duress_pin")) {
        String dp = _server->arg("duress_pin");
        if (dp.length() == 4) strncpy(_newDuressPin, dp.c_str(), sizeof(_newDuressPin) - 1);
    }

    // Add Wi-Fi if provided
    if (_server->hasArg("ssid") && _server->hasArg("wifipass")) {
        String s = _server->arg("ssid");
        String pass = _server->arg("wifipass");
        if (s.length() > 0 && _wifi) {
            _wifi->addNetwork(s.c_str(), pass.c_str());
        }
    }

    // Update 24 Coins & Balances
    for (int i = 0; i < CryptoCoinRegistry::getCoinCount(); i++) {
        bool en = _server->hasArg("c_" + String(i));
        CryptoCoinRegistry::setCoinEnabled((SupportedCoinId)i, en);

        if (_server->hasArg("b_" + String(i))) {
            float bal = _server->arg("b_" + String(i)).toFloat();
            CryptoCoinRegistry::updateBalance((SupportedCoinId)i, bal);
        }
    }

    _setupDone = true;

    String successHtml = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>";
    successHtml += "<style>body{background:#0a0a0c;color:#00ff66;font-family:sans-serif;text-align:center;padding:40px;}</style></head>";
    successHtml += "<body><h2>✅ CONFIGURATION SAVED!</h2><p style='color:#e0e6ed;'>Your Crypto TKey S3 is provisioned. The Access Point is turning off to save power and keep the dongle cool. Your key is now ready!</p></body></html>";

    _server->send(200, "text/html", successHtml);
}
