/**
 * config.h — Global Configuration for T-Dongle S3 Security Key & Crypto Vault
 * ==============================================================================
 * Hardware: LilyGo T-Dongle S3 (ESP32-S3, ST7735 0.96" TFT, WS2812 RGB, microSD)
 */

#pragma once

#include <Arduino.h>

// ─── Hardware Pin Definitions ────────────────────────────────────────────────
#define PIN_TFT_BL        38      // TFT backlight (active LOW on LilyGo T-Dongle S3)
#define TFT_BL_ON         LOW
#define TFT_BL_OFF        HIGH
#define PIN_LED           40      // WS2812B data line
#define PIN_BTN           0       // BOOT button (active LOW, internal pullup)
#define LED_COUNT         1       // Single WS2812 RGB LED

// ─── MicroSD (SDIO 1-Bit SD_MMC) ─────────────────────────────────────────────
#define PIN_SD_CLK        12      // SD_MMC CLK
#define PIN_SD_CMD        16      // SD_MMC CMD
#define PIN_SD_D0         17      // SD_MMC D0

// ─── Display Configuration ───────────────────────────────────────────────────
#define DISP_W            160
#define DISP_H            80
#define DISP_ROTATION     1       // Landscape mode (160x80)

// ─── Button Cadence Timings (ms) ─────────────────────────────────────────────
#define BTN_DEBOUNCE_MS        35
#define BTN_LONG_PRESS_MS      650     // Hold > 650ms = Confirm / OK
#define BTN_VERY_LONG_MS      2200     // Hold > 2200ms = Backspace / Cancel
#define BTN_PANIC_HOLD_MS     5500     // Hold > 5500ms = Emergency Duress Wipe

// ─── Security & PIN Settings ─────────────────────────────────────────────────
#define PIN_LENGTH             4
#define DEFAULT_MASTER_PIN     "1234"   // Default master PIN for demonstration
#define EMERGENCY_DURESS_PIN   "9999"   // Triggers immediate flash scrub + decoy crash

// ─── Operating States ────────────────────────────────────────────────────────
enum DeviceState {
    STATE_BOOT_SPLASH,         // Boot splash & self-test
    STATE_LOCKED,              // Awaiting PIN entry (Amber LED)
    STATE_IDLE_DASHBOARD,      // Authenticated & ready (Breathing Cyan LED)
    STATE_FIDO_AUTH_REQUEST,   // WebAuthn / Passkey user presence requested (Pulsing Green LED)
    STATE_CRYPTO_SIGN_REQUEST, // Clear-signing transaction verification (Pulsing Green LED)
    STATE_AIRGAP_SD,           // MicroSD transaction parsed / offline signing (Solid Blue LED)
    STATE_BLE_COMPANION,       // BLE mobile companion active (Pulsing Purple LED)
    STATE_DURESS_WIPED         // Decoy system crash after zeroization (LED Dark)
};

// ─── Button Events ───────────────────────────────────────────────────────────
enum ButtonEvent {
    BTN_NONE,
    BTN_SHORT_PRESS,       // Instant on release (<650ms): +1 / Next
    BTN_LONG_PRESS,        // Triggered at 650ms: Confirm / Enter / Commit
    BTN_VERY_LONG_PRESS,   // Triggered at 2200ms: Backspace / Back / Reject
    BTN_PANIC_HOLD         // Triggered at 5500ms: Emergency Hardware Zeroization
};
