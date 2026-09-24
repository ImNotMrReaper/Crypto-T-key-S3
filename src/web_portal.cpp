/**
 * web_portal.cpp — Setup portal (see web_portal.h for the security model)
 */

#include "web_portal.h"
#include "portal_page.h"
#include "crypto_wallet.h"
#include "crypto_p256.h"
#include "crypto_coins.h"
#include "wifi_manager.h"
#include "pin_vault.h"
#include "ui_theme.h"
#include <Preferences.h>
#include <esp_mac.h>
#include <mbedtls/md.h>
#include <mbedtls/pkcs5.h>
#include <mbedtls/sha256.h>
#include <mbedtls/platform_util.h>

#define SESSION_IDLE_MS      (10UL * 60UL * 1000UL)
#define LOGIN_MAX_FAILS      5
#define SETUP_PW_ITERATIONS  20000
#define SETUP_PW_MIN_LEN     8
#define MAX_NEW_NETWORKS     5

// ─── Helpers ─────────────────────────────────────────────────────────────────

static void randomHex(char* out, size_t bytes) {
    uint8_t buf[32];
    CryptoP256::secureRandom(buf, bytes);
    for (size_t i = 0; i < bytes; i++) sprintf(out + 2 * i, "%02x", buf[i]);
    out[2 * bytes] = '\0';
    mbedtls_platform_zeroize(buf, sizeof(buf));
}

static bool ctEqualStr(const char* a, const char* b) {
    size_t la = strlen(a), lb = strlen(b);
    uint8_t d = la != lb;
    for (size_t i = 0; i < la && i < lb; i++) d |= a[i] ^ b[i];
    return d == 0;
}

static void toHex(const uint8_t* d, size_t n, char* out) {
    for (size_t i = 0; i < n; i++) sprintf(out + 2 * i, "%02x", d[i]);
    out[2 * n] = '\0';
}

static bool fromHex(const char* hex, uint8_t* out, size_t n) {
    if (strlen(hex) != 2 * n) return false;
    for (size_t i = 0; i < n; i++) {
        unsigned v;
        if (sscanf(hex + 2 * i, "%2x", &v) != 1) return false;
        out[i] = v;
    }
    return true;
}

static String jsonEsc(const char* s) {
    String o;
    for (; *s; s++) {
        char c = *s;
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if ((uint8_t)c < 0x20) { char u[8]; snprintf(u, sizeof(u), "\\u%04x", c); o += u; }
        else o += c;
    }
    return o;
}

// Overwrite a String's buffer before releasing it (String::clear() only resets the length)
static void wipe(String& s) {
    if (s.length()) mbedtls_platform_zeroize((void*)s.c_str(), s.length());
    s.clear();
}

static bool allDigits(const String& s) {
    for (size_t i = 0; i < s.length(); i++) {
        if (!isdigit((unsigned char)s[i])) return false;
    }
    return true;
}

// Key derivation at 80 MHz is slow; run it at full speed and drop back afterwards.
struct PortalCpuBoost {
    uint32_t prev;
    PortalCpuBoost() : prev(getCpuFrequencyMhz()) { setCpuFrequencyMhz(240); }
    ~PortalCpuBoost() { setCpuFrequencyMhz(prev); }
};

// ─── Setup password (NVS vault_sec) ──────────────────────────────────────────
//   setup_pw2      = "<iterations>$<salt hex 32>$<pbkdf2 hex 64>"
//   setup_pwd_hash = legacy unsalted SHA-256 hex (upgraded on the next good login)

static void pbkdf2(const String& pass, const uint8_t salt[16], uint32_t iters, uint8_t out[32]) {
    PortalCpuBoost boost;
    mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256, (const uint8_t*)pass.c_str(), pass.length(),
                                  salt, 16, iters, 32, out);
}

