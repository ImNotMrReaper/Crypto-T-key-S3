/**
 * wifi_manager.cpp — Non-blocking multi-network Wi-Fi with USB-host / wall-power modes
 */

#include "wifi_manager.h"
#include "wall_ticker.h"
#include "USB.h"

void WifiManager::begin() {
    // Our networks live in "wifi_cfg"; the driver's own copy of every AP/STA config
    // ("nvs.net80211") only duplicated credentials and filled the 20 KB NVS partition.
    WiFi.persistent(false);
    Preferences drv;
    if (drv.begin("nvs.net80211", true)) {
        drv.end();
        drv.begin("nvs.net80211", false);
        drv.clear();
        drv.end();
    }
    loadFromNvs();
    _bootMs = millis();
    radioOff();  // decided in update(): off on a computer, bursts on wall power
    Serial.printf("[WIFI] %d saved network(s). Radio off until wall power is detected.\n", _savedCount);
}

void WifiManager::loadFromNvs() {
    _savedCount = 0;
    if (!_prefs.begin("wifi_cfg", true)) return;

    _savedCount = _prefs.getInt("count", 0);
    if (_savedCount > MAX_WIFI_NETWORKS) _savedCount = MAX_WIFI_NETWORKS;
    if (_savedCount < 0) _savedCount = 0;

    for (int i = 0; i < _savedCount; i++) {
        char keySsid[16], keyPass[16];
        snprintf(keySsid, sizeof(keySsid), "s_%d", i);
        snprintf(keyPass, sizeof(keyPass), "p_%d", i);
        memset(&_networks[i], 0, sizeof(_networks[i]));
        _prefs.getString(keySsid, _networks[i].ssid, sizeof(_networks[i].ssid));
        _prefs.getString(keyPass, _networks[i].pass, sizeof(_networks[i].pass));
    }
    _prefs.end();
}

void WifiManager::saveToNvs() {
    if (!_prefs.begin("wifi_cfg", false)) return;
    _prefs.putInt("count", _savedCount);
    for (int i = 0; i < _savedCount; i++) {
        char keySsid[16], keyPass[16];
        snprintf(keySsid, sizeof(keySsid), "s_%d", i);
        snprintf(keyPass, sizeof(keyPass), "p_%d", i);
        _prefs.putString(keySsid, _networks[i].ssid);
        _prefs.putString(keyPass, _networks[i].pass);
    }
    _prefs.end();
}

bool WifiManager::addNetwork(const char* ssid, const char* pass) {
    if (!ssid || strlen(ssid) == 0 || strlen(ssid) > 32) return false;
    if (pass && strlen(pass) > 64) return false;

    for (int i = 0; i < _savedCount; i++) {
        if (strcmp(_networks[i].ssid, ssid) == 0) {
            strncpy(_networks[i].pass, pass ? pass : "", sizeof(_networks[i].pass) - 1);
            saveToNvs();
            Serial.printf("[WIFI] Updated credentials for '%s'\n", ssid);
            return true;
        }
    }
    if (_savedCount >= MAX_WIFI_NETWORKS) {
        Serial.println("[WIFI] Maximum saved networks reached (10).");
        return false;
    }
    memset(&_networks[_savedCount], 0, sizeof(WifiCreds));
    strncpy(_networks[_savedCount].ssid, ssid, sizeof(_networks[_savedCount].ssid) - 1);
    strncpy(_networks[_savedCount].pass, pass ? pass : "", sizeof(_networks[_savedCount].pass) - 1);
    _savedCount++;
    saveToNvs();
    Serial.printf("[WIFI] Stored network '%s' (Total: %d)\n", ssid, _savedCount);
    return true;
}

bool WifiManager::removeNetwork(const char* ssid) {
    if (!ssid) return false;
    int foundIdx = -1;
    for (int i = 0; i < _savedCount; i++) {
        if (strcmp(_networks[i].ssid, ssid) == 0) {
            foundIdx = i;
            break;
        }
    }
    if (foundIdx < 0) return false;
    for (int i = foundIdx; i < _savedCount - 1; i++) _networks[i] = _networks[i + 1];
    _savedCount--;
    saveToNvs();
    Serial.printf("[WIFI] Removed network '%s'\n", ssid);
    return true;
}

const WifiCreds* WifiManager::getNetwork(int index) const {
    if (index >= 0 && index < _savedCount) return &_networks[index];
    return nullptr;
}

void WifiManager::setPortalActive(bool active) {
    _portalActive = active;
    if (active) _phase = PHASE_OFF;  // the portal reconfigures the radio as an access point
}

