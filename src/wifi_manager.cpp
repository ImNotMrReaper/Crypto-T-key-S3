/**
 * wifi_manager.cpp — Implementation of Persistent Multi-Network Wi-Fi Engine
 */

#include "wifi_manager.h"

void WifiManager::begin() {
    loadFromNvs();
    WiFi.mode(WIFI_STA);
    WiFi.setTxPower(WIFI_POWER_8_5dBm); // Low TX power to prevent heat dissipation
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM); // Automatic modem sleep between beacons
    
    if (_savedCount > 0) {
        connectBest();
    } else {
        WiFi.mode(WIFI_OFF);
    }
    Serial.printf("[WIFI] Initialized. %d saved networks in flash memory.\n", _savedCount);
}

void WifiManager::loadFromNvs() {
    _savedCount = 0;
    if (!_prefs.begin("wifi_cfg", true)) return;

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
        Serial.println("[WIFI] Maximum saved networks reached (10).");
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

    for (int i = foundIdx; i < _savedCount - 1; i++) {
        _networks[i] = _networks[i + 1];
    }
    _savedCount--;
    saveToNvs();
    Serial.printf("[WIFI] Removed network '%s'\n", ssid);
    return true;
}

const WifiCreds* WifiManager::getNetwork(int index) const {
    if (index >= 0 && index < _savedCount) return &_networks[index];
    return nullptr;
}

bool WifiManager::connectBest() {
    if (_savedCount == 0) return false;

    WiFi.mode(WIFI_STA);
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);

    // Fast synchronous scan
    Serial.println("[WIFI] Scanning for known networks...");
    int n = WiFi.scanNetworks(false, false, false, 300);
    int bestSavedIdx = -1;
    int bestRssi = -120;

    for (int i = 0; i < n; i++) {
        String foundSsid = WiFi.SSID(i);
        int32_t foundRssi = WiFi.RSSI(i);

        for (int s = 0; s < _savedCount; s++) {
            if (foundSsid.equals(_networks[s].ssid)) {
                if (foundRssi > bestRssi) {
                    bestRssi = foundRssi;
                    bestSavedIdx = s;
                }
            }
        }
    }

    WiFi.scanDelete();

    if (bestSavedIdx >= 0) {
        Serial.printf("[WIFI] Connecting to '%s' (%d dBm)...\n", _networks[bestSavedIdx].ssid, bestRssi);
        WiFi.begin(_networks[bestSavedIdx].ssid, _networks[bestSavedIdx].pass);
        _connecting = true;
        return true;
    } else {
        // Try first saved network as fallback
        Serial.printf("[WIFI] Trying fallback to '%s'...\n", _networks[0].ssid);
        WiFi.begin(_networks[0].ssid, _networks[0].pass);
        _connecting = true;
        return true;
    }
}

void WifiManager::disconnect() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    _connecting = false;
}

bool WifiManager::isConnected() {
    return (WiFi.status() == WL_CONNECTED);
}

String WifiManager::getIp() {
    if (isConnected()) return WiFi.localIP().toString();
    return "0.0.0.0";
}

String WifiManager::getSsid() {
    if (isConnected()) return WiFi.SSID();
    return "Disconnected";
}

int8_t WifiManager::getRssi() {
    if (isConnected()) return WiFi.RSSI();
    return 0;
}

void WifiManager::update() {
    // Periodic reconnection check every 30 seconds if disconnected
    if (_savedCount > 0 && !isConnected() && (millis() - _lastCheck > 30000)) {
        _lastCheck = millis();
        connectBest();
    }
}
