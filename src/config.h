/**
 * config.h — Global Configuration for Crypto TKey S3 Authenticator & Vault
 * ==============================================================================
 * Hardware: LilyGo T-Dongle S3 (ESP32-S3, ST7735 0.96" TFT, APA102/WS2812 RGB, microSD)
 */

#pragma once

#include <Arduino.h>

// ─── Hardware Pin Definitions ────────────────────────────────────────────────
#define PIN_TFT_BL        38      // TFT backlight (active LOW on LilyGo T-Dongle S3)
#define TFT_BL_ON         LOW
#define TFT_BL_OFF        HIGH
#define PIN_LED_DATA      40      // APA102 RGB LED Data (DI)
#define PIN_LED_CLK       39      // APA102 RGB LED Clock (CI)
#define PIN_LED           PIN_LED_DATA // Alias
#define PIN_BTN           0       // BOOT button (active LOW, internal pullup)
#define LED_COUNT         1       // Single onboard RGB LED

// ─── MicroSD (SDIO 1-Bit SD_MMC) ─────────────────────────────────────────────
#define PIN_SD_CLK        12      // SD_MMC CLK
#define PIN_SD_CMD        16      // SD_MMC CMD
#define PIN_SD_D0         17      // SD_MMC D0

// ─── Display Configuration (lilygo-tdongle-ui-dev standard) ──────────────────
#define DISP_W            160
#define DISP_H            80
#define DISP_ROTATION     1       // Landscape mode (160x80)

// ─── Thermal & Power Management ──────────────────────────────────────────────
#define CPU_FREQ_MHZ              80      // 80MHz dynamic throttling (cool operation)
#define DISPLAY_SLEEP_TIMEOUT_MS  45000   // 45 seconds inactivity sleep timer
#define LED_BRIGHTNESS_LIMIT      35      // Scaled brightness (prevents LED heat)

// ─── Button Cadence Timings (ms) ─────────────────────────────────────────────
#define BTN_DEBOUNCE_MS        35
#define BTN_DOUBLE_TAP_MS     320     // Click twice within 320ms = Double Click (Backspace)
#define BTN_LONG_PRESS_MS     600     // Hold 600ms..2000ms = Confirm / Next
#define BTN_VERY_LONG_MS     2200     // Hold 2200ms..5500ms = Clear All / Reset
#define BTN_PANIC_HOLD_MS    6000     // Hold > 6000ms = Emergency Duress Wipe

// ─── Security & PIN Settings ─────────────────────────────────────────────────
#define PIN_MIN_LENGTH         4
#define PIN_MAX_LENGTH         8
#define PIN_LENGTH             PIN_MAX_LENGTH  // Max buffer size
#define DEFAULT_MASTER_PIN     "1234"          // Default master PIN for vault setup
#define EMERGENCY_DURESS_PIN   "9999"          // Triggers immediate flash scrub + decoy crash

// ─── Cyberpunk / Antigravity UI Palette (RGB565) ─────────────────────────────
#define COLOR_BG               0x0000   // True OLED Pitch Black
#define COLOR_NEON_CYAN        0x073F   // Active highlights
#define COLOR_SIGNAL_GREEN     0x07E3   // WebAuthn UP verified
#define COLOR_CYBER_GOLD       0xFD80   // PIN entry & warnings
#define COLOR_CRIMSON_PANIC    0xF8A4   // Panic / Duress wipe
#define COLOR_DARK_GRAY        0x18E3   // Slot background cards
#define COLOR_HEADER_BG        0x0010   // Dark Navy Header

// ─── Operating States ────────────────────────────────────────────────────────
enum DeviceState {
    STATE_BOOT_SPLASH,         // Boot splash & cryptographic self-test
    STATE_SETUP_WALKTHROUGH,   // First-Time Setup Wizard (OOBE)
    STATE_IDLE_READY,          // Screen 1: Base Home Screen (Clock, Wi-Fi, System stats)
    STATE_PASSKEY_HUB,         // Screen 2: Dedicated Passkey Authentication Hub (WebAuthn / FIDO2)
    STATE_PORTFOLIO_TRACKER,   // Screen 3: Crypto & Asset Hub (Public Prices/Balances)
    STATE_PIN_ENTRY,           // Master PIN gate (Vault / Offline Signer access)
    STATE_VAULT_DASHBOARD,     // Unlocked Crypto Vault (BIP-39 / Addresses)
    STATE_SEED_ENTROPY_COLLECT,// Collecting human timing jitter for seed generation
    STATE_SEED_WORD_DISPLAY,   // Word-by-word BIP-39 mnemonic verification
    STATE_SEED_WORDS_VIEW,     // On-device view of existing seed words (PIN-gated)
    STATE_FIDO_AUTH_PROMPT,    // WebAuthn / Passkey user presence prompt
    STATE_CRYPTO_SIGN_PROMPT,  // Clear-signing transaction verification (WYSIWYS)
    STATE_AIRGAP_SD_SIGN,      // MicroSD PSBT air-gap signer
    STATE_DURESS_WIPED         // Decoy system crash after zeroization
};

// ─── Button Events ───────────────────────────────────────────────────────────
enum ButtonEvent {
    BTN_NONE,
    BTN_SHORT_PRESS,       // Single tap (<350ms): +1 / Next / UP Confirm
    BTN_DOUBLE_CLICK,      // Double tap (<320ms gap): Backspace / Delete / Undo
    BTN_LONG_PRESS,        // Hold 600ms+ then release: Confirm / Enter / Commit
    BTN_VERY_LONG_PRESS,   // Hold 2200ms+ then release: Clear All / Reset / Back
    BTN_PANIC_HOLD         // Continuous hold > 6000ms: Emergency Hardware Zeroization
};
