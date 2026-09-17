/**
 * button_cadence.cpp — Implementation of Single-Button Input
 */

#include "button_cadence.h"

void ButtonCadence::begin(uint8_t pin) {
    _pin = pin;
    pinMode(_pin, INPUT_PULLUP);
    _lastRawState = digitalRead(_pin);
    _stableState = _lastRawState;
    _pressStartTime = 0;
    _lastReleaseTime = 0;
    _clickCount = 0;
    _longReported = false;
    _panicReported = false;
}

uint32_t ButtonCadence::currentHoldDuration() const {
    if (_stableState == LOW) {
        return millis() - _pressStartTime;
    }
    return 0;
}

ButtonEvent ButtonCadence::update() {
    uint32_t now = millis();
    bool rawState = digitalRead(_pin);
    ButtonEvent event = BTN_NONE;

    // Debounce
    if (rawState != _lastRawState) {
        _lastDebounceTime = now;
        _lastRawState = rawState;
    }

    if ((now - _lastDebounceTime) > BTN_DEBOUNCE_MS) {
        if (rawState != _stableState) {
            _stableState = rawState;

            if (_stableState == LOW) {
                // Button Pressed Down
                _pressStartTime = now;
                _longReported = false;
                _panicReported = false;
            } else {
                // Button Released Up
                uint32_t duration = now - _pressStartTime;
                _lastReleaseTime = now;

                if (!_longReported && !_panicReported) {
                    _clickCount++;
                }
            }
        }
    }

    // Check for active hold conditions while button is held DOWN
    if (_stableState == LOW) {
        uint32_t holdTime = now - _pressStartTime;
        if (holdTime >= BTN_PANIC_HOLD_MS && !_panicReported) {
            _panicReported = true;
            _longReported = true;
            _clickCount = 0;
            return BTN_PANIC_HOLD;
        } else if (holdTime >= BTN_LONG_PRESS_MS && !_longReported && !_panicReported) {
            _longReported = true;
            _clickCount = 0;
            return BTN_LONG_PRESS;
        }
    }

    // Check clicks when button is UP
    if (_stableState == HIGH && _clickCount > 0) {
        if (_clickCount >= 2) {
            _clickCount = 0;
            return BTN_DOUBLE_CLICK;
        }
        if ((now - _lastReleaseTime) > BTN_DOUBLE_CLICK_MS) {
            _clickCount = 0;
            return BTN_SHORT_PRESS;
        }
    }

    return event;
}