static void storeSetupPassword(const String& pass) {
    uint8_t salt[16], hash[32];
    CryptoP256::secureRandom(salt, sizeof(salt));
    pbkdf2(pass, salt, SETUP_PW_ITERATIONS, hash);
    char rec[128], sh[33], hh[65];
    toHex(salt, 16, sh);
    toHex(hash, 32, hh);
    snprintf(rec, sizeof(rec), "%d$%s$%s", SETUP_PW_ITERATIONS, sh, hh);
    Preferences p;
    p.begin("vault_sec", false);
    p.putString("setup_pw2", rec);
    p.remove("setup_pwd_hash");
    p.end();
    mbedtls_platform_zeroize(hash, sizeof(hash));
}

static bool verifySetupPassword(const String& pass) {
    Preferences p;
    p.begin("vault_sec", true);
    String rec = p.getString("setup_pw2", "");
    String legacy = p.getString("setup_pwd_hash", "");
    p.end();

    if (rec.length()) {
        int a = rec.indexOf('$'), b = rec.indexOf('$', a + 1);
        uint32_t iters = rec.substring(0, a).toInt();
        uint8_t salt[16], want[32], got[32];
        if (a < 0 || b < 0 || iters < 1000 || !fromHex(rec.substring(a + 1, b).c_str(), salt, 16) ||
            !fromHex(rec.substring(b + 1).c_str(), want, 32)) return false;
        pbkdf2(pass, salt, iters, got);
        uint8_t d = 0;
        for (int i = 0; i < 32; i++) d |= got[i] ^ want[i];
        mbedtls_platform_zeroize(got, sizeof(got));
        return d == 0;
    }
    if (legacy.length() == 64) {
        uint8_t h[32];
        char hh[65];
        mbedtls_sha256((const uint8_t*)pass.c_str(), pass.length(), h, 0);
        toHex(h, 32, hh);
        if (!ctEqualStr(hh, legacy.c_str())) return false;
        storeSetupPassword(pass);   // upgrade to salted PBKDF2
        Serial.println("[PORTAL] Setup password upgraded to salted PBKDF2");
        return true;
    }
    return false;
}

bool WebPortal::hasSetupPassword() {
    Preferences p;
    p.begin("vault_sec", true);
    bool has = p.getString("setup_pw2", "").length() > 0 || p.getString("setup_pwd_hash", "").length() == 64;
    p.end();
    return has;
}

// ─── Lifecycle ───────────────────────────────────────────────────────────────

void WebPortal::begin(CryptoWallet* wallet, WifiManager* wifi, bool isProvisioned) {
    _wallet = wallet;
    _wifi = wifi;
    _isProvisioned = isProvisioned;
    _setupDone = false;
    _exitRequested = false;
    _session[0] = _csrf[0] = '\0';

    // SSID from the MAC (stable per device); password random for every portal session
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(_apSsid, sizeof(_apSsid), "T-Key-%02X%02X", mac[4], mac[5]);
    static const char* ALPHA = "abcdefghjkmnpqrstuvwxyz23456789";   // no 0/o/1/l/i
    uint8_t rnd[10];
    CryptoP256::secureRandom(rnd, sizeof(rnd));
    for (int i = 0; i < 10; i++) _apPass[i] = ALPHA[rnd[i] % 31];
    _apPass[10] = '\0';
    snprintf(_wifiQr, sizeof(_wifiQr), "WIFI:T:WPA;S:%s;P:%s;;", _apSsid, _apPass);

    WiFi.mode(WIFI_AP);
    WiFi.setTxPower(WIFI_POWER_8_5dBm);   // low TX power: less heat, shorter range
    IPAddress apIP(192, 168, 4, 1);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    WiFi.softAP(_apSsid, _apPass, 6, 0, 2);   // WPA2-PSK, max 2 clients
    delay(100);

    _dns = new DNSServer();
    _dns->start(53, "*", apIP);   // captive portal: every name resolves to the device

    _server = new WebServer(80);
    const char* headers[] = {"Cookie", "X-TKey-CSRF"};
    _server->collectHeaders(headers, 2);
    _server->on("/", HTTP_GET, [this]() { handlePage(); });
    _server->on("/api/state", HTTP_GET, [this]() { handleState(); });
    _server->on("/api/login", HTTP_POST, [this]() { handleLogin(); });
    _server->on("/api/save", HTTP_POST, [this]() { handleSave(); });
    _server->on("/api/scan", HTTP_GET, [this]() { handleScan(); });
    _server->on("/api/exit", HTTP_POST, [this]() { handleExit(); });
    for (const char* probe : {"/generate_204", "/gen_204", "/hotspot-detect.html", "/ncsi.txt", "/connecttest.txt"}) {
        _server->on(probe, HTTP_GET, [this]() { handlePage(); });
    }
    _server->onNotFound([this]() { handleNotFound(); });
    _server->begin();

    _isRunning = true;
    Serial.printf("[PORTAL] Setup portal on WPA2 SSID %s at http://192.168.4.1 (password on the device screen)\n", _apSsid);
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
    mbedtls_platform_zeroize(_apPass, sizeof(_apPass));
    mbedtls_platform_zeroize(_wifiQr, sizeof(_wifiQr));
    mbedtls_platform_zeroize(_session, sizeof(_session));
    mbedtls_platform_zeroize(_csrf, sizeof(_csrf));
    _isRunning = false;
    Serial.println("[PORTAL] Setup portal stopped, radio off.");
}

