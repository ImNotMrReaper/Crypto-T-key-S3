/**
 * button_cadence.cpp — Implementation of High-Precision Single-Button Input
 */

#include "button_cadence.h"

void ButtonCadence::begin(uint8_t pin) {
    _pin = pin;
    pinMode(_pin, INPUT_PULLUP);
    _lastRawState = digitalRead(_pin);
    _stableState = _lastRawState;
    _pressStartTime = 0;
    _waitingForSecondTap = false;
    _firstTapReleaseTime = 0;
    _panicFired = false;
}

uint32_t ButtonCadence::currentHoldDuration() const {
    if (_stableState == LOW) {
        return millis() - _pressStartTime;
    }
    return 0;
}

uint8_t ButtonCadence::getHoldStage() const {
    if (_stableState != LOW) return 0;
    uint32_t dur = millis() - _pressStartTime;
    if (dur >= BTN_PANIC_HOLD_MS) return 3; // Duress
    if (dur >= BTN_VERY_LONG_MS)  return 2; // Reset / Back
    if (dur >= BTN_LONG_PRESS_MS) return 1; // Confirm
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
                _panicFired = false;
            } else {
                // Button Released Up! Evaluate hold duration at release
                uint32_t duration = now - _pressStartTime;

                if (_panicFired) {
                    // Panic was already fired while holding, ignore release
                    _waitingForSecondTap = false;
                } else if (duration >= BTN_VERY_LONG_MS) {
                    // Held > 2200ms = Reset PIN / Clear all
                    _waitingForSecondTap = false;
                    event = BTN_VERY_LONG_PRESS;
                } else if (duration >= BTN_LONG_PRESS_MS) {
                    // Held 600ms..2200ms = Confirm / Next
                    _waitingForSecondTap = false;
                    event = BTN_LONG_PRESS;
                } else if (duration >= BTN_DEBOUNCE_MS) {
                    // Quick tap (<600ms)
                    if (_waitingForSecondTap && (now - _firstTapReleaseTime <= BTN_DOUBLE_TAP_MS)) {
                        // Second tap received within 320ms -> DOUBLE CLICK!
                        _waitingForSecondTap = false;
                        event = BTN_DOUBLE_CLICK; // Delete / Backspace!
                    } else {
                        // First tap of a potential double-click
                        _waitingForSecondTap = true;
                        _firstTapReleaseTime = now;
                    }
                }
            }
        }
    }

    // While holding down, continuously check for emergency panic threshold
    if (_stableState == LOW && !_panicFired) {
        uint32_t holdTime = now - _pressStartTime;
        if (holdTime >= BTN_PANIC_HOLD_MS) {
            _panicFired = true;
            _waitingForSecondTap = false;
            return BTN_PANIC_HOLD;
        }
    }

    // Check if waiting for second tap has timed out (confirmed SINGLE TAP)
    if (_waitingForSecondTap && _stableState == HIGH) {
        if (now - _firstTapReleaseTime > BTN_DOUBLE_TAP_MS) {
            _waitingForSecondTap = false;
            event = BTN_SHORT_PRESS; // Single Tap confirmed!
        }
    }

    return event;
}
