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
#include "sd_vault.h"
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
    // Keep the captive setup AP off common home-router subnets. A 192.168.4.1 AP
    // collides with the user's LAN on this workstation and steals its route/DNS.
    IPAddress apIP(10, 77, 0, 1);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    WiFi.softAP(_apSsid, _apPass, 6, 0, 2);   // WPA2-PSK, max 2 clients
    WiFi.setTxPower(WIFI_POWER_8_5dBm);       // low TX power: less heat, shorter range (after AP started)
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
    _server->on("/api/theme_preview", HTTP_POST, [this]() { handleThemePreview(); });
    _server->on("/api/sd_backup", HTTP_POST, [this]() { handleSdBackup(); });
    _server->on("/api/sd_restore", HTTP_POST, [this]() { handleSdRestore(); });
    _server->on("/api/sd_full_backup", HTTP_POST, [this]() { handleSdFullBackup(); });
    _server->on("/api/sd_full_restore", HTTP_POST, [this]() { handleSdFullRestore(); });
    _server->on("/api/sd_erase", HTTP_POST, [this]() { handleSdErase(); });
    for (const char* probe : {"/generate_204", "/gen_204", "/hotspot-detect.html", "/ncsi.txt", "/connecttest.txt"}) {
        _server->on(probe, HTTP_GET, [this]() { handlePage(); });
    }
    _server->onNotFound([this]() { handleNotFound(); });
    _server->begin();

    _isRunning = true;
    Serial.printf("[PORTAL] Setup portal on WPA2 SSID %s at http://10.77.0.1 (password on the device screen)\n", _apSsid);
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

#ifdef TKEY_TEST_SERIAL_TOUCH
void WebPortal::testSession(const char** session, const char** csrf) {
    randomHex(_session, 16);
    randomHex(_csrf, 16);
    _sessionSeenMs = millis();
    *session = _session;
    *csrf = _csrf;
}
#endif

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
    _server->sendHeader("Location", "http://10.77.0.1/", true);
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
    o += ",\"key_name\":\"" + jsonEsc(homeTheme.activeKeyName()) + "\"";
    o += ",\"wallpaper\":" + String((int)homeTheme.wallpaper);
    char hex[8];
    snprintf(hex, sizeof(hex), "%06lx", (unsigned long)homeTheme.savedRgb());
    o += ",\"theme\":{\"rgb\":\"";
    o += hex;
    o += "\",\"fx\":";
    o += (int)homeTheme.fx;
    o += ",\"speed\":";
    o += (int)homeTheme.speed;
    o += ",\"brightness\":";
    o += (int)homeTheme.brightness;
    o += ",\"activeCustom\":";
    o += (int)homeTheme.activeCustomMode;
    o += ",\"customModes\":[";
    for (uint8_t i = 0; i < homeTheme.customCount && i < MAX_CUSTOM_MODES; i++) {
        if (i) o += ",";
        o += "{\"name\":\"" + jsonEsc(homeTheme.customModes[i].name) + "\",\"fx\":";
        o += (int)homeTheme.customModes[i].fx;
        o += ",\"speed\":";
        o += (int)homeTheme.customModes[i].speed;
        o += ",\"colors\":[";
        for (uint8_t ci = 0; ci < homeTheme.customModes[i].colorCount; ci++) {
            if (ci) o += ",";
            char chex[10];   // "rrggbb" with quotes is 8 chars + NUL
            snprintf(chex, sizeof(chex), "\"%02x%02x%02x\"",
                     homeTheme.customModes[i].colors[ci][0],
                     homeTheme.customModes[i].colors[ci][1],
                     homeTheme.customModes[i].colors[ci][2]);
            o += chex;
        }
        o += "]}";
    }
    o += "]},\"wifi\":[";
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
    EmergencyPolicy pol = EmergencyPolicyStore::load();
    o += "],\"policy\":{";
    o += "\"duress\":\"";
    o += EmergencyPolicyStore::actionName(pol.action[TRIG_DURESS_PIN]);
    o += "\",\"panic\":\"";
    o += EmergencyPolicyStore::actionName(pol.action[TRIG_PANIC_HOLD]);
    o += "\",\"lockout\":\"";
    o += EmergencyPolicyStore::actionName(pol.action[TRIG_PIN_LOCKOUT]);
    o += "\",\"countdown\":";
    o += String((int)pol.panicCountdownS);
    o += "},\"sd\":{";
    bool sdMounted = sdVault.isMounted() || sdVault.begin();
    bool sdHasBackup = sdMounted && sdVault.hasSeedBackup();
    bool sdHasFull = sdMounted && sdVault.hasFullBackup();
    o += "\"mounted\":";
    o += sdMounted ? "true" : "false";
    o += ",\"hasBackup\":";
    o += sdHasBackup ? "true" : "false";
    o += ",\"hasFullBackup\":";
    o += sdHasFull ? "true" : "false";
    o += "}}";
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
    if (_setupRequired) {
        sendError(400, "New firmware was installed: review and save the settings once to finish setup.");
        return;
    }
    homeTheme.clearPreview();
    _exitRequested = true;
    sendJson(200, "{\"ok\":true}");
}