// ─── Sessions ────────────────────────────────────────────────────────────────

void WebPortal::startSession() {
    randomHex(_session, 16);
    randomHex(_csrf, 16);
    _sessionSeenMs = millis();
    _server->sendHeader("Set-Cookie", String("tk=") + _session + "; Path=/; HttpOnly; SameSite=Strict");
}

bool WebPortal::isAuthenticated() {
    if (!_session[0] || millis() - _sessionSeenMs > SESSION_IDLE_MS) return false;
    String cookie = _server->header("Cookie");
    int i = cookie.indexOf("tk=");
    if (i < 0) return false;
    String tok = cookie.substring(i + 3, i + 3 + 32);
    if (!ctEqualStr(tok.c_str(), _session)) return false;
    _sessionSeenMs = millis();
    return true;
}

bool WebPortal::checkCsrf() {
    return _csrf[0] && ctEqualStr(_server->header("X-TKey-CSRF").c_str(), _csrf);
}

void WebPortal::sendJson(int code, const String& body) {
    _server->sendHeader("Cache-Control", "no-store");
    _server->send(code, "application/json", body);
}

void WebPortal::sendError(int code, const char* msg) {
    sendJson(code, String("{\"ok\":false,\"error\":\"") + jsonEsc(msg) + "\"}");
}

// ─── Handlers ────────────────────────────────────────────────────────────────

void WebPortal::handleNotFound() {
    _server->sendHeader("Location", "http://192.168.4.1/", true);
    _server->send(302, "text/plain", "");
}

void WebPortal::handlePage() {
    _server->sendHeader("Cache-Control", "no-store");
    _server->sendHeader("X-Frame-Options", "DENY");
    _server->sendHeader("Content-Security-Policy",
                        "default-src 'self'; style-src 'unsafe-inline'; script-src 'unsafe-inline'; img-src data:");
    _server->send_P(200, "text/html", PORTAL_PAGE);
}

