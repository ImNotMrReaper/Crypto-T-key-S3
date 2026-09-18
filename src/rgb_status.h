/**
 * rgb_status.h — Non-Blocking APA102-2020 DotStar Status LED Controller
 * ======================================================================
 * Hardware: LilyGo T-Dongle S3 on-board RGB LED
 * Pinout:   GPIO 40 = Data (DI), GPIO 39 = Clock (CI)
 * Protocol: 2-Wire SPI (BGR color order)
 */

#pragma once

#include <Arduino.h>
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
    void begin(uint8_t pinData = PIN_LED_DATA, uint8_t pinClk = PIN_LED_CLK);
    void setMode(LedMode mode);
    void update();
    void flashSuccess();
    void setPixel(uint8_t r, uint8_t g, uint8_t b, uint8_t brightness = 4);

private:
    void sendFrame(uint8_t r, uint8_t g, uint8_t b, uint8_t brightness);
    void writeByte(uint8_t byte);

    uint8_t _pinData = PIN_LED_DATA;
    uint8_t _pinClk = PIN_LED_CLK;
    LedMode _currentMode = LED_MODE_OFF;
    uint32_t _lastUpdate = 0;
    uint16_t _step = 0;
    bool _direction = true;
    uint32_t _flashUntil = 0;
};
