/**
 * web_portal.cpp — SoftAP Captive Setup Portal Implementation
 * Standard: Follows embedded-setup-portal-dev skill.
 * Zero mnemonic leakage, asynchronous Wi-Fi scanning, captive OS redirection,
 * and complete thermal radio shutdown.
 */

#include "web_portal.h"
#include "crypto_wallet.h"
#include "wifi_manager.h"
#include "crypto_coins.h"
#include "seed_gen.h"
#include <mbedtls/sha256.h>

static void computeSha256Hex(const char* input, char* outHex64) {
    uint8_t digest[32];
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0); // 0 for SHA-256
    mbedtls_sha256_update(&ctx, (const uint8_t*)input, strlen(input));
    mbedtls_sha256_finish(&ctx, digest);
    mbedtls_sha256_free(&ctx);

    for (int i = 0; i < 32; i++) {
        sprintf(outHex64 + (i * 2), "%02x", digest[i]);
    }
    outHex64[64] = '\0';
}

void WebPortal::begin(CryptoWallet* wallet, WifiManager* wifi, bool isProvisioned, const char* setupPwdHash) {
    _wallet = wallet;
    _wifi = wifi;
    _isProvisioned = isProvisioned;
    _setupDone = false;

    if (setupPwdHash && strlen(setupPwdHash) == 64) {
        strncpy(_storedPasswordHash, setupPwdHash, sizeof(_storedPasswordHash) - 1);
        strncpy(_setupPasswordHash, setupPwdHash, sizeof(_setupPasswordHash) - 1);
        _authenticated = false;
    } else {
        _storedPasswordHash[0] = '\0';
        _setupPasswordHash[0] = '\0';
        _authenticated = true; // fresh unprovisioned setup
    }

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
    _server->on("/login", HTTP_POST, [this]() { handleLogin(); });
    _server->on("/generate_204", HTTP_GET, [this]() { handleRoot(); });
    _server->on("/gen_204", HTTP_GET, [this]() { handleRoot(); });
    _server->on("/hotspot-detect.html", HTTP_GET, [this]() { handleRoot(); });
    _server->on("/ncsi.txt", HTTP_GET, [this]() { handleRoot(); });
    _server->on("/connecttest.txt", HTTP_GET, [this]() { handleRoot(); });
    _server->on("/scan", HTTP_GET, [this]() { handleScan(); });
    _server->on("/save", HTTP_POST, [this]() { handleSave(); });
    _server->onNotFound([this]() { handleNotFound(); });
    _server->begin();

    _isRunning = true;
    Serial.println("[PORTAL] 🌐 SoftAP Captive Setup Portal active at http://192.168.4.1 (SSID: 'T-Key-Setup')");
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
    Serial.println("[PORTAL] ❄️ SoftAP stopped & radio powered down (Thermal Protection Active).");
}

void WebPortal::handleNotFound() {
    _server->sendHeader("Location", "http://192.168.4.1/", true);
    _server->send(302, "text/plain", "");
}

bool WebPortal::isAuthenticated() {
    return _authenticated || !_isProvisioned || strlen(_storedPasswordHash) == 0;
}

void WebPortal::handleLogin() {
    if (!_server->hasArg("setup_pass")) {
        handleRoot();
        return;
    }

    String pass = _server->arg("setup_pass");
    char enteredHash[65];
    computeSha256Hex(pass.c_str(), enteredHash);

    if (strcmp(enteredHash, _storedPasswordHash) == 0) {
        _authenticated = true;
        Serial.println("[PORTAL] 🔓 Setup Portal Unlocked via Setup Password");
        _server->sendHeader("Location", "/", true);
        _server->send(302, "text/plain", "");
    } else {
        Serial.println("[PORTAL] ⛔ Setup Portal: Invalid Password Attempt");
        String errHtml = "<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'>";
        errHtml += "<meta name='viewport' content='width=device-width,initial-scale=1.0'>";
        errHtml += "<title>Access Denied</title><style>";
        errHtml += "body{background:#0A0D14;color:#FF1744;font-family:system-ui,-apple-system,sans-serif;text-align:center;padding:40px 16px;}";
        errHtml += ".card{background:#121622;border:1px solid #FF1744;border-radius:12px;padding:24px;max-width:400px;margin:0 auto;}";
        errHtml += "h2{color:#FF1744;margin-bottom:12px;font-size:1.1rem;}";
        errHtml += "p{color:#F0F4FC;font-size:0.85rem;line-height:1.5;margin-bottom:16px;}";
        errHtml += ".btn{background:#00E5FF;color:#000;border:none;border-radius:8px;padding:10px 20px;font-weight:700;text-decoration:none;font-size:0.9rem;}";
        errHtml += "</style></head><body><div class='card'>";
        errHtml += "<h2>⛔ ACCESS DENIED</h2>";
        errHtml += "<p>The Setup Password you entered is incorrect. Device settings remain securely locked.</p>";
        errHtml += "<a href='/' class='btn'>TRY AGAIN</a>";
        errHtml += "</div></body></html>";
        _server->send(401, "text/html", errHtml);
    }
}