void WebPortal::handleState() {
    bool hasPw = hasSetupPassword();
    // Nothing to protect yet: whoever joined the AP (password from the screen) gets a session
    if (!hasPw && !isAuthenticated()) startSession();

    if (!isAuthenticated() && hasPw) {
        uint32_t lockLeft = (int32_t)(_lockUntilMs - millis()) > 0 ? (_lockUntilMs - millis()) / 1000 : 0;
        sendJson(200, String("{\"auth\":false,\"provisioned\":") + (_isProvisioned ? "true" : "false") +
                          ",\"lockedFor\":" + lockLeft + "}");
        return;
    }

    String o;
    o.reserve(9000);
    o = "{\"auth\":true,\"csrf\":\"";
    o += _csrf;
    o += "\",\"provisioned\":";
    o += _isProvisioned ? "true" : "false";
    o += ",\"hasPassword\":";
    o += hasPw ? "true" : "false";
    o += ",\"hasPin\":";
    o += PinVault::length() > 0 ? "true" : "false";
    o += ",\"hasDuress\":";
    o += PinVault::hasDuress() ? "true" : "false";
    o += ",\"hasSeed\":";
    o += (_wallet && _wallet->hasSeed()) ? "true" : "false";
    char hex[8];
    snprintf(hex, sizeof(hex), "%06lx", (unsigned long)homeTheme.rgb());
    o += ",\"theme\":{\"rgb\":\"";
    o += hex;
    o += "\",\"fx\":";
    o += (int)homeTheme.fx;
    o += "},\"wifi\":[";
    for (int i = 0; _wifi && i < _wifi->getSavedCount(); i++) {
        if (i) o += ",";
        o += "\"" + jsonEsc(_wifi->getNetwork(i)->ssid) + "\"";
    }
    o += "],\"families\":[";
    for (int f = 0; f < FAM_COUNT; f++) {
        if (f) o += ",";
        o += "\"";
        o += WalletFamilies::name((WalletFamily)f);
        o += "\"";
    }
    o += "],\"coins\":[";
    for (int i = 0; i < CryptoCoinRegistry::getCoinCount(); i++) {
        const CoinAsset* c = CryptoCoinRegistry::getCoinByIndex(i);
        char row[160];
        snprintf(row, sizeof(row), "%s[\"%s\",\"%s\",\"%s\",%d,%d,\"%02x%02x%02x\",%d]", i ? "," : "",
                 c->symbol, c->name, c->meta->network, (int)c->meta->family, (int)c->meta->category,
                 c->meta->r, c->meta->g, c->meta->b, c->enabled ? 1 : 0);
        o += row;
    }
    o += "]}";
    sendJson(200, o);
}

void WebPortal::handleLogin() {
    if ((int32_t)(_lockUntilMs - millis()) > 0) {
        sendError(429, "Too many attempts. Wait for the lockout to end.");
        return;
    }
    String pass = _server->arg("password");
    bool ok = pass.length() > 0 && pass.length() <= 128 && verifySetupPassword(pass);
    wipe(pass);
    if (!ok) {
        if (++_loginFails >= LOGIN_MAX_FAILS) {
            _lockUntilMs = millis() + _lockMinutes * 60000UL;
            _lockMinutes = min<uint32_t>(_lockMinutes * 2, 60);
            _loginFails = 0;
            Serial.println("[PORTAL] Too many wrong setup passwords: login locked");
        }
        delay(400);   // slow down guessing a little more
        sendError(401, "Wrong setup password.");
        return;
    }
    _loginFails = 0;
    _lockMinutes = 5;
    startSession();
    Serial.println("[PORTAL] Setup portal login OK");
    sendJson(200, "{\"ok\":true}");
}

void WebPortal::handleScan() {
    if (!isAuthenticated()) {
        sendError(401, "Not logged in.");
        return;
    }
    int n = WiFi.scanNetworks(false, false, false, 300);
    String json = "[";
    for (int i = 0; i < n; ++i) {
        if (i) json += ",";
        json += "{\"ssid\":\"" + jsonEsc(WiFi.SSID(i).c_str()) + "\",\"rssi\":" + String(WiFi.RSSI(i)) +
                ",\"secure\":" + (WiFi.encryptionType(i) != WIFI_AUTH_OPEN ? "true" : "false") + "}";
    }
    json += "]";
    WiFi.scanDelete();
    sendJson(200, json);
}

void WebPortal::handleExit() {
    if (!isAuthenticated() || !checkCsrf()) {
        sendError(403, "Not allowed.");
        return;
    }
    if (!_isProvisioned) {
        sendError(400, "Finish the first setup before leaving.");
        return;
    }
    _exitRequested = true;
    sendJson(200, "{\"ok\":true}");
}