void WebPortal::handleThemePreview() {
    if (!isAuthenticated()) {
        sendError(401, "Please log in first.");
        return;
    }
    // A revert only restores the saved theme, so it may also carry the CSRF token in the body:
    // navigator.sendBeacon (used when the page closes) can't set request headers.
    bool revert = _server->hasArg("revert") && _server->arg("revert").length();
    bool bodyToken = revert && _csrf[0] && ctEqualStr(_server->arg("csrf").c_str(), _csrf);
    if (!checkCsrf() && !bodyToken) {
        sendError(403, "Invalid session. Reload the setup page.");
        return;
    }

    if (revert) {
        homeTheme.clearPreview();
        sendJson(200, "{\"ok\":true,\"reverted\":true}");
        return;
    }

    String rgb = _server->arg("theme_rgb");
    if (rgb.length() != 6) {
        sendError(400, "Invalid hex colour.");
        return;
    }

    for (int i = 0; i < 6; i++) {
        char c = rgb[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
            sendError(400, "Invalid hex character.");
            return;
        }
    }

    int fx = _server->hasArg("theme_fx") ? _server->arg("theme_fx").toInt() : (int)homeTheme.fx;
    if (fx < 0 || fx >= HOME_FX_COUNT) fx = (int)HOME_FX_BREATHE;

    int speed = _server->hasArg("theme_speed") ? _server->arg("theme_speed").toInt() : (int)homeTheme.speed;
    if (speed < 1 || speed > 5) speed = 3;

    int bright = _server->hasArg("theme_bright") ? _server->arg("theme_bright").toInt() : (int)homeTheme.brightness;
    if (bright < 0 || bright > 2) bright = 1;

    int wp = _server->hasArg("theme_wallpaper") ? _server->arg("theme_wallpaper").toInt() : (int)homeTheme.wallpaper;
    if (wp < 0 || wp >= WALLPAPER_COUNT) wp = 0;

    uint32_t v = strtoul(rgb.c_str(), nullptr, 16);
    uint8_t r = (v >> 16) & 0xFF;
    uint8_t g = (v >> 8) & 0xFF;
    uint8_t b = v & 0xFF;

    homeTheme.setPreview(r, g, b, (HomeEffect)fx, (uint8_t)speed, (LedBrightness)bright, millis(), (HomeWallpaper)wp);
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

    String polDuress = _server->arg("pol_duress");
    String polPanic = _server->arg("pol_panic");
    String polLockout = _server->arg("pol_lockout");
    String polCountdown = _server->arg("pol_countdown");
    EmergencyPolicy newPol = EmergencyPolicyStore::load();
    bool policySpecified = false;

    if (!err && (polDuress.length() || polPanic.length() || polLockout.length() || polCountdown.length())) {
        policySpecified = true;
        EmergencyAction aDuress, aPanic, aLockout;
        if (!EmergencyPolicyStore::parseAction(polDuress.c_str(), &aDuress) ||
            !EmergencyPolicyStore::isAllowed(TRIG_DURESS_PIN, aDuress)) {
            err = "Invalid duress emergency action.";
        } else if (!EmergencyPolicyStore::parseAction(polPanic.c_str(), &aPanic) ||
                   !EmergencyPolicyStore::isAllowed(TRIG_PANIC_HOLD, aPanic)) {
            err = "Invalid panic emergency action.";
        } else if (!EmergencyPolicyStore::parseAction(polLockout.c_str(), &aLockout) ||
                   !EmergencyPolicyStore::isAllowed(TRIG_PIN_LOCKOUT, aLockout)) {
            err = "Invalid lockout emergency action.";
        } else if (!allDigits(polCountdown) || polCountdown.toInt() < 0 || polCountdown.toInt() > PANIC_COUNTDOWN_MAX_S) {
            err = "Invalid panic countdown seconds (0 to 10).";
        } else {
            newPol.action[TRIG_DURESS_PIN] = aDuress;
            newPol.action[TRIG_PANIC_HOLD] = aPanic;
            newPol.action[TRIG_PIN_LOCKOUT] = aLockout;
            newPol.panicCountdownS = (uint8_t)polCountdown.toInt();
            if (!EmergencyPolicyStore::isValid(newPol)) {
                err = "Invalid emergency policy configuration.";
            }
        }
    }

    String coins = "," + _server->arg("coins") + ",";
    int selected = 0;
    for (int i = 0; i < CryptoCoinRegistry::getCoinCount(); i++) {
        if (coins.indexOf(String(",") + CryptoCoinRegistry::getCoinByIndex(i)->symbol + ",") >= 0) selected++;
    }
    if (!err && selected == 0) err = "Select at least one coin.";

    String rgb = _server->arg("theme_rgb");
    int fx = _server->arg("theme_fx").toInt();
    int speed = _server->hasArg("theme_speed") ? _server->arg("theme_speed").toInt() : 3;
    int bright = _server->hasArg("theme_bright") ? _server->arg("theme_bright").toInt() : 1;
    int activeCustom = _server->hasArg("theme_custom_idx") ? _server->arg("theme_custom_idx").toInt() : -1;
    int wp = _server->hasArg("theme_wallpaper") ? _server->arg("theme_wallpaper").toInt() : (int)homeTheme.wallpaper;
    if (wp < 0 || wp >= WALLPAPER_COUNT) wp = 0;

    char validatedName[17];
    strncpy(validatedName, homeTheme.keyName, sizeof(validatedName) - 1);
    validatedName[sizeof(validatedName) - 1] = '\0';
    if (_server->hasArg("key_name")) {
        String keyName = _server->arg("key_name");
        if (!HomeTheme::validateKeyName(keyName.c_str(), validatedName, sizeof(validatedName))) {
            err = "Invalid key name (max 16 printable characters).";
        }
    }

    if (!err && (rgb.length() != 6 || fx < 0 || fx >= HOME_FX_COUNT)) err = "Invalid home theme.";
    if (!err && (speed < 1 || speed > 5)) speed = 3;
    if (!err && (bright < 0 || bright > 2)) bright = 1;
    if (!err && (activeCustom < -1 || activeCustom >= MAX_CUSTOM_MODES)) activeCustom = -1;

    if (err) {
        wipe(pin); wipe(duress); wipe(pw);
        sendError(400, err);
        return;
    }

    // ── Apply ──
    // PIN and duress PIN change together: same length, never equal, all or nothing
    const char* newDuress = duress.length() ? duress.c_str() : (clearDuress ? "" : nullptr);
    if ((pin.length() || newDuress) && !PinVault::setPins(pin.length() ? pin.c_str() : nullptr, newDuress)) {
        err = PinVault::lastError()[0] ? PinVault::lastError() : "The PIN could not be saved.";
    }
    wipe(pin); wipe(duress);
    if (err) {
        wipe(pw);
        sendError(400, err);
        return;
    }
    if (pw.length()) storeSetupPassword(pw);
    wipe(pw);

    if (policySpecified) {
        if (EmergencyPolicyStore::save(newPol)) {
            _policyChanged = true;
        } else {
            sendError(500, "Failed to save emergency policy.");
            return;
        }
    }

    for (int i = 0; i < CryptoCoinRegistry::getCoinCount(); i++) {
        CoinAsset* c = CryptoCoinRegistry::getCoinByIndex(i);
        CryptoCoinRegistry::setCoinEnabled(i, coins.indexOf(String(",") + c->symbol + ",") >= 0);
    }
    CryptoCoinRegistry::savePreferences();

    uint32_t v = strtoul(rgb.c_str(), nullptr, 16);
    homeTheme.r = (v >> 16) & 0xFF;
    homeTheme.g = (v >> 8) & 0xFF;
    homeTheme.b = v & 0xFF;
    homeTheme.fx = (HomeEffect)fx;
    homeTheme.speed = (uint8_t)speed;
    homeTheme.brightness = (LedBrightness)bright;
    homeTheme.activeCustomMode = (int8_t)activeCustom;
    strncpy(homeTheme.keyName, validatedName, sizeof(homeTheme.keyName) - 1);
    homeTheme.keyName[sizeof(homeTheme.keyName) - 1] = '\0';
    homeTheme.wallpaper = (HomeWallpaper)wp;

    if (_server->hasArg("cm_count")) {
        int cmCount = _server->arg("cm_count").toInt();
        if (cmCount < 0) cmCount = 0;
        if (cmCount > MAX_CUSTOM_MODES) cmCount = MAX_CUSTOM_MODES;
        homeTheme.customCount = cmCount;
        for (int i = 0; i < cmCount; i++) {
            String prefix = "cm_" + String(i) + "_";
            String cname = _server->arg(prefix + "name");
            int cfx = _server->arg(prefix + "fx").toInt();
            int cspd = _server->arg(prefix + "speed").toInt();
            String ccolors = _server->arg(prefix + "colors");

            memset(homeTheme.customModes[i].name, 0, sizeof(homeTheme.customModes[i].name));
            strncpy(homeTheme.customModes[i].name, cname.c_str(), sizeof(homeTheme.customModes[i].name) - 1);
            homeTheme.customModes[i].fx = (cfx >= 0 && cfx < HOME_FX_COUNT) ? (HomeEffect)cfx : HOME_FX_BREATHE;
            homeTheme.customModes[i].speed = (cspd >= 1 && cspd <= 5) ? cspd : 3;

            uint8_t colorIdx = 0;
            int start = 0;
            while (start < (int)ccolors.length() && colorIdx < 4) {
                int comma = ccolors.indexOf(',', start);
                String chex = (comma >= 0) ? ccolors.substring(start, comma) : ccolors.substring(start);
                chex.trim();
                if (chex.length() == 6) {
                    uint32_t cv = strtoul(chex.c_str(), nullptr, 16);
                    homeTheme.customModes[i].colors[colorIdx][0] = (cv >> 16) & 0xFF;
                    homeTheme.customModes[i].colors[colorIdx][1] = (cv >> 8) & 0xFF;
                    homeTheme.customModes[i].colors[colorIdx][2] = cv & 0xFF;
                    colorIdx++;
                }
                if (comma < 0) break;
                start = comma + 1;
            }
            homeTheme.customModes[i].colorCount = (colorIdx > 0) ? colorIdx : 1;
        }
    }
    homeTheme.clearPreview();
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

void WebPortal::handleSdBackup() {
    if (!isAuthenticated() || !checkCsrf()) {
        sendError(403, "Session expired. Reload the page.");
        return;
    }

    if (!sdVault.isMounted() && !sdVault.begin()) {
        sendError(400, "No microSD card detected.");
        return;
    }

    if (!_wallet || !_wallet->hasSeed()) {
        sendError(400, "No wallet seed to back up.");
        return;
    }

    String pin = _server->arg("pin");
    String pass = _server->arg("passphrase");
    String pass2 = _server->arg("passphrase_confirm");

    if (pass != pass2) {
        wipe(pin); wipe(pass); wipe(pass2);
        sendError(400, "Passphrases do not match.");
        return;
    }

    bool validAscii = pass.length() >= SD_VAULT_MIN_PASSPHRASE_LEN && pass.length() <= 128;
    for (size_t i = 0; i < pass.length(); i++) {
        if (pass[i] < 32 || pass[i] > 126) { validAscii = false; break; }
    }
    if (!validAscii) {
        wipe(pin); wipe(pass); wipe(pass2);
        sendError(400, "Passphrase must be at least 12 printable characters.");
        return;
    }

    PinVault::Result pRes = PinVault::check(pin.c_str());
    wipe(pin);

    if (pRes == PinVault::DURESS) {
        wipe(pass); wipe(pass2);
        _emergencyRequested = true;
        _emergencyTrigger = TRIG_DURESS_PIN;
        sendError(401, "Wrong PIN.");
        return;
    }
    if (pRes == PinVault::LOCKED_OUT) {
        wipe(pass); wipe(pass2);
        _emergencyRequested = true;
        _emergencyTrigger = TRIG_PIN_LOCKOUT;
        sendError(401, "Device locked out.");
        return;
    }
    if (pRes != PinVault::OK) {
        wipe(pass); wipe(pass2);
        sendError(401, "Wrong PIN.");
        return;
    }

    PortalCpuBoost boost;
    if (!_wallet->unlock()) {
        wipe(pass); wipe(pass2);
        sendError(500, "Failed to access wallet seed.");
        return;
    }

    const char* mnemonic = _wallet->getMnemonicPhrase();
    bool ok = false;
    if (mnemonic && strlen(mnemonic) > 0) {
        ok = sdVault.backupSeedV2(mnemonic, pass.c_str());
    }
    _wallet->lock();
    wipe(pass); wipe(pass2);

    if (!ok) {
        sendError(500, "Failed to write encrypted backup to SD card.");
        return;
    }

    sendJson(200, "{\"ok\":true}");
}

void WebPortal::handleSdRestore() {
    if (!isAuthenticated() || !checkCsrf()) {
        sendError(403, "Session expired. Reload the page.");
        return;
    }

    if (!sdVault.isMounted() && !sdVault.begin()) {
        sendError(400, "No microSD card detected.");
        return;
    }

    if (!sdVault.hasSeedBackup()) {
        sendError(400, "No backup file found on microSD card.");
        return;
    }

    if (_wallet && _wallet->hasSeed()) {
        String replaceArg = _server->arg("replace");
        if (replaceArg != "1") {
            sendError(400, "Replacement confirmation required.");
            return;
        }

        String pin = _server->arg("pin");
        PinVault::Result pRes = PinVault::check(pin.c_str());
        wipe(pin);

        if (pRes == PinVault::DURESS) {
            _emergencyRequested = true;
            _emergencyTrigger = TRIG_DURESS_PIN;
            sendError(401, "Wrong PIN.");
            return;
        }
        if (pRes == PinVault::LOCKED_OUT) {
            _emergencyRequested = true;
            _emergencyTrigger = TRIG_PIN_LOCKOUT;
            sendError(401, "Device locked out.");
            return;
        }
        if (pRes != PinVault::OK) {
            sendError(401, "Wrong PIN.");
            return;
        }
    }

    String pass = _server->arg("passphrase");
    if (pass.length() == 0) {
        wipe(pass);
        sendError(400, "Passphrase is required.");
        return;
    }

    PortalCpuBoost boost;
    char mnemonicBuf[256];
    memset(mnemonicBuf, 0, sizeof(mnemonicBuf));
    bool restored = sdVault.restoreSeed(mnemonicBuf, sizeof(mnemonicBuf), pass.c_str());
    wipe(pass);

    if (!restored) {
        mbedtls_platform_zeroize(mnemonicBuf, sizeof(mnemonicBuf));
        sendError(400, "Failed to decrypt backup. Incorrect passphrase or corrupted backup.");
        return;
    }

    if (!CryptoWallet::isValidMnemonic(mnemonicBuf)) {
        mbedtls_platform_zeroize(mnemonicBuf, sizeof(mnemonicBuf));
        sendError(500, "Decrypted seed is not a valid BIP-39 mnemonic.");
        return;
    }

    bool installed = false;
    if (_wallet) {
        installed = _wallet->setMnemonic(mnemonicBuf);
    }
    mbedtls_platform_zeroize(mnemonicBuf, sizeof(mnemonicBuf));

    if (!installed) {
        sendError(500, "Failed to save restored wallet.");
        return;
    }

    sendJson(200, "{\"ok\":true}");
}

static void secureWipeFile(const char* path) {
    if (!SD_MMC.exists(path)) return;
    File f = SD_MMC.open(path, "r+");
    if (f) {
        size_t sz = f.size();
        if (sz > 0) {
            uint8_t buf[128];
            // Pass 1: Cryptographic random bytes
            f.seek(0);
            size_t rem = sz;
            while (rem > 0) {
                size_t chunk = rem < sizeof(buf) ? rem : sizeof(buf);
                CryptoP256::secureRandom(buf, chunk);
                if (f.write(buf, chunk) != chunk) break;
                rem -= chunk;
            }
            f.flush();
            // Pass 2: Overwrite with zeros
            f.seek(0);
            memset(buf, 0, sizeof(buf));
            rem = sz;
            while (rem > 0) {
                size_t chunk = rem < sizeof(buf) ? rem : sizeof(buf);
                if (f.write(buf, chunk) != chunk) break;
                rem -= chunk;
            }
            f.flush();
        }
        f.close();
    }
    SD_MMC.remove(path);
}

static void wipePsbtDir(const char* dirPath) {
    File dir = (dirPath && dirPath[0]) ? SD_MMC.open(dirPath) : SD_MMC.open("/");
    if (!dir || !dir.isDirectory()) return;
    File file = dir.openNextFile();
    while (file) {
        const char* name = file.name();
        bool isDir = file.isDirectory();
        String fullPath;
        if (name && name[0] == '/') {
            fullPath = name;
        } else if (dirPath && dirPath[0]) {
            fullPath = String(dirPath) + "/" + (name ? name : "");
        } else {
            fullPath = "/" + String(name ? name : "");
        }
        file.close();
        if (!isDir && (fullPath.endsWith(".psbt") || fullPath.endsWith(".PSBT"))) {
            secureWipeFile(fullPath.c_str());
        }
        file = dir.openNextFile();
    }
    dir.close();
}

void WebPortal::handleSdFullBackup() {
    if (!isAuthenticated() || !checkCsrf()) {
        sendError(403, "Session expired. Reload the page.");
        return;
    }

    if (!sdVault.isMounted() && !sdVault.begin()) {
        sendError(400, "No microSD card detected.");
        return;
    }

    if (!hasSetupPassword()) {
        sendError(400, "Set a setup password before creating a full backup.");
        return;
    }

    String pass = _server->arg("setup_pass");
    if (pass.length() == 0 || pass.length() > 128) {
        wipe(pass);
        sendError(400, "Current setup password required.");
        return;
    }

    if (!verifySetupPassword(pass)) {
        wipe(pass);
        sendError(401, "Wrong setup password.");
        return;
    }

    PortalCpuBoost boost;
    bool ok = sdVault.backupFull(pass.c_str());
    wipe(pass);

    if (!ok) {
        sendError(500, "Failed to write full backup to SD card.");
        return;
    }

    sendJson(200, "{\"ok\":true}");
}

void WebPortal::handleSdFullRestore() {
    if (!isAuthenticated() || !checkCsrf()) {
        sendError(403, "Session expired. Reload the page.");
        return;
    }

    if (!sdVault.isMounted() && !sdVault.begin()) {
        sendError(400, "No microSD card detected.");
        return;
    }

    if (!sdVault.hasFullBackup()) {
        sendError(400, "No full backup found on microSD card (/vault/full.tkb).");
        return;
    }

    String pass = _server->arg("setup_pass");
    if (pass.length() == 0 || pass.length() > 128) {
        wipe(pass);
        sendError(400, "Backup setup password required.");
        return;
    }

    PortalCpuBoost boost;
    bool ok = sdVault.restoreFull(pass.c_str());
    wipe(pass);

    if (!ok) {
        sendError(400, "Failed to restore backup. Incorrect setup password or corrupted file.");
        return;
    }

    _rebootRequested = true;
    sendJson(200, "{\"ok\":true,\"rebooting\":true}");
}

void WebPortal::handleSdErase() {
    if (!isAuthenticated() || !checkCsrf()) {
        sendError(403, "Session expired. Reload the page.");
        return;
    }

    if (!sdVault.isMounted() && !sdVault.begin()) {
        sendError(400, "No microSD card detected.");
        return;
    }

    String confirm = _server->arg("confirm");
    if (confirm != "ERASE") {
        sendError(400, "Type ERASE in uppercase to confirm.");
        return;
    }

    // Pass 1: wipe standard seed and passkey files via engine
    sdVault.wipeVault();

    // Pass 2: overwrite and delete full backup & temp files in /vault
    const char* extraFiles[] = {
        SD_VAULT_FULL_FILE,
        "/vault/full.tkb.tmp",
        "/vault/full.tkb.old",
        SD_VAULT_TMP_FILE,
        SD_VAULT_OLD_FILE
    };
    for (size_t i = 0; i < sizeof(extraFiles) / sizeof(extraFiles[0]); i++) {
        secureWipeFile(extraFiles[i]);
    }

    // Pass 3: sweep any remaining files in /vault
    File vdir = SD_MMC.open(SD_VAULT_DIR);
    if (vdir && vdir.isDirectory()) {
        File f = vdir.openNextFile();
        while (f) {
            const char* name = f.name();
            bool isDir = f.isDirectory();
            String path;
            if (name && name[0] == '/') {
                path = name;
            } else {
                path = String(SD_VAULT_DIR) + "/" + (name ? name : "");
            }
            f.close();
            if (!isDir) {
                secureWipeFile(path.c_str());
            }
            f = vdir.openNextFile();
        }
        vdir.close();
    }

    // Pass 4: scan and wipe all .psbt files in /psbt and root /
    wipePsbtDir("/psbt");
    wipePsbtDir("");

    sendJson(200, "{\"ok\":true}");
}
