/**
 * rgb_status.cpp — Implementation of WS2812 Status LED Effects
 */

#include "rgb_status.h"

void RgbStatus::begin(uint8_t pin, uint8_t count) {
    _pixel.updateType(NEO_GRB + NEO_KHZ800);
    _pixel.updateLength(count);
    _pixel.setPin(pin);
    _pixel.begin();
    _pixel.setBrightness(45); // Safe power level
    _pixel.clear();
    _pixel.show();
}

void RgbStatus::setMode(LedMode mode) {
    _currentMode = mode;
    _step = 0;
    _direction = true;
    update();
}

void RgbStatus::flashSuccess() {
    _flashUntil = millis() + 600;
}

void RgbStatus::update() {
    uint32_t now = millis();

    // Priority flash on success
    if (now < _flashUntil) {
        _pixel.setPixelColor(0, _pixel.Color(0, 255, 60));
        _pixel.show();
        return;
    }

    if (now - _lastUpdate < 20) return; // 50 Hz refresh
    _lastUpdate = now;

    switch (_currentMode) {
        case LED_MODE_SOLID_AMBER:
            _pixel.setPixelColor(0, _pixel.Color(255, 120, 0));
            break;

        case LED_MODE_BREATHE_CYAN: {
            // Smooth sine/triangle breathing effect (10 - 180 brightness)
            if (_direction) {
                _step += 3;
                if (_step >= 180) _direction = false;
            } else {
                if (_step > 15) _step -= 3;
                else _direction = true;
            }
            uint8_t val = (uint8_t)_step;
            _pixel.setPixelColor(0, _pixel.Color(0, val, val));
            break;
        }

        case LED_MODE_PULSE_GREEN: {
            // Rapid pulse awaiting confirmation
            if (_direction) {
                _step += 12;
                if (_step >= 240) _direction = false;
            } else {
                if (_step > 20) _step -= 12;
                else _direction = true;
            }
            uint8_t val = (uint8_t)_step;
            _pixel.setPixelColor(0, _pixel.Color(0, val, 20));
            break;
        }

        case LED_MODE_SOLID_BLUE:
            _pixel.setPixelColor(0, _pixel.Color(0, 80, 255));
            break;

        case LED_MODE_PULSE_PURPLE: {
            if (_direction) {
                _step += 5;
                if (_step >= 200) _direction = false;
            } else {
                if (_step > 20) _step -= 5;
                else _direction = true;
            }
            uint8_t val = (uint8_t)_step;
            _pixel.setPixelColor(0, _pixel.Color(val, 0, val));
            break;
        }

        case LED_MODE_STROBE_RED:
            _step++;
            if ((_step % 6) < 3) {
                _pixel.setPixelColor(0, _pixel.Color(255, 0, 0));
            } else {
                _pixel.setPixelColor(0, 0);
            }
            break;

        case LED_MODE_OFF:
        default:
            _pixel.setPixelColor(0, 0);
            break;
    }

    _pixel.show();
}