void WebPortal::handleScan() {
    int n = WiFi.scanNetworks(false, false, false, 300);
    String json = "[";
    for (int i = 0; i < n; ++i) {
        if (i > 0) json += ",";
        json += "{\"ssid\":\"" + WiFi.SSID(i) + "\",";
        json += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
        json += "\"secure\":" + String(WiFi.encryptionType(i) != WIFI_AUTH_OPEN ? "true" : "false") + "}";
    }
    json += "]";
    WiFi.scanDelete();
    _server->send(200, "application/json", json);
}

void WebPortal::handleRoot() {
    if (!isAuthenticated()) {
        String loginHtml;
        loginHtml.reserve(3000);
        loginHtml = "<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'>";
        loginHtml += "<meta name='viewport' content='width=device-width,initial-scale=1.0'>";
        loginHtml += "<title>Crypto TKey S3 - Setup Login</title><style>";
        loginHtml += ":root{--bg:#0A0D14;--surface:#121622;--border:#1E2538;--cyan:#00E5FF;--green:#00FF66;--red:#FF1744;--text:#F0F4FC;--muted:#7E8B9F;}";
        loginHtml += "*{box-sizing:border-box;margin:0;padding:0;font-family:system-ui,-apple-system,sans-serif;}";
        loginHtml += "body{background:var(--bg);color:var(--text);padding:24px 16px;display:flex;justify-content:center;align-items:center;min-height:90vh;}";
        loginHtml += ".container{width:100%;max-width:420px;}";
        loginHtml += ".header{text-align:center;margin-bottom:24px;}";
        loginHtml += ".header h1{font-size:1.4rem;color:var(--cyan);letter-spacing:1px;margin-bottom:6px;}";
        loginHtml += ".header p{font-size:0.82rem;color:var(--muted);}";
        loginHtml += ".card{background:var(--surface);border:1px solid var(--border);border-radius:12px;padding:22px;box-shadow:0 8px 24px rgba(0,0,0,0.5);}";
        loginHtml += ".lock-icon{font-size:2.5rem;text-align:center;margin-bottom:12px;}";
        loginHtml += ".card-title{font-size:1.05rem;font-weight:700;color:var(--text);text-align:center;margin-bottom:8px;}";
        loginHtml += ".card-desc{font-size:0.8rem;color:var(--muted);text-align:center;line-height:1.4;margin-bottom:20px;}";
        loginHtml += "input[type='password']{width:100%;background:#07090F;border:1px solid var(--border);border-radius:8px;padding:12px 14px;color:var(--text);font-size:1rem;outline:none;margin-bottom:16px;}";
        loginHtml += "input[type='password']:focus{border-color:var(--cyan);}";
        loginHtml += ".btn{width:100%;background:var(--cyan);color:#000;border:none;border-radius:8px;padding:14px;font-weight:700;font-size:0.95rem;cursor:pointer;letter-spacing:1px;}";
        loginHtml += ".shield-note{background:rgba(0,229,255,0.05);border-left:3px solid var(--cyan);padding:10px 12px;border-radius:4px;font-size:0.75rem;color:#8EC5FC;margin-top:16px;line-height:1.4;}";
        loginHtml += "</style></head><body><div class='container'>";
        loginHtml += "<div class='header'><h1>⚡ CRYPTO TKEY S3</h1><p>Hardware Security Key & Crypto Vault</p></div>";
        loginHtml += "<div class='card'>";
        loginHtml += "<div class='lock-icon'>🔒</div>";
        loginHtml += "<div class='card-title'>Setup Portal Locked</div>";
        loginHtml += "<div class='card-desc'>This device is provisioned. Enter your Setup Admin Password to access configuration settings and master controls.</div>";
        loginHtml += "<form method='POST' action='/login'>";
        loginHtml += "<input type='password' name='setup_pass' placeholder='Enter Setup Password' autofocus required>";
        loginHtml += "<button type='submit' class='btn'>UNLOCK SETUP PORTAL</button>";
        loginHtml += "</form>";
        loginHtml += "<div class='shield-note'>🛡️ <b>Anti-Tamper Protection:</b> Unauthorized users within wireless range cannot alter master PINs, recovery seeds, or security keys without this password.</div>";
        loginHtml += "</div></div></body></html>";
        _server->send(200, "text/html", loginHtml);
        return;
    }

    String html;
    html.reserve(12000);

    html = "<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'>";
    html += "<meta name='viewport' content='width=device-width,initial-scale=1.0,maximum-scale=1.0,user-scalable=no'>";
    html += "<title>Crypto TKey S3 Setup</title><style>";
    html += ":root{--bg:#0A0D14;--surface:#121622;--border:#1E2538;--cyan:#00E5FF;--green:#00FF66;--amber:#FFB300;--red:#FF1744;--text:#F0F4FC;--muted:#7E8B9F;}";
    html += "*{box-sizing:border-box;margin:0;padding:0;font-family:system-ui,-apple-system,sans-serif;}";
    html += "body{background:var(--bg);color:var(--text);padding:16px;display:flex;justify-content:center;}";
    html += ".container{width:100%;max-width:500px;}";
    html += ".header{text-align:center;margin-bottom:20px;}";
    html += ".header h1{font-size:1.3rem;letter-spacing:1px;color:var(--cyan);margin-bottom:4px;}";
    html += ".header p{font-size:0.8rem;color:var(--muted);}";
    html += ".card{background:var(--surface);border:1px solid var(--border);border-radius:12px;padding:16px;margin-bottom:16px;}";
    html += ".card-title{font-size:0.9rem;font-weight:700;color:var(--cyan);margin-bottom:12px;display:flex;align-items:center;justify-content:space-between;}";
    html += ".form-group{margin-bottom:14px;}";
    html += "label{display:block;font-size:0.75rem;color:var(--muted);margin-bottom:6px;text-transform:uppercase;font-weight:600;}";
    html += "input[type='text'],input[type='password'],input[type='number'],select{width:100%;background:#07090F;border:1px solid var(--border);border-radius:8px;padding:10px 12px;color:var(--text);font-size:0.9rem;outline:none;}";
    html += "input:focus,select:focus{border-color:var(--cyan);}";
    html += ".field-desc{font-size:0.72rem;color:var(--muted);margin-top:4px;line-height:1.3;}";
    html += ".warning-box{background:rgba(255,179,0,0.08);border-left:3px solid var(--amber);padding:10px;border-radius:4px;font-size:0.75rem;color:#FFE082;margin-top:6px;line-height:1.4;}";
    html += ".coins-grid{display:grid;grid-template-columns:repeat(2,1fr);gap:8px;max-height:260px;overflow-y:auto;padding-right:4px;}";
    html += ".coin-pill{background:#07090F;border:1px solid var(--border);border-radius:8px;padding:8px 10px;display:flex;align-items:center;justify-content:space-between;}";
    html += ".coin-lbl{display:flex;align-items:center;gap:6px;font-size:0.82rem;font-weight:600;color:#fff;cursor:pointer;}";
    html += ".coin-lbl input{accent-color:var(--cyan);}";
    html += ".auto-badge{font-size:0.65rem;background:rgba(0,229,255,0.12);color:var(--cyan);padding:2px 6px;border-radius:4px;font-weight:600;}";
    html += ".btn{width:100%;background:var(--cyan);color:#000;border:none;border-radius:8px;padding:14px;font-weight:700;font-size:0.95rem;cursor:pointer;margin-top:10px;letter-spacing:1px;}";
    html += ".btn-scan{padding:6px 12px;font-size:0.75rem;background:var(--border);color:var(--text);border-radius:6px;border:none;cursor:pointer;}";
    html += ".btn-scan:hover{background:#2a334d;}";
    html += ".seed-shield{background:rgba(0,229,255,0.05);border:1px dashed var(--cyan);border-radius:8px;padding:14px;text-align:center;}";
    html += ".seed-shield h3{font-size:0.85rem;color:var(--cyan);margin-bottom:6px;}";
    html += ".seed-shield p{font-size:0.75rem;color:var(--muted);line-height:1.4;}";
    html += ".explainer{background:#0d141f;border-left:3px solid var(--cyan);padding:10px 14px;border-radius:4px;font-size:0.75rem;line-height:1.5;color:#9cb3cc;margin-top:10px;}";
    html += "</style></head><body>";

    html += "<div class='container'><div class='header'>";
    html += "<h1>⚡ CRYPTO TKEY S3</h1>";
    html += "<p>Secure Hardware Vault & Passkey Onboarding</p></div>";

    html += "<form method='POST' action='/save'>";

    // STEP 1: SECURITY PINS & SETUP PASSWORD
    html += "<div class='card'><div class='card-title'>1. Master PIN & Setup Security Passwords</div>";
    html += "<div class='form-group'><label>Master Device PIN (4-8 Digits)</label>";
    html += "<input type='password' name='pin' maxlength='8' pattern='[0-9]*' placeholder='1234' value='" + String(_newPin) + "' required>";
    html += "<div class='field-desc'>Entered on the physical 0.96\" screen to unlock the crypto vault & transaction signers.</div></div>";

    html += "<div class='form-group'><label>Setup Portal Admin Password</label>";
    if (_isProvisioned && strlen(_storedPasswordHash) > 0) {
        html += "<input type='password' name='setup_pass' placeholder='Leave blank to keep current setup password'>";
        html += "<div class='field-desc'>Setup password is currently set. Enter a new password only if you wish to change it.</div>";
    } else {
        html += "<input type='password' name='setup_pass' placeholder='Create a strong password for this setup page' required>";
        html += "<div class='field-desc'>Protects this web portal against unauthorized tampering if someone plugs in your dongle.</div>";
    }
    html += "</div>";

    html += "<div class='form-group'><label>Duress Wipe PIN (Optional Decoy)</label>";
    html += "<input type='password' name='duress' maxlength='8' pattern='[0-9]*' placeholder='8888' value='" + String(_newDuressPin) + "'>";
    html += "<div class='warning-box'>Entering this PIN triggers an instant cryptographic flash erase, scrubs all keys, and renders an authentic Guru Meditation crash decoy.</div>";
    html += "</div></div>";

    // STEP 2: WI-FI PROFILES
    html += "<div class='card'><div class='card-title'><span>2. Wi-Fi Configuration</span>";
    html += "<button type='button' class='btn-scan' onclick='scanWifi()'>Scan SSIDs</button></div>";
    html += "<div class='form-group'><label>Network SSID (2.4 GHz)</label>";
    html += "<input type='text' id='ssid' name='ssid' placeholder='Home or Mobile Hotspot'>";
    html += "<select id='ssid_select' style='display:none;margin-top:6px;' onchange='selectSsid(this)'><option value=''>-- Select Nearby Network --</option></select>";
    html += "</div>";
    html += "<div class='form-group'><label>Wi-Fi Password</label>";
    html += "<input type='password' name='pass' placeholder='WPA2 / WPA3 Password'></div></div>";

    // STEP 3: ASSET SELECTION (Zero manual balance entry)
    html += "<div class='card'><div class='card-title'>3. Active Cryptocurrencies (Automated On-Chain Tracking)</div>";
    html += "<div class='explainer' style='margin-bottom:12px;'>";
    html += "<b>⚡ Automated On-Chain Tracking Active:</b><br>";
    html += "Your Crypto TKey S3 derives genuine deposit addresses directly from your master seed for all enabled blockchains (Bitcoin SegWit, Ethereum EVM, Solana, Dogecoin, etc.). You never need to manually enter balances—when connected, balances and market metrics are synchronized automatically.";
    html += "</div>";
    html += "<div class='coins-grid'>";
    for (int i = 0; i < CryptoCoinRegistry::getCoinCount(); i++) {
        const CoinAsset* coin = CryptoCoinRegistry::getCoin((SupportedCoinId)i);
        if (!coin) continue;
        html += "<div class='coin-pill'>";
        html += "<label class='coin-lbl'><input type='checkbox' name='c_" + String(i) + "' value='1' " + (coin->enabled ? "checked" : "") + ">";
        html += "<span>" + String(coin->symbol) + "</span></label>";
        html += "<span class='auto-badge'>⚡ Auto-Track</span>";
        html += "</div>";
    }
    html += "</div></div>";

    // STEP 4: AIR-GAPPED SEED VERIFICATION (Zero HTTP Leakage)
    html += "<div class='card'><div class='card-title'>4. Master Recovery Seed</div>";
    html += "<div class='seed-shield'>";
    html += "<h3>🔒 Air-Gapped Display Security</h3>";
    html += "<p>Your 12-word recovery mnemonic is generated directly on the ESP32-S3 TRNG and displayed <strong>exclusively on the physical 0.96\" TFT LCD</strong>.</p>";
    html += "<p style='margin-top:6px; color:var(--amber); font-weight:600;'>To guarantee zero air-sniffing or network interception, words are NEVER transmitted to web browsers.</p>";
    html += "</div>";
    html += "<div class='explainer'>";
    html += "<b>🛡️ Zero Setup Needed for WebAuthn / Passkeys:</b><br>";
    html += "When plugged into USB, your T-Dongle S3 functions as a native FIDO2/WebAuthn authenticator. Passkeys work out of the box with 1-tap physical touch.";
    html += "</div></div>";

    html += "<button type='submit' class='btn'>SAVE & LAUNCH TKEY</button>";
    html += "</form></div>";

    // Client-side script for interactive WiFi scanning
    html += "<script>";
    html += "function scanWifi(){";
    html += "const btn=document.querySelector('.btn-scan');";
    html += "btn.innerText='Scanning...';";
    html += "fetch('/scan')";
    html += ".then(r=>r.json())";
    html += ".then(nets=>{";
    html += "btn.innerText='Rescan';";
    html += "const sel=document.getElementById('ssid_select');";
    html += "sel.innerHTML='<option value=\"\">-- Select Nearby Network --</option>';";
    html += "nets.forEach(n=>{";
    html += "const opt=document.createElement('option');";
    html += "opt.value=n.ssid;";
    html += "opt.text=n.ssid+' ('+n.rssi+' dBm)'+(n.secure?' 🔒':'');";
    html += "sel.appendChild(opt);";
    html += "});";
    html += "sel.style.display='block';";
    html += "if(nets.length>0){document.getElementById('ssid').value=nets[0].ssid;}";
    html += "})";
    html += ".catch(()=>{btn.innerText='Retry';});";
    html += "}";
    html += "function selectSsid(elem){";
    html += "if(elem.value){document.getElementById('ssid').value=elem.value;}";
    html += "}";
    html += "</script></body></html>";

    _server->send(200, "text/html", html);
}

