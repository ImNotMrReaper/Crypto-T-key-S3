/**
 * button_cadence.h — High-Precision Single Button Cadence Handler
 * ================================================================
 * Detects Short Press, Long Press, Double Click, and Panic Hold
 * on a single GPIO button (GPIO 0 / BOOT).
 */

#pragma once

#include "config.h"

class ButtonCadence {
public:
    void begin(uint8_t pin = PIN_BTN);
    ButtonEvent update();
    bool isPressedNow() const { return (_stableState == LOW); }
    uint32_t currentHoldDuration() const;
    uint8_t getHoldStage() const; // 0=None, 1=Confirm ready, 2=Reset ready, 3=Panic ready

private:
    uint8_t  _pin = PIN_BTN;
    bool     _lastRawState = HIGH;
    bool     _stableState = HIGH;
    uint32_t _lastDebounceTime = 0;
    uint32_t _pressStartTime = 0;

    bool     _waitingForSecondTap = false;
    uint32_t _firstTapReleaseTime = 0;
    bool     _panicFired = false;
};
