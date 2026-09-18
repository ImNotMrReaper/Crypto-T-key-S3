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
#define PIN_LED_DATA      40      // APA102 RGB LED Data (DI)
#define PIN_LED_CLK       39      // APA102 RGB LED Clock (CI)
#define PIN_LED           PIN_LED_DATA // Backwards compatibility alias
#define PIN_BTN           0       // BOOT button (active LOW, internal pullup)
#define LED_COUNT         1       // Single APA102 RGB LED

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
#define BTN_DOUBLE_TAP_MS     320     // Click twice within 320ms = Double Click (DELETE / Backspace)
#define BTN_LONG_PRESS_MS     600     // Hold 600ms..2000ms = Confirm / Next
#define BTN_VERY_LONG_MS     2200     // Hold 2200ms..5500ms = Clear All / Reset
#define BTN_PANIC_HOLD_MS    6000     // Hold > 6000ms = Emergency Duress Wipe

// ─── Security & PIN Settings ─────────────────────────────────────────────────
#define PIN_LENGTH             4
#define DEFAULT_MASTER_PIN     "1234"   // Default master PIN for demonstration
#define EMERGENCY_DURESS_PIN   "9999"   // Triggers immediate flash scrub + decoy crash

// ─── Operating States ────────────────────────────────────────────────────────
enum DeviceState {
    STATE_BOOT_SPLASH,         // Boot splash & self-test
    STATE_SETUP_WALKTHROUGH,   // First-Time Setup Wizard (Custom PIN creation & seed backup)
    STATE_LOCKED,              // Awaiting PIN entry (Amber LED)
    STATE_IDLE_DASHBOARD,      // Authenticated & ready (Breathing Cyan LED)
    STATE_WALLET_VIEW,         // Multi-Currency Address Explorer (BTC / ETH / SOL / DOGE)
    STATE_CRYPTO_PRICES,       // Live Cryptocurrency Price Ticker (USD)
    STATE_WIFI_CONFIG,         // Wi-Fi network manager & live connection status
    STATE_FIDO_AUTH_REQUEST,   // WebAuthn / Passkey user presence requested (Pulsing Green LED)
    STATE_CRYPTO_SIGN_REQUEST, // Clear-signing transaction verification (Pulsing Green LED)
    STATE_AIRGAP_SD,           // MicroSD transaction parsed / offline signing (Solid Blue LED)
    STATE_BLE_COMPANION,       // BLE mobile companion active (Pulsing Purple LED)
    STATE_DURESS_WIPED         // Decoy system crash after zeroization (LED Dark)
};

// ─── Button Events ───────────────────────────────────────────────────────────
enum ButtonEvent {
    BTN_NONE,
    BTN_SHORT_PRESS,       // Single tap (<350ms): +1 / Next / Advance
    BTN_DOUBLE_CLICK,      // Double tap (<320ms gap): Backspace / Delete / Undo
    BTN_LONG_PRESS,        // Hold 600ms+ then release: Confirm / Enter / Commit
    BTN_VERY_LONG_PRESS,   // Hold 2200ms+ then release: Clear All / Reset / Back
    BTN_PANIC_HOLD         // Continuous hold > 6000ms: Emergency Hardware Zeroization
};
