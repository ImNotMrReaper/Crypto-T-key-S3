/**
 * wifi_manager.cpp — Implementation of Persistent Multi-Network Wi-Fi Engine
 */

#include "wifi_manager.h"

void WifiManager::begin() {
    loadFromNvs();
    WiFi.mode(WIFI_OFF);
    Serial.printf("[WIFI] Initialized. %d saved networks in flash memory.\n", _savedCount);
}

void WifiManager::loadFromNvs() {
    _savedCount = 0;
    if (!_prefs.begin("wifi_cfg", true)) { // Read-only
        return;
    }

    _savedCount = _prefs.getInt("count", 0);
    if (_savedCount > MAX_WIFI_NETWORKS) _savedCount = MAX_WIFI_NETWORKS;

    for (int i = 0; i < _savedCount; i++) {
        char keySsid[16], keyPass[16];
        snprintf(keySsid, sizeof(keySsid), "s_%d", i);
        snprintf(keyPass, sizeof(keyPass), "p_%d", i);

        String s = _prefs.getString(keySsid, "");
        String p = _prefs.getString(keyPass, "");

        strncpy(_networks[i].ssid, s.c_str(), sizeof(_networks[i].ssid) - 1);
        strncpy(_networks[i].pass, p.c_str(), sizeof(_networks[i].pass) - 1);
    }

    _prefs.end();
}

void WifiManager::saveToNvs() {
    if (!_prefs.begin("wifi_cfg", false)) { // Read-write
        return;
    }

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
    if (!ssid || strlen(ssid) == 0) return false;

    // Check if network already exists, update password if so
    for (int i = 0; i < _savedCount; i++) {
        if (strcmp(_networks[i].ssid, ssid) == 0) {
            strncpy(_networks[i].pass, pass ? pass : "", sizeof(_networks[i].pass) - 1);
            saveToNvs();
            Serial.printf("[WIFI] Updated credentials for '%s'\n", ssid);
            connectBest();
            return true;
        }
    }

    if (_savedCount >= MAX_WIFI_NETWORKS) {
        Serial.println("[WIFI] Maximum saved networks reached (10). Remove one first.");
        return false;
    }

    strncpy(_networks[_savedCount].ssid, ssid, sizeof(_networks[_savedCount].ssid) - 1);
    strncpy(_networks[_savedCount].pass, pass ? pass : "", sizeof(_networks[_savedCount].pass) - 1);
    _savedCount++;
    saveToNvs();

    Serial.printf("[WIFI] Stored new network '%s' (Total: %d)\n", ssid, _savedCount);
    connectBest();
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

    // Shift remaining
    for (int i = foundIdx; i < _savedCount - 1; i++) {
        _networks[i] = _networks[i + 1];
    }
    _savedCount--;
    saveToNvs();

    Serial.printf("[WIFI] Removed network '%s' (Remaining: %d)\n", ssid, _savedCount);
    return true;
}

void WifiManager::clearAll() {
    _savedCount = 0;
    if (_prefs.begin("wifi_cfg", false)) {
        _prefs.clear();
        _prefs.end();
    }
    WiFi.disconnect(true);
    Serial.println("[WIFI] All stored networks erased from flash memory.");
}

const char* WifiManager::getSavedSsid(int index) const {
    if (index >= 0 && index < _savedCount) {
        return _networks[index].ssid;
    }
    return "";
}

int WifiManager::scanNetworks() {
    Serial.println("[WIFI] Scanning nearby 2.4GHz Wi-Fi networks...");
    WiFi.mode(WIFI_STA);
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
    int n = WiFi.scanNetworks(false, true); // synchronous, show hidden
    Serial.printf("[WIFI] Scan complete. Found %d networks.\n", n);
    for (int i = 0; i < n; i++) {
        Serial.printf("  [%02d] %-20s (RSSI: %3d dBm, Ch: %2d, Enc: %s)\n",
            i + 1,
            WiFi.SSID(i).c_str(),
            WiFi.RSSI(i),
            WiFi.channel(i),
            (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "Open" : "Secured"
        );
    }
    return n;
}

bool WifiManager::connectBest() {
    if (_savedCount == 0) {
        Serial.println("[WIFI] No saved networks to connect to.");
        return false;
    }

    Serial.println("[WIFI] Scanning for known networks in range...");
    int n = WiFi.scanNetworks();
    if (n <= 0) {
        Serial.println("[WIFI] No Wi-Fi networks found in range.");
        return false;
    }

    int bestSavedIdx = -1;
    int bestRssi = -999;

    for (int i = 0; i < n; i++) {
        String visibleSsid = WiFi.SSID(i);
        int visibleRssi = WiFi.RSSI(i);

        for (int j = 0; j < _savedCount; j++) {
            if (visibleSsid == _networks[j].ssid) {
                if (visibleRssi > bestRssi) {
                    bestRssi = visibleRssi;
                    bestSavedIdx = j;
                }
            }
        }
    }

    if (bestSavedIdx >= 0) {
        Serial.printf("[WIFI] Connecting to best known network: '%s' (RSSI: %d dBm)...\n",
                      _networks[bestSavedIdx].ssid, bestRssi);
        return connectTo(_networks[bestSavedIdx].ssid, _networks[bestSavedIdx].pass);
    }

    Serial.println("[WIFI] None of the saved networks are currently in range.");
    return false;
}

bool WifiManager::connectTo(const char* ssid, const char* pass) {
    if (!ssid) return false;
    WiFi.mode(WIFI_STA);
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
    WiFi.disconnect();
    delay(50);
    WiFi.begin(ssid, pass);
    _isConnecting = true;
    _connectStartTime = millis();
    return true;
}

void WifiManager::disconnect() {
    WiFi.disconnect();
    _isConnecting = false;
    Serial.println("[WIFI] Disconnected.");
}

void WifiManager::update() {
    uint32_t now = millis();

    // Check connecting state timeout
    if (_isConnecting) {
        if (WiFi.status() == WL_CONNECTED) {
            _isConnecting = false;
            Serial.printf("[WIFI] Connected! SSID: %s | IP: %s | RSSI: %d dBm\n",
                          WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), WiFi.RSSI());
        } else if (now - _connectStartTime > 12000) {
            _isConnecting = false;
            Serial.println("[WIFI] Connection attempt timed out.");
        }
        return;
    }

    // If disconnected and has saved networks, periodically attempt to connect to known networks
    if (WiFi.status() != WL_CONNECTED && _savedCount > 0) {
        if (now - _lastScanAttempt > _scanInterval) {
            _lastScanAttempt = now;
            connectBest();
        }
    }
}