void WebPortal::handleSave() {
    if (!isAuthenticated()) {
        handleRoot();
        return;
    }

    if (_server->hasArg("pin")) {
        String p = _server->arg("pin");
        if (p.length() >= 4 && p.length() <= 8) {
            strncpy(_newPin, p.c_str(), sizeof(_newPin) - 1);
        }
    }

    if (_server->hasArg("setup_pass")) {
        String sp = _server->arg("setup_pass");
        sp.trim();
        if (sp.length() >= 4) {
            computeSha256Hex(sp.c_str(), _setupPasswordHash);
            strncpy(_storedPasswordHash, _setupPasswordHash, sizeof(_storedPasswordHash) - 1);
            Serial.println("[PORTAL] 🔑 New Setup Admin Password configured and hashed.");
        }
    }

    if (_server->hasArg("duress")) {
        String dp = _server->arg("duress");
        if (dp.length() >= 4 && dp.length() <= 8) {
            strncpy(_newDuressPin, dp.c_str(), sizeof(_newDuressPin) - 1);
        }
    }

    // Add Wi-Fi credentials if provided
    String s = _server->hasArg("ssid") ? _server->arg("ssid") : "";
    String pass = _server->hasArg("pass") ? _server->arg("pass") : (_server->hasArg("wifipass") ? _server->arg("wifipass") : "");
    if (s.length() > 0 && _wifi) {
        _wifi->addNetwork(s.c_str(), pass.c_str());
        Serial.printf("[PORTAL] Registered Wi-Fi SSID '%s'\n", s.c_str());
    }

    // Update 24 Coins (Enable/Disable toggles, zero manual balance inputs)
    for (int i = 0; i < CryptoCoinRegistry::getCoinCount(); i++) {
        bool en = _server->hasArg("c_" + String(i));
        CryptoCoinRegistry::setCoinEnabled((SupportedCoinId)i, en);
    }

    _setupDone = true;

    String successHtml = "<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'>";
    successHtml += "<meta name='viewport' content='width=device-width,initial-scale=1.0'>";
    successHtml += "<title>Configuration Saved</title><style>";
    successHtml += "body{background:#0A0D14;color:#00FF66;font-family:system-ui,-apple-system,sans-serif;text-align:center;padding:40px 16px;}";
    successHtml += ".card{background:#121622;border:1px solid #1E2538;border-radius:12px;padding:24px;max-width:440px;margin:0 auto;}";
    successHtml += "h2{color:#00E5FF;margin-bottom:12px;font-size:1.2rem;}";
    successHtml += "p{color:#F0F4FC;font-size:0.85rem;line-height:1.5;margin-bottom:10px;}";
    successHtml += "</style></head><body><div class='card'>";
    successHtml += "<h2>✅ CONFIGURATION SAVED</h2>";
    successHtml += "<p>Your Crypto TKey S3 is provisioned and armed.</p>";
    successHtml += "<p style='color:#7E8B9F;'>The SoftAP radio has been shut down to prevent heat buildup. Your device is now running cool in secure standalone mode.</p>";
    successHtml += "</div></body></html>";

    _server->send(200, "text/html", successHtml);
}
