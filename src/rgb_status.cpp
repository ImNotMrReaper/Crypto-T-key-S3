/**
 * rgb_status.cpp — Implementation of APA102-2020 DotStar Status LED
 * =================================================================
 * Hardware: LilyGo T-Dongle S3 on-board RGB LED
 * Pinout:   GPIO 40 = DI (Data), GPIO 39 = CI (Clock)
 * Color Order: BGR
 */

#include "rgb_status.h"

void RgbStatus::begin(uint8_t pinData, uint8_t pinClk) {
    _pinData = pinData;
    _pinClk = pinClk;

    pinMode(_pinData, OUTPUT);
    pinMode(_pinClk, OUTPUT);
    digitalWrite(_pinData, LOW);
    digitalWrite(_pinClk, LOW);

    // Initial clear
    setPixel(0, 0, 0, 0);
}

void RgbStatus::writeByte(uint8_t byte) {
    for (int8_t i = 7; i >= 0; i--) {
        digitalWrite(_pinData, (byte & (1 << i)) ? HIGH : LOW);
        digitalWrite(_pinClk, HIGH);
        digitalWrite(_pinClk, LOW);
    }
}

void RgbStatus::sendFrame(uint8_t r, uint8_t g, uint8_t b, uint8_t brightness) {
    // 1. Start frame: 32 zero bits
    for (uint8_t i = 0; i < 4; i++) {
        writeByte(0x00);
    }

    // 2. LED frame: 3 bits 1 (0xE0) + 5 bits global brightness (0..31)
    uint8_t brightHeader = 0xE0 | (brightness & 0x1F);
    writeByte(brightHeader);

    // 3. Color bytes in BGR order for LilyGo T-Dongle-S3
    writeByte(b);
    writeByte(g);
    writeByte(r);

    // 4. End frame: 32 one bits
    for (uint8_t i = 0; i < 4; i++) {
        writeByte(0xFF);
    }
    digitalWrite(_pinData, LOW);
}

void RgbStatus::setPixel(uint8_t r, uint8_t g, uint8_t b, uint8_t brightness) {
    sendFrame(r, g, b, brightness);
}

void RgbStatus::setMode(LedMode mode) {
    _currentMode = mode;
    _step = 0;
    _direction = true;
    _isFlashing = false;
    _isRainbowFlash = false;
    _lastUpdate = 0; // Force immediate update
    update();
}

RgbColor RgbStatus::wheel(uint8_t wheelPos) {
    wheelPos = 255 - wheelPos;
    if (wheelPos < 85) {
        return { (uint8_t)(255 - wheelPos * 3), 0, (uint8_t)(wheelPos * 3) };
    }
    if (wheelPos < 170) {
        wheelPos -= 85;
        return { 0, (uint8_t)(wheelPos * 3), (uint8_t)(255 - wheelPos * 3) };
    }
    wheelPos -= 170;
    return { (uint8_t)(wheelPos * 3), (uint8_t)(255 - wheelPos * 3), 0 };
}

RgbColor RgbStatus::getCoinRgb(const char* symbol) {
    if (!symbol) return { 0, 229, 255 }; // Cyan default
    if (strcmp(symbol, "BTC") == 0)  return { 255, 140, 0 };   // Bitcoin Gold/Orange
    if (strcmp(symbol, "ETH") == 0)  return { 138, 75, 255 };  // Ethereum Royal Violet
    if (strcmp(symbol, "SOL") == 0)  return { 20, 241, 149 };  // Solana Neon Turquoise
    if (strcmp(symbol, "DOGE") == 0) return { 255, 195, 15 };  // Doge Sunny Gold
    if (strcmp(symbol, "XMR") == 0)  return { 255, 80, 0 };    // Monero Flame Orange
    if (strcmp(symbol, "ADA") == 0)  return { 0, 90, 255 };    // Cardano Deep Blue
    if (strcmp(symbol, "XRP") == 0)  return { 0, 210, 255 };   // Ripple Ice Cyan
    if (strcmp(symbol, "USDT") == 0) return { 38, 161, 123 };  // Tether Mint Green
    return { 0, 229, 255 };
}

void RgbStatus::setCoinColor(uint8_t r, uint8_t g, uint8_t b) {
    _coinR = r;
    _coinG = g;
    _coinB = b;
    setMode(LED_MODE_COIN_GLOW);
}

void RgbStatus::flashSuccess() {
    _flashUntil = millis() + 600;
    _isFlashing = true;
    _isRainbowFlash = false;
    _flashR = 0; _flashG = 255; _flashB = 60;
    setPixel(0, 255, 60, 5);
}

void RgbStatus::flashRainbow(uint16_t durationMs) {
    _flashUntil = millis() + durationMs;
    _isFlashing = true;
    _isRainbowFlash = true;
}

void RgbStatus::flashTap(uint8_t r, uint8_t g, uint8_t b, uint16_t durationMs) {
    _flashUntil = millis() + durationMs;
    _isFlashing = true;
    _isRainbowFlash = false;
    _flashR = r; _flashG = g; _flashB = b;
    setPixel(r, g, b, 6);
}

void RgbStatus::flashDoubleTap(uint8_t r, uint8_t g, uint8_t b) {
    flashTap(r, g, b, 120);
}

