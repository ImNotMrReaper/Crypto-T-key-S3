/**
 * power_mgr.cpp — Implementation of Thermal & Power Management
 */

#include "power_mgr.h"
#include "config.h"

uint32_t PowerManager::_lastActivityMs = 0;
bool     PowerManager::_displaySleeping = false;
bool     PowerManager::_keepAwake = false;

void PowerManager::init() {
    // 1. Throttle CPU clock to cool 80MHz (drops thermal load by >60% while
    //    retaining full cryptographic hardware acceleration for NIST P-256 / SHA-256)
    setCpuFrequencyMhz(80);

    // 2. Explicitly cut power to Wi-Fi RF power amplifiers
    esp_wifi_stop();
    esp_wifi_deinit();

    // 3. Explicitly disable Bluetooth controller and power domains
    esp_bt_controller_disable();
    esp_bt_controller_deinit();

    _lastActivityMs = millis();
    _displaySleeping = false;
    _keepAwake = false;

    // Ensure display backlight is turned ON initially (Active LOW on GPIO 38)
    pinMode(PIN_TFT_BL, OUTPUT);
    digitalWrite(PIN_TFT_BL, TFT_BL_ON);
}

void PowerManager::setKeepAwake(bool keepAwake) {
    _keepAwake = keepAwake;
    if (_keepAwake && _displaySleeping) {
        wakeDisplay();
    }
}

bool PowerManager::getKeepAwake() {
    return _keepAwake;
}

void PowerManager::update(bool userActive) {
    uint32_t now = millis();

    if (userActive || _keepAwake) {
        _lastActivityMs = now;
        if (_displaySleeping) {
            wakeDisplay();
        }
    }

    // Auto-dim / sleep display after DISPLAY_SLEEP_TIMEOUT_MS of inactivity
    // strictly skipped if _keepAwake is enabled (e.g. on live price ticker)
    if (!_keepAwake && !_displaySleeping && (now - _lastActivityMs > DISPLAY_SLEEP_TIMEOUT_MS)) {
        digitalWrite(PIN_TFT_BL, TFT_BL_OFF); // Active LOW: HIGH = OFF
        _displaySleeping = true;
    }
}

void PowerManager::wakeDisplay() {
    digitalWrite(PIN_TFT_BL, TFT_BL_ON); // Active LOW: LOW = ON
    _displaySleeping = false;
    _lastActivityMs = millis();
}

void PowerManager::toggleDisplaySleep() {
    if (_displaySleeping) {
        wakeDisplay();
    } else {
        _displaySleeping = true;
        digitalWrite(PIN_TFT_BL, TFT_BL_OFF);
    }
}

bool PowerManager::isDisplaySleeping() {
    return _displaySleeping;
}