void WifiManager::radioOff() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    _phase = PHASE_OFF;
}

void WifiManager::startScan() {
    WiFi.mode(WIFI_STA);
    WiFi.setTxPower(WIFI_POWER_8_5dBm);   // low TX power: less heat in the enclosed dongle
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    WiFi.scanNetworks(true, false, false, 300);  // async
    _phase = PHASE_SCANNING;
    _phaseStart = millis();
}

void WifiManager::connectNextCandidate() {
    if (_candIdx >= _candCount) {
        Serial.println("[WIFI] No saved network reachable this burst");
        radioOff();
        _lastBurst = millis();
        return;
    }
    const WifiCreds& n = _networks[_candidates[_candIdx++]];
    Serial.printf("[WIFI] Joining '%s'...\n", n.ssid);
    WiFi.begin(n.ssid, n.pass);
    _phase = PHASE_CONNECTING;
    _phaseStart = millis();
}

void WifiManager::update() {
    WallTicker::applyResults();
    if (_portalActive) return;

    uint32_t now = millis();
    bool usbHost = (bool)USB;  // true once a computer has enumerated us

    if (usbHost && !_forceWall) {
        if (_phase != PHASE_OFF || WiFi.getMode() != WIFI_OFF) {
            Serial.println("[WIFI] USB host detected: radio off (prices arrive over USB)");
            radioOff();
        }
        _wallMode = false;
        return;
    }
    if (now - _bootMs < WIFI_HOST_GRACE_MS) return;  // give USB a chance to enumerate
    _wallMode = true;
    if (_savedCount == 0) return;

    switch (_phase) {
        case PHASE_OFF:
            if (_lastBurst == 0 || now - _lastBurst >= WIFI_BURST_PERIOD_MS) startScan();
            break;

        case PHASE_SCANNING: {
            int n = WiFi.scanComplete();
            if (n == WIFI_SCAN_RUNNING) {
                if (now - _phaseStart > 10000) {
                    WiFi.scanDelete();
                    radioOff();
                    _lastBurst = now;
                }
                break;
            }
            // Rank saved networks that are in range by signal strength (strongest first)
            int32_t rssi[MAX_WIFI_NETWORKS];
            _candCount = 0;
            for (int s = 0; s < _savedCount; s++) {
                int32_t best = -1000;
                for (int i = 0; i < n; i++) {
                    if (WiFi.SSID(i) == _networks[s].ssid && WiFi.RSSI(i) > best) best = WiFi.RSSI(i);
                }
                if (best > -1000) {
                    int k = _candCount++;
                    while (k > 0 && rssi[k - 1] < best) {
                        rssi[k] = rssi[k - 1];
                        _candidates[k] = _candidates[k - 1];
                        k--;
                    }
                    rssi[k] = best;
                    _candidates[k] = s;
                }
            }
            WiFi.scanDelete();
            _candIdx = 0;
            connectNextCandidate();
            break;
        }

        case PHASE_CONNECTING:
            if (WiFi.status() == WL_CONNECTED) {
                bool withBalances = _lastBalance == 0 || now - _lastBalance >= WIFI_BALANCE_PERIOD_MS;
                if (withBalances) _lastBalance = now;
                Serial.printf("[WIFI] Online via '%s' (%d dBm): fetching%s\n", WiFi.SSID().c_str(),
                              WiFi.RSSI(), withBalances ? " prices + balances" : " prices");
                WallTicker::start(withBalances);
                _phase = PHASE_FETCHING;
                _phaseStart = now;
            } else if (now - _phaseStart > 12000) {
                WiFi.disconnect(true);
                connectNextCandidate();
            }
            break;

        case PHASE_FETCHING:
            if (!WallTicker::running() || now - _phaseStart > 45000) {
                radioOff();          // duty cycle: RF only for the few seconds of each burst
                _lastBurst = now;
            }
            break;
    }
}

bool WifiManager::isConnected() {
    return WiFi.status() == WL_CONNECTED;
}

const char* WifiManager::getConnectedSsid() {
    static char ssidBuf[33];
    if (isConnected()) {
        strncpy(ssidBuf, WiFi.SSID().c_str(), sizeof(ssidBuf) - 1);
        ssidBuf[sizeof(ssidBuf) - 1] = '\0';
        return ssidBuf;
    }
    return _wallMode ? "WALL" : "USB";
}

int8_t WifiManager::getRssi() {
    return isConnected() ? WiFi.RSSI() : 0;
}
