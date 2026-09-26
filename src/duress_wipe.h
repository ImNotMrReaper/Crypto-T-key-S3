/**
 * duress_wipe.h — Emergency Key Scrub & Decoy Panic Handler
 * ==========================================================
 * Destroys all stored credentials in NVS/flash upon entering
 * the Duress PIN or holding emergency panic trigger.
 */

#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include "rgb_status.h"

class DuressWipe {
public:
    static void setRamScrubber(void (*fn)());
    static void setSdWipe(bool enabled);
    static void execute(TFT_eSPI& tft, RgbStatus& rgb, const char* reason = "PANIC_PIN");
    static bool isWiped();

private:
    static void (*_ramScrubber)();
    static bool _sdWipeEnabled;

    static void zeroizeMemoryAndNVS();
    static void renderDecoyPanicScreen(TFT_eSPI& tft);
};
