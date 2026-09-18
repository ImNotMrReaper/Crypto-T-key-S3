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
    _lastUpdate = 0; // Force immediate update
    update();
}

void RgbStatus::flashSuccess() {
    _flashUntil = millis() + 600;
    setPixel(0, 255, 60, 5);
}

void RgbStatus::update() {
    uint32_t now = millis();

    // Priority flash on success
    if (now < _flashUntil) {
        setPixel(0, 255, 60, 5);
        return;
    }

    if (now - _lastUpdate < 20) return; // 50 Hz refresh
    _lastUpdate = now;

    switch (_currentMode) {
        case LED_MODE_SOLID_AMBER:
            // Crisp warm amber / gold (Locked / PIN entry)
            setPixel(255, 130, 0, 4);
            break;

        case LED_MODE_BREATHE_CYAN: {
            // Smooth sine/triangle breathing effect (Cyan: R=0, G=val, B=val)
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
            // Rapid pulse awaiting confirmation
            if (_direction) {
                _step += 14;
                if (_step >= 250) _direction = false;
            } else {
                if (_step > 25) _step -= 14;
                else _direction = true;
            }
            uint8_t val = (uint8_t)_step;
            setPixel(0, val, 25, 4);
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
                setPixel(255, 0, 0, 5); // Full strobe red
            } else {
                setPixel(0, 0, 0, 0);
            }
            break;

        case LED_MODE_FLASH_GREEN_OK:
            setPixel(0, 255, 50, 5);
            break;

        case LED_MODE_OFF:
        default:
            setPixel(0, 0, 0, 0);
            break;
    }
}