void RgbStatus::setEntropyJitter(uint32_t jitterHash) {
    _step = (uint16_t)(jitterHash & 0xFF);
    RgbColor c = wheel((uint8_t)_step);
    flashTap(c.r, c.g, c.b, 90);
}

void RgbStatus::setHoldProgress(float progress0to1) {
    _holdProgress = progress0to1;
    if (_currentMode != LED_MODE_HOLD_RAMP) {
        _currentMode = LED_MODE_HOLD_RAMP;
    }
}

void RgbStatus::update() {
    uint32_t now = millis();

    // Priority flash handling (tactile tap, rainbow success, error strobe)
    if (_isFlashing) {
        if (now < _flashUntil) {
            if (_isRainbowFlash) {
                uint8_t pos = (uint8_t)((now * 2) & 0xFF);
                RgbColor c = wheel(pos);
                setPixel(c.r, c.g, c.b, 6);
            } else {
                setPixel(_flashR, _flashG, _flashB, 5);
            }
            return;
        } else {
            _isFlashing = false;
            _isRainbowFlash = false;
        }
    }

    if (now - _lastUpdate < 20) return; // 50 Hz smooth refresh
    _lastUpdate = now;

    switch (_currentMode) {
        case LED_MODE_SOLID_AMBER:
            // Crisp warm amber / gold (Locked / PIN entry)
            setPixel(255, 130, 0, 4);
            break;

        case LED_MODE_BREATHE_CYAN: {
            // Smooth Antigravity Cyan breathing effect
            if (_direction) {
                _step += 4;
                if (_step >= 220) _direction = false;
            } else {
                if (_step > 20) _step -= 4;
                else _direction = true;
            }
            uint8_t val = (uint8_t)_step;
            setPixel(0, val, val, 4);
            break;
        }

        case LED_MODE_PULSE_GREEN: {
            // Rhythmic heartbeat pulse awaiting biometric / FIDO2 User Presence confirmation
            _step = (_step + 8) % 360;
            // Pseudo sine wave breathing
            uint8_t val = (uint8_t)(128 + 120 * sin(_step * 3.14159f / 180.0f));
            setPixel(0, val > 30 ? val : 30, (uint8_t)(val * 0.3f), 5);
            break;
        }

        case LED_MODE_COIN_GLOW: {
            // Organic glowing brand color of the currently selected cryptocurrency
            if (_direction) {
                _step += 3;
                if (_step >= 240) _direction = false;
            } else {
                if (_step > 50) _step -= 3;
                else _direction = true;
            }
            float scale = (float)_step / 255.0f;
            uint8_t r = (uint8_t)(_coinR * scale);
            uint8_t g = (uint8_t)(_coinG * scale);
            uint8_t b = (uint8_t)(_coinB * scale);
            setPixel(r, g, b, 4);
            break;
        }

        case LED_MODE_SOFTAP_PULSE: {
            // Radiant Cyberpunk Violet / Neon Magenta pulse during SoftAP Captive Portal
            if (_direction) {
                _step += 6;
                if (_step >= 240) _direction = false;
            } else {
                if (_step > 30) _step -= 6;
                else _direction = true;
            }
            float scale = (float)_step / 255.0f;
            setPixel((uint8_t)(220 * scale), 0, (uint8_t)(200 * scale), 4);
            break;
        }

        case LED_MODE_RAINBOW_CYCLE: {
            _step = (_step + 2) & 0xFF;
            RgbColor c = wheel((uint8_t)_step);
            setPixel(c.r, c.g, c.b, 4);
            break;
        }

        case LED_MODE_HOLD_RAMP: {
            // Crescendo from ambient to brilliant emerald green as button is held
            float p = _holdProgress;
            if (p < 0.0f) p = 0.0f;
            if (p > 1.0f) p = 1.0f;
            uint8_t r = (uint8_t)(255 * (1.0f - p));
            uint8_t g = (uint8_t)(255 * p);
            uint8_t b = (uint8_t)(50 * (1.0f - p));
            setPixel(r, g, b, 5);
            break;
        }

        case LED_MODE_SOLID_BLUE:
            // Crisp sapphire blue (Air-Gap SD mode)
            setPixel(0, 80, 255, 4);
            break;

        case LED_MODE_PULSE_PURPLE: {
            // BLE companion pulse
            if (_direction) {
                _step += 6;
                if (_step >= 220) _direction = false;
            } else {
                if (_step > 25) _step -= 6;
                else _direction = true;
            }
            uint8_t val = (uint8_t)_step;
            setPixel(val, 0, val, 4);
            break;
        }

        case LED_MODE_STROBE_RED:
            _step++;
            if ((_step % 6) < 3) {
                setPixel(255, 0, 0, 7); // Full strobe red (Duress Wipe)
            } else {
                setPixel(0, 0, 0, 0);
            }
            break;

        case LED_MODE_FLASH_GREEN_OK:
            setPixel(0, 255, 50, 5);
            break;

        case LED_MODE_ENTROPY_CHURN: {
            _step = (_step + 5) & 0xFF;
            RgbColor c = wheel((uint8_t)_step);
            setPixel(c.r, c.g, c.b, 4);
            break;
        }

        case LED_MODE_OFF:
        default:
            setPixel(0, 0, 0, 0);
            break;
    }
}

