/**
 * power_mgr.h — Thermal & Power Management Engine for Crypto TKey S3
 * ==============================================================================
 * Disables high-power RF radios (Wi-Fi, Bluetooth) to eliminate thermal
 * dissipation, manages dynamic 80MHz CPU frequency scaling, and handles
 * display backlight sleep timeouts.
 */

#pragma once

#include <Arduino.h>
#include <esp_wifi.h>
#include <esp_bt.h>
#include <esp_pm.h>

class PowerManager {
public:
    static void init();
    static void update(bool userActive);
    static void wakeDisplay();
    static void toggleDisplaySleep();
    static bool isDisplaySleeping();
    static void setKeepAwake(bool keepAwake);
    static bool getKeepAwake();

private:
    static uint32_t _lastActivityMs;
    static bool     _displaySleeping;
    static bool     _keepAwake;
};