void WebPortal::handleSave() {
    if (!isAuthenticated() || !checkCsrf()) {
        sendError(403, "Session expired. Reload the page.");
        return;
    }
    String pin = _server->arg("pin"), duress = _server->arg("duress"), pw = _server->arg("setup_pass");
    bool clearDuress = _server->arg("duress_clear") == "1";
    bool needPin = PinVault::length() == 0, needPw = !hasSetupPassword();

    // ── Validate everything before changing anything ──
    const char* err = nullptr;
    if (pin.length() && (pin.length() < 4 || pin.length() > 8 || !allDigits(pin))) err = "The PIN must be 4 to 8 digits.";
    else if (needPin && !pin.length()) err = "Choose a PIN.";
    else if (duress.length() && (duress.length() < 4 || duress.length() > 8 || !allDigits(duress))) err = "The duress PIN must be 4 to 8 digits.";
    else if (duress.length() && duress == pin) err = "The duress PIN must differ from the PIN.";
    else if (pw.length() && (pw.length() < SETUP_PW_MIN_LEN || pw.length() > 128)) err = "The setup password needs at least 8 characters.";
    else if (needPw && !pw.length()) err = "Choose a setup password.";

    String coins = "," + _server->arg("coins") + ",";
    int selected = 0;
    for (int i = 0; i < CryptoCoinRegistry::getCoinCount(); i++) {
        if (coins.indexOf(String(",") + CryptoCoinRegistry::getCoinByIndex(i)->symbol + ",") >= 0) selected++;
    }
    if (!err && selected == 0) err = "Select at least one coin.";

    String rgb = _server->arg("theme_rgb");
    int fx = _server->arg("theme_fx").toInt();
    if (!err && (rgb.length() != 6 || fx < HOME_FX_SOLID || fx > HOME_FX_RAINBOW)) err = "Invalid home theme.";

    if (err) {
        wipe(pin); wipe(duress); wipe(pw);
        sendError(400, err);
        return;
    }

    // ── Apply ──
    if (pin.length() && !PinVault::setPin(pin.c_str())) err = "The PIN could not be saved.";
    if (!err && duress.length() && !PinVault::setDuressPin(duress.c_str())) err = "The duress PIN must differ from the PIN.";
    if (!err && !duress.length() && clearDuress) PinVault::setDuressPin("");
    wipe(pin); wipe(duress);
    if (err) {
        wipe(pw);
        sendError(400, err);
        return;
    }
    if (pw.length()) storeSetupPassword(pw);
    wipe(pw);

    for (int i = 0; i < CryptoCoinRegistry::getCoinCount(); i++) {
        CoinAsset* c = CryptoCoinRegistry::getCoinByIndex(i);
        CryptoCoinRegistry::setCoinEnabled(i, coins.indexOf(String(",") + c->symbol + ",") >= 0);
    }
    CryptoCoinRegistry::savePreferences();

    uint32_t v = strtoul(rgb.c_str(), nullptr, 16);
    homeTheme.r = v >> 16; homeTheme.g = v >> 8; homeTheme.b = v;
    homeTheme.fx = (HomeEffect)fx;
    homeTheme.save();

    if (_wifi) {
        for (int i = 0; i < MAX_WIFI_NETWORKS; i++) {
            String d = _server->arg("wd" + String(i));
            if (d.length()) _wifi->removeNetwork(d.c_str());
        }
        for (int i = 0; i < MAX_NEW_NETWORKS; i++) {
            String s = _server->arg("ws" + String(i)), p = _server->arg("wp" + String(i));
            if (s.length() && s.length() <= 32 && p.length() <= 63) _wifi->addNetwork(s.c_str(), p.c_str());
        }
    }

    Preferences p;
    p.begin("vault_sec", false);
    p.putBool("provisioned", true);
    p.end();

    _setupDone = true;
    Serial.printf("[PORTAL] Settings saved: %d coin(s), theme #%06lx fx %d\n", selected, (unsigned long)v, fx);
    sendJson(200, "{\"ok\":true}");
}
