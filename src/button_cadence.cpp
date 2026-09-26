/**
 * button_cadence.cpp — Implementation of High-Precision Single-Button Input
 */

#include "button_cadence.h"

void ButtonCadence::begin(uint8_t pin) {
    _pin = pin;
    pinMode(_pin, INPUT_PULLUP);
    _lastRawState = digitalRead(_pin);
    _stableState = _lastRawState;
    _lastDebounceTime = 0;
    _pressStartTime = 0;
    _waitingForSecondTap = false;
    _firstTapReleaseTime = 0;
    _isSecondTap = false;
    _panicFired = false;
    _pendingEvent = BTN_NONE;
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
    // 1. Drain pending event queue first (at most one event emitted per call)
    if (_pendingEvent != BTN_NONE) {
        ButtonEvent ev = _pendingEvent;
        _pendingEvent = BTN_NONE;
        return ev;
    }

    uint32_t now = millis();
    bool rawState = digitalRead(_pin);
    ButtonEvent event = BTN_NONE;

    // Debounce filter
    if (rawState != _lastRawState) {
        _lastDebounceTime = now;
        _lastRawState = rawState;
    }

    if ((now - _lastDebounceTime) > BTN_DEBOUNCE_MS) {
        if (rawState != _stableState) {
            _stableState = rawState;

            if (_stableState == LOW) {
                // Button Pressed Down
                _pressStartTime = _lastDebounceTime;
                _panicFired = false;

                // Double-tap window is measured from release #1 to PRESS #2
                if (_waitingForSecondTap) {
                    if (_pressStartTime - _firstTapReleaseTime <= BTN_DOUBLE_TAP_MS) {
                        _isSecondTap = true;
                    } else {
                        // Press occurred after double-tap window expired; flush tap #1
                        _isSecondTap = false;
                        event = BTN_SHORT_PRESS;
                    }
                    _waitingForSecondTap = false;
                } else {
                    _isSecondTap = false;
                }
            } else {
                // Button Released Up! Evaluate hold duration at release
                uint32_t duration = _lastDebounceTime - _pressStartTime;

                if (_panicFired) {
                    // Panic was already fired while holding, release emits nothing
                    _waitingForSecondTap = false;
                    _isSecondTap = false;
                } else if (duration >= BTN_VERY_LONG_MS) {
                    // Held >= 2200ms = Reset / Clear all
                    if (_isSecondTap) {
                        event = BTN_SHORT_PRESS;
                        _pendingEvent = BTN_VERY_LONG_PRESS;
                    } else {
                        event = BTN_VERY_LONG_PRESS;
                    }
                    _waitingForSecondTap = false;
                    _isSecondTap = false;
                } else if (duration >= BTN_LONG_PRESS_MS) {
                    // Held 600ms..2200ms = Confirm / Next
                    if (_isSecondTap) {
                        event = BTN_SHORT_PRESS;
                        _pendingEvent = BTN_LONG_PRESS;
                    } else {
                        event = BTN_LONG_PRESS;
                    }
                    _waitingForSecondTap = false;
                    _isSecondTap = false;
                } else if (duration >= BTN_DEBOUNCE_MS) {
                    // Quick tap (<600ms)
                    if (_isSecondTap) {
                        // Second tap started within double-tap window and ended as a quick tap
                        event = BTN_DOUBLE_CLICK;
                        _isSecondTap = false;
                        _waitingForSecondTap = false;
                    } else {
                        // First tap of a potential double click
                        _waitingForSecondTap = true;
                        _firstTapReleaseTime = _lastDebounceTime;
                    }
                } else {
                    _isSecondTap = false;
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
            if (_isSecondTap) {
                _isSecondTap = false;
                _pendingEvent = BTN_PANIC_HOLD;
                return BTN_SHORT_PRESS;
            }
            return BTN_PANIC_HOLD;
        }
    }

    // Check if waiting for second tap has timed out while button is idle
    if (_waitingForSecondTap && _stableState == HIGH) {
        // Do not time out while a press that started inside the window is currently being debounced
        // (unsigned subtraction stays correct across the 49-day millis() wrap)
        bool debouncingPotentialTap = (rawState == LOW) &&
            ((_lastDebounceTime - _firstTapReleaseTime) <= BTN_DOUBLE_TAP_MS);

        if (!debouncingPotentialTap && (now - _firstTapReleaseTime > BTN_DOUBLE_TAP_MS)) {
            _waitingForSecondTap = false;
            event = BTN_SHORT_PRESS; // Single Tap confirmed!
        }
    }

    return event;
}
