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
    LED_MODE_BREATHE_GREEN,    // Passkey Hub Armed / Ready
    LED_MODE_PULSE_GREEN,      // User Presence Auth / Sign Request
    LED_MODE_FLASH_GREEN_OK,   // Action Confirmed
    LED_MODE_SOLID_BLUE,       // Air-Gapped SD Signer
    LED_MODE_PULSE_PURPLE,     // BLE Companion Mode
    LED_MODE_STROBE_RED,       // Duress Wipe / Emergency
    LED_MODE_COIN_GLOW,        // Glowing / Breathing Brand Color of Active Cryptocurrency
    LED_MODE_SOFTAP_PULSE,     // Neon Magenta / Violet SoftAP Captive Portal Broadcast
    LED_MODE_RAINBOW_CYCLE,    // Smooth Multi-Color Rainbow Wave
    LED_MODE_ENTROPY_CHURN,    // Dynamic Seed Entropy Jitter Sparkles
    LED_MODE_HOLD_RAMP         // Button Hold Crescendo
};

struct RgbColor {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

class RgbStatus {
public:
    void begin(uint8_t pinData = PIN_LED_DATA, uint8_t pinClk = PIN_LED_CLK);
    void setMode(LedMode mode);
    void update();
    void flashSuccess();
    void flashRainbow(uint16_t durationMs = 750);
    void flashTap(uint8_t r = 200, uint8_t g = 255, uint8_t b = 255, uint16_t durationMs = 60);
    void flashDoubleTap(uint8_t r = 255, uint8_t g = 140, uint8_t b = 0);
    void setCoinColor(uint8_t r, uint8_t g, uint8_t b);
    void setEntropyJitter(uint32_t jitterHash);
    void setHoldProgress(float progress0to1);
    void setPixel(uint8_t r, uint8_t g, uint8_t b, uint8_t brightness = 4);

    static RgbColor getCoinRgb(const char* symbol);

private:
    void sendFrame(uint8_t r, uint8_t g, uint8_t b, uint8_t brightness);
    void writeByte(uint8_t byte);
    static RgbColor wheel(uint8_t wheelPos);

    uint8_t _pinData = PIN_LED_DATA;
    uint8_t _pinClk = PIN_LED_CLK;
    LedMode _currentMode = LED_MODE_OFF;
    uint32_t _lastUpdate = 0;
    uint16_t _step = 0;
    bool _direction = true;
    uint32_t _flashUntil = 0;

    // Transient override colors
    bool _isFlashing = false;
    bool _isRainbowFlash = false;
    uint8_t _flashR = 0;
    uint8_t _flashG = 0;
    uint8_t _flashB = 0;

    // Dynamic Coin Brand Color
    uint8_t _coinR = 0;
    uint8_t _coinG = 229;
    uint8_t _coinB = 255;

    // Hold progress (0.0 to 1.0)
    float _holdProgress = 0.0f;
};

