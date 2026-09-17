/**
 * rgb_status.h — Non-Blocking WS2812 Status LED Controller
 * ==========================================================
 * Hardware: LilyGo T-Dongle S3 WS2812 on GPIO 40
 */

#pragma once

#include <Adafruit_NeoPixel.h>
#include "config.h"

enum LedMode {
    LED_MODE_OFF,
    LED_MODE_SOLID_AMBER,      // Locked / PIN entry
    LED_MODE_BREATHE_CYAN,     // Idle Dashboard / Ready
    LED_MODE_PULSE_GREEN,      // User Presence Auth / Sign Request
    LED_MODE_FLASH_GREEN_OK,   // Action Confirmed
    LED_MODE_SOLID_BLUE,       // Air-Gapped SD Signer
    LED_MODE_PULSE_PURPLE,     // BLE Companion Mode
    LED_MODE_STROBE_RED        // Duress Wipe
};

class RgbStatus {
public:
    void begin(uint8_t pin = PIN_LED, uint8_t count = LED_COUNT);
    void setMode(LedMode mode);
    void update();
    void flashSuccess();

private:
    Adafruit_NeoPixel _pixel;
    LedMode _currentMode = LED_MODE_OFF;
    uint32_t _lastUpdate = 0;
    uint16_t _step = 0;
    bool _direction = true;
    uint32_t _flashUntil = 0;
};
